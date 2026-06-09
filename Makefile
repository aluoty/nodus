CC = gcc
CFLAGS = -Wall -Wextra -O3
TARGET = nodus

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) main.c -o $(TARGET)

clean:
	rm -f $(TARGET)
