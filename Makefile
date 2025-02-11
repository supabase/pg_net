PG_CFLAGS = -std=c11 -Werror -Wno-declaration-after-statement
ifeq ($(COVERAGE), 1)
PG_CFLAGS += --coverage
endif
EXTENSION = pg_net
EXTVERSION = 0.14.0

DATA = $(wildcard sql/*--*.sql)

EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = $(EXTENSION)
SRC = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c, src/%.o, $(SRC))

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

# Find <curl/curl.h> from system headers
PG_CPPFLAGS := $(CPPFLAGS) -DEXTVERSION=\"$(EXTVERSION)\"

all: $(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

$(EXTENSION).control:
	sed "s/@PG_NET_VERSION@/$(EXTVERSION)/g" $(EXTENSION).control.in > $(EXTENSION).control

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
