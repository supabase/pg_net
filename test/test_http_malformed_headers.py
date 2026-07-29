from sqlalchemy import text
from common import collect_response_sync, http_request


def test_http_header_missing_value(sess):
    """Check that a `MissingValue: ` header is processed correctly"""

    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=missing-value'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert "MissingValue" in response["headers"]


def test_http_header_injection(sess):
    """
    Check that a `HeaderInjection Injected-Header: This header
    contains an injection` header fails without crashing
    """

    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=header-injection'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "ERROR"
    assert "Weird server reply" in response["message"]


def test_http_header_spaces(sess):
    """
    Check that a `Spaces In Header Name: This header name contains spaces`
    header is processed correctly
    """

    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=spaces-in-header-name'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert "Spaces In Header Name" in response["headers"]


def test_http_header_non_printable_chars(sess):
    """
    Check that a `NonPrintableChars: NonPrintableChars\\u0001\\u0002`
    header is processed correctly
    """

    request_id = http_request(sess, text(
        """
        select net.http_get(
            url:='http://localhost:8080/pathological?malformed-header=non-printable-chars'
        );
    """
    ))

    response = collect_response_sync(sess, request_id)

    assert response is not None
    assert response["status"] == "SUCCESS"
    assert response["headers"]["NonPrintableChars"] == "NonPrintableChars\x01\x02"
