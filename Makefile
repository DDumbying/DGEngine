CC = gcc

CFLAGS = -Wall -Wextra -std=c11

INCLUDES = -Iexternal/glad/include

LIBS = -lSDL2 -lGL

SRC = $(shell find src external -name "*.c")

OUT = bin/dgengine

all:
	$(CC) $(SRC) $(CFLAGS) $(INCLUDES) $(LIBS) -o $(OUT)

clean:
	rm -f $(OUT)
