#ifndef __CPOOL_H
#define __CPOOL_H

#ifdef __cplusplus
#define _Bool bool
extern "C" {
#endif //__cplusplus

#ifndef CPOOL_NO_STDDEF
#include <stddef.h>
#endif //CPOOL_NO_STDDEF

#if defined(CPOOL_NO_THREAD_LOCAL)
	#define CPOOL_THREAD_LOCAL
#elif defined(_MSC_VER)
    #define CPOOL_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
    #define CPOOL_THREAD_LOCAL __thread
#elif __STDC_VERSION__ >= 201112L
    #define CPOOL_THREAD_LOCAL _Thread_local
#else
	#warning "Thread-local declaration not supported, context will be tagged as global."
	#define CPOOL_THREAD_LOCAL
#endif //CPOOL_NO_THREAD_LOCAL

#define cpool_set_context(pool) __cpool_ctx = (pool)
#define palloc_t(T) (T*)palloc(sizeof(T))
#define pcalloc_t(n, T) (T*)pcalloc((n), sizeof(T))

typedef struct __cpool *cpool_t;
extern CPOOL_THREAD_LOCAL cpool_t __cpool_ctx;

void *palloc(size_t size);
void *pcalloc(size_t num, size_t size);

cpool_t cpool_init(size_t block_size);
void cpool_uninit(cpool_t pool);
void cpool_reset(void);
int cpool_align(size_t align);
void cpool_save(void);
void cpool_restore(void);

void *pmemdup(void *p, size_t s);
char *pstrdup(char *p);

#endif //__CPOOL_H
#ifdef CPOOL_IMPLEMENTATION

#ifndef CPOOL_MAX_SAVE_DEPTH
#define CPOOL_MAX_SAVE_DEPTH 8
#endif //CPOOL_MAX_SAVE_DEPHT

#ifndef CPOOL_NO_STDINT
#include <stdint.h>
#endif //CPOOL_NO_STDINT

#ifndef CPOOL_NO_LIMITS
#include <limits.h>
#endif //CPOOL_NO_LIMITS

#ifndef CPOOL_ALLOC
#include <stdlib.h>
#define CPOOL_ALLOC(size) malloc((size))
#endif //CPOOL_ALLOC

#ifndef CPOOL_FREE
#include <stdlib.h>
#define CPOOL_FREE(p) free((p))
#endif //CPOOL_FREE

#ifndef CPOOL_MEMSET
#include <string.h>
#define CPOOL_MEMSET(p, v, s) memset((p), (v), (s))
#endif //CPOOL_MEMSET

#ifndef CPOOL_MEMCPY
#include <string.h>
#define CPOOL_MEMCPY(d, s, n) memcpy((d), (s), (n))
#endif //CPOOL_MEMCPY

#include <assert.h>
#include <errno.h>


struct __cpool_large_block {
	void *data;
	struct __cpool_large_block *next;
};

struct __cpool_block {
	void *data;
	struct __cpool_block *next;
	_Bool full;
	size_t head;
};

struct __cpool_block_snapshot {
	size_t head;
	_Bool full;
};

struct __cpool_snapshot {
	struct __cpool_block_snapshot current_data;
	struct __cpool_block *current;
	struct __cpool_block *last;
	struct __cpool_large_block *large_current;
};

struct __cpool {
	struct __cpool_block *root;
	struct __cpool_block *current;
	struct __cpool_block *last;
	size_t block_size;
	struct __cpool_large_block *large_root;
	struct __cpool_large_block *large_last;
	
	struct __cpool_snapshot snapshots[CPOOL_MAX_SAVE_DEPTH];
	uint16_t snapshot_end;
};

CPOOL_THREAD_LOCAL cpool_t __cpool_ctx = NULL;

static struct __cpool_block *__init_block(size_t block_size) {
	struct __cpool_block *block = (struct __cpool_block *)CPOOL_ALLOC(sizeof(struct __cpool_block) + block_size);
    if (!block) {
        errno = ENOMEM;
        return NULL;
    }
    CPOOL_MEMSET(block, 0, sizeof(struct __cpool_block));
    block->data = (void *)(block + 1);
    return block;
}

static void *__alloc_large_block(const size_t size) {
	struct __cpool_large_block **lb = &(__cpool_ctx->large_root);
	if (*lb) lb = &(__cpool_ctx->large_last->next);
	
	*lb = (struct __cpool_large_block*)CPOOL_ALLOC(sizeof(struct __cpool_large_block));
	if (!*lb) {
		errno = ENOMEM;
		return NULL;
	}
	CPOOL_MEMSET(*lb, 0, sizeof(struct __cpool_large_block));

	(*lb)->data = CPOOL_ALLOC(size);
	if (!(*lb)->data) {
		CPOOL_FREE(*lb);
		*lb = NULL;
		errno = ENOMEM;
		return NULL;
	}

	__cpool_ctx->large_last = *lb;
	return (*lb)->data;
}

static inline int __append_block(cpool_t pool) {
    pool->last->next = __init_block(pool->block_size);
	if (!pool->last->next) {
		return ENOMEM;
	}
    pool->last = pool->last->next;
    pool->current = pool->last;
	return 0;
}

void *palloc(const size_t size) {
	assert(size > 0);
	if (size > __cpool_ctx->block_size) {
		return __alloc_large_block(size);
	}

	if (!__cpool_ctx->current || __cpool_ctx->current->head + size > __cpool_ctx->block_size) {
		if (__cpool_ctx->current && __cpool_ctx->current->next) __cpool_ctx->current = __cpool_ctx->current->next;
		else if (__append_block(__cpool_ctx)) return NULL;
	}

	void *ret = (void*)((char*)__cpool_ctx->current->data + __cpool_ctx->current->head);
	__cpool_ctx->current->head += size;
	if (__cpool_ctx->current->head == __cpool_ctx->block_size) {
		__cpool_ctx->current->full = 1;
		__cpool_ctx->current = __cpool_ctx->current->next;
	}
	return ret;
}

void *pcalloc(size_t num, size_t size) {
	if (num != 0 && size > SIZE_MAX / num) {errno = EOVERFLOW; return NULL;};
	void *ret = palloc(size * num);
	if (!ret) return NULL;
	CPOOL_MEMSET(ret, 0, size * num);
	return ret;
}

cpool_t cpool_init(size_t block_size) {
	cpool_t pool = (cpool_t)CPOOL_ALLOC(sizeof(struct __cpool));
	if (!pool) {
		errno = ENOMEM;
		return NULL;
	}
	CPOOL_MEMSET(pool, 0, sizeof(struct __cpool));

	pool->block_size = block_size;
	pool->root = __init_block(block_size);
	pool->current = pool->root;
	pool->last = pool->root;

	if (!pool->root) {
		CPOOL_FREE(pool);
		errno = ENOMEM;
		return NULL;
	}

	__cpool_ctx = pool;
	return pool;
}

void cpool_reset() {
	struct __cpool_block *b = __cpool_ctx->root;
	__cpool_ctx->last = __cpool_ctx->root; __cpool_ctx->current = __cpool_ctx->root;
	while (b) {
		b->head = 0;
		b->full = 0;
		__cpool_ctx->last = b;
		b = b->next;
	}

	struct __cpool_large_block *lb = __cpool_ctx->large_root;
	while (lb) {
		struct __cpool_large_block *next = lb->next;
		CPOOL_FREE(lb->data);
		lb->data = NULL;
		CPOOL_FREE(lb);
		lb = next;
	}
	__cpool_ctx->large_root = NULL;
	__cpool_ctx->large_last = NULL;

	__cpool_ctx->snapshot_end = 0;
}

void cpool_save() {
	assert(__cpool_ctx->snapshot_end < CPOOL_MAX_SAVE_DEPTH);

	struct __cpool_block *snap_current = __cpool_ctx->current
      ? __cpool_ctx->current
      : __cpool_ctx->last;

	__cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current_data.head = snap_current ? snap_current->head : 0;
	__cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current_data.full = snap_current ? snap_current->full : 0;
	__cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current = __cpool_ctx->current;
	__cpool_ctx->snapshots[__cpool_ctx->snapshot_end].last = __cpool_ctx->last;
	__cpool_ctx->snapshots[__cpool_ctx->snapshot_end].large_current = __cpool_ctx->large_last;
	__cpool_ctx->snapshot_end++;
}

int cpool_align(size_t align) {
	assert(align > 0 && (align & (align - 1)) == 0);
	if (!__cpool_ctx->current) {
		if (__append_block(__cpool_ctx)) return ENOMEM;
	}
	__cpool_ctx->current->head = (__cpool_ctx->current->head + align - 1) & ~(align - 1);
	if (__cpool_ctx->current->head >= __cpool_ctx->block_size) {
		__cpool_ctx->current->full = 1;
		if (__append_block(__cpool_ctx)) return ENOMEM;
	}
	return 0;
}

void cpool_restore(void) {
	assert(__cpool_ctx->snapshot_end > 0);
	__cpool_ctx->snapshot_end--;
	struct __cpool_block *last = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].last;
	struct __cpool_block *current = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current;
	struct __cpool_block *b = current ? current : last;
	if (!b) b = last;

	if (b) {
		b->head = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current_data.head;
		b->full = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].current_data.full;
		b = b->next;
	}
	while (b) {
		b->head = 0;
		b->full = 0;
		b = b->next;
	}
	__cpool_ctx->current = current ? current : last;
	__cpool_ctx->last = last;

	struct __cpool_large_block *lb = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].large_current;
	if (lb == NULL) lb = __cpool_ctx->large_root;
	else lb = lb->next;
	while (lb) {
		struct __cpool_large_block *next = lb->next;
		CPOOL_FREE(lb->data);
		lb->next = NULL;
		CPOOL_FREE(lb);
		lb = next;
	}
	__cpool_ctx->large_last = __cpool_ctx->snapshots[__cpool_ctx->snapshot_end].large_current;
	if (__cpool_ctx->large_last) __cpool_ctx->large_last->next = NULL;
	else __cpool_ctx->large_root = NULL;

	b = last;
	while (b && b->next) b = b->next;
	__cpool_ctx->last = b;
}

void *pmemdup(void *p, size_t s) {
	void *ret = palloc(s);
	if (!ret) return NULL;
	CPOOL_MEMCPY(ret, p, s);
	return ret;
}

char *pstrdup(char *p) {
	return (char*)pmemdup(p, strlen(p)+1);
}

void cpool_uninit(cpool_t pool) {
	if (!pool) return;
	struct __cpool_block *b = pool->root;
    while (b) {
        struct __cpool_block *next = b->next;
        CPOOL_FREE(b);
        b = next;
    }

    struct __cpool_large_block *lb = pool->large_root;
    while (lb) {
        struct __cpool_large_block *next = lb->next;
        CPOOL_FREE(lb->data);
        CPOOL_FREE(lb);

        lb = next;
    }

	pool->snapshot_end = 0;
    CPOOL_FREE(pool);
}
#endif //CPOOL_IMPLEMENTATION
#ifdef __cplusplus
}
#endif //__cplusplus
