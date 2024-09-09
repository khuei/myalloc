CC=clang
CFLAGS=-O2 -Wall -g -fPIC -shared

TARGET=libmyalloc.so

all: $(TARGET)

$(TARGET): allocator.c
	$(CC) $(CFLAGS) allocator.c -o $(TARGET) -lm

clean:
	rm $(TARGET)
