MAKEFLAGS += -r -R
.DEFAULT_GOAL = dis
CC := clang
dis : rwlocktest.dis rwlocktest.out

.PHONY : clean
clean:
	rm -f rwlocktest.dis rwlocktest.out

%.out : %.c
	$(CC) -march=native -pthread -std=gnu11 -g3 -DNDEBUG -O3 -flto -o $@ $< -lrt
%.dis : %.out
	objdump -SlwTC $< 1>$@ 2>/dev/null
