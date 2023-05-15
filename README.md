<h1 style= 'border-bottom: 0;'>PG_NET</h1>

<em>a PostgreSQL extension that enables asynchronous (non-blocking) HTTP/HTTPS requests with SQL</em>

<p>
    <a href="">
        <img src="https://img.shields.io/badge/postgresql-12+-blue.svg" alt="PostgreSQL version" height="18">
    </a>
    <a href="https://github.com/supabase/pg_net/blob/master/LICENSE">
        <img src="https://img.shields.io/pypi/l/markdown-subtemplate.svg" alt="License" height="18">
    </a>
    <a href="https://github.com/supabase/pg_net/actions">
        <img src="https://github.com/supabase/pg_net/actions/workflows/main.yml/badge.svg" alt="Tests" height="18">
    </a>
</p>

<h3>Documentation</h3>: <a href="https://supabase.github.io/pg_net" target="_blank">https://supabase.github.io/pg_net</a>

<hr />
<h1>Contents</h1>
<ul>
    <li>
        <a href='#introduction'>
            Introduction
        </a>
    </li>
    <li>
        <a href='#technical_explanation'>Technical Explanation</a>
    </li>
    <li>
        <a href='#installation'>Installation</a>
    </li>
    <li>
        <a href='#making_requests'>
            Requests API
        </a>
        <ul>
            <li>
                Monitoring requests
            </li>
            <li> 
                GET requests
            </li>
            <li>
                POST requests
            </li>
            <li>
                DELETE requests
            </li>
        </ul>
    </li>
    <li>
        <a href='#applied'>
            Practical Examples
        </a>
        <ul>
            <li>
                Syncing data with an external data source with triggers
            </li>
            <li>
                Calling a serverless function every minute with PG_CRON
            </li>
            <li>
                Retrying failed requests
            </li>
        </ul>
    </li>
    <li>
        <a href='#contributing'>Contributing</a>
    </li>
</ul>

<hr />

<h1 id= 'introduction' >Introduction</h1>
<p>
    The PG_NET extension enables PostgreSQL to make asynchronous HTTP/HTTPS requests in SQL. It eliminates the need for servers to continuously poll for database changes and instead allows the database to proactively notify external resources about significant events. It seamlessly integrates with triggers, cron jobs (e.g., <a href='https://github.com/citusdata/pg_cron'>PG_CRON</a>), and procedures, unlocking numerous possibilities. Notably, PG_NET powers Supabase's Webhook functionality, highlighting its robustness and reliability.
</p>

Common use cases for the PG_NET extension include:

<ul>
    <li>Calling external APIs</li>
    <li>Syncing data with outside resources</li>
    <li>Calling a serverless function when an event, such as an insert, occurred</li>
</ul>

However, it is important to note that the extension has a few limitations. Currently, it only supports three types of asynchronous requests:

<ul>
    <li>async http GET requests</li>
    <li>async http POST requests with a <em>JSON</em> payload</li>
    <li>async http DELETE requests</li>
</ul>

Ultimately, though, PG_NET offers developers more flexibility in how they monitor and connect their database with external resources.

<hr />

<h1 id='technical_explanation'>Technical Explanation</h1>

The extension introduces a new `net` schema, which contains two unlogged tables, a type of table in PostgreSQL that offers performance improvements at the expense of durability. You can read more about unlogged tables [here](https://pgpedia.info/u/unlogged-table.html). The two tables are:

1. **`http_request_queue`**: This table serves as a queue for requests waiting to be executed. Upon successful execution of a request, the corresponding data is removed from the queue.

    The SQL statement to create this table is:

    ```sql
    CREATE UNLOGGED TABLE
        net.http_request_queue (
            id bigint NOT NULL DEFAULT nextval('net.http_request_queue_id_seq'::regclass),
            method text NOT NULL,
            url text NOT NULL,
            headers jsonb NOT NULL,
            body bytea NULL,
            timeout_milliseconds integer NOT NULL
        )
    ```

2. **`_http_response`**: This table holds the responses of each executed request.

    The SQL statement to create this table is:

    ```sql
    CREATE UNLOGGED TABLE
        net._http_response (
            id bigint NULL,
            status_code integer NULL,
            content_type text NULL,
            headers jsonb NULL,
            content text NULL,
            timed_out boolean NULL,
            error_msg text NULL,
            created timestamp with time zone NOT NULL DEFAULT now()
        )
    ```

When any of the three request functions (`http_get`, `http_post`, `http_delete`) are invoked, they create an entry in the `net.http_request_queue` table.

The extension employs C's [libCurl](https://curl.se/libcurl/c/) library within a PostgreSQL [background worker](https://www.postgresql.org/docs/current/bgworker.html) to manage HTTP requests. This background worker regularly checks the `http_request_queue` table and executes the requests it finds there.

Once a response is received, it gets stored in the `_http_response` table. By monitoring this table, you can keep track of response statuses and messages.

<hr />

<h1 id='installation'>Installation</h1>

<h3>Enabling the Extension with Supabase</h3>
You can activate the `pg_net` extension via Supabase's dashboard by following these steps:

1. Navigate to the 'Database' page.
2. Select 'Extensions' from the sidebar.
3. Search for "pg_net" and enable the extension.

<h3>Local Setup</h3>

<h4>Installation on Your Device/Server</h4>

Clone this repo and run

```bash
make && make install
```

To make the extension available to the database add on `postgresql.conf`:

```
shared_preload_libraries = 'pg_net'
```

<h4>Activation in PostgreSQL</h4>
To activate the extension in PostgreSQL, run the create extension command. The extension creates its own schema named net to avoid naming conflicts.

```psql
create extension pg_net;
```

<h1 id= 'making_requests'>Requests API</h1>

<h3>Monitoring Requests</h3>

When you call a request function (http_get, http_post, http_delete), the function will return a request_id immediately. You can use this request_id with the net.\_http_collect_response function to check the results of your request.

The net.\_http_collect_response function can be used to fetch the response of a request. This function returns a custom type \_net.http_response_result\* that has three columns:

1. <strong>response</strong>: This is an aggregate of the response's status_code (integer), headers (JSONB), and body (text).
2. <strong>status</strong>: This value indicates the execution status of the request itself, which can be either 'PENDING', 'SUCCESS', or 'ERROR'. It's important not to confuse this status with the HTTP status code of the response (like 200 for 'OK', 404 for 'Not Found', etc.) that is found in the response column
3. <strong>message</strong>: This contains the response's message as text.

<h5>Observing a Request's Response</h5>

```sql
SELECT
    message,
    response,
    status
FROM
    net._http_collect_response(<response_id>);
```

> WARNING: Although the status column can be 'PENDING', 'SUCCESS', or 'ERROR', there is a bug that makes all 'PENDING' requests displayed as 'ERROR'.

The individual values within the response column can be extracted as shown in the following SQL query:

<h5>Extracting response values</h5>

```sql
SELECT
    *,
    (response).status_code,
    (response).headers,
    (response).body
    -- Individual headers and values from the body can be extracted
    (((response).body::JSON)->'key-from-json-body') AS some_value
FROM
    net._http_collect_response(<request_id>);
```

<h3>GET requests</h3>
<h4>net.http_get function signature</h4>

```sql
net.http_get(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- WARNING: this is currently ignored, so there is no timeout
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 1000
)
    -- request_id reference
    returns bigint

    strict
    volatile
    parallel safe
    language plpgsql
```

<h4>Examples:</h4>
The following examples use the <a href='https://learning.postman.com/docs/developer/echo-api/'>Postman Echo API</a>.

<h5>Calling an API</h5>

```sql
SELECT net.http_get (
    'https://postman-echo.com/get?foo1=bar1&foo2=bar2'
) AS request_id;
```

> NOTE: You can view the response with the following query:
>
> ```sql
> SELECT
>     *,
>     ((response).body::JSON)->'args' AS args
> FROM
>     net._http_collect_response(56);
> ```

<h5>Calling an API with URL encoded params</h5>

```sql
SELECT net.http_get(
  'https://postman-echo.com/get',
  -- Equivalent to calling https://postman-echo.com/get?foo1=bar1&foo2=bar2&encoded=%21
  -- The "!" is url-encoded as %21
  '{"foo1": "bar1", "foo2": "bar2", "encoded": "!"}'::JSONB
) AS request_id;
```

<h5>Calling an API with an API key</h5>

```sql
SELECT net.http_get(
  'https://postman-echo.com/get?foo1=bar1&foo2=bar2',
   headers := '{"API-KEY-HEADER": "<API KEY>"}'::JSONB
) AS request_id;
```

<h3>POST requests</h3>

<h4>net.http_post function signature</h4>

```sql
net.http_post(
    -- url for the request
    url text,
    -- body of the POST request
    body jsonb default '{}'::jsonb,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{"Content-Type": "application/json"}'::jsonb,
    -- WARNING: this is currently ignored, so there is no timeout
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 1000
)
    -- request_id reference
    returns bigint

    volatile
    parallel safe
    language plpgsql
```

The following examples posts to the <a href='https://learning.postman.com/docs/developer/echo-api/'>Postman Echo API</a>

<h5>Sending data to an API</h5>

```sql
SELECT net.http_post(
    'https://postman-echo.com/post',
    '{"key": "value", "key": 5}'::JSONB,
    headers := '{"API-KEY-HEADER": "<API KEY>"}'::JSONB
) AS request_id;
```

<h5>Sending single table row as a payload</h5>

> NOTE: If multiple rows are sent using this method, each row will be sent as a seperate request.

```sql
WITH selected_row AS (
    SELECT
        *
    FROM target_table
    LIMIT 1
)
SELECT
    net.http_post(
        'https://postman-echo.com/post',
        to_jsonb(selected_row.*),
        headers := '{"API-KEY-HEADER": "<API KEY>"}'::JSONB
    ) AS request_id
FROM selected_row;
```

<h5>Sending multiple table rows as a payload</h5>

> WARNING: when sending multiple rows, be careful to limit your payload size.

```sql
WITH selected_rows AS (
    SELECT
        -- Converts all the rows into a JSONB array
        jsonb_agg(to_jsonb(target_table)) AS JSON_payload
    FROM target_table
    -- Generally good practice to LIMIT the max amount of rows
)
SELECT
    net.http_post(
        'https://postman-echo.com/post'::TEXT,
        JSON_payload,
        headers := '{"API-KEY-HEADER": "<API KEY>"}'::JSONB
    ) AS request_id
FROM selected_rows;
```

<hr />

<h3>DELETE requests</h3>

<h4>net.http_delete function signature</h4>

```sql
net.http_delete(
    -- url for the request
    url text,
    -- key/value pairs to be url encoded and appended to the `url`
    params jsonb default '{}'::jsonb,
    -- key/values to be included in request headers
    headers jsonb default '{}'::jsonb,
    -- WARNING: this is currently ignored, so there is no timeout
    -- the maximum number of milliseconds the request may take before being cancelled
    timeout_milliseconds int default 2000
)
    -- request_id reference
    returns bigint

    strict
    volatile
    parallel safe
    language plpgsql
    security definer
```

<h4>Examples:</h4>
The following examples use the <a href='https://dummy.restapiexample.com/employees'>Dummy Rest API</a>.

<h5>Sending a delete request to an API</h5>

```sql
    SELECT net.http_delete(
        'https://dummy.restapiexample.com/api/v1/delete/2'
    ) AS request_id;
```

<h5>Sending a delete request with a row id as a query param</h5>

```sql
WITH selected_id AS (
    SELECT
        id
    FROM target_table
    LIMIT 1 -- if not limited, it will make a delete request for each returned row
)
SELECT
    net.http_delete(
        'https://dummy.restapiexample.com/api/v1/delete/'::TEXT,
        format('{"id": "%s"}', id)::JSONB
    ) AS request_id
FROM selected_id;
```

<h5>Sending a delete request with a row id as a path param</h5>

```sql
WITH selected_id AS (
    SELECT
        id
    FROM target_table
    LIMIT 1 -- if not limited, it will make a delete request for each returned row
)
SELECT
    net.http_delete(
        'https://dummy.restapiexample.com/api/v1/delete/' || id
    ) AS request_id
FROM selected_row
```

<h1 id= 'applied'>Practical Examples</h1>

<h3>Syncing data with an external data source with triggers</h3>

The following example comes from <a href='https://typesense.org/docs/guide/supabase-full-text-search.html#syncing-individual-deletes'>Typesense's Supabase Sync guide</a>

```sql
-- Create the function to delete the record from Typesense
CREATE OR REPLACE FUNCTION delete_record()
    RETURNS TRIGGER
    LANGUAGE plpgSQL
AS $$
BEGIN
    SELECT net.http_delete(
        url := format('<TYPESENSE URL>/collections/products/documents/%s', OLD.id),
        headers := '{"X-Typesense-API-KEY": "<Typesense_API_KEY>"}'
    )
    RETURN OLD;
END $$;

-- Create the trigger that calls the function when a record is deleted from the products table
CREATE TRIGGER delete_products_trigger
    AFTER DELETE ON public.products
    FOR EACH ROW
    EXECUTE FUNCTION delete_products();
```

<h3>Calling a serverless function every minute with PG_CRON</h3>

The <a href='https://github.com/citusdata/pg_cron'>PG_CRON</a> extension enables PostgreSQL to become its own cron server. With it you can schedule regular calls to activate serverless functions.

> Useful links:
>
> <ul>
>   <li><a href='https://supabase.com/docs/guides/database/extensions/pgcron'>Supabase Installation Guide</a></li>
>   <li><a href='https://crontab.guru/'>Cron Syntax Helper</a></li>
> </ul>

<h3>Example Cron job to call serverless function</h3>

```sql
SELECT cron.schedule(
    'cron-job-name',
    '* * * * *', -- Executes every minute (cron syntax)
	$$
    -- SQL query
    SELECT net.http_get(
        -- URL of Edge function
        url:='https://<reference id>.functions.Supabase.co/example',
        headers:='{
            "Content-Type": "application/json",
            "Authorization": "Bearer <TOKEN>"
        }'::JSONB
    ) as request_id;
	$$
  );
```

<h3>Retrying failed requests</h3>

<h3>Managing and Retrying Failed Requests</h3>
Every request made is logged within the net._http_response table. To identify failed requests, you can execute a query on the table, filtering for requests where the status code is 500 or higher.

```sql
SELECT
    *
FROM net._http_response
WHERE status_code >= 500;
```

While the net.\_http_response table logs each request, it doesn't store all the necessary information to retry failed requests. To facilitate this, we need to create a request tracking table and a wrapper function around the PG_NET request functions. This will help us store the required details for each request.

<h3>Creating a Request Tracker Table</h3>

The following SQL script creates a request_tracker table to store detailed information about each request.

```sql
CREATE TABLE request_tracker(
    method TEXT,
    url TEXT,
    params JSONB,
    body JSONB,
    headers JSONB,
    request_id BIGINT
)
```

<h3>Creating a Request Wrapper Function</h3>

Below is a function called request_wrapper, which wraps around the PG_NET request functions. This function records every request's details in the request_tracker table, facilitating future retries if needed.

```sql
CREATE OR REPLACE FUNCTION request_wrapper(
    method TEXT,
    url TEXT,
    params JSONB DEFAULT '{}'::JSONB,
    body JSONB DEFAULT '{}'::JSONB,
    headers JSONB DEFAULT '{}'::JSONB
)
RETURNS BIGINT
AS $$
DECLARE
    request_id BIGINT;
BEGIN

    IF method = 'DELETE' THEN
        SELECT net.http_delete(
            url:=url,
            params:=params,
            headers:=headers
        ) INTO request_id;
    ELSIF method = 'POST' THEN
        SELECT net.http_post(
            url:=url,
            body:=body,
            params:=params,
            headers:=headers
        ) INTO request_id;
    ELSIF method = 'GET' THEN
        SELECT net.http_get(
            url:=url,
            params:=params,
            headers:=headers
        ) INTO request_id;
    ELSE
        RAISE EXCEPTION 'Method must be DELETE, POST, or GET';
    END IF;

    INSERT INTO request_tracker (method, url, params, body, headers, request_id)
    VALUES (method, url, params, body, headers, request_id);

    RETURN request_id;
END;
$$
LANGUAGE plpgsql;
```

To retry a failed request recorded via the wrapper function, use the following query. This will select a failed request, retry it, and then remove the original request data from both the net.\_http_response and request_tracker tables.

```sql
WITH retry_request AS (
    SELECT
        request_tracker.method,
        request_tracker.url,
        request_tracker.params,
        request_tracker.body,
        request_tracker.headers,
        request_tracker.request_id
    FROM request_tracker
    INNER JOIN net._http_response ON net._http_response.id = request_tracker.request_id
    WHERE net._http_response.status_code >= 500
    LIMIT 3
),
retry AS (
    SELECT
        request_wrapper(retry_request.method, retry_request.url, retry_request.params, retry_request.body, retry_request.headers)
    FROM retry_request
),
delete_http_response AS (
    DELETE FROM net._http_response
    WHERE id IN (SELECT request_id FROM retry_request)
    RETURNING *
)
DELETE FROM request_tracker
WHERE request_id IN (SELECT request_id FROM retry_request)
RETURNING *;
```

The above function can be called using cron jobs or manually to retry failed requests. It may also be beneficial to clean the request_tracker table in the process.

<h1 id='contributing'>Contributing</h1>
Checkout the <a href='docs/contributing.md'>Contributing</a> page to learn more about adding to the project.
