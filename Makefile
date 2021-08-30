EXTENSION = pg_net
EXTVERSION = 0.2

DATA = $(wildcard sql/*--*.sql)

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = pg_net
OBJS = src/worker.o

all: sql/$(EXTENSION)--$(EXTVERSION).sql

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
