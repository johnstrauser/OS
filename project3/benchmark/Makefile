all: clean test

test:
	gcc test.c -g -L../ -lmy_vm -lpthread -m32 -o test

clean:
	rm -rf test
