def test_queue_table_insert_default(conn) -> None:
  sql = "CREATE USER test_user;"
  conn.execute(sql)

  sql = """
  insert into net.http_request_queue(method, url, headers, timeout_milliseconds)
    values (
        'GET',
        'http://localhost:8080',
        '{}'::jsonb,
        5000
    )
    returning id;
  """
  id = conn.execute(sql).fetchall()[0][0]
  conn.commit()
  sql = f"SELECT * FROM net.http_request_queue where id = {id};"
  response = conn.execute(sql).fetchall()
  assert len(response) == 1
  sql = "set role test_user;"
  response = conn.execute(sql)
  sql = f"SELECT * FROM net.http_request_queue where id = {id};"
  response = conn.execute(sql).fetchall()
  assert len(response) == 1
  sql = "RESET ROLE;"
  response = conn.execute(sql)
  sql = "DROP ROLE test_user;"
  conn.execute(sql)
  conn.commit()


def test_queue_table_insert_non_default(conn) -> None:
  sql = "CREATE role test_role;"
  conn.execute(sql)
  sql = "CREATE USER test_user;"
  conn.execute(sql)
  sql = "GRANT test_role to test_user;"
  conn.execute(sql)

  sql = """
  insert into net.http_request_queue(method, url, headers, timeout_milliseconds, rolename)
    values (
        'GET',
        'http://localhost:8080',
        '{}'::jsonb,
        5000,
        'test_role'
    )
    returning id;
  """
  id = conn.execute(sql).fetchall()[0][0]
  conn.commit()
  sql = f"SELECT * FROM net.http_request_queue where id = {id};"
  response = conn.execute(sql).fetchall()
  assert len(response) == 1
  sql = f"set role test_user;"
  conn.execute(sql)
  sql = f"SELECT * FROM net.http_request_queue where id = {id};"
  response = conn.execute(sql).fetchall()
  assert len(response) == 1

  sql = "RESET ROLE"
  conn.execute(sql)

  sql = "REVOKE test_role FROM test_user;"
  conn.execute(sql)

  sql = "set role test_user;"
  conn.execute(sql)

  sql = f"SELECT * FROM net.http_request_queue where id = {id};"
  response = conn.execute(sql).fetchall()
  assert len(response) == 0
  sql = "RESET ROLE;"
  conn.execute(sql)
  sql = "DROP ROLE test_user;"
  conn.execute(sql)
  sql = "DROP ROLE test_role;"
  conn.execute(sql)
  conn.commit()
