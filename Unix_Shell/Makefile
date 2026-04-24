CC = gcc
CFLAGS = -Wall -Werror

seush: seush.c
	$(CC) $(CFLAGS) -o seush seush.c

clean:
	rm -f seush

test: seush
	./test-seush.sh

.PHONY: clean test
