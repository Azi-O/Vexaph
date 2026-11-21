CC ?= gcc
ARCH ?= native
ARCH_FLAGS := $(if $(filter generic,$(ARCH)),,-march=$(ARCH))
CFLAGS ?= -O3 -std=gnu11 -Wall -Wextra $(ARCH_FLAGS) -pthread
CPPFLAGS += -Iinclude -MMD -MP
LDLIBS += -lm -lpthread
SRC := $(wildcard src/core/*.c) $(wildcard src/brain/*.c) $(wildcard src/book/*.c) $(wildcard src/platform/*.c) $(wildcard src/interface/uci.c)
OBJ := $(patsubst src/%.c,build/obj/%.o,$(SRC))
BIN := build/vexaph

all: $(BIN) build/nn-190f102a22c3.nnue

build/nn-190f102a22c3.nnue: nn-190f102a22c3.nnue
	@mkdir -p build
	cp $< $@

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

build/obj/main.o: src/main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BIN): $(OBJ) build/obj/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

perft_test: $(OBJ) tests/perft_main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o build/perft_test tests/perft_main.c $(OBJ) $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean perft_test

unit_test: $(OBJ) tests/test_main.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o build/unit_test tests/test_main.c $(OBJ) $(LDLIBS)

.PHONY: unit_test

bench_time: $(OBJ) tools/bench_time.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o build/bench_time tools/bench_time.c $(OBJ) $(LDLIBS)

.PHONY: bench_time

-include $(OBJ:.o=.d) build/obj/main.d
