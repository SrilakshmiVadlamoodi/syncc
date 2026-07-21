CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -g
TARGET  := minicc

SRCS := main.c lexer.c parser.c semantic.c codegen.c optimizer.c error_engine.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Usage: make run FILE=samples/valid.mini
run: $(TARGET)
	./$(TARGET) $(FILE)

clean:
	rm -f $(OBJS) $(TARGET)
