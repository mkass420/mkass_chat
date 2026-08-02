from __future__ import annotations

import struct
from dataclasses import dataclass

from integration_common import TestCase, TestContext, require
from protocol import (
    ERROR_CODE_BUSY,
    ERROR_CODE_INCOMPLETE,
    ERROR_CODE_INVALID_STATE,
    ERROR_CODE_NOT_FOUND,
    FILE_CHUNK_DATA_SIZE,
    FILE_ID_SIZE,
    FILE_MAX_NAME_LENGTH,
    MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST,
    MSG_TYPE_FILE_DOWNLOAD_BEGIN_RESPONSE,
    MSG_TYPE_FILE_DOWNLOAD_CHUNK_REQUEST,
    MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE,
    MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST,
    MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE,
    MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST,
    MSG_TYPE_FILE_UPLOAD_CHUNK_RESPONSE,
    MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST,
    MSG_TYPE_FILE_UPLOAD_FINISH_RESPONSE,
    assert_error_response,
    assert_response,
    build_frame,
    deterministic_bytes,
    recv_frame,
)


@dataclass(frozen=True)
class DownloadResult:
    file_name: bytes
    data: bytes
    compressed_chunks: int


def _encode_upload_begin(file_name: bytes, file_size: int) -> bytes:
    require(len(file_name) > 0, "Имя файла не должно быть пустым")
    require(
        len(file_name) <= FILE_MAX_NAME_LENGTH,
        "Имя файла превышает тестовый лимит",
    )
    return struct.pack("!QH", file_size, len(file_name)) + file_name


def _decode_download_begin(payload: bytes) -> tuple[int, bytes]:
    require(len(payload) >= 10, "Слишком короткий DOWNLOAD_BEGIN_RESPONSE")
    file_size, name_len = struct.unpack("!QH", payload[:10])
    require(
        len(payload) == 10 + name_len,
        "Неверная длина DOWNLOAD_BEGIN_RESPONSE",
    )
    return file_size, payload[10:]


def _upload_file(
    context: TestContext,
    file_name: bytes,
    data: bytes,
    *,
    compress_chunks: bool,
) -> bytes:
    request_id = 20_000

    with context.connect() as sock:
        sock.sendall(
            build_frame(
                MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST,
                request_id,
                _encode_upload_begin(file_name, len(data)),
            )
        )
        assert_response(
            recv_frame(sock),
            expected_type=MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE,
            expected_request_id=request_id,
            expected_payload=b"",
            expected_compressed=False,
        )
        request_id += 1

        for offset in range(0, len(data), FILE_CHUNK_DATA_SIZE):
            chunk = data[offset : offset + FILE_CHUNK_DATA_SIZE]
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST,
                    request_id,
                    chunk,
                    compress=compress_chunks and len(chunk) >= 512,
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_FILE_UPLOAD_CHUNK_RESPONSE,
                expected_request_id=request_id,
                expected_payload=b"",
                expected_compressed=False,
            )
            request_id += 1

        sock.sendall(
            build_frame(
                MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST,
                request_id,
            )
        )
        response = recv_frame(sock)
        assert_response(
            response,
            expected_type=MSG_TYPE_FILE_UPLOAD_FINISH_RESPONSE,
            expected_request_id=request_id,
            expected_payload=response.payload,
            expected_compressed=False,
        )
        require(
            len(response.payload) == FILE_ID_SIZE,
            f"Неверный размер FileId: {len(response.payload)}",
        )
        return response.payload


def _download_file(context: TestContext, file_id: bytes) -> DownloadResult:
    request_id = 30_000

    with context.connect() as sock:
        sock.sendall(
            build_frame(
                MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST,
                request_id,
                file_id,
            )
        )
        begin_response = recv_frame(sock)
        require(
            begin_response.message_type == MSG_TYPE_FILE_DOWNLOAD_BEGIN_RESPONSE,
            f"Неверный DOWNLOAD_BEGIN response type={begin_response.message_type}",
        )
        require(
            begin_response.request_id == request_id,
            "DOWNLOAD_BEGIN_RESPONSE потерял request_id",
        )
        require(
            not begin_response.is_compressed,
            "DOWNLOAD_BEGIN_RESPONSE не должен быть сжат",
        )

        file_size, file_name = _decode_download_begin(begin_response.payload)
        request_id += 1

        output = bytearray()
        compressed_chunks = 0

        while len(output) < file_size:
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_DOWNLOAD_CHUNK_REQUEST,
                    request_id,
                )
            )
            response = recv_frame(sock)
            require(
                response.message_type == MSG_TYPE_FILE_DOWNLOAD_CHUNK_RESPONSE,
                f"Неверный DOWNLOAD_CHUNK response type={response.message_type}",
            )
            require(
                response.request_id == request_id,
                "DOWNLOAD_CHUNK_RESPONSE потерял request_id",
            )
            require(len(response.payload) > 0, "Сервер вернул пустой download chunk")
            require(
                len(response.payload) <= FILE_CHUNK_DATA_SIZE,
                "Download chunk превышает FILE_CHUNK_DATA_SIZE",
            )

            if response.is_compressed:
                compressed_chunks += 1

            output.extend(response.payload)
            require(
                len(output) <= file_size,
                "Сервер прислал больше байт, чем объявил в metadata",
            )
            request_id += 1

        return DownloadResult(
            file_name=file_name,
            data=bytes(output),
            compressed_chunks=compressed_chunks,
        )


def make_file_tests(context: TestContext) -> list[TestCase]:
    def test_small_binary_file_round_trip_after_reconnect() -> None:
        file_name = "пример.bin".encode("utf-8")
        data = b"binary\x00data\xff" + bytes(range(256))
        file_id = _upload_file(
            context,
            file_name,
            data,
            compress_chunks=False,
        )
        result = _download_file(context, file_id)

        require(result.file_name == file_name, "Имя скачанного файла отличается")
        require(result.data == data, "Содержимое скачанного файла отличается")

    def test_multichunk_compressed_file_round_trip() -> None:
        file_name = b"compressed-multichunk.dat"
        data = b"compressible file block\x00" * 7_000 + deterministic_bytes(
            7_000, seed=b"file-tail"
        )
        require(len(data) > FILE_CHUNK_DATA_SIZE * 2, "Тестовый файл слишком мал")

        file_id = _upload_file(
            context,
            file_name,
            data,
            compress_chunks=True,
        )
        result = _download_file(context, file_id)

        require(result.file_name == file_name, "Имя скачанного файла отличается")
        require(result.data == data, "Многочанковый файл повреждён")
        require(
            result.compressed_chunks > 0,
            "Сервер не сжал ни одного DOWNLOAD_CHUNK_RESPONSE",
        )

    def test_empty_file_round_trip() -> None:
        file_name = b"empty.txt"
        file_id = _upload_file(
            context,
            file_name,
            b"",
            compress_chunks=False,
        )
        result = _download_file(context, file_id)

        require(result.file_name == file_name, "Имя пустого файла отличается")
        require(result.data == b"", "Пустой файл неожиданно содержит данные")

    def test_unknown_file_id_returns_error() -> None:
        request_id = 40_000
        unknown_id = deterministic_bytes(FILE_ID_SIZE, seed=b"unknown-file-id")

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_DOWNLOAD_BEGIN_REQUEST,
                    request_id,
                    unknown_id,
                )
            )
            assert_error_response(
                recv_frame(sock),
                expected_request_id=request_id,
                expected_code=ERROR_CODE_NOT_FOUND,
                contains=b"not found",
            )

    def test_incomplete_upload_finish_returns_error() -> None:
        request_id = 41_000

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST,
                    request_id,
                    _encode_upload_begin(b"incomplete.bin", 100),
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE,
                expected_request_id=request_id,
                expected_payload=b"",
            )

            request_id += 1
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST,
                    request_id,
                    b"only a part",
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_FILE_UPLOAD_CHUNK_RESPONSE,
                expected_request_id=request_id,
                expected_payload=b"",
            )

            request_id += 1
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_FINISH_REQUEST,
                    request_id,
                )
            )
            assert_error_response(
                recv_frame(sock),
                expected_request_id=request_id,
                expected_code=ERROR_CODE_INCOMPLETE,
                contains=b"incomplete",
            )

    def test_file_chunk_without_begin_returns_error() -> None:
        request_id = 42_000

        with context.connect() as sock:
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_CHUNK_REQUEST,
                    request_id,
                    b"orphan chunk",
                )
            )
            assert_error_response(
                recv_frame(sock),
                expected_request_id=request_id,
                expected_code=ERROR_CODE_INVALID_STATE,
                contains=b"state",
            )

    def test_second_upload_begin_returns_busy() -> None:
        request_id = 43_000

        with context.connect() as sock:
            begin = _encode_upload_begin(b"first.bin", 1)
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST,
                    request_id,
                    begin,
                )
            )
            assert_response(
                recv_frame(sock),
                expected_type=MSG_TYPE_FILE_UPLOAD_BEGIN_RESPONSE,
                expected_request_id=request_id,
                expected_payload=b"",
            )

            request_id += 1
            sock.sendall(
                build_frame(
                    MSG_TYPE_FILE_UPLOAD_BEGIN_REQUEST,
                    request_id,
                    _encode_upload_begin(b"second.bin", 1),
                )
            )
            assert_error_response(
                recv_frame(sock),
                expected_request_id=request_id,
                expected_code=ERROR_CODE_BUSY,
                contains=b"active",
            )

    return [
        TestCase(
            "files",
            "маленький бинарный файл после переподключения",
            test_small_binary_file_round_trip_after_reconnect,
        ),
        TestCase(
            "files",
            "многочанковый файл со сжатием",
            test_multichunk_compressed_file_round_trip,
        ),
        TestCase("files", "пустой файл", test_empty_file_round_trip),
        TestCase("files", "неизвестный FileId", test_unknown_file_id_returns_error),
        TestCase(
            "files",
            "finish незавершённого upload",
            test_incomplete_upload_finish_returns_error,
        ),
        TestCase(
            "files",
            "upload chunk без begin",
            test_file_chunk_without_begin_returns_error,
        ),
        TestCase(
            "files",
            "повторный upload begin",
            test_second_upload_begin_returns_busy,
        ),
    ]
