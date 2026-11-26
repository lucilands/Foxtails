CFLAGS=-Wall -Wextra -std=gnu99 -Iinclude -MMD -MP -flto -O2 -Werror -march=native -fno-fast-math -fsigned-zeros -ffp-contract=on -fno-strict-aliasing -fno-omit-frame-pointer
LDFLAGS=-lpthread

UTILS_OBJ:=utils/sockets.o\
		   utils/workers.o\
		   utils/http.o \
		   utils/config.o \
		   utils/server.o \
		   utils/routing.o

BINDIR=bin
FOXTAIL_SRC=foxtails/src
FOXFANG_SRC=foxfang/src

IN_FILES=scripts/foxrun.in scripts/foxctl.in
IN_OUT=$(addprefix $(BINDIR)/,$(notdir $(IN_FILES:.in=)))
vpath %.in $(sort $(dir $(IN_FILES)))

subst_vars=$(shell grep -ho '{[A-Za-z_][A-Za-z0-9_]*}' $(1) | tr -d '{}' | sort -u)

$(foreach f,$(IN_FILES),$(foreach v,$(call subst_vars,$(f)),$(if $(filter undefined,$(origin $(v))),$(error $(v) is used in $(f) but not defined))))


.PHONY: all clean foxfang foxtails run
all: foxfang foxtails $(IN_OUT)

include foxtails/foxtails.make
include foxfang/foxfang.make

$(IN_OUT): $(BINDIR)/%: %.in Makefile
	sed -e '' $(foreach v,$(call subst_vars,$<),-e 's|{$(v)}|$($(v))|g') $< > $@
	chmod +x $@

$(UTILS_OBJ):%.o: %.c
	$(CC) -o$@ $(CFLAGS) -c $<

$(BINDIR):
	mkdir -p $@

-include $(UTILS_OBJ:.o=.d)

clean:
	rm -r $(BINDIR)
	rm $(UTILS_OBJ)
	rm $(UTILS_OBJ:%.o=%.d)
	rm $(OBJ)
