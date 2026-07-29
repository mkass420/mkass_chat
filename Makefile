CC = gcc

CPPFLAGS = -Iinclude -Itests/unit
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -g
LDLIBS = -lz -lsqlite3

COMMON_SRC = \
	src/common/protocol.c \
	src/common/frame.c \
	src/common/binary.c \
	src/common/file_protocol.c \
	src/common/file_id.c

SERVER_CORE_SRC = \
	src/server/transport.c \
	src/server/connection.c \
	src/server/dispatcher.c \
	src/server/file_storage.c \
	src/server/file_transfer.c

UNIT_TEST_SRC = \
	tests/unit/test_main.c \
	tests/unit/test_protocol.c \
	tests/unit/test_frame.c \
	tests/unit/test_binary.c \
	tests/unit/test_file_protocol.c \
	tests/unit/test_transport.c \
	tests/unit/test_connection.c \
	tests/unit/test_dispatcher.c \
	tests/unit/test_file_modules.c \
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
