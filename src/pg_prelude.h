// pragmas needed to pass compiling with -Wextra
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"

#include <postgres.h>
#include <postmaster/bgworker.h>
#include <pgstat.h>
#include <storage/condition_variable.h>
#include <storage/ipc.h>
#include <storage/latch.h>
#include <storage/proc.h>
#include <storage/shmem.h>
#include <access/xact.h>
#include <access/hash.h>
#include <catalog/namespace.h>
#include <catalog/pg_authid.h>
#include <catalog/pg_extension.h>
#include <catalog/pg_type.h>
#include <commands/defrem.h>
#include <commands/extension.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <nodes/makefuncs.h>
#include <nodes/pg_list.h>
#include <tcop/utility.h>
#include <tsearch/ts_locale.h>
#include <utils/acl.h>
#include <utils/builtins.h>
#include <utils/fmgrprotos.h>
#include <utils/guc.h>
#include <utils/guc_tables.h>
#include <utils/hsearch.h>
#include <utils/json.h>
#include <utils/jsonb.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>
#include <utils/regproc.h>
#include <utils/snapmgr.h>
#include <utils/varlena.h>

#pragma GCC diagnostic pop
