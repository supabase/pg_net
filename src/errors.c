#include "pg_prelude.h"
#include "curl_prelude.h"
#include "errors.h"

/*
 * Show a more detailed error message when a timeout happens, which includes the DNS, TCP/SSL handshake and HTTP request/response time. An example message is like:
 *
 * "Timeout of 800 ms reached. Total time: 801.159000 ms (DNS time: 73.407000 ms, TCP/SSL handshake time: 677.256000 ms, HTTP Request/Respose time: 50.103000 ms)"
 *
 * Curl allows to calculate the above by applying substractions on some internal timings. Refer to https://blog.cloudflare.com/a-question-of-timing/ for an explanation of these timings.
 *
 * There are extra considerations:
 *
 * - If a step (e.g. TCP handshake [CURLINFO_CONNECT_TIME]) surpasses the request timeout, its given timing is 0.
 *   However the step duration can still be determined by using the total time (CURLINFO_TOTAL_TIME).
 *   We want to show at which step the timeout occurred.
 *
 * - If a step is omitted its given timing is 0. This can happen on non-HTTPS requests with the SSL handshake time (CURLINFO_APPCONNECT_TIME).
 *
 * - The pretransfer time (CURLINFO_PRETRANSFER_TIME) is greater than 0 when the HTTP request step starts.
 */
curl_timeout_msg detailed_timeout_strerror(CURL *ez_handle, int32 timeout_milliseconds){
  double namelookup;    EREPORT_CURL_GETINFO(ez_handle, CURLINFO_NAMELOOKUP_TIME,    &namelookup);
  double appconnect;    EREPORT_CURL_GETINFO(ez_handle, CURLINFO_APPCONNECT_TIME,    &appconnect);
  double connect;       EREPORT_CURL_GETINFO(ez_handle, CURLINFO_CONNECT_TIME,       &connect);
  double pretransfer;   EREPORT_CURL_GETINFO(ez_handle, CURLINFO_PRETRANSFER_TIME,   &pretransfer);
  double starttransfer; EREPORT_CURL_GETINFO(ez_handle, CURLINFO_STARTTRANSFER_TIME, &starttransfer);
  double total;         EREPORT_CURL_GETINFO(ez_handle, CURLINFO_TOTAL_TIME,         &total);

  elog(DEBUG2, "The curl timings are time_namelookup: %f, time_connect: %f, time_appconnect: %f, time_pretransfer: %f, time_starttransfer: %f, time_total: %f",
      namelookup, connect, appconnect, pretransfer, starttransfer, total);

  // Steps at which the request timed out
  bool timedout_at_dns       = namelookup == 0 && connect == 0; // if DNS time is 0 and no TCP occurred, it timed out at the DNS step
  bool timedout_at_handshake = pretransfer == 0; // pretransfer determines if the HTTP step started, if 0 no HTTP ocurred and thus the timeout occurred at TCP or SSL handshake step
  bool timedout_at_http      = pretransfer > 0; // The HTTP step did start and the timeout occurred here

  // Calculate the steps times
  double _dns_time =
    timedout_at_dns ?
      total: // get the total since namelookup will be 0 because of the timeout
    timedout_at_handshake ?
      namelookup:
    timedout_at_http ?
      namelookup:
    0;

  double _handshake_time =
    timedout_at_dns ?
      0:
    timedout_at_handshake ?
      total - namelookup: // connect or appconnect will be 0 because of the timeout, get the total - DNS step time
    timedout_at_http ?
      (connect - namelookup) +                    // TCP handshake time
      (appconnect > 0 ? (appconnect - connect): 0): // SSL handshake time. Prevent a negative here which can happen when no SSL is involved (plain HTTP request) and appconnect is 0
    0;

  double _http_time =
    timedout_at_dns ?
      0:
    timedout_at_handshake ?
      0:
    timedout_at_http ?
      total - pretransfer:
    0;

  // convert seconds to milliseconds
  double dns_time_ms       = _dns_time       * 1000;
  double handshake_time_ms = _handshake_time * 1000;
  double http_time_ms      = _http_time      * 1000;
  double total_time_ms     = total           * 1000;

  // build the error message
  curl_timeout_msg result = {.msg = {}};
  snprintf(result.msg, CURL_TIMEOUT_MSG_SIZE,
    "Timeout of %d ms reached. Total time: %f ms (DNS time: %f ms, TCP/SSL handshake time: %f ms, HTTP Request/Response time: %f ms)",
    timeout_milliseconds, total_time_ms, dns_time_ms, handshake_time_ms, http_time_ms
  );
  return result;
}
