PG_CONFIG ?= pg_config

EXTENSION = pgjq
EXTVERSION = 0.1.0

MODULE_big = $(EXTENSION)
OBJS = pgjq.o
PGFILEDESC = "Use jq in Postgres"

DATA = $(wildcard sql/*--*.sql)

PKG_CONFIG ?= pkg-config

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-extension=pgjq

CFLAGS += $(shell $(PKG_CONFIG) --cflags jq)
LIBS += $(shell $(PKG_CONFIG) --libs jq)
SHLIB_LINK := $(LIBS)

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

tdd: clean all uninstall install installcheck