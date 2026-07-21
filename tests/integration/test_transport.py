#!/usr/bin/env python3
"""
Интеграционные тесты минимального TCP-транспорта chat-server.

Проверяется:
  - PING_REQUEST -> PING_RESPONSE;
  - ECHO_REQUEST -> ECHO_RESPONSE, включая бинарные данные и пустой payload;
  - сохранение request_id;
  - максимальный разрешённый payload;
  - frame, отправленный мелкими частями;
  - несколько frames в одном TCP send;
  - повторное использование сессий после переподключений;
  - несколько одновременных клиентов;
  - закрытие соединения при нарушениях протокола;
  - работоспособность сервера после ошибочных клиентов.

Сервер должен быть запущен отдельно, например:
    ./build/chat-server 127.0.0.1 5555

Запуск тестов:
    python3 test_transport.py
    python3 test_transport.py --host 127.0.0.1 --port 5555
    python3 test_transport.py --clients 16 --iterations 50
"""

from __future__ import annotations

import argparse
import concurrent.futures
import socket
import struct
import sys
import time
import zlib
from dataclasses import dataclass
from typing import Callable, Iterable, Optional


# ---------------------------------------------------------------------------
# Protocol constants
# ---------------------------------------------------------------------------

PROTOCOL_MAGIC = 0x4348

PACKET_FLAG_COMPRESSED = 0x01

PACKET_HEADER_FORMAT = "!HBBIIII"
PACKET_HEADER_SIZE = struct.calcsize(PACKET_HEADER_FORMAT)

PROTOCOL_MAX_PAYLOAD_LENGTH = 64 * 1024
PROTOCOL_MAX_UNCOMPRESSED_LENGTH = 256 * 1024

MSG_TYPE_PING_REQUEST = 1
MSG_TYPE_PING_RESPONSE = 2
MSG_TYPE_ECHO_REQUEST = 3
MSG_TYPE_ECHO_RESPONSE = 4

assert PACKET_HEADER_SIZE == 20


# ---------------------------------------------------------------------------
# Test infrastructure
# ---------------------------------------------------------------------------

class TestFailure(AssertionError):
    """Ожидаемая ошибка интеграционного теста."""


@dataclass(frozen=True)
class ReceivedFrame:
    magic: int
    message_type: int
    flags: int
    request_id: int
    uncompressed_len: int
    payload_len: int
    payload_crc32: int
    payload: bytes


@dataclass(frozen=True)
class TestCase:
    name: str
    function: Callable[[], None]


@dataclass
class TestContext:
    host: str
    port: int
    timeout: float
    clients: int
    iterations: int
    reconnects: int

    def connect(self) -> socket.socket:
        try:
            sock = socket.create_connection(
                (self.host, self.port),
                timeout=self.timeout,
            )
        except OSError as exc:
            raise TestFailure(
                f"Не удалось подключиться к {self.host}:{self.port}: {exc}"
            ) from exc

        sock.settimeout(self.timeout)
        return sock


def require(condition: bool, message: str) -> None:
    if not condition:
        raise TestFailure(message)


def crc32(payload: bytes) -> int:
    return zlib.crc32(payload) & 0xFFFFFFFF


def build_frame(
    message_type: int,
    request_id: int,
    payload: bytes = b"",
    *,
    magic: int = PROTOCOL_MAGIC,
    flags: int = 0,
    uncompressed_len: Optional[int] = None,
    declared_payload_len: Optional[int] = None,
    payload_crc32: Optional[int] = None,
) -> bytes:
    """Формирует frame, включая намеренно некорректные варианты для тестов."""
    if uncompressed_len is None:
        uncompressed_len = len(payload)

    if declared_payload_len is None:
        declared_payload_len = len(payload)

    if payload_crc32 is None:
        payload_crc32 = crc32(payload)

    header = struct.pack(
        PACKET_HEADER_FORMAT,
        magic,
        message_type,
        flags,
        request_id,
        uncompressed_len,
        declared_payload_len,
        payload_crc32,
    )

    return header + payload


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()

    while len(data) < size:
        try:
            chunk = sock.recv(size - len(data))
        except socket.timeout as exc:
            raise TestFailure(
                f"Тайм-аут при чтении: получено {len(data)} из {size} байт"
            ) from exc
        except OSError as exc:
            raise TestFailure(f"Ошибка recv(): {exc}") from exc

        if not chunk:
            raise TestFailure(
                f"Сервер закрыл соединение: получено {len(data)} из {size} байт"
            )

        data.extend(chunk)

    return bytes(data)


def recv_frame(sock: socket.socket) -> ReceivedFrame:
    raw_header = recv_exact(sock, PACKET_HEADER_SIZE)

    (
        magic,
        message_type,
        flags,
        request_id,
        uncompressed_len,
        payload_len,
        expected_crc32,
    ) = struct.unpack(PACKET_HEADER_FORMAT, raw_header)

    require(
        payload_len <= PROTOCOL_MAX_PAYLOAD_LENGTH,
        f"Сервер объявил слишком большой payload_len={payload_len}",
    )

    payload = recv_exact(sock, payload_len)
    actual_crc32 = crc32(payload)

    require(magic == PROTOCOL_MAGIC, f"Неверный magic в ответе: {magic:#06x}")
    require(flags == 0, f"Неожиданные flags в несжатом ответе: {flags:#04x}")
    require(
        uncompressed_len == payload_len,
        "Для несжатого ответа uncompressed_len не равен payload_len",
    )
    require(
        actual_crc32 == expected_crc32,
        (
            "Неверный CRC ответа: "
            f"ожидался {expected_crc32:#010x}, "
            f"получен {actual_crc32:#010x}"
        ),
    )

    return ReceivedFrame(
        magic=magic,
        message_type=message_type,
        flags=flags,
        request_id=request_id,
        uncompressed_len=uncompressed_len,
        payload_len=payload_len,
        payload_crc32=expected_crc32,
        payload=payload,
    )


def assert_response(
    frame: ReceivedFrame,
    *,
    expected_type: int,
    expected_request_id: int,
    expected_payload: bytes,
) -> None:
    require(
        frame.message_type == expected_type,
        f"Неверный тип ответа: {frame.message_type}, ожидался {expected_type}",
    )
    require(
        frame.request_id == expected_request_id,
        f"Неверный request_id: {frame.request_id}, ожидался {expected_request_id}",
    )
    require(
        frame.payload == expected_payload,
        (
            "Payload ответа отличается: "
            f"получено {len(frame.payload)} байт, "
            f"ожидалось {len(expected_payload)}"
        ),
    )


def expect_disconnect_after(
    context: TestContext,
    raw_frame: bytes,
    *,
    shutdown_write: bool = False,
) -> None:
    """Требует закрытия соединения без ответа после некорректного frame."""
    with context.connect() as sock:
        sock.sendall(raw_frame)

        if shutdown_write:
            sock.shutdown(socket.SHUT_WR)

        try:
            data = sock.recv(1)
        except (ConnectionResetError, BrokenPipeError):
            return
        except socket.timeout as exc:
            raise TestFailure(
                "Сервер не закрыл соединение после ошибки протокола"
            ) from exc
        except OSError:
            return

        require(
            data == b"",
            f"Ожидалось закрытие соединения, сервер прислал: {data!r}",
        )


def send_in_chunks(
    sock: socket.socket,
    data: bytes,
    chunk_sizes: Iterable[int],
    delay: float,
) -> None:
    offset = 0

    for chunk_size in chunk_sizes:
        if offset >= len(data):
            break

        end = min(offset + chunk_size, len(data))
        sock.sendall(data[offset:end])
        offset = end

        if delay > 0:
            time.sleep(delay)

    if offset < len(data):
        sock.sendall(data[offset:])


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def make_tests(context: TestContext) -> list[TestCase]:
    def test_ping_and_binary_echo() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_PING_REQUEST, request_id=1))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_PING_RESPONSE,
                expected_request_id=1,
                expected_payload=b"",
            )

            payload = (
                b"Hello, transport!\x00"
                b"\x01\x02\x03\x7f\x80\xfe\xff"
                + bytes(range(32))
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

    def test_empty_echo() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_ECHO_REQUEST, request_id=3))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=3,
                expected_payload=b"",
            )

    def test_fragmented_frame() -> None:
        payload = b"Header and payload intentionally fragmented over TCP."
        raw_frame = build_frame(
            MSG_TYPE_ECHO_REQUEST,
            request_id=100,
            payload=payload,
        )

        with context.connect() as sock:
            send_in_chunks(
                sock,
                raw_frame,
                chunk_sizes=[1] * PACKET_HEADER_SIZE + [2, 3, 5, 7],
                delay=0.001,
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_ECHO_RESPONSE,
                expected_request_id=100,
                expected_payload=payload,
            )

    def test_multiple_frames_in_one_send() -> None:
        requests = [
            (MSG_TYPE_PING_REQUEST, 201, b"", MSG_TYPE_PING_RESPONSE),
            (MSG_TYPE_ECHO_REQUEST, 202, b"first", MSG_TYPE_ECHO_RESPONSE),
            (
                MSG_TYPE_ECHO_REQUEST,
                203,
                b"second\x00binary\xff",
                MSG_TYPE_ECHO_RESPONSE,
            ),
            (MSG_TYPE_PING_REQUEST, 204, b"", MSG_TYPE_PING_RESPONSE),
        ]

        raw = b"".join(
            build_frame(message_type, request_id, payload)
            for message_type, request_id, payload, _ in requests
        )

        with context.connect() as sock:
            sock.sendall(raw)

            for _, request_id, payload, expected_type in requests:
                assert_response(
                    recv_frame(sock),
                    expected_type=expected_type,
                    expected_request_id=request_id,
                    expected_payload=payload,
                )

    def test_maximum_payload() -> None:
        payload = bytes(index % 251 for index in range(PROTOCOL_MAX_PAYLOAD_LENGTH))

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
            )

    def test_reconnect_and_session_reuse() -> None:
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
                payload = (
                    struct.pack("!II", worker_id, iteration)
                    + bytes(
                        (worker_id + iteration + offset) % 256
                        for offset in range(128)
                    )
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

    def test_invalid_magic_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(MSG_TYPE_PING_REQUEST, request_id=400, magic=0x1234),
        )

    def test_unknown_type_disconnects() -> None:
        expect_disconnect_after(context, build_frame(254, request_id=401))

    def test_unknown_flags_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(MSG_TYPE_PING_REQUEST, request_id=402, flags=0x80),
        )

    def test_zero_request_id_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(MSG_TYPE_PING_REQUEST, request_id=0),
        )

    def test_payload_policy_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_PING_REQUEST,
                request_id=403,
                payload=b"not allowed",
            ),
        )

    def test_invalid_crc_disconnects() -> None:
        payload = b"CRC must not match"
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=404,
                payload=payload,
                payload_crc32=crc32(payload) ^ 0xFFFFFFFF,
            ),
        )

    def test_uncompressed_length_mismatch_disconnects() -> None:
        payload = b"length mismatch"
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=405,
                payload=payload,
                uncompressed_len=len(payload) + 1,
            ),
        )

    def test_oversized_payload_header_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=406,
                uncompressed_len=PROTOCOL_MAX_PAYLOAD_LENGTH + 1,
                declared_payload_len=PROTOCOL_MAX_PAYLOAD_LENGTH + 1,
                payload_crc32=0,
            ),
        )

    def test_oversized_uncompressed_length_disconnects() -> None:
        payload = b"x"
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=407,
                payload=payload,
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=PROTOCOL_MAX_UNCOMPRESSED_LENGTH + 1,
            ),
        )

    def test_compression_not_supported_disconnects() -> None:
        payload = b"syntactically valid compressed payload"
        expect_disconnect_after(
            context,
            build_frame(
                MSG_TYPE_ECHO_REQUEST,
                request_id=408,
                payload=payload,
                flags=PACKET_FLAG_COMPRESSED,
                uncompressed_len=len(payload) * 2,
            ),
        )

    def test_server_message_from_client_disconnects() -> None:
        expect_disconnect_after(
            context,
            build_frame(MSG_TYPE_PING_RESPONSE, request_id=409),
        )

    def test_truncated_frame_disconnects_after_eof() -> None:
        raw = build_frame(
            MSG_TYPE_ECHO_REQUEST,
            request_id=410,
            payload=b"complete payload",
        )
        expect_disconnect_after(context, raw[:7], shutdown_write=True)

    def test_server_survives_protocol_errors() -> None:
        with context.connect() as sock:
            sock.sendall(build_frame(MSG_TYPE_PING_REQUEST, request_id=500))
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_PING_RESPONSE,
                expected_request_id=500,
                expected_payload=b"",
            )

    return [
        TestCase("PING и бинарный ECHO", test_ping_and_binary_echo),
        TestCase("пустой ECHO payload", test_empty_echo),
        TestCase("фрагментированный TCP frame", test_fragmented_frame),
        TestCase("несколько frames в одном send", test_multiple_frames_in_one_send),
        TestCase("максимальный payload 64 КиБ", test_maximum_payload),
        TestCase(
            "переподключения и повторное использование сессий",
            test_reconnect_and_session_reuse,
        ),
        TestCase("несколько одновременных клиентов", test_concurrent_clients),
        TestCase("неверный magic", test_invalid_magic_disconnects),
        TestCase("неизвестный MessageType", test_unknown_type_disconnects),
        TestCase("неизвестные flags", test_unknown_flags_disconnects),
        TestCase("request_id == 0 у запроса", test_zero_request_id_disconnects),
        TestCase("нарушение payload policy", test_payload_policy_disconnects),
        TestCase("неверный CRC32", test_invalid_crc_disconnects),
        TestCase(
            "несовпадение несжатых длин",
            test_uncompressed_length_mismatch_disconnects,
        ),
        TestCase("payload_len выше лимита", test_oversized_payload_header_disconnects),
        TestCase(
            "uncompressed_len выше лимита",
            test_oversized_uncompressed_length_disconnects,
        ),
        TestCase(
            "пока неподдерживаемое сжатие",
            test_compression_not_supported_disconnects,
        ),
        TestCase(
            "server response, присланный клиентом",
            test_server_message_from_client_disconnects,
        ),
        TestCase(
            "оборванный frame и EOF",
            test_truncated_frame_disconnects_after_eof,
        ),
        TestCase(
            "сервер жив после ошибочных клиентов",
            test_server_survives_protocol_errors,
        ),
    ]


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Интеграционные тесты TCP-транспорта chat-server"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--clients", type=int, default=8)
    parser.add_argument("--iterations", type=int, default=25)
    parser.add_argument("--reconnects", type=int, default=20)
    parser.add_argument(
        "--only",
        help="запустить только тесты, содержащие подстроку в названии",
    )
    parser.add_argument("--list", action="store_true")

    args = parser.parse_args()

    if not (1 <= args.port <= 65535):
        parser.error("--port должен быть в диапазоне 1..65535")
    if args.timeout <= 0:
        parser.error("--timeout должен быть положительным")
    if args.clients <= 0:
        parser.error("--clients должен быть положительным")
    if args.iterations <= 0:
        parser.error("--iterations должен быть положительным")
    if args.reconnects <= 0:
        parser.error("--reconnects должен быть положительным")

    return args


def main() -> int:
    args = parse_arguments()
    context = TestContext(
        host=args.host,
        port=args.port,
        timeout=args.timeout,
        clients=args.clients,
        iterations=args.iterations,
        reconnects=args.reconnects,
    )

    tests = make_tests(context)

    if args.only:
        needle = args.only.casefold()
        tests = [test for test in tests if needle in test.name.casefold()]
        if not tests:
            print(f"Тесты по фильтру {args.only!r} не найдены", file=sys.stderr)
            return 2

    if args.list:
        for index, test in enumerate(tests, start=1):
            print(f"{index:2}. {test.name}")
        return 0

    print(
        f"Сервер: {context.host}:{context.port}\n"
        f"Тайм-аут: {context.timeout:.2f} с\n"
        f"Параллельные клиенты: {context.clients}, "
        f"итераций на клиента: {context.iterations}\n"
    )

    passed = 0
    failed = 0
    total_started = time.perf_counter()

    for index, test in enumerate(tests, start=1):
        started = time.perf_counter()

        try:
            test.function()
        except KeyboardInterrupt:
            print("\nТестирование прервано пользователем")
            return 130
        except Exception as exc:
            elapsed = time.perf_counter() - started
            failed += 1
            print(
                f"[FAIL] {index:02}/{len(tests):02} "
                f"{test.name} ({elapsed:.3f} с)"
            )
            print(f"       {type(exc).__name__}: {exc}")
        else:
            elapsed = time.perf_counter() - started
            passed += 1
            print(
                f"[PASS] {index:02}/{len(tests):02} "
                f"{test.name} ({elapsed:.3f} с)"
            )

    total_elapsed = time.perf_counter() - total_started

    print(
        "\nИтог:\n"
        f"  успешно: {passed}\n"
        f"  ошибок:  {failed}\n"
        f"  всего:   {len(tests)}\n"
        f"  время:   {total_elapsed:.3f} с"
    )

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
