from sqlalchemy import text


def http_request(sess, query):
    """
    Execute query and commit to wake up the background worker.
    """
    result = sess.execute(query).fetchone()
    # Commit to wakeup background worker
    sess.commit()
    return result


def collect_response_sync(sess, request_id):
    """
    Wait for request with request_id to complete and return its response.

    Flattens net._http_collect_response's nested composite return type
    (status, message, response(status_code, headers, body)) into a single
    row, so callers get a name-addressable mapping (e.g. result["body"])
    instead of indexing into a stringified nested tuple.
    """
    return sess.execute(
        text(
            """
        select
            status,
            message,
            (response).status_code,
            (response).headers,
            (response).body
        from net._http_collect_response(:request_id, async:=false);
    """
        ),
        {"request_id": request_id},
    ).mappings().fetchone()
