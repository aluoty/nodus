CC = gcc
CFLAGS = -Wall -Wextra -O3
TARGET = build/nodus

all: $(TARGET)

$(TARGET): src/main.c
	mkdir -p build
	$(CC) $(CFLAGS) src/main.c -o $(TARGET)

clean:
	rm -f $(TARGET)
