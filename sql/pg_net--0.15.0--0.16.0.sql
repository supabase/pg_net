alter function net._await_response(bigint) parallel unsafe called on null input;

alter function net._urlencode_string(varchar) called on null input;

alter function net._encode_url_with_params_array(text, text[]) called on null input;

alter function net._await_response(bigint) parallel unsafe called on null input;

alter function net.http_get(text, jsonb , jsonb , int) parallel unsafe called on null input;

alter function net.http_post(text, jsonb , jsonb , jsonb, int) parallel unsafe;

alter function net.http_delete(text, jsonb , jsonb, int, jsonb) parallel unsafe;

alter function net._http_collect_response(bigint, bool) parallel unsafe called on null input;

alter function net.http_collect_response(bigint, bool) parallel unsafe called on null input;
