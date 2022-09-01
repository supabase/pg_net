alter function net._encode_url_with_params_array ( text, text[]) strict;
alter table net.http_request_queue drop constraint http_request_queue_pkey;
alter table net._http_response drop constraint _http_response_pkey;
drop trigger ensure_worker_is_up on net.http_request_queue;
alter function net._check_worker_is_up() rename to check_worker_is_up;
drop index net._http_response_created_idx;
