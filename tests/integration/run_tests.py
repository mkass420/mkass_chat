#!/usr/bin/env python3
from __future__ import annotations

import argparse
import socket
import sys
import time

from compression_cases import make_compression_tests
from file_cases import make_file_tests
from integration_common import TestCase, TestContext
from protocol_error_cases import make_protocol_error_tests
from service_cases import make_service_tests
from transport_cases import make_transport_tests

ALL_SUITES = (
    "service",
    "transport",
    "compression",
    "protocol-errors",
    "files",
)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Интеграционные тесты chat-server"
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--timeout", type=float, default=2.0)
    parser.add_argument("--startup-timeout", type=float, default=5.0)
    parser.add_argument("--clients", type=int, default=8)
    parser.add_argument("--iterations", type=int, default=25)
    parser.add_argument("--reconnects", type=int, default=20)
    parser.add_argument(
        "--suite",
        action="append",
        choices=ALL_SUITES,
        help="запустить только выбранный набор; можно указать несколько раз",
    )
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
    if args.startup_timeout <= 0:
        parser.error("--startup-timeout должен быть положительным")
    if args.clients <= 0:
        parser.error("--clients должен быть положительным")
    if args.iterations <= 0:
        parser.error("--iterations должен быть положительным")
    if args.reconnects <= 0:
        parser.error("--reconnects должен быть положительным")

    return args


def collect_tests(context: TestContext) -> list[TestCase]:
    return [
        *make_service_tests(context),
        *make_transport_tests(context),
        *make_compression_tests(context),
        *make_protocol_error_tests(context),
        *make_file_tests(context),
    ]


def wait_for_server(context: TestContext, startup_timeout: float) -> None:
    deadline = time.monotonic() + startup_timeout
    last_error: OSError | None = None

    while time.monotonic() < deadline:
        try:
            with socket.create_connection(
                (context.host, context.port),
                timeout=min(context.timeout, 0.25),
            ):
                return
        except OSError as exc:
            last_error = exc
            time.sleep(0.05)

    raise RuntimeError(
        f"Сервер {context.host}:{context.port} не запустился: {last_error}"
    )


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

    tests = collect_tests(context)

    if args.suite:
        selected = set(args.suite)
        tests = [test for test in tests if test.suite in selected]

    if args.only:
        needle = args.only.casefold()
        tests = [test for test in tests if needle in test.name.casefold()]

    if not tests:
        print("Подходящие тесты не найдены", file=sys.stderr)
        return 2

    if args.list:
        for index, test in enumerate(tests, start=1):
            print(f"{index:2}. [{test.suite}] {test.name}")
        return 0

    wait_for_server(context, args.startup_timeout)

    print(
        f"Сервер: {context.host}:{context.port}\n"
        f"Тайм-аут: {context.timeout:.2f} с\n"
        f"Параллельные клиенты: {context.clients}, "
        f"итераций на клиента: {context.iterations}\n"
        f"Наборы: {', '.join(sorted({test.suite for test in tests}))}\n"
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
                f"[{test.suite}] {test.name} ({elapsed:.3f} с)"
            )
            print(f"       {type(exc).__name__}: {exc}")
        else:
            elapsed = time.perf_counter() - started
            passed += 1
            print(
                f"[PASS] {index:02}/{len(tests):02} "
                f"[{test.suite}] {test.name} ({elapsed:.3f} с)"
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
