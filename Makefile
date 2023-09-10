PG_CONFIG ?= pg_config

EXTENSION = pgjq
EXTVERSION = 0.1.0

MODULE_big = $(EXTENSION)
OBJS = pgjq.o
PGFILEDESC = "Use jq in Postgres"

DATA = $(wildcard sql/*--*.sql)

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-extension=pgjq


# Change this if you have jq installed somewhere else
current_dir = $(shell pwd)
JQ_PREFIX ?= $(current_dir)/jq/build

PG_CPPFLAGS += -I$(JQ_PREFIX)/include
PG_LDFLAGS += -L$(JQ_PREFIX)/lib
SHLIB_LINK += -ljq

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

tdd: clean all uninstall install installcheck