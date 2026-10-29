#! /usr/bin/env bash

xpg psql \
  --file=<(cat << 'EOF'
DO $$
DECLARE
  extension_objects text[];
  extension_object text;
BEGIN

SELECT array_agg(pg_catalog.pg_describe_object(classid, objid, 0)) into extension_objects
FROM pg_catalog.pg_depend
INNER JOIN (
	SELECT e.oid
	FROM pg_catalog.pg_extension e
	WHERE e.extname OPERATOR(pg_catalog.=) 'pg_net' 
	COLLATE pg_catalog.default
	ORDER BY 1
) AS pg_net_oid 
ON pg_net_oid.oid = refobjid 
WHERE refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass 
	AND deptype = 'e'
ORDER BY 1;

FOR extension_object IN select unnest(extension_objects) LOOP
  BEGIN 
    EXECUTE 'alter extension pg_net drop ' || extension_object;
  EXCEPTION WHEN OTHERS THEN
    RAISE NOTICE 'failed on %: %', extension_object, sqlerrm;
  END;
END LOOP;

END;
$$;
EOF) \
  --command="\\! pg_dump --schema=net --user=postgres --restrict-key=0 --schema-only | grep -v '^\\\\restrict 0$' | grep -v '^\\\\unrestrict 0$' | grep -v '^SET .*;' | grep -v 'SELECT pg_catalog.set_config' | pgpp > pg_net.sql"
