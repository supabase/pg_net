create extension curl_worker;

select http.async_get('http://supabase.io');

select *
from http.request_queue;

