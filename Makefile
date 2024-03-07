CC=clang
CFLAGS=-Wall -g -O0
RM=rm -f
OBJECTS=parse.o main.o walk.o

.PHONY: run
run: html
	@echo "======================"
	@./html test.html

html: $(OBJECTS)
	$(CC) $(CFLAGS) -o html $(OBJECTS)

.PHONY: clean
clean:
	$(RM) *.o
	$(RM) html
