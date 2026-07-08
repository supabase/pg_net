# the `-Wno`s quiet C90 warnings
PG_CFLAGS = -std=c11 -Wextra -Wall -Werror \
	-Wold-style-definition \
	-Wno-declaration-after-statement \
	-Wno-vla \
	-Wno-long-long
ifeq ($(COVERAGE), 1)
PG_CFLAGS += --coverage
endif

ifeq ($(CC),gcc)
  GCC_MAJ := $(firstword $(subst ., ,$(shell $(CC) -dumpfullversion -dumpversion)))
  GCC_GE14 = $(shell test $(GCC_MAJ) -ge 14; echo $$?)
  ifeq ($(GCC_GE14),0)
    PG_CFLAGS += -Wmissing-variable-declarations
  endif
endif

UNAME_S := $(shell uname -s)
PG_CONFIG = pg_config

ifeq ($(UNAME_S),Darwin)
    # PG16 switched the loadable-module extension from .so to .dylib on macOS
    # So we check the postgres version to decide the correct extension to use
    PG_MAJORVERSION := $(shell $(PG_CONFIG) --version | sed -E 's/PostgreSQL ([0-9]+).*/\1/')
    ifeq ($(shell test $(PG_MAJORVERSION) -ge 16; echo $$?),0)
        SHARED_EXT  := dylib
    else
        SHARED_EXT  := so
    endif
else
    SHARED_EXT  := so
endif

EXTENSION = pg_net
EXTVERSION = 0.20.4

DATA = $(wildcard sql/*--*.sql)

EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = $(EXTENSION)
HEADERS = $(wildcard src/*.h)
SOURCES = $(wildcard src/*.c)

ifdef BUILD_DIR
OBJS = $(patsubst src/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
else
OBJS = $(patsubst src/%.c, src/%.o, $(SOURCES)) # if no BUILD_DIR, just build on src so standard PGXS `make` works
endif

SHLIB_LINK = -lcurl

# Find <curl/curl.h> from system headers
PG_CPPFLAGS := $(CPPFLAGS) -DEXTVERSION=\"$(EXTVERSION)\"

all: sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

build: $(BUILD_DIR)/$(EXTENSION).$(SHARED_EXT) sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

$(BUILD_DIR)/.gitignore: sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control
	mkdir -p $(BUILD_DIR)/extension
	cp $(EXTENSION).control $(BUILD_DIR)/extension
	cp sql/$(EXTENSION)--$(EXTVERSION).sql $(BUILD_DIR)/extension
	echo "*" > $(BUILD_DIR)/.gitignore

$(BUILD_DIR)/%.o: src/%.c $(HEADERS) $(BUILD_DIR)/.gitignore
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(EXTENSION).$(SHARED_EXT): $(EXTENSION).$(SHARED_EXT)
	mv $? $@

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

$(EXTENSION).control: $(EXTENSION).control.in
	sed "s/@EXTVERSION@/$(EXTVERSION)/g" $(EXTENSION).control.in > $@

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

.PHONY: test
test:
	net-with-nginx python -m pytest -s -vv
