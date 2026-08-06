#ifndef __CONFIG_H
#define __CONFIG_H

#include <stddef.h>

// TODO: Store section offsets in hash-map for faster lookups
typedef struct {
    struct {
        const char *map;
        const char *path;
        int fd;
        size_t size;
    } config_file;
} config_t;

config_t config_parse(const char *restrict path);
void config_delete(config_t conf);
char* config_get(config_t conf, char *section, char *key); // Caller is responsible for freeing returned string
int config_get_int(config_t conf, char *section, char *key);

#endif //__CONFIG_
