\set ON_ERROR_STOP on

BEGIN;
SELECT net.http_get('http://localhost:8080') AS request_id \gset
-- COMMIT is required otherwise the test runs forever
COMMIT;

SELECT (result).status,
       (result).message,
       ((result).response).status_code,
       ((result).response).body
FROM (
  SELECT net._http_collect_response(:request_id, async := false) AS result
) AS response;
