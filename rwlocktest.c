#define _GNU_SOURCE
#include <assert.h>
#include <byteswap.h>
#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>

typedef atomic_uint_least64_t au64;
typedef atomic_uint_least32_t au32;
typedef atomic_uint_least16_t au16;
typedef atomic_uint_least8_t au8;

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

  static inline u64
bits_p2_up(const u64 v)
{
  // clz(0) is undefined
  return (v > 1) ? (1lu << (64lu - (u64)__builtin_clzl(v - 1lu))) : v;
}
typedef u64 rwlock;

  static inline u64
time_nsec(void)
{
  struct timespec ts;
  // MONO_RAW is 5x to 10x slower than MONO
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000lu + ts.tv_nsec;
}

  static inline double
time_sec(void)
{
  const u64 nsec = time_nsec();
  return ((double)nsec) / 1000000000.0;
}

  static inline double
time_diff_sec(const double last)
{
  return time_sec() - last;
}

// Lehmer's generator is 2x faster than xorshift
/**
 * D. H. Lehmer, Mathematical methods in large-scale computing units.
 * Proceedings of a Second Symposium on Large Scale Digital Calculating
 * Machinery;
 * Annals of the Computation Laboratory, Harvard Univ. 26 (1951), pp. 141-146.
 *
 * P L'Ecuyer,  Tables of linear congruential generators of different sizes and
 * good lattice structure. Mathematics of Computation of the American
 * Mathematical
 * Society 68.225 (1999): 249-260.
 */
static __thread union {
  __uint128_t v128;
  u64 v64[2];
} rseed_u128 = {.v64[0] = 4294967291, .v64[1] = 1549556881};

  static inline u64
random_u64(void)
{
  const u64 r = rseed_u128.v64[1];
  rseed_u128.v128 *= 0xda942042e4dd58b5lu;
  return r;
}

  static inline void
srandom_u64(const u64 seed)
{
  rseed_u128.v128 = (((__uint128_t)(~seed)) << 64) | (seed | 1);
  (void)random_u64();
}

#define RAND64_MAX_D ((double)(~0lu))



static u64 process_ncpu;
static u64 process_cpu_set_size;

__attribute__((constructor))
  static void
process_init(void)
{
  process_ncpu = sysconf(_SC_NPROCESSORS_CONF);
  const size_t s1 = CPU_ALLOC_SIZE(process_ncpu);
  const size_t s2 = sizeof(cpu_set_t);
  process_cpu_set_size = s1 > s2 ? s1 : s2;
}

  static inline cpu_set_t *
process_cpu_set_alloc(void)
{
  return malloc(process_cpu_set_size);
}

  static u64
process_affinity_core_count(void)
{
  cpu_set_t * const set = process_cpu_set_alloc();
  if (sched_getaffinity(0, process_cpu_set_size, set) != 0) {
    free(set);
    return process_ncpu;
  }

  const u64 nr = (u64)CPU_COUNT_S(process_cpu_set_size, set);
  free(set);
  return nr ? nr : process_ncpu;
}

  static u64
process_affinity_core_list(const u64 max, u64 * const cores)
{
  memset(cores, 0, max * sizeof(cores[0]));
  cpu_set_t * const set = process_cpu_set_alloc();
  if (sched_getaffinity(0, process_cpu_set_size, set) != 0)
    return 0;

  const u64 nr_affinity = CPU_COUNT_S(process_cpu_set_size, set);
  const u64 nr = nr_affinity < max ? nr_affinity : max;
  u64 j = 0;
  for (u64 i = 0; i < process_ncpu; i++) {
    if (CPU_ISSET_S((int)i, process_cpu_set_size, set))
      cores[j++] = i;

    if (j >= nr)
      break;
  }
  free(set);
  return j;
}

struct fork_join_info {
  u64 tot;
  u64 rank; // 0 to n-1
  u64 core;
  pthread_t tid;
  void *(*func)(void *);
  void * argv;
  struct fork_join_info * all;
  u64 padding[1];
};

// recursive tree fork-join
  static void *
thread_do_fork_join_worker(void * const ptr)
{
  struct fork_join_info * const fji = (typeof(fji))ptr;
  const u64 rank = fji->rank;
  const u64 span0 = (rank ? (rank & -rank) : bits_p2_up(fji->tot)) >> 1;
  if (span0) {
    cpu_set_t * const set = process_cpu_set_alloc();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    for (u64 span = span0; span; span >>= 1) {
      const u64 cr = rank + span; // child rank
      if (cr >= fji->tot)
        continue;
      struct fork_join_info * const cfji = &(fji->all[cr]);
      CPU_ZERO_S(process_cpu_set_size, set);
      CPU_SET_S(cfji->core, process_cpu_set_size, set);
      pthread_attr_setaffinity_np(&attr, process_cpu_set_size, set);
      const int r = pthread_create(&(cfji->tid), &attr, thread_do_fork_join_worker, cfji);
      if (r == 0) {
        char thname[24];
        sprintf(thname, "fj_%lu", cr);
        pthread_setname_np(cfji->tid, thname);
      } else {
        fprintf(stderr, "pthread_create %lu..%lu = %d: %s\n", rank, cr, r, strerror(r));
        cfji->tid = 0;
      }
    }
    pthread_attr_destroy(&attr);
    free(set);
  }
  void * const ret = fji->func(fji->argv);
  for (u64 span = 1; span <= span0; span <<= 1) {
    const u64 cr = rank + span;
    if (cr >= fji->tot)
      break;
    struct fork_join_info * const cfji = &(fji->all[cr]);
    if (cfji->tid) {
      const int r = pthread_join(cfji->tid, NULL);
      if (r)
        fprintf(stderr, "pthread_join %lu..%lu = %d: %s\n", rank, cr, r, strerror(r));
    } else {
      fprintf(stderr, "skip joining %lu..%lu\n", rank, cr);
    }
  }
  return ret;
}

  static double
thread_fork_join(const u64 nr, void *(*func) (void *), const bool args, void * const argx)
{
  const u64 nr_threads = nr ? nr : process_affinity_core_count();

  u64 cores[process_ncpu];
  u64 ncores = process_affinity_core_list(process_ncpu, cores);
  if (ncores == 0) { // force to use all cores
    ncores = process_ncpu;
    for (u64 i = 0; i < process_ncpu; i++)
      cores[i] = i;
  }

  struct fork_join_info * const fjis = malloc(sizeof(*fjis) * nr_threads);
  for (u64 i = 0; i < nr_threads; i++) {
    fjis[i].tot = nr_threads;
    fjis[i].rank = i;
    fjis[i].core = cores[i % ncores];
    fjis[i].tid = 0;
    fjis[i].func = func;
    fjis[i].argv = args ? ((void **)argx)[i] : argx;
    fjis[i].all = fjis;
  }

  // save current affinity
  cpu_set_t * const set0 = process_cpu_set_alloc();
  sched_getaffinity(0, process_cpu_set_size, set0);

  // master thread shares thread0's core
  cpu_set_t * const set = process_cpu_set_alloc();
  CPU_ZERO_S(process_cpu_set_size, set);
  CPU_SET_S(fjis[0].core, process_cpu_set_size, set);
  sched_setaffinity(0, process_cpu_set_size, set);
  free(set);

  const double t0 = time_sec();
  thread_do_fork_join_worker(&(fjis[0]));
  const double dt = time_diff_sec(t0);

  // restore original affinity
  sched_setaffinity(0, process_cpu_set_size, set0);
  free(set0);
  free(fjis);
  return dt;
}

typedef au32 lock_t;
typedef u32 lock_v;
static_assert(sizeof(lock_t) == sizeof(lock_v), "lock size");
static_assert(sizeof(lock_t) <= sizeof(rwlock), "lock size");

#define RWLOCK_WSHIFT ((sizeof(lock_t) * 8 - 1))
#define RWLOCK_WBIT ((((lock_v)1) << RWLOCK_WSHIFT))

  static inline void
rwlock_init(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  atomic_store(pvar, 0);
}

  static inline bool
rwlock_trylock_read(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  if ((atomic_fetch_add(pvar, 1) >> RWLOCK_WSHIFT) == 0) {
    return true;
  } else {
    atomic_fetch_sub(pvar, 1);
    return false;
  }
}

  static inline void
rwlock_lock_read(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  while (rwlock_trylock_read(lock) == false) {
    do {
      _mm_pause();
    } while (atomic_load(pvar) >> RWLOCK_WSHIFT);
  }
}

  static inline void
rwlock_unlock_read(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  atomic_fetch_sub(pvar, 1);
}

  static inline bool
rwlock_trylock_write(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  lock_v v0 = atomic_load(pvar);
  if (v0 == 0) {
    if (atomic_compare_exchange_weak(pvar, &v0, RWLOCK_WBIT))
      return true;
  }
  return false;
}

  static inline void
rwlock_lock_write(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  while (rwlock_trylock_write(lock) == false) {
    do {
      _mm_pause();
    } while (atomic_load(pvar));
  }
}

  static inline void
rwlock_unlock_write(rwlock * const lock)
{
  lock_t * const pvar = (typeof(pvar))lock;
  atomic_fetch_sub(pvar, RWLOCK_WBIT);
}

#undef RWLOCK_WSHIFT
#undef RWLOCK_WBIT

struct rwlock_worker_info {
  rwlock * locks;
  u64 id_mask;
  au64 seq;
  au64 nr_w;
  au64 nr_r;
  u64 nr_writer;
  u64 end_time;
};

  static void *
rwlock_worker(void * const ptr)
{
  struct rwlock_worker_info * const info = (typeof(info))ptr;
  const u64 seq = atomic_fetch_add(&(info->seq), 1);
  const bool is_writer = seq < info->nr_writer ? true : false;
  const u64 mask = info->id_mask;
  const u64 end_time = info->end_time;
  srandom_u64(time_nsec());
  u64 count = 0;
  do {
    for (u64 i = 0; i < 1024; i++) {
      const u64 x = random_u64() & mask;
      if (is_writer) {
        rwlock_lock_write(&(info->locks[x]));
        //_mm_pause();
        rwlock_unlock_write(&(info->locks[x]));
      } else {
        rwlock_lock_read(&(info->locks[x]));
        //_mm_pause();
        rwlock_unlock_read(&(info->locks[x]));
      }
    }
    count += 1024;
  } while (time_nsec() < end_time);

  if (is_writer) {
    atomic_fetch_add(&(info->nr_w), count);
  } else {
    atomic_fetch_add(&(info->nr_r), count);
  }
  return NULL;
}

  static void
test_rwlocks(void)
{
  const u64 ncpus = process_affinity_core_count();
  for (u64 p = 6; p <= 9; p+=3) {
    const u64 nlocks = 1lu << p;
    struct rwlock_worker_info info;
    info.locks = (typeof(info.locks))malloc(sizeof(info.locks[0]) * nlocks);
    info.id_mask = nlocks - 1;;
    info.seq = 0;
    info.nr_writer = 0;
    for (u64 i = 0; i < nlocks; i++) {
      rwlock_init(&(info.locks[i]));
    }
    for (u64 i = 4; i <= ncpus; i += 2) {
      info.seq = 0;
      atomic_store(&(info.nr_w), 0);
      atomic_store(&(info.nr_r), 0);
      info.end_time = time_nsec() + (UINT64_C(2) << 30);
      const double dt = thread_fork_join(i, rwlock_worker, false, &info);
      const u64 nr_w = atomic_load(&info.nr_w);
      const double mw = ((double)nr_w) * 0.000001 / dt;
      const u64 nr_r = atomic_load(&info.nr_r);
      const double mr = ((double)nr_r) * 0.000001 / dt;
      printf("%lu NTH %3lu NW %3lu NL %3lu R %6.2lf W %6.2lf\n", 0lu, i, 0lu, nlocks, mr, mw);
    }
    free(info.locks);
  }
}

  int
main()
{
  test_rwlocks();
  test_rwlocks();
}
