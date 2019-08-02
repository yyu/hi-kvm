CFLAGS = -Wall -Wextra -Werror -O2

.PHONY: run

run: hikvm
	./hikvm

hikvm: hikvm.o payload.o
	$(CC) $^ -o $@

payload.o: payload.ld guest16.o
	$(LD) -T $< -o $@
