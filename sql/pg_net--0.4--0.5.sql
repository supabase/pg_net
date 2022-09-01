alter function net._encode_url_with_params_array ( text, text[]) strict;
alter table net.http_request_queue drop constraint http_request_queue_pkey;
alter table net._http_response drop constraint _http_response_pkey;
