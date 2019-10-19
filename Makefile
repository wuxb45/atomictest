MAKEFLAGS += -r -R
CC ?= clang
rwlocktest.out : rwlocktest.c
	$(CC) -march=native -pthread -std=gnu11 -g3 -DNDEBUG -O3 -flto -o rwlocktest.out rwlocktest.c -lrt
