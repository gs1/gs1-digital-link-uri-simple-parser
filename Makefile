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


ifeq ($(MAKECMDGOALS),test)
UNIT_TEST_CFLAGS = -DUNIT_TESTS
endif

ifeq ($(MAKECMDGOALS),fuzzer)
SANITIZE = yes
FUZZER_CFLAGS = -DFUZZER
FUZZER_SAN_OPT = ,fuzzer
FUZZER_CORPUS = corpus
endif


ifeq ($(DEBUG),yes)
DEBUG_CFLAGS = -DPRNT
endif

ifeq ($(SANITIZE),yes)
CC=clang
SAN_LDFLAGS = -fuse-ld=lld
SAN_CFLAGS = -fsanitize=address,leak,undefined$(FUZZER_SAN_OPT) -fno-omit-frame-pointer -fno-optimize-sibling-calls -O1
SAN_ENV = ASAN_OPTIONS="symbolize=1 detect_leaks=1" LSAN_OPTIONS="fast_unwind_on_malloc=0:malloc_context_size=50" ASAN_SYMBOLIZER_PATH="$(shell which llvm-symbolizer)"
endif

ifeq ($(SANITIZE),noleak)
CC=clang
SAN_LDFLAGS = -fuse-ld=lld
SAN_CFLAGS = -fsanitize=address,undefined$(FUZZER_SAN_OPT) -fno-omit-frame-pointer -fno-optimize-sibling-calls -O1
SAN_ENV = ASAN_OPTIONS="symbolize=1" LSAN_OPTIONS="fast_unwind_on_malloc=0:malloc_context_size=50" ASAN_SYMBOLIZER_PATH="$(shell which llvm-symbolizer)"
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

LDLIBS = -lc
CFLAGS = -g -O2 $(CFLAGS_FORTIFY) -Wall -Wextra -Wconversion -Wformat -Wformat-security -Wdeclaration-after-statement -pedantic -Werror -MMD -fPIC $(SAN_CFLAGS) $(UNIT_TEST_CFLAGS) $(DEBUG_CFLAGS) $(SLOW_TESTS_CFLAGS) $(FUZZER_CFLAGS)

EXAMPLE_BIN = example-bin
EXAMPLE_SRC = example.c
EXAMPLE_OBJ = $(EXAMPLE_SRC:.c=.o)

TEST_BIN = gs1dlparser-test
TEST_SRC = gs1dlparser.c

FUZZER_BIN = gs1dlparser-fuzzer
FUZZER_SRC = gs1dlparser.c
FUZZER_OBJ = $(FUZZER_SRC:.c=.o)

ALL_SRCS = $(wildcard *.c)
SRCS = gs1dlparser.c
OBJS = $(SRCS:.c=.o)
DEPS = $(ALL_SRCS:.c=.d)


.PHONY: all clean example test fuzzer

default: example


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


#
#  Main
#
$(EXAMPLE_BIN): $(OBJS) $(EXAMPLE_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) $(EXAMPLE_OBJ) -o $(EXAMPLE_BIN)


#
#  Test binary
#
$(TEST_BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TEST_BIN)


#
#  Fuzzer binary
#
$(FUZZER_CORPUS)/:
	mkdir -p $@

$(FUZZER_BIN): $(OBJS)
	$(CC) $(CFLAGS) $(FUZZER_OBJ) -o $(FUZZER_BIN)


#
#  Utility targets
#

example: $(EXAMPLE_BIN)

test: $(TEST_BIN)
	$(SAN_ENV) ./$(TEST_BIN) $(TEST)

fuzzer: $(FUZZER_BIN) | $(FUZZER_CORPUS)/
	@echo
	@echo Start fuzzing as follows:
	@echo
	@echo '$(SAN_ENV)' ./$(FUZZER_BIN) -jobs=`$(NPROC)` -workers=`$(NPROC)` $(FUZZER_CORPUS)
	@echo

clean:
	$(RM) $(OBJS) $(EXAMPLE_OBJ) $(EXAMPLE_BIN) $(TEST_BIN) $(FUZZER_BIN) $(DEPS)

-include $(DEPS)
