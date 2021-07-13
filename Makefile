EXTENSION = curl_worker
DATA = $(wildcard sql/*--*.sql)

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = curl_worker
OBJS = src/worker.o

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
