ALTER FUNCTION net._await_response (bigint) PARALLEL unsafe
                                            CALLED ON NULL INPUT;

ALTER FUNCTION net._urlencode_string (varchar) CALLED ON NULL INPUT;

ALTER FUNCTION net._encode_url_with_params_array (text, text[]) CALLED ON NULL INPUT;

ALTER FUNCTION net._await_response (bigint) PARALLEL unsafe
                                            CALLED ON NULL INPUT;

ALTER FUNCTION net.http_get (text, jsonb, jsonb, integer) PARALLEL unsafe
                                                          CALLED ON NULL INPUT;

ALTER FUNCTION net.http_post (text, jsonb, jsonb, jsonb, integer) PARALLEL unsafe;

ALTER FUNCTION net.http_delete (text, jsonb, jsonb, integer, jsonb) PARALLEL unsafe;

ALTER FUNCTION net._http_collect_response (bigint, bool) PARALLEL unsafe
                                                         CALLED ON NULL INPUT;

ALTER FUNCTION net.http_collect_response (bigint, bool) PARALLEL unsafe
                                                        CALLED ON NULL INPUT
