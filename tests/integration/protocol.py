from __future__ import annotations

import hashlib
import socket
import struct
import time
import zlib
from dataclasses import dataclass
from typing import Iterable, Optional

from integration_common import TestContext, TestFailure, require

PROTOCOL_MAGIC = 0x4348
PACKET_FLAG_COMPRESSED = 0x01
PACKET_KNOWN_FLAGS = PACKET_FLAG_COMPRESSED
PACKET_HEADER_FORMAT = "!HBBIIII"
PACKET_HEADER_SIZE = struct.calcsize(PACKET_HEADER_FORMAT)

PROTOCOL_MAX_PAYLOAD_LENGTH = 64 * 1024
PROTOCOL_MAX_UNCOMPRESSED_LENGTH = 256 * 1024

FILE_MAX_SIZE = 128 * 1024 * 1024
FILE_CHUNK_DATA_SIZE = 60 * 1024
FILE_ID_SIZE = 16
FILE_MAX_NAME_LENGTH = 255

MSG_TYPE_PING_REQUEST = 1
MSG_TYPE_PING_RESPONSE = 2
MSG_TYPE_ECHO_REQUEST = 3
MSG_TYPE_ECHO_RESPONSE = 4

MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST = 40
MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE = 41
MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST = 42
MSG_TYPE_FILE_UPLOAD_CHUNK_RESPONSE = 43
MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST = 44
MSG_TYPE_FILE_UPLOAD_FINISH_RESPONSE = 45
MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST = 46
MSG_TYPE_FILE_DOWNLOAD_BEGIN_RESPONSE = 47
MSG_TYPE_FILE_DOWNLOAD_CHUNK_REQUEST = 48
MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE = 49

MSG_TYPE_ERROR_RESPONSE = 255

assert PACKET_HEADER_SIZE == 20


@dataclass(frozen=True)
class ReceivedFrame:
    magic: int
    message_type: int
    flags: int
    request_id: int
    uncompressed_len: int
    payload_len: int
    payload_crc32: int
    wire_payload: bytes
    payload: bytes

    @property
    def is_compressed(self) -> bool:
        return (self.flags & PACKET_FLAG_COMPRESSED) != 0


def crc32(payload: bytes) -> int:
    return zlib.crc32(payload) & 0xFFFFFFFF


def deterministic_bytes(size: int, seed: bytes = b"mkass-chat") -> bytes:
    output = bytearray()
    counter = 0

    while len(output) < size:
        output.extend(
            hashlib.sha256(seed + struct.pack("!I", counter)).digest()
        )
        counter += 1

    return bytes(output[:size])


def build_raw_frame(
    message_type: int,
    request_id: int,
    wire_payload: bytes = b"",
    *,
    magic: int = PROTOCOL_MAGIC,
    flags: int = 0,
    uncompressed_len: Optional[int] = None,
    declared_payload_len: Optional[int] = None,
    payload_crc32: Optional[int] = None,
) -> bytes:
    """Формирует wire-frame, включая намеренно некорректные варианты."""
    if uncompressed_len is None:
        uncompressed_len = len(wire_payload)
    if declared_payload_len is None:
        declared_payload_len = len(wire_payload)
    if payload_crc32 is None:
        payload_crc32 = crc32(wire_payload)

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

    return header + wire_payload


def build_frame(
    message_type: int,
    request_id: int,
    payload: bytes = b"",
    *,
    compress: bool = False,
) -> bytes:
    """Формирует корректный frame из логического payload."""
    if len(payload) > PROTOCOL_MAX_UNCOMPRESSED_LENGTH:
        raise ValueError("Логический payload превышает протокольный лимит")

    if compress:
        if not payload:
            raise ValueError("Пустой payload не должен сжиматься")
        wire_payload = zlib.compress(payload)
        flags = PACKET_FLAG_COMPRESSED
    else:
        wire_payload = payload
        flags = 0

    if len(wire_payload) > PROTOCOL_MAX_PAYLOAD_LENGTH:
        raise ValueError("Wire payload превышает протокольный лимит")

    return build_raw_frame(
        message_type,
        request_id,
        wire_payload,
        flags=flags,
        uncompressed_len=len(payload),
    )


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


def _decompress_exact(wire_payload: bytes, expected_size: int) -> bytes:
    decompressor = zlib.decompressobj()

    try:
        payload = decompressor.decompress(
            wire_payload,
            expected_size + 1,
        )
        payload += decompressor.flush()
    except zlib.error as exc:
        raise TestFailure(f"Сервер прислал повреждённый zlib stream: {exc}") from exc

    require(decompressor.eof, "Сжатый ответ не завершён Z_STREAM_END")
    require(
        decompressor.unused_data == b"",
        "После zlib stream присутствуют лишние байты",
    )
    require(
        decompressor.unconsumed_tail == b"",
        "zlib stream не был потреблён полностью",
    )
    require(
        len(payload) == expected_size,
        (
            "Размер распакованного ответа отличается: "
            f"получено {len(payload)}, ожидалось {expected_size}"
        ),
    )

    return payload


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

    require(magic == PROTOCOL_MAGIC, f"Неверный magic в ответе: {magic:#06x}")
    require(
        (flags & ~PACKET_KNOWN_FLAGS) == 0,
        f"Неизвестные flags в ответе: {flags:#04x}",
    )
    require(
        payload_len <= PROTOCOL_MAX_PAYLOAD_LENGTH,
        f"Сервер объявил слишком большой payload_len={payload_len}",
    )
    require(
        uncompressed_len <= PROTOCOL_MAX_UNCOMPRESSED_LENGTH,
        f"Сервер объявил слишком большой uncompressed_len={uncompressed_len}",
    )

    wire_payload = recv_exact(sock, payload_len)
    actual_crc32 = crc32(wire_payload)

    require(
        actual_crc32 == expected_crc32,
        (
            "Неверный CRC ответа: "
            f"ожидался {expected_crc32:#010x}, "
            f"получен {actual_crc32:#010x}"
        ),
    )

    if (flags & PACKET_FLAG_COMPRESSED) != 0:
        payload = _decompress_exact(wire_payload, uncompressed_len)
    else:
        require(
            uncompressed_len == payload_len,
            "Для несжатого ответа uncompressed_len не равен payload_len",
        )
        payload = wire_payload

    return ReceivedFrame(
        magic=magic,
        message_type=message_type,
        flags=flags,
        request_id=request_id,
        uncompressed_len=uncompressed_len,
        payload_len=payload_len,
        payload_crc32=expected_crc32,
        wire_payload=wire_payload,
        payload=payload,
    )


def assert_response(
    frame: ReceivedFrame,
    *,
    expected_type: int,
    expected_request_id: int,
    expected_payload: bytes,
    expected_compressed: Optional[bool] = None,
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

    if expected_compressed is not None:
        require(
            frame.is_compressed == expected_compressed,
            (
                "Неверный режим сжатия ответа: "
                f"compressed={frame.is_compressed}, "
                f"ожидалось {expected_compressed}"
            ),
        )


def assert_error_response(
    frame: ReceivedFrame,
    *,
    expected_request_id: int,
    contains: Optional[bytes] = None,
) -> None:
    require(
        frame.message_type == MSG_TYPE_ERROR_RESPONSE,
        f"Ожидался ERROR_RESPONSE, получен type={frame.message_type}",
    )
    require(
        frame.request_id == expected_request_id,
        f"Неверный request_id ошибки: {frame.request_id}",
    )
    require(not frame.is_compressed, "ERROR_RESPONSE не должен быть сжат")

    if contains is not None:
        require(
            contains.lower() in frame.payload.lower(),
            f"Текст ошибки {frame.payload!r} не содержит {contains!r}",
        )


def expect_disconnect_after(
    context: TestContext,
    raw_frame: bytes,
    *,
    shutdown_write: bool = False,
) -> None:
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
