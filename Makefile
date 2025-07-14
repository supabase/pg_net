SRC_DIR = src

# the `-Wno`s quiet C90 warnings
PG_CFLAGS = -std=c11 -Wextra -Wall -Werror \
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

EXTENSION = pg_net
EXTVERSION = 0.19.0

DATA = $(wildcard sql/*--*.sql)

EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --use-existing --inputdir=test

MODULE_big = $(EXTENSION)
SRC = $(wildcard $(SRC_DIR)/*.c)

ifdef BUILD_DIR
OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC))
else
OBJS = $(patsubst $(SRC_DIR)/%.c, src/%.o, $(SRC)) # if no BUILD_DIR, just build on src so standard PGXS `make` works
endif

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

# Find <curl/curl.h> from system headers
PG_CPPFLAGS := $(CPPFLAGS) -DEXTVERSION=\"$(EXTVERSION)\"

all: sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

build: $(BUILD_DIR)/$(EXTENSION).so sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control

$(BUILD_DIR)/.gitignore: sql/$(EXTENSION)--$(EXTVERSION).sql $(EXTENSION).control
	mkdir -p $(BUILD_DIR)/extension
	cp $(EXTENSION).control $(BUILD_DIR)/extension
	cp sql/$(EXTENSION)--$(EXTVERSION).sql $(BUILD_DIR)/extension
	echo "*" > $(BUILD_DIR)/.gitignore

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(BUILD_DIR)/.gitignore
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(EXTENSION).so: $(EXTENSION).so
	mv $? $@

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql
	cp $< $@

$(EXTENSION).control:
	sed "s/@EXTVERSION@/$(EXTVERSION)/g" $(EXTENSION).control.in > $@

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

.PHONY: test
test:
	net-with-nginx python -m pytest -s -vv
