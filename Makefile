CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g
DEPFLAGS := -MMD -MP
TARGET  := minicc

LIB_SRCS  := $(filter-out main.c,$(wildcard *.c))
LIB_OBJS  := $(LIB_SRCS:.c=.o)
MAIN_OBJ  := main.o
TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(TEST_SRCS:.c=.bin)

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJ) $(LIB_OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -I. -c $< -o $@

tests/%.bin: tests/%.c tests/support.c tests/support.h tests/test.h $(LIB_OBJS)
	$(CC) $(CFLAGS) -I. -o $@ $< tests/support.c $(LIB_OBJS)

# Usage: make run FILE=samples/valid.mini
run: $(TARGET)
	./$(TARGET) $(FILE)

test: $(TARGET) $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; ./$$t || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -f $(LIB_OBJS) $(MAIN_OBJ) $(LIB_OBJS:.o=.d) main.d $(TARGET) $(TARGET).exe $(TEST_BINS) .minicc_cache

-include $(LIB_OBJS:.o=.d) main.d
