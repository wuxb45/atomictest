rwlocktest.out : rwlocktest.c
	clang  -march=native -pthread -std=gnu11 -g3 -DNDEBUG -O3 -flto -o rwlocktest.out rwlocktest.c -lrt
