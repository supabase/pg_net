## Roadmap

- [ ] Create a requests table and populate it
- [ ] Read rows from the requests table inside the background worker(use [postgres SPI](https://www.postgresql.org/docs/current/spi.html))
- [ ] Create a [curl multi handle](https://curl.se/libcurl/c/curl_multi_add_handle.html) for each of the rows and make the requests
