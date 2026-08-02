from __future__ import annotations

import concurrent.futures
import struct

from integration_common import TestCase, TestContext
from protocol import (
    ERROR_CODE_NOT_IMPLEMENTED,
    MSG_TYPE_ECHO_REQUEST,
    MSG_TYPE_ECHO_RESPONSE,
    MSG_TYPE_PING_REQUEST,
    MSG_TYPE_PING_RESPONSE,
    MSG_TYPE_REGISTER_REQUEST,
    PROTOCOL_MAX_PAYLOAD_LENGTH,
    assert_error_response,
    assert_response,
    build_frame,
    deterministic_bytes,
    recv_frame,
)


def make_service_tests(context: TestContext) -> list[TestCase]:
    def test_ping_and_binary_echo() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_PING_REQUEST, request_id=1))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_PING_RESPONSE,
                expected_request_id=1,
                expected_payload=b"",
                expected_compressed=False,
            )

            payload = b"Hello, transport!\x00\x01\x02\x03\x7f\x80\xfe\xff" + bytes(
                range(32)
            )
            sock.sendall(
                build_frame(MSG_TYPE_ECHO_REQUEST, request_id=2, payload=payload)
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=2,
                expected_payload=payload,
            )

    def test_unsupported_request_returns_structured_error() -> None:
        request_id = 4

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_REGISTER_REQUEST,
                    request_id=request_id,
                    payload=b"not implemented",
                )
            )
            assert_error_response(
                recv_frame(sock),
                expected_request_id=request_id,
                expected_code=ERROR_CODE_NOT_IMPLEMENTED,
                contains=b"not implemented",
            )

    def test_empty_echo() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_ECHO_REQUEST, request_id=3))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=3,
                expected_payload=b"",
                expected_compressed=False,
            )

    def test_maximum_wire_payload() -> None:
        payload = deterministic_bytes(
            PROTOCOL_MAX_PAYLOAD_LENGTH,
            seed=b"maximum-wire-payload",
        )

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_ECHO_REQUEST,
                    request_id=300,
                    payload=payload,
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=300,
                expected_payload=payload,
                expected_compressed=False,
            )

    def test_reconnect_and_connection_reuse() -> None:
        for index in range(context.reconnects):
            request_id = 1_000 + index

            with context.connect() as sock:
                sock.sendall(build_frame(MSG_TYPE_PING_REQUEST, request_id=request_id))
                assert_response(
                    recv_frame(sock),
                    expected_type=MSG_TYPE_PING_RESPONSE,
                    expected_request_id=request_id,
                    expected_payload=b"",
                )

    def concurrent_worker(worker_id: int) -> None:
        with context.connect() as sock:
            for iteration in range(context.iterations):
                request_id = 10_000 + worker_id * context.iterations + iteration
                payload = struct.pack("!II", worker_id, iteration) + bytes(
                    (worker_id + iteration + offset) % 256 for offset in range(128)
                )
                sock.sendall(
                    build_frame(
                        MSG_TYPE_ECHO_REQUEST,
                        request_id=request_id,
                        payload=payload,
                    )
                )
                assert_response(
                    recv_frame(sock),
                    expected_type=MSG_TYPE_ECHO_RESPONSE,
                    expected_request_id=request_id,
                    expected_payload=payload,
                )

    def test_concurrent_clients() -> None:
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=context.clients
        ) as executor:
            futures = [
                executor.submit(concurrent_worker, worker_id)
                for worker_id in range(context.clients)
            ]

            for future in concurrent.futures.as_completed(futures):
                future.result()

    return [
        TestCase("service", "PING и бинарный ECHO", test_ping_and_binary_echo),
        TestCase(
            "service",
            "структурированная ошибка",
            test_unsupported_request_returns_structured_error,
        ),
        TestCase("service", "пустой ECHO payload", test_empty_echo),
        TestCase(
            "service", "максимальный wire payload 64 КиБ", test_maximum_wire_payload
        ),
        TestCase(
            "service",
            "переподключения и повторное использование соединений",
            test_reconnect_and_connection_reuse,
        ),
        TestCase(
            "service", "несколько одновременных клиентов", test_concurrent_clients
        ),
    ]
