#-----------------------------------------------------------------------------#
#
# Makefile for plv8
#
# @param DISABLE_DIALECT if defined, not build dialects (i.e. plcoffee, etc)
# @param ENABLE_DEBUGGER_SUPPORT enables v8 debugger agent
#
# There are two ways to build plv8.
# 1. Dynamic link to v8 (default)
#   You need to have libv8.so and header file installed.
# 2. Static link to v8 with snapshot
#   'make static' will download v8 and build, then statically link to it.
#
#-----------------------------------------------------------------------------#
PLV8U_VERSION = 0.1.0-dev

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)

PG_VERSION_NUM := $(shell cat `$(PG_CONFIG) --includedir-server`/pg_config*.h \
		   | perl -ne 'print $$1 and exit if /PG_VERSION_NUM\s+(\d+)/')

# set your custom C++ compler
CUSTOM_CC = g++
CC = gcc
SRCS = plv8.cc plv8_type.cc plv8_func.cc plv8_param.cc plv8u_func.cc helpers/file.cc $(JSCS)
OBJS = $(SRCS:.cc=.o)

MODULE_big = plv8u
EXTENSION = plv8u
PLV8U_DATA = plv8u.control plv8u--$(PLV8U_VERSION).sql

DATA = $(PLV8U_DATA)
DATA_built = plv8u.sql
REGRESS = init-extension plv8 plv8-errors inline json startup_pre startup varparam json_conv \
 		  jsonb_conv window guc es6 arraybuffer composites currentresource

SHLIB_LINK += -lv8
ifdef V8_OUTDIR
SHLIB_LINK += -L$(V8_OUTDIR)
else
SHLIB_LINK += -lv8_libplatform
endif


OPTFLAGS = -O2 -std=c++11 -fno-rtti

CCFLAGS = -Wall $(OPTFLAGS) $(OPT_ENABLE_DEBUGGER_SUPPORT)

ifdef V8_SRCDIR
override CPPFLAGS += -I$(V8_SRCDIR) -I$(V8_SRCDIR)/include
endif

ifeq ($(OS),Windows_NT)
	# noop for now, it could come in handy later
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Darwin)
		# nothing to do anymore, setting -stdlib=libstdc++ breaks things
	endif
endif

all:

plv8_config.h: plv8_config.h.in Makefile
	sed -e 's/^#undef PLV8U_VERSION/#define PLV8U_VERSION "$(PLV8U_VERSION)"/' $< > $@

%.o : %.cc plv8_config.h plv8.h
	$(CUSTOM_CC) $(CCFLAGS) $(CPPFLAGS) -fPIC -c -o $@ $<


# VERSION specific definitions
ifeq ($(shell test $(PG_VERSION_NUM) -ge 90100 && echo yes), yes)

DATA_built =
all: $(DATA)
%--$(PLV8U_VERSION).sql: plv8u.sql.common
	sed -e 's/@LANG_NAME@/$*/g' $< | sed -e 's/@PLV8U_VERSION@/$(PLV8U_VERSION)/g' | $(CC) -E -P $(CPPFLAGS) -DLANG_$* - > $@
%.control: plv8u.control.common
	sed -e 's/@PLV8U_VERSION@/$(PLV8U_VERSION)/g' $< | $(CC) -E -P -DLANG_$* - > $@
subclean:
	rm -f plv8_config.h $(DATA) $(JSCS)

else

DATA = uninstall_plv8u.sql
%.sql.in: plv8u.sql.common
	sed -e 's/@LANG_NAME@/$*/g' $< | $(CC) -E -P $(CPPFLAGS) -DLANG_$* - > $@
subclean:
	rm -f plv8_config.h *.sql.in $(JSCS)

endif

# < 9.4, drop jsonb_conv
ifeq ($(shell test $(PG_VERSION_NUM) -lt 90400 && echo yes), yes)
REGRESS := $(filter-out jsonb_conv, $(REGRESS))
endif

# < 9.2, drop json_conv
ifeq ($(shell test $(PG_VERSION_NUM) -lt 90200 && echo yes), yes)
REGRESS := $(filter-out json_conv, $(REGRESS))
endif

# < 9.1, drop init-extension and dialect, add init at the beginning
ifeq ($(shell test $(PG_VERSION_NUM) -lt 90100 && echo yes), yes)
REGRESS := init $(filter-out init-extension dialect, $(REGRESS))
endif

# < 9.0, drop inline, startup, varparam and window
ifeq ($(shell test $(PG_VERSION_NUM) -lt 90000 && echo yes), yes)
REGRESS := $(filter-out inline startup varparam window, $(REGRESS))
endif

clean: subclean

# build will be created by Makefile.v8
distclean:
	rm -rf build

static:
	$(MAKE) -f Makefile.v8

# Check if META.json.version and PLV8_VERSION is equal.
# Ideally we want to have only one place for this number, but parsing META.json
# is a bit challenging; at one time we had v8/d8 parsing META.json to pass
# this value to source file, but it turned out those utilities may not be
# available everywhere.  Since this integrity matters only developers,
# we just check it if they are available.  We may come up with a better
# solution to have it in one place in the future.
META_VER = $(shell v8 -e 'print(JSON.parse(read("META.json")).version)' 2>/dev/null)
ifndef META_VER
META_VER = $(shell d8 -e 'print(JSON.parse(read("META.json")).version)' 2>/dev/null)
endif
ifndef META_VER
META_VER = $(shell lsc -e 'console.log(JSON.parse(require("fs").readFileSync("META.json")).version)' 2>/dev/null)
endif
ifndef META_VER
META_VER = $(shell coffee -e 'console.log(JSON.parse(require("fs").readFileSync("META.json")).version)' 2>/dev/null)
endif
ifndef META_VER
META_VER = $(shell node -e 'console.log(JSON.parse(require("fs").readFileSync("META.json")).version)' 2>/dev/null)
endif

integritycheck:
ifneq ($(META_VER),)
	test "$(META_VER)" = "$(PLV8U_VERSION)"
endif

installcheck: integritycheck

.PHONY: subclean integritycheck
include $(PGXS)
