EXTENSION = pg_net
EXTVERSION = 0.7

DATA = $(wildcard sql/*--*.sql)

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = $(EXTENSION)
OBJS = src/worker.o src/util.o

all: sql/$(EXTENSION)--$(EXTVERSION).sql

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

# Find <curl/curl.h> from system headers
PG_CPPFLAGS := $(CPPFLAGS)

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
