# Problem statement

The relevant part is the use of `atomic_fetch_add` and `atomic_fetch_sub` in `rwlock_*` functions.
In short, clang-8 and earlier versions generate `lock addl` and `lock subl` for those atomics.
clang-9 generates `lock incl` and `lock decl`, and also `lock xadd` if adding 2 or other numbers.
The results show that `lock addl` and `lock subl` can be more efficient in some cases.
The rwlock implementation can be wrong. Any suggestions are welcome.
I have not identified the actual cause.
Right now I just personally hope to get the old compilation results with clang-9.

In this microbenchmark each thread repeatedly acquires and releases a randomly selected lock from a set.
The number of threads and the number of locks are changed in the benchmark.
Each thread is pinned to a unique core, assuming #threads <= #cores.
The program reads the process' affinity settings to determine how many cores can be used.

Change the compiler with CC=x such as `CC=clang-9`. The default is `clang`.
If having multiple sockets, use `numactl -N 0 ./rwlocktest.out` to let it run on one socket.

# Results
Tested on both Broadwell (16-core) and Skylake (14-core) servers, with Archlinux (kernel 5.3.x) and Ubuntu 18.04, respectively.

Broadwell has HT turned off. Skylake has HT turned on (not on purpose) so I used the first 14 cores out of the 28 on a socket.  Skylake results are omitted (similar to Broadwell's).

Results on Broadwell are listed below.
Server specs: Archlinux, linux 5.3.6, Xeon E5-2697A v4, mitigations=off.

We observed that with more locks (128+), the binary produced by clang-9 yields 15-20 percent lower throughput.
However, with a \_mm\_pause() added in between the lock() and unlock(), the peak throughput (180+) can be reached by both of them.
But the throughput under high-contention (32 locks) is much lower (roughly 40 vs 60).
I have not exhausted all the possibilities by adding `pause`s, reordering the statements,
and writing inline assembly to force doing `lock addl` with clang-9.
Some are slightly better, but none has produced results as good as clang-8's.

## results with clang 8/9 (formatted for comparison)

    #threads:   4   #locks:  32  clang-9:  48.66 million ops/sec   clang-8:  48.60 million ops/sec
    #threads:   6   #locks:  32  clang-9:  54.94 million ops/sec   clang-8:  55.55 million ops/sec
    #threads:   8   #locks:  32  clang-9:  57.63 million ops/sec   clang-8:  58.30 million ops/sec
    #threads:  10   #locks:  32  clang-9:  59.89 million ops/sec   clang-8:  61.12 million ops/sec
    #threads:  12   #locks:  32  clang-9:  61.24 million ops/sec   clang-8:  62.14 million ops/sec
    #threads:  14   #locks:  32  clang-9:  61.87 million ops/sec   clang-8:  62.75 million ops/sec
    #threads:  16   #locks:  32  clang-9:  61.41 million ops/sec   clang-8:  62.77 million ops/sec
    #threads:   4   #locks: 128  clang-9:  58.54 million ops/sec   clang-8:  64.85 million ops/sec
    #threads:   6   #locks: 128  clang-9:  77.23 million ops/sec   clang-8:  86.35 million ops/sec
    #threads:   8   #locks: 128  clang-9:  92.27 million ops/sec   clang-8: 102.66 million ops/sec
    #threads:  10   #locks: 128  clang-9: 103.32 million ops/sec   clang-8: 117.78 million ops/sec
    #threads:  12   #locks: 128  clang-9: 114.16 million ops/sec   clang-8: 128.92 million ops/sec
    #threads:  14   #locks: 128  clang-9: 121.44 million ops/sec   clang-8: 137.28 million ops/sec
    #threads:  16   #locks: 128  clang-9: 126.56 million ops/sec   clang-8: 144.77 million ops/sec
    #threads:   4   #locks: 512  clang-9:  55.80 million ops/sec   clang-8:  68.21 million ops/sec
    #threads:   6   #locks: 512  clang-9:  76.55 million ops/sec   clang-8:  93.21 million ops/sec
    #threads:   8   #locks: 512  clang-9:  95.32 million ops/sec   clang-8: 116.38 million ops/sec
    #threads:  10   #locks: 512  clang-9: 113.31 million ops/sec   clang-8: 138.59 million ops/sec
    #threads:  12   #locks: 512  clang-9: 128.26 million ops/sec   clang-8: 157.17 million ops/sec
    #threads:  14   #locks: 512  clang-9: 141.07 million ops/sec   clang-8: 171.94 million ops/sec
    #threads:  16   #locks: 512  clang-9: 150.64 million ops/sec   clang-8: 185.40 million ops/sec
    #threads:   4   #locks:  32  clang-9:  49.35 million ops/sec   clang-8:  49.83 million ops/sec
    #threads:   6   #locks:  32  clang-9:  54.67 million ops/sec   clang-8:  55.24 million ops/sec
    #threads:   8   #locks:  32  clang-9:  57.63 million ops/sec   clang-8:  58.63 million ops/sec
    #threads:  10   #locks:  32  clang-9:  60.32 million ops/sec   clang-8:  61.11 million ops/sec
    #threads:  12   #locks:  32  clang-9:  61.18 million ops/sec   clang-8:  62.23 million ops/sec
    #threads:  14   #locks:  32  clang-9:  61.82 million ops/sec   clang-8:  62.03 million ops/sec
    #threads:  16   #locks:  32  clang-9:  60.91 million ops/sec   clang-8:  62.77 million ops/sec
    #threads:   4   #locks: 128  clang-9:  58.78 million ops/sec   clang-8:  64.83 million ops/sec
    #threads:   6   #locks: 128  clang-9:  77.39 million ops/sec   clang-8:  86.15 million ops/sec
    #threads:   8   #locks: 128  clang-9:  92.70 million ops/sec   clang-8: 103.39 million ops/sec
    #threads:  10   #locks: 128  clang-9: 104.53 million ops/sec   clang-8: 117.19 million ops/sec
    #threads:  12   #locks: 128  clang-9: 114.24 million ops/sec   clang-8: 128.67 million ops/sec
    #threads:  14   #locks: 128  clang-9: 121.34 million ops/sec   clang-8: 137.87 million ops/sec
    #threads:  16   #locks: 128  clang-9: 126.83 million ops/sec   clang-8: 144.52 million ops/sec
    #threads:   4   #locks: 512  clang-9:  55.78 million ops/sec   clang-8:  68.18 million ops/sec
    #threads:   6   #locks: 512  clang-9:  76.47 million ops/sec   clang-8:  92.97 million ops/sec
    #threads:   8   #locks: 512  clang-9:  94.64 million ops/sec   clang-8: 116.29 million ops/sec
    #threads:  10   #locks: 512  clang-9: 113.06 million ops/sec   clang-8: 138.39 million ops/sec
    #threads:  12   #locks: 512  clang-9: 128.22 million ops/sec   clang-8: 156.90 million ops/sec
    #threads:  14   #locks: 512  clang-9: 140.87 million ops/sec   clang-8: 172.90 million ops/sec
    #threads:  16   #locks: 512  clang-9: 150.84 million ops/sec   clang-8: 184.95 million ops/sec`

## result with \_mm\_pause() between lock and unlock (uncomment line #372)

    #threads:   4   #locks:  32  clang-9:  38.18 million ops/sec   clang-8:  39.24 million ops/sec
    #threads:   6   #locks:  32  clang-9:  40.24 million ops/sec   clang-8:  40.11 million ops/sec
    #threads:   8   #locks:  32  clang-9:  40.58 million ops/sec   clang-8:  40.55 million ops/sec
    #threads:  10   #locks:  32  clang-9:  39.76 million ops/sec   clang-8:  40.95 million ops/sec
    #threads:  12   #locks:  32  clang-9:  39.70 million ops/sec   clang-8:  40.93 million ops/sec
    #threads:  14   #locks:  32  clang-9:  39.65 million ops/sec   clang-8:  40.56 million ops/sec
    #threads:  16   #locks:  32  clang-9:  39.36 million ops/sec   clang-8:  39.61 million ops/sec
    #threads:   4   #locks: 128  clang-9:  63.01 million ops/sec   clang-8:  63.58 million ops/sec
    #threads:   6   #locks: 128  clang-9:  73.84 million ops/sec   clang-8:  74.76 million ops/sec
    #threads:   8   #locks: 128  clang-9:  82.95 million ops/sec   clang-8:  84.40 million ops/sec
    #threads:  10   #locks: 128  clang-9:  85.74 million ops/sec   clang-8:  89.98 million ops/sec
    #threads:  12   #locks: 128  clang-9:  90.47 million ops/sec   clang-8:  93.14 million ops/sec
    #threads:  14   #locks: 128  clang-9:  90.55 million ops/sec   clang-8:  96.25 million ops/sec
    #threads:  16   #locks: 128  clang-9:  92.44 million ops/sec   clang-8:  98.99 million ops/sec
    #threads:   4   #locks: 512  clang-9:  74.36 million ops/sec   clang-8:  74.64 million ops/sec
    #threads:   6   #locks: 512  clang-9: 100.40 million ops/sec   clang-8: 100.49 million ops/sec
    #threads:   8   #locks: 512  clang-9: 123.69 million ops/sec   clang-8: 122.31 million ops/sec
    #threads:  10   #locks: 512  clang-9: 140.79 million ops/sec   clang-8: 143.30 million ops/sec
    #threads:  12   #locks: 512  clang-9: 158.99 million ops/sec   clang-8: 158.62 million ops/sec
    #threads:  14   #locks: 512  clang-9: 172.64 million ops/sec   clang-8: 172.19 million ops/sec
    #threads:  16   #locks: 512  clang-9: 184.51 million ops/sec   clang-8: 184.57 million ops/sec
    #threads:   4   #locks:  32  clang-9:  38.36 million ops/sec   clang-8:  39.67 million ops/sec
    #threads:   6   #locks:  32  clang-9:  40.14 million ops/sec   clang-8:  40.06 million ops/sec
    #threads:   8   #locks:  32  clang-9:  40.62 million ops/sec   clang-8:  40.85 million ops/sec
    #threads:  10   #locks:  32  clang-9:  39.99 million ops/sec   clang-8:  40.77 million ops/sec
    #threads:  12   #locks:  32  clang-9:  39.58 million ops/sec   clang-8:  40.88 million ops/sec
    #threads:  14   #locks:  32  clang-9:  39.69 million ops/sec   clang-8:  40.50 million ops/sec
    #threads:  16   #locks:  32  clang-9:  39.17 million ops/sec   clang-8:  40.54 million ops/sec
    #threads:   4   #locks: 128  clang-9:  63.34 million ops/sec   clang-8:  63.62 million ops/sec
    #threads:   6   #locks: 128  clang-9:  73.35 million ops/sec   clang-8:  75.12 million ops/sec
    #threads:   8   #locks: 128  clang-9:  80.88 million ops/sec   clang-8:  84.62 million ops/sec
    #threads:  10   #locks: 128  clang-9:  85.64 million ops/sec   clang-8:  89.63 million ops/sec
    #threads:  12   #locks: 128  clang-9:  88.48 million ops/sec   clang-8:  92.91 million ops/sec
    #threads:  14   #locks: 128  clang-9:  92.44 million ops/sec   clang-8:  95.29 million ops/sec
    #threads:  16   #locks: 128  clang-9:  91.18 million ops/sec   clang-8:  98.35 million ops/sec
    #threads:   4   #locks: 512  clang-9:  74.33 million ops/sec   clang-8:  74.70 million ops/sec
    #threads:   6   #locks: 512  clang-9: 100.29 million ops/sec   clang-8:  99.91 million ops/sec
    #threads:   8   #locks: 512  clang-9: 123.39 million ops/sec   clang-8: 124.08 million ops/sec
    #threads:  10   #locks: 512  clang-9: 143.11 million ops/sec   clang-8: 143.09 million ops/sec
    #threads:  12   #locks: 512  clang-9: 160.06 million ops/sec   clang-8: 158.95 million ops/sec
    #threads:  14   #locks: 512  clang-9: 173.70 million ops/sec   clang-8: 173.07 million ops/sec
    #threads:  16   #locks: 512  clang-9: 185.78 million ops/sec   clang-8: 184.45 million ops/sec

# clang versions

```
$ clang --version
clang version 9.0.0 (tags/RELEASE_900/final)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
```

```
$ clang --version
clang version 8.0.1 (tags/RELEASE_801/final)
Target: x86_64-pc-linux-gnu
Thread model: posix
InstalledDir: /usr/bin
```

# Disassembly

## clang-8

```
rwlock_trylock_read():
/home/x/work/rw/rwlocktest.c:284
  if ((atomic_fetch_add(pvar, 1) >> RWLOCK_WSHIFT) == 0) {
    2869: f0 83 04 f0 01        lock addl $0x1,(%rax,%rsi,8)
    286e: 7e e0                 jle    2850 <rwlock_worker+0x250>
rwlock_worker():
/home/x/work/rw/rwlocktest.c:373
        rwlock_unlock_read(&(info->locks[x]));
    2870: 48 8b 03              mov    (%rbx),%rax
rwlock_unlock_read():
/home/x/work/rw/rwlocktest.c:307
  atomic_fetch_sub(pvar, 1);
    2873: f0 83 2c f0 01        lock subl $0x1,(%rax,%rsi,8)
```

## clang-9

```
rwlock_trylock_read():
    283f: 90                    nop
/home/x/work/rw/rwlocktest.c:284
  if ((atomic_fetch_add(pvar, 1) >> RWLOCK_WSHIFT) == 0) {
    2840: f0 ff 04 f0           lock incl (%rax,%rsi,8)
    2844: 7f 1a                 jg     2860 <rwlock_worker+0x270>
/home/x/work/rw/rwlocktest.c:287
    atomic_fetch_sub(pvar, 1);
    2846: f0 ff 0c f0           lock decl (%rax,%rsi,8)
```

## clang-9, but with `atomic_fetch_add(pvar, 3)`
(benchmarking results omitted, not improved)

```
rwlock_trylock_read():
/home/x/work/rw/rwlocktest.c:284
  if ((atomic_fetch_add(pvar, 3) >> RWLOCK_WSHIFT) == 0) {
    2840: ba 03 00 00 00        mov    $0x3,%edx
    2845: f0 0f c1 14 f0        lock xadd %edx,(%rax,%rsi,8)
    284a: 85 d2                 test   %edx,%edx
    284c: 79 22                 jns    2870 <rwlock_worker+0x280>
/home/x/work/rw/rwlocktest.c:287
    atomic_fetch_sub(pvar, 3);
    284e: f0 83 2c f0 03        lock subl $0x3,(%rax,%rsi,8)
```

