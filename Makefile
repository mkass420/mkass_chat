CC = gcc

CPPFLAGS = -Iinclude -Itests/unit
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -g
LDLIBS = -lz -lsqlite3

COMMON_SRC = \
	src/common/protocol.c \
	src/common/frame.c

SERVER_CORE_SRC = \
	src/server/session.c \
	src/server/dispatcher.c

SERVER_SRC = \
	src/server/main.c \
	src/server/server.c \
	$(SERVER_CORE_SRC) \
	$(COMMON_SRC)

UNIT_TEST_SRC = \
	tests/unit/test_main.c \
	tests/unit/test_protocol.c \
	tests/unit/test_frame.c \
	tests/unit/test_session.c \
	tests/unit/test_dispatcher.c \
	$(SERVER_CORE_SRC) \
	$(COMMON_SRC)

.PHONY: all server test test-unit test-unit-sanitize clean

all: server

build:
	mkdir -p build

server: build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SERVER_SRC) -o build/chat-server $(LDLIBS)

test: test-unit test-integration

test-unit: build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(UNIT_TEST_SRC) -o build/unit-tests $(LDLIBS)
	./build/unit-tests

test-unit-sanitize: build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(UNIT_TEST_SRC) -o build/unit-tests-sanitize $(LDLIBS)
	ASAN_OPTIONS=detect_leaks=1 ./build/unit-tests-sanitize

test-integration: server
	@mkdir -p build
	@./build/chat-server 127.0.0.1 5555 \
		> build/integration-server.log 2>&1 & \
	server_pid=$$!; \
	trap 'kill -TERM $$server_pid 2>/dev/null || true; wait $$server_pid 2>/dev/null || true' EXIT; \
	sleep 0.5; \
	python3 tests/integration/test_transport.py \
		--host 127.0.0.1 \
		--port 5555

clean:
	rm -rf build
