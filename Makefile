CC      = gcc
CFLAGS  = -Wall -Wextra -g
SRC     = src/ls-v1.1.0.c
TARGET  = bin/ls

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET) obj/*.o

.PHONY: all clean
