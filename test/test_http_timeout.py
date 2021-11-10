import time

import pytest
from sqlalchemy import text

# def test_http_get_err_on_default_timeout(sess):
#     """net.http_get with default timeout errs on a slow reply"""

#     (request_id,) = sess.execute(
#         """
#         select net.http_get(url := 'http://localhost:8080/slow-reply');
#     """
#     ).fetchone()

#     sess.commit()

#     time.sleep(2)

#     (response,) = sess.execute(
#         text(
#             """
#         select error_msg from net._http_response where id = :request_id;
#     """
#         ),
#         {"request_id": request_id},
#     ).fetchone()

#     assert response == u'Timeout was reached'

# def test_http_get_succeed_with_gt_timeout(sess):
#     """net.http_get with a timeout greater than the default one succeeds on a slow reply"""

#     (request_id,) = sess.execute(
#         """
#         select net.http_get(url := 'http://localhost:8080/slow-reply', timeout_milliseconds := 2500);
#     """
#     ).fetchone()

#     sess.commit()

#     time.sleep(2.5)

#     (response,) = sess.execute(
#         text(
#             """
#         select content from net._http_response where id = :request_id;
#     """
#         ),
#         {"request_id": request_id},
#     ).fetchone()

#     assert 'after 2 seconds' in str(response)
