MODULE_big = curl_worker
OBJS = src/worker.o

PG_CONFIG = pg_config
SHLIB_LINK = -lcurl

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
