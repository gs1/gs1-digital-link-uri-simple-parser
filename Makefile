#
# GS1 Digital Link URI parser
#
# @author Copyright (c) 2021 GS1 AISBL.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
#
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#


ifneq ($(filter fuzzer fuzzer-corpus-seeds,$(MAKECMDGOALS)),)
SANITIZE = yes
FUZZER_SAN_OPT = ,fuzzer
FUZZER_CORPUS = corpus
endif


ifeq ($(DEBUG),yes)
DEBUG_CFLAGS = -DPRNT
endif

ifeq ($(SANITIZE),yes)
CC=clang
SAN_LDFLAGS = -fuse-ld=lld
SAN_CFLAGS = -fsanitize=address,leak,undefined$(FUZZER_SAN_OPT) -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_O = -O1
SAN_ENV = ASAN_OPTIONS="symbolize=1 detect_leaks=1" LSAN_OPTIONS="fast_unwind_on_malloc=0:malloc_context_size=50" ASAN_SYMBOLIZER_PATH="$(shell which llvm-symbolizer)"
endif

ifeq ($(SANITIZE),noleak)
CC=clang
SAN_LDFLAGS = -fuse-ld=lld
SAN_CFLAGS = -fsanitize=address,undefined$(FUZZER_SAN_OPT) -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_O = -O1
SAN_ENV = ASAN_OPTIONS="symbolize=1" LSAN_OPTIONS="fast_unwind_on_malloc=0:malloc_context_size=50" ASAN_SYMBOLIZER_PATH="$(shell which llvm-symbolizer)"
endif

# MemorySanitizer: detects reads of uninitialised memory. Mutually exclusive
# with ASAN.
#
#   make MSAN=yes test
#
ifeq ($(MSAN),yes)
CC=clang
SAN_LDFLAGS = -fuse-ld=lld
SAN_CFLAGS = -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_O = -O1
SAN_ENV = MSAN_SYMBOLIZER_PATH="$(shell which llvm-symbolizer)"
endif

# Code coverage: builds with gcov instrumentation, runs the tests and
# generates an HTML report via lcov/genhtml.
#
#   make coverage
#
ifeq ($(COVERAGE),yes)
CFLAGS_O = -O0
COV_CFLAGS = --coverage
endif

CFLAGS_O ?= -O2

# GCC static analyser
#
#   make ANALYZER=yes CC=gcc test
#
ifeq ($(ANALYZER),yes)
ANALYZER_CFLAGS = -fanalyzer
endif

ifneq ($(shell uname -s),Darwin)
LDFLAGS = -Wl,--as-needed -Wl,-Bsymbolic-functions -Wl,-z,relro -Wl,-z,now $(SAN_LDFLAGS)
CFLAGS_FORTIFY = -D_FORTIFY_SOURCE=2
NPROC = nproc
else
LDFLAGS =
CFLAGS_FORTIFY =
NPROC = sysctl -n hw.ncpu
endif

# MemorySanitizer does not intercept the fortified libc functions, so the
# writes that they make would appear uninitialised
ifeq ($(MSAN),yes)
CFLAGS_FORTIFY =
endif

LDLIBS = -lc
CFLAGS = -std=c99 -g $(CFLAGS_O) $(CFLAGS_FORTIFY) -Wall -Wextra -Wconversion -Wformat=2 -Wshadow -Wdeclaration-after-statement -pedantic -Wundef -Wnull-dereference -Wstrict-prototypes -Werror -fstack-protector-strong -MMD -fPIC $(SAN_CFLAGS) $(COV_CFLAGS) $(ANALYZER_CFLAGS) $(DEBUG_CFLAGS)

EXAMPLE_BIN = example-bin
EXAMPLE_SRC = example.c
EXAMPLE_OBJ = $(EXAMPLE_SRC:.c=.o)

TEST_BIN = gs1dlparser-test
TEST_SRC = gs1dlparser.c
TEST_OBJ = gs1dlparser-test.o

FUZZER_BIN = gs1dlparser-fuzzer
FUZZER_SRC = gs1dlparser.c
FUZZER_OBJ = gs1dlparser-fuzzer.o

ALL_SRCS = $(wildcard *.c)
SRCS = gs1dlparser.c
OBJS = $(SRCS:.c=.o)
DEPS = $(ALL_SRCS:.c=.d) $(TEST_OBJ:.o=.d) $(FUZZER_OBJ:.o=.d)


.PHONY: all clean example test coverage fuzzer fuzzer-corpus-seeds superlinter

default: example


#
#  Objects are invalidated whenever the toolchain or the compiler flags
#  change, since goals such as test and SANITIZE=yes share objects with plain
#  builds yet produce objects that cannot be linked together
#
BUILD_FLAGS = $(CC) $(CFLAGS)
BUILD_FLAGS_STAMP = .build-flags

.PHONY: FORCE
FORCE:

$(BUILD_FLAGS_STAMP): FORCE
	@echo '$(BUILD_FLAGS)' | cmp -s - $@ || echo '$(BUILD_FLAGS)' > $@

%.o: %.c $(BUILD_FLAGS_STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

#
#  The unit tests and fuzzer are built from their own objects so that they
#  are never linked with an object of the library, or of each other
#
$(TEST_OBJ): $(TEST_SRC) $(BUILD_FLAGS_STAMP)
	$(CC) $(CFLAGS) -DUNIT_TESTS -c $< -o $@

$(FUZZER_OBJ): $(FUZZER_SRC) $(BUILD_FLAGS_STAMP)
	$(CC) $(CFLAGS) -DFUZZER -c $< -o $@


#
#  Main
#
$(EXAMPLE_BIN): $(OBJS) $(EXAMPLE_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(EXAMPLE_OBJ) -o $(EXAMPLE_BIN)


#
#  Test binary
#
$(TEST_BIN): $(TEST_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(TEST_OBJ) -o $(TEST_BIN)


#
#  Fuzzer binary
#
$(FUZZER_CORPUS)/:
	mkdir -p $@

$(FUZZER_BIN): $(FUZZER_OBJ)
	$(CC) $(CFLAGS) $(FUZZER_OBJ) -o $(FUZZER_BIN)


#
#  Utility targets
#

example: $(EXAMPLE_BIN)

test: $(TEST_BIN)
	$(SAN_ENV) ./$(TEST_BIN) $(TEST)

coverage:
	$(MAKE) clean
	$(MAKE) COVERAGE=yes test
	lcov --capture --directory . --output-file coverage.info
	lcov --remove coverage.info --ignore-errors unused '*/acutest.h' --output-file coverage.info
	genhtml coverage.info --output-directory coverage-report
	@echo
	@echo "Coverage report: coverage-report/index.html"

#
#  Run Super Linter locally with the configuration used by CI
#
superlinter:
	docker run --rm \
		-e RUN_LOCAL=true \
		-e VALIDATE_ALL_CODEBASE=true \
		-e DEFAULT_BRANCH=main \
		-e VALIDATE_GIT_COMMITLINT=false \
		-e LINTER_RULES_PATH=.github/linters \
		-e 'FILTER_REGEX_EXCLUDE=(corpus/|coverage-report/)' \
		$(shell grep -v '^\#' .github/linters/superlinter-disabled.env | grep '=' | sed 's/^/-e /') \
		-v `pwd`:/tmp/lint \
		ghcr.io/super-linter/super-linter:v8

#
#  Seed an empty corpus with the distinct string literals of the unit tests,
#  merged so that only those adding coverage are retained
#
fuzzer-corpus-seeds: $(FUZZER_BIN) | $(FUZZER_CORPUS)/
	@if [ -z "$$(ls -A $(FUZZER_CORPUS) 2>/dev/null)" ]; then \
		temp_dir="$$(mktemp -d)" && \
		awk ' \
		{ \
			while (match($$0, /"[^"\\]*(\\.[^"\\]*)*"/)) { \
				str = substr($$0, RSTART, RLENGTH); \
				$$0 = substr($$0, RSTART + RLENGTH); \
				gsub(/^"|"$$/, "", str); \
				gsub(/\\"/, "\"", str); gsub(/\\\\/, "\\", str); \
				if (length(str) > 0 && !seen[str]++) { \
					filename = sprintf("'"$$temp_dir"'/seeds-%05d", ++count); \
					printf "%s", str > filename; \
					close(filename); \
				} \
			} \
		}' $(FUZZER_SRC) && \
		echo "Merging extracted strings into $(FUZZER_CORPUS)" && \
		$(SAN_ENV) ./$(FUZZER_BIN) -merge=1 $(FUZZER_CORPUS) "$$temp_dir" >/dev/null 2>&1; \
		rm -rf "$$temp_dir"; \
	else \
		echo "Corpus $(FUZZER_CORPUS) already exists, skipping seed generation"; \
	fi

fuzzer: $(FUZZER_BIN) fuzzer-corpus-seeds | $(FUZZER_CORPUS)/
	@echo
	@echo Start fuzzing as follows:
	@echo
	@echo '$(SAN_ENV)' ./$(FUZZER_BIN) -jobs=`$(NPROC)` -workers=`$(NPROC)` $(FUZZER_CORPUS)
	@echo

clean:
	$(RM) $(OBJS) $(TEST_OBJ) $(FUZZER_OBJ) $(EXAMPLE_OBJ) $(EXAMPLE_BIN) $(TEST_BIN) $(FUZZER_BIN) $(DEPS) $(BUILD_FLAGS_STAMP) $(TEST_OBJ:.o=.gcda) $(TEST_OBJ:.o=.gcno) coverage.info
	$(RM) -r coverage-report

-include $(DEPS)
