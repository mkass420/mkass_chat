from __future__ import annotations

import socket
from dataclasses import dataclass
from typing import Callable


class TestFailure(AssertionError):
    """Ожидаемая ошибка интеграционного теста."""


@dataclass(frozen=True)
class TestCase:
    suite: str
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
