CC = cc
PYTHON = python3
BUILD = build
CPPFLAGS = -D_POSIX_C_SOURCE=200809L
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Werror -O1 -g
LDFLAGS =
PROGRAMS = identity buffer copyfd fileinfo run pipeline signal_wait mapcount threads echo_server echo_client pollpipe
BINARIES = $(addprefix $(BUILD)/,$(PROGRAMS)) $(BUILD)/sift

.PHONY: all test sanitize book clean
all: $(BINARIES)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/threads: examples/threads.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS) -pthread

$(BUILD)/%: examples/%.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD)/sift: capstone/sift.c capstone/matcher.c capstone/matcher.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) capstone/sift.c capstone/matcher.c -o $@ $(LDFLAGS)

$(BUILD)/test_matcher: tests/test_matcher.c capstone/matcher.c capstone/matcher.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Icapstone tests/test_matcher.c capstone/matcher.c -o $@ $(LDFLAGS)

test: all $(BUILD)/test_matcher
	$(BUILD)/test_matcher
	$(PYTHON) tests/check.py $(BUILD)

sanitize:
	$(MAKE) BUILD=build-sanitize CFLAGS='-std=c17 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' LDFLAGS='-fsanitize=address,undefined' test

book:
	$(PYTHON) tools/build_book.py

clean:
	$(PYTHON) -c 'import shutil; [shutil.rmtree(p, ignore_errors=True) for p in ("build", "build-sanitize")]'
