#include "lib1.h"
#include <stdatomic.h>
typedef atomic_uint_least64_t au64;

void bt0(void) { debug_backtrace(); }
void bt1(void) { bt0(); }
void bt2(void) { bt1(); }

  void *
th(void * ptr)
{
  (void)ptr;
  printf("thread\n");
  return NULL;
}

  static void
test_misc(void)
{
  u64 t0 = time_nsec();
  const double d0 = time_sec();
  bt2();
  watch_u64_usr1(&t0);
  u32 r32 = bits_reverse_u32(0x12345678u);
  printf("reverse u32 0x12345678 -> %"PRIx32"\n", r32);
  u64 r64 = bits_reverse_u64(0x123456789abcdef0lu);
  printf("reverse u64 0x123456789abcdef0 -> %"PRIx64"\n", r64);

  printf("rss %"PRIu64"\n", process_get_rss());
  const u64 cc = process_affinity_core_count();
  printf("affinity_core_count %"PRIu64"\n", cc);
  u64 cores[64] = {};
  const u64 nc = process_affinity_core_list(64, cores);
  printf("affinity cores: ");
  for (u64 i = 0; i < nc; i++) {
    printf(" %"PRIu64, cores[i]);
  }
  printf("\n");
  printf("cpu time %"PRIu64"\n", process_cpu_time_usec());
  thread_fork_join(cc, th, NULL);

  char s10[11];
  str10_u32(s10, 1234567);
  printf("%.10s\n", s10);

  char ** const toks = string_tokens("    1.23 456 799\tok\nnext line     looks   ok\n", " \t\n\r");
  for (u64 i = 0; toks[i]; i++) {
    printf("token [%"PRIu64"]:   %s\n", i, toks[i]);
  }
  free(toks);

  printf("xorshift(0) -> %"PRIu64"\n", xorshift(0));

  void * pg = pages_alloc_1gb(1);
  if (pg) {
    pages_unmap(pg, 1<<30);
    printf("got 1gb page\n");
  } else {
    printf("no 1gb page\n");
  }
  pg = pages_alloc_2mb(1);
  if (pg) {
    pages_unmap(pg, 1<<21);
    printf("got 2mb page\n");
  } else {
    printf("no 2mb page\n");
  }
  pg = pages_alloc_4kb(1);
  if (pg) {
    pages_unmap(pg, 1<<12);
    printf("got 4kb page\n");
  } else {
    printf("no 4kb page\n");
  }

  pg = malloc(1024);
  if (pg == NULL) {
    printf("malloc failed\n");
    exit(0);
  }
  memset(pg, 0, 1024);
  cpu_clflush1(pg);
  cpu_mfence();
  memset(pg, 1, 1024);
  cpu_clflush(pg, 1024);

  qsort_u16(pg, 512);
  qsort_u32(pg, 256);
  qsort_u64(pg, 128);
  u32 c = crc32(pg, 1024);
  u32 x = xxhash32(pg, 1024);
  u64 xx = xxhash64(pg, 1024);

  printf("crc32 %"PRIu32"\n", c);
  printf("xx32 %"PRIu32" xx64 %"PRIu64"\n", x, xx);
  free(pg);
  //debug_dump_maps(stderr);
  struct kv * const kv = NULL;
  if (sizeof(*kv) != ((char *)kv->kv - (char *)kv)) {
    printf("kv->kv not aligned at end\n");
  }

  // done
  u64 dt = time_diff_nsec(t0);
  double dd = time_diff_sec(d0);
  printf("nsec %"PRIu64" sec %.9lf\n", dt, dd);
  dt = time_nsec();
  dt = time_diff_nsec(dt);
  dd = time_sec();
  dd = time_diff_sec(dd);
  printf("cost nsec %"PRIu64" sec %.9lf\n", dt, dd);
  printf("Done. Good.\n");
}

  static void
test_str(void)
{
  char * str = calloc(1, 21);
  str16_u32(str, 0x1234abcdu);
  printf("str16_u32: [ %s ]\n", str);
  str10_u32(str, 1234567890u);
  printf("str10_u32: [ %s ]\n", str);

  str16_u64(str, UINT64_C(0xef9087651234abcd));
  printf("str16_u64: [ %s ]\n", str);
  str10_u64(str, UINT64_C(13243546571234567890));
  printf("str10_u64: [ %s ]\n", str);
}

  static void
test_hash(void)
{
  const u64 nt = 1024lu*1024*128;
  const u64 nr = nt >> 3;
  u64 szbest;
  u64 * const buf = pages_alloc_best(nr * sizeof(*buf), true, &szbest);
  for (u64 i = 0; i < nr; i++) {
    buf[i] = random_u64();
  }
  u64 x = 0;
  for (u64 r = 256; r <= nr; r<<=1) {
    const u64 ni = nt / r;
    //printf("hash %"PRIu64"MB, hotset %"PRIu64"KB\n", nt >> 20, r >> 7);

    const double t0 = time_sec();
    for (u64 i = 0; i < ni; i++) {
      x += crc32(buf, r << 3);
    }
    const double d0 = time_diff_sec(t0);
    printf("crc32    %016"PRIx64" %.2lf\n", x, d0);

    const double t2 = time_sec();
    for (u64 i = 0; i < ni; i++) {
      x += xxhash32(buf, r << 3);
    }
    const double d2 = time_diff_sec(t2);
    printf("xxhash32 %016"PRIx64" %.2lf\n", x, d2);

    const double t3 = time_sec();
    for (u64 i = 0; i < ni; i++) {
      x += xxhash64(buf, r << 3);
    }
    const double d3 = time_diff_sec(t3);
    printf("xxhash64 %016"PRIx64" %.2lf\n", x, d3);
  }
  pages_unmap(buf, szbest);
}

  static void
test_segv(void)
{
  u64 z = 0;
  for (u64 i = 0; i < 10000; i++) {
    const u64 x = random_u64();
    const u64 * const p = (typeof(p))x;
    const u64 y = *p;
    z ^= y;
  }
  printf("crazy z = %"PRIu64"\n", z);
}

  static void
test_genperf(struct rgen * const gi, const char * const tag, const u64 nr_gen)
{
  const double t0 = time_sec();
  uint64_t r = 0;
  for (uint64_t i = 0; i < nr_gen; i++) {
    r += rgen_next_wait(gi);
  }
  const double dt = time_diff_sec(t0);
  (void)r;
  printf("rgen %s dt %.3lf mops %.3lf\n", tag, dt, ((double)nr_gen)/dt * 1e-6);
}

  static void
test_zipf(void)
{
  char tag[128];
  for (u64 i = 1000000; i < 1000000000; i *= 10) {
    struct rgen * const gi = rgen_new_zipfian(0, i);
    for (u64 j = 0; j < 2; j++) {
      sprintf(tag, "range %"PRIu64" round %"PRIu64, i, j);
      test_genperf(gi, tag, 100000000);
    }
    rgen_destroy(gi);
  }
}

  static void
test_gen(void)
{
  struct rgen * gi;

  const u64 nr_gen = 10000;
  gi = rgen_new_uniform(0, 1000000);
  test_genperf(gi, "uniform", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_zipfian(0, 1000000);
  test_genperf(gi, "zipfian", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_xzipfian(0, 1000000);
  test_genperf(gi, "xzipfian", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_unizipf(0, 1000000, 100);
  test_genperf(gi, "unizipf", nr_gen);
  rgen_destroy(gi);

  gi = rgen_new_counter(0, 1000000);
  test_genperf(gi, "counter", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_skipinc(0, 1000000, 99);
  test_genperf(gi, "skipinc", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_reducer(0, 1000000);
  test_genperf(gi, "reducer", nr_gen);
  rgen_destroy(gi);

  gi = rgen_new_counter_unsafe(0, 1000000);
  test_genperf(gi, "counter_unsafe", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_skipinc_unsafe(0, 1000000, 99);
  test_genperf(gi, "skipinc_unsafe", nr_gen);
  rgen_destroy(gi);
  gi = rgen_new_reducer_unsafe(0, 1000000);
  test_genperf(gi, "reducer_unsafe", nr_gen);
  rgen_destroy(gi);
}

  static void
test_gentrace(void)
{
  const char * const fn = "/tmp/lib1-test-genhead.bin";
  FILE * const out = fopen(fn, "wb");
  for (u64 i = 0; i < 200; i++) {
    const u32 r = (u32)random_u64();
    printf(" %"PRIu32, r);
    fwrite(&r, sizeof(r), 1, out);
  }
  printf("\n\n");
  fclose(out);
  struct rgen * const gen = rgen_new_trace32(fn);
  for (u64 i = 0; i < 400; i++) {
    const u32 r = (u32)rgen_next_wait(gen);
    printf(" %"PRIu32, r);
  }
  rgen_destroy(gen);
}

  static void
test_asyncgen(void)
{
  thread_set_affinity(0);
  struct rgen * const g1 = rgen_new_unizipf(0, 1000000, 1024);
  debug_assert(g1);
  struct rgen * const g2 = rgen_dup(g1);
  debug_assert(g2);
  const bool rc = rgen_async_convert(g2, 1);
  (void)rc;
  debug_assert(rc);
  if (g2 == NULL) {
    printf("convert failed\n");
    rgen_destroy(g1);
    printf("rgen destroyed\n");
    return;
  }
  {
    rgen_async_wait(g2);
    const double t0 = time_sec();
    uint64_t r = 0;
    const u64 nr_gen = 200000000;
    for (uint64_t i = 0; i < nr_gen; i++) {
      r = rgen_next_wait(g2);
      if (i < 16) printf("gen %"PRIu64"\n", r);
    }
    const double dt = time_diff_sec(t0);
    (void)r;
    printf("%s dt %.3lf mops %.3lf\n", __func__, dt, ((double)nr_gen) / (dt * 1.0e6));
  }
  {
    rgen_async_wait(g2);
    const double t0 = time_sec();
    uint64_t r = 0;
    const u64 nr_gen = 200000000;
    for (uint64_t i = 0; i < nr_gen; i++) {
      r = rgen_next_nowait(g2);
      if (i < 16) printf("gen %"PRIu64"\n", r);
    }
    const double dt = time_diff_sec(t0);
    (void)r;
    printf("%s dt %.3lf mops %.3lf\n", __func__, dt, ((double)nr_gen) / (dt * 1.0e6));
  }
  rgen_destroy(g2);
  rgen_destroy(g1);
}

  static void
test_gdb_1(void)
{
  debug_wait_gdb();
}

  static void
test_gdb(void)
{
  test_gdb_1();
}

  static void
test_signal(void)
{
  printf("Generating a segment fault\n");
  sleep(1);
  u64 * i = (u64 *)(u64)usleep(1);
  *i = 123;
}

struct mutexlock_worker_info {
  mutexlock * locks;
  u64 id_mask;
  au64 nr_ops;
  u64 end_time;
};

  static void *
mutexlock_worker(void * const ptr)
{
  struct mutexlock_worker_info * const info = (typeof(info))ptr;
  const u64 mask = info->id_mask;
  const u64 end_time = info->end_time;
  srandom_u64(time_nsec());
  u64 count = 0;
  do {
    for (u64 i = 0; i < 1024; i++) {
      const u64 x = random_u64() & mask;
      mutexlock_lock(&(info->locks[x]));
      mutexlock_unlock(&(info->locks[x]));
    }
    count += 1024;
  } while (time_nsec() < end_time);
  atomic_fetch_add(&(info->nr_ops), count);
  return NULL;
}

  static void
test_mutexlock(void)
{
  for (u64 p = 0; p <= 12; p+=3) {
    const u64 nlocks = 1lu << p;
    struct mutexlock_worker_info info;
    info.locks = (typeof(info.locks))malloc(sizeof(info.locks[0]) * nlocks);
    info.id_mask = nlocks - 1;;
    for (u64 i = 0; i < nlocks; i++) {
      mutexlock_init(&(info.locks[i]));
    }
    for (u64 i = 0; i < 2; i++) {
      atomic_store(&(info.nr_ops), 0);
      info.end_time = time_nsec() + (UINT64_C(5) << 30);
      const double dt = thread_fork_join(0, mutexlock_worker, &info);
      const u64 nr = atomic_load(&info.nr_ops);
      const double mops = ((double)nr) * 0.000001 / dt;
      printf("mutexlock NR %"PRIu64" SIZE %zu %.2lf mops\n", nlocks, nlocks * sizeof(mutexlock), mops);
    }
    free(info.locks);
  }
}

struct spinlock_worker_info {
  spinlock * locks;
  u64 id_mask;
  au64 nr_ops;
  u64 end_time;
};

  static void *
spinlock_worker(void * const ptr)
{
  struct spinlock_worker_info * const info = (typeof(info))ptr;
  const u64 mask = info->id_mask;
  const u64 end_time = info->end_time;
  srandom_u64(time_nsec());
  u64 count = 0;
  do {
    for (u64 i = 0; i < 1024; i++) {
      const u64 x = random_u64() & mask;
      spinlock_lock(&(info->locks[x]));
      spinlock_unlock(&(info->locks[x]));
    }
    count += 1024;
  } while (time_nsec() < end_time);
  atomic_fetch_add(&(info->nr_ops), count);
  return NULL;
}

  static void
test_spinlock(void)
{
  for (u64 p = 0; p <= 12; p+=2) {
    const u64 nlocks = 1lu << p;
    struct spinlock_worker_info info;
    info.locks = (typeof(info.locks))malloc(sizeof(info.locks[0]) * nlocks);
    info.id_mask = nlocks - 1;;
    for (u64 i = 0; i < nlocks; i++) {
      spinlock_init(&(info.locks[i]));
    }
    for (u64 i = 0; i < 1; i++) {
      atomic_store(&(info.nr_ops), 0);
      info.end_time = time_nsec() + (UINT64_C(4) << 30);
      const double dt = thread_fork_join(0, spinlock_worker, &info);
      const u64 nr = atomic_load(&info.nr_ops);
      const double mops = ((double)nr) * 0.000001 / dt;
      printf("spinlock NR %"PRIu64" SIZE %zu %.2lf mops\n", nlocks, nlocks * sizeof(spinlock), mops);
    }
    free(info.locks);
  }
}

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
  printf("%s ", is_writer ? "W" : "R");
  const u64 mask = info->id_mask;
  const u64 end_time = info->end_time;
  srandom_u64(time_nsec());
  u64 count = 0;
  do {
    for (u64 i = 0; i < 1024; i++) {
      const u64 x = random_u64() & mask;
      if (is_writer) {
        rwlock_lock_write(&(info->locks[x]));
        rwlock_unlock_write(&(info->locks[x]));
      } else {
        rwlock_lock_read(&(info->locks[x]));
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
test_rwlock(void)
{
  for (u64 w = 0; w < 4; w++) {
    for (u64 p = 0; p <= 12; p+=3) {
      const u64 nlocks = 1lu << p;
      struct rwlock_worker_info info;
      info.locks = (typeof(info.locks))malloc(sizeof(info.locks[0]) * nlocks);
      info.id_mask = nlocks - 1;;
      info.seq = 0;
      info.nr_writer = w;
      for (u64 i = 0; i < nlocks; i++) {
        rwlock_init(&(info.locks[i]));
      }
      for (u64 i = 0; i < 2; i++) {
        info.seq = 0;
        atomic_store(&(info.nr_w), 0);
        atomic_store(&(info.nr_r), 0);
        info.end_time = time_nsec() + (UINT64_C(5) << 30);
        const double dt = thread_fork_join(0, rwlock_worker, &info);
        const u64 nr_w = atomic_load(&info.nr_w);
        const double mw = ((double)nr_w) * 0.000001 / dt;
        const u64 nr_r = atomic_load(&info.nr_r);
        const double mr = ((double)nr_r) * 0.000001 / dt;
        printf("rwlock NR %"PRIu64" SIZE %zu r %.2lf w %.2lf\n", nlocks, nlocks * sizeof(rwlock), mr, mw);
      }
      free(info.locks);
    }
  }
}

#define RCU_NR_NODES ((1024))
struct rcu_reader_info {
  struct rcu_node * nodes[RCU_NR_NODES];
  au64 nr_op;
};

  static void *
rcu_worker_reader(void * ptr)
{
  struct rcu_reader_info * const info = (typeof(info))ptr;
  const double t0 = time_sec();
  u64 c = 0;
  u64 ctr[4] = {0,0,0,0};
  do {
    for (u64 i = 0; i < 1024; i++) {
      struct rcu_node * const node = info->nodes[random_u64() % RCU_NR_NODES];
      volatile u64 * ptr = (typeof(ptr))rcu_read_ref(node);
      if (ptr) {
        debug_assert(*ptr < 4);
        ctr[*ptr]++;
      }
      rcu_read_unref(node, (void *)ptr);
    }
    c += 1024;
  } while (time_diff_sec(t0) < 10.0);
  printf("%s c %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64" %"PRIu64"\n", __func__, c, ctr[0], ctr[1], ctr[2], ctr[3]);
  (void)atomic_fetch_add(&(info->nr_op), c);
  return NULL;
}

  static void
test_rcu(void)
{
  struct rcu_reader_info info;
  for (u64 i = 0; i < RCU_NR_NODES; i++) {
    info.nodes[i] = rcu_node_create();
  }
  info.nr_op = 0;
  const u64 nth = process_affinity_core_count();
  u64 cores[nth];
  process_affinity_core_list(nth, cores);
  pthread_t pt[nth];
  for (u64 i = 0; i < nth; i++) {
    thread_create_at(cores[i], &pt[i], rcu_worker_reader, &info);
  }
  const double t0 = time_sec();
  u64 nu = 0;
  volatile u64 * const xs = (typeof(xs))malloc(sizeof(xs[0]) * RCU_NR_NODES * 8);
  volatile u64 * const ys = (typeof(ys))malloc(sizeof(ys[0]) * RCU_NR_NODES * 8);
  do {
    const u64 i = random_u64() % RCU_NR_NODES;
    struct rcu_node * const node = info.nodes[i];
    const u64 ii = i * 8;
    xs[ii] = 1;
    atomic_thread_fence(memory_order_seq_cst);
    rcu_update(node, (u64 *)(&xs[ii]));
    ys[ii] = 200000;
    (void)ys[ii];
    atomic_thread_fence(memory_order_seq_cst);
    ys[ii] = 2;
    atomic_thread_fence(memory_order_seq_cst);
    rcu_update(node, (u64 *)(&ys[ii]));
    xs[ii] = 100000;
    (void)xs[ii];
    atomic_thread_fence(memory_order_seq_cst);
    nu++;
  } while (time_diff_sec(t0) < 10.0);
  for (u64 i = 0; i < nth; i++) {
    pthread_join(pt[i], NULL);
  }
  printf("%s nth %"PRIu64" update %"PRIu64" read %"PRIu64" read-mops %.2lf\n", __func__, nth, nu, info.nr_op, ((double)info.nr_op)*1.0e-7);
}

  static bool
match_u64(const void * const p1, const void * const p2)
{
  return memcmp(p1, p2, sizeof(u64)) ? false : true;
}

  static u64
hash_u64(const void * const p)
{
  return xxhash64(p, sizeof(u64));
}

  static void
test_rcache(void)
{
#define RCACHE_TEST_NKEYS ((1lu << 20))
  u64 * keys = (typeof(keys))malloc(sizeof(*keys) * RCACHE_TEST_NKEYS);
  for (u64 i = 0; i < RCACHE_TEST_NKEYS; i++) {
    keys[i] = ((i << 32) | i);
  }
  struct rcache * const cache = (typeof(cache))rcache_create(16, match_u64, hash_u64, hash_u64);
  debug_assert(cache);
  struct rgen * const gen = rgen_new_unizipf(0, RCACHE_TEST_NKEYS - 1, 1024);
  debug_assert(gen);
  for (u64 x = 0; x < 100; x++) {
    const u64 nprobe = 100000;
    u64 hit = 0;
    for (u64 i = 0; i < nprobe; i++) {
      const u64 id = rgen_next_wait(gen);
      u64 * const key = &(keys[id]);
      u64 * const get = rcache_get(cache, key);
      debug_assert((get == NULL) || (get == key));
      if (get) {
        hit++;
      } else {
        rcache_hint(cache, key);
      }
    }
    printf("get hit %"PRIu64"/%"PRIu64"\n", hit, nprobe);
  }

  for (u64 i = 0; i < 1024; i++) {
    u64 * const key = &(keys[i]);
    rcache_invalidate_key(cache, key);
  }
  for (u64 i = 1024; i < 2048; i++) {
    u64 * const key = &(keys[i]);
    rcache_invalidate_kv(cache, key);
  }

  u64 hit0 = 0;
  for (u64 i = 0; i < 2048; i++) {
    u64 * const key = &(keys[i]);
    if (rcache_get(cache, key)) hit0++;
  }
  printf("hit should be 0. It is %"PRIu64"\n", hit0);
  rcache_destroy(cache);
  rgen_destroy(gen);
}

struct alloc_info {
  struct oalloc * oa;
  double end_time;
  au64 nr_op;
  u64 dummy;
};

  static void *
oalloc_worker(void * const ptr)
{
  struct alloc_info * const ai = (typeof(ai))ptr;
  u64 n = 0;
  struct oalloc * const oa = ai->oa;
  u64 dummy = 0;
  do {
    for (u64 i = 0; i < 1000; i++) {
      dummy += ((u64)oalloc_alloc(8, oa));
    }
    n += 1000;
  } while (time_sec() < ai->end_time);
  atomic_fetch_add(&(ai->nr_op), n);
  ai->dummy = dummy;
  return NULL;
}

  static void
test_oalloc(void)
{
  struct alloc_info ai;
  ai.oa = oalloc_create();
  const double dt = 5.0;
  ai.end_time = time_sec() + dt;
  ai.nr_op = 0;
  thread_fork_join(0, oalloc_worker, &ai);
  printf("[%s] nop %"PRIu64" mop/sec %.3lf\n", __func__, ai.nr_op, (double)ai.nr_op / (dt * 1.0e6));
}

  static void
test_gcache(void)
{
  struct gcache * const g = gcache_create(128, 8);
  u64 pos0[100];
  u64 sizes[100];
  for (u64 i = 0; i < 100; i++) {
    sizes[i] = random_u64() % 1000;
    const bool r = gcache_push(g, sizes[i], &(pos0[i]));
    (void)r;
    debug_assert(r);
  }
  u64 *ppos[100];
  struct gcache_iter * const gi = gcache_iter_create(g);
  for (u64 i = 0; i < 100; i++) {
    ppos[i] = gcache_iter_next(gi);
  }
  debug_assert(gcache_iter_next(gi) == NULL);
  qsort_u64((u64 *)ppos, 100);
  for (u64 i = 0; i < 100; i++) {
    if (ppos[i] != &(pos0[i])) {
      printf("! mismatch %p %p\n", &(pos0[i]), ppos[i]);
    }
  }
  gcache_iter_destroy(gi);
  gcache_destroy(g);
}

  static void *
malloc_worker(void * const ptr)
{
  struct alloc_info * const ai = (typeof(ai))ptr;
  u64 n = 0;
  u64 dummy = 0;
  do {
    for (u64 i = 0; i < 1000; i++) {
      dummy += ((u64)malloc(8));
    }
    n += 1000;
  } while (time_sec() < ai->end_time);
  atomic_fetch_add(&(ai->nr_op), n);
  ai->dummy = dummy;
  return NULL;
}

  static void
test_malloc(void)
{
  struct alloc_info ai;
  const double dt = 5.0;
  ai.end_time = time_sec() + dt;
  ai.nr_op = 0;
  thread_fork_join(0, malloc_worker, &ai);
  printf("[%s] nop %"PRIu64" mop/sec %.3lf\n", __func__, ai.nr_op, (double)ai.nr_op / (dt * 1.0e6));
}

  static void
test_perf(void)
{
  debug_perf_start();
  sleep(1);
  u64 * const buf = malloc(sizeof(u64) * 1024*1024);
  u64 x = 0;
  for (u64 i = 0; i < (1024*1024); i++) {
    buf[i] = random_u64();
  }
  debug_perf_switch();
  sleep(1);
  for (u64 i = 0; i < (1024*1024); i++) {
    buf[i] = xxhash64(&(buf[i]), sizeof(u64));
    x += buf[i];
  }
  debug_perf_switch();
  sleep(1);
  for (u64 i = 0; i < (1024*1024); i++) {
    buf[i] = xxhash32(&(buf[i]), sizeof(u64));
    x += buf[i];
  }
  debug_perf_stop();
  sleep(1);
  for (u64 i = 0; i < (1024*1024); i++) {
    buf[i] = xxhash64(&(buf[i]), sizeof(u64));
    x += buf[i];
  }
  printf("dummy %"PRIu64"\n", x);
}

  static void
one_damp(struct damp * const d, const double v)
{
  char c = damp_add_test(d, v) ? 't':'f';
  double avg = damp_average(d);
  printf("add %lf result %c avg %lf\n", v, c, avg);
}

  static void
test_damp(void)
{
  struct damp * const d = damp_create(8, 0.004, 0.05);
  debug_assert(d);
  one_damp(d, 40.0);
  one_damp(d, 40.09);
  one_damp(d, 40.11);
  damp_clean(d);
  one_damp(d, 55.0);
  one_damp(d, 57.0);
  one_damp(d, 55.0);
  one_damp(d, 57.0);
  one_damp(d, 55.0);
  one_damp(d, 57.0);
  one_damp(d, 55.0);
  one_damp(d, 57.0);
  one_damp(d, 55.0);
  one_damp(d, 57.0);
}

  static void
test_shownuma(void)
{
  const u64 nr = process_affinity_core_count();
  u64 cores[nr];
  process_affinity_core_list(nr, cores);
  printf("%"PRIu64" cores: ", nr);
  for (u64 i = 0; i < nr; i++) {
    printf(" %"PRIu64, cores[i]);
  }
  printf("\n");
}

  static void *
__fjworker(void * const ptr)
{
  u64 * const pn = (typeof(pn))ptr;
  const u64 dt = time_diff_nsec(*pn);
  u64 x = *pn;
  for (u64 i = 0; i < 10000000; i++) {
    x ^= xxhash64(&x, 8);
  }
  printf("%"PRIu64" | %"PRIx64"\n", dt, x);
  return NULL;
}

  static void
test_forkjoin(void)
{
  u64 t0 = time_nsec();
  thread_fork_join(0, __fjworker, &t0);
}

  static void
__do_xlog_u64(const u64 nr, const u64 res, const bool cycle)
{
  fprintf(stdout, "%s nr %lu res %lu\n", __func__, nr, res);
  struct xlog * const xlog = xlog_create(nr >> 2, sizeof(u64));
  debug_assert(xlog);
  for (u64 i = 0; i < nr; i++) {
    const u64 t0 = time_nsec();
    const u64 dt = time_diff_nsec(t0);
    if (cycle) {
      xlog_append_cycle(xlog, &dt);
    } else {
      xlog_append(xlog, &dt);
    }
  }
  qsort_u64_sample((const u64 *)xlog->ptr, xlog->nr_rec, res, stdout);
  xlog_destroy(xlog);
}

  static void
__do_xlog_double(const u64 nr, const u64 res, const bool cycle)
{
  fprintf(stdout, "%s nr %lu res %lu\n", __func__, nr, res);
  struct xlog * const xlog = xlog_create(nr >> 2, sizeof(u64));
  debug_assert(xlog);
  for (u64 i = 0; i < nr; i++) {
    const double t0 = time_sec();
    const double dt = time_diff_sec(t0);
    if (cycle) {
      xlog_append_cycle(xlog, &dt);
    } else {
      xlog_append(xlog, &dt);
    }
  }
  qsort_double_sample((const double *)xlog->ptr, xlog->nr_rec, res, stdout);
  xlog_destroy(xlog);
}

  static void
test_xlog(void)
{
  __do_xlog_u64(1024*1024, 20, true);
  __do_xlog_u64(256*256, 30, false);
  __do_xlog_double(1024*1024, 25, true);
  __do_xlog_double(256*256, 35, false);
}

  static void
test_kviter(void)
{
  char * argx[4][3] = {
    {"api", "0", "kvmap2"}, {"api", "0", "cuckoo"},
    {"api", "0", "skiplist"}, {"api", "0", "chainmap"}, };
  struct kv * const key = malloc(1024);
  struct kv * const out = malloc(1024);
  for (u64 sid = 0; sid < 4; sid++) {
    struct kvmap_api * api = NULL;
    if (0 > kvmap_api_helper(3, argx[sid], &api, NULL, false)) continue;
    kv_refill_str(key, "hello", "world");
    api->set(api->map, key);
    kv_refill_str(key, "Monica", "Bill");
    api->set(api->map, key);
    kv_refill_str(key, "e=", "mc2");
    api->set(api->map, key);
    void * const iter = api->iter_create(api->map);
    for (struct kv * kv = api->iter_next(iter, out); kv; kv = api->iter_next(iter, out)) {
      printf("[%s] %.*s -> %.*s\n", argx[sid][2], kv->klen, (char *)kv_key_ptr(kv), kv->vlen, (char *)kv_value_ptr(kv));
    }
    api->iter_destroy(iter);
    kvmap_api_destroy(api);
  }
  free(key);
  free(out);
}

  static int
keycomp_u64(const struct kv * const kv1, const struct kv * const kv2)
{
  debug_assert(kv1->klen == sizeof(u64));
  debug_assert(kv2->klen == sizeof(u64));
  const u64 k1 = *((u64 *)(kv1->kv));
  const u64 k2 = *((u64 *)(kv2->kv));
  if (k1 < k2) {
    return -1;
  } else if (k1 > k2) {
    return 1;
  } else {
    return 0;
  }
}

  static void
kv_refill_u64_u64(struct kv * const kv, const u64 key, const u64 value)
{
  kv->klen = sizeof(u64);
  kv->vlen = sizeof(u64);
  memcpy(kv->kv, &key, sizeof(u64));
  memcpy(kv->kv + sizeof(u64), &value, sizeof(u64));
  kv_update_hash(kv);
}

  static void
test_kvu64(void)
{
#define NI ((32))
#define NJ ((4))
  struct skiplist * const l = skiplist_create_f(NULL, keycomp_u64);
  struct kv * const kv = malloc(1024);
  for (u64 i = 0; i < NI; i++) {
    for (u64 j = 0; j < NJ; j++) {
      const u64 key = random_u64();
      const u64 value = random_u64();
      kv_refill_u64_u64(kv, key, value);
      printf(" %016lx:%016lx", key, value);
      skiplist_set(l, kv);
    }
    printf("\n");
  }
  printf("==\n");
  struct skiplist_iter * const iter = skiplist_iter_create(l);
  u64 count = 0;
  for (struct kv * next = skiplist_iter_next(iter, kv); next; next = skiplist_iter_next(iter, kv)) {
    printf(" %016lx:%016lx", *((u64*)(next->kv)), *((u64*)(next->kv+sizeof(u64))));
    count++;
    if ((count % NJ) == 0) {
      printf("\n");
    }
  }
  skiplist_iter_destroy(iter);
  struct kv * const head = skiplist_head(l, kv);
  printf("head %016lx:%016lx\n", *((u64*)(head->kv)), *((u64*)(head->kv+sizeof(u64))));
  struct kv * const tail = skiplist_tail(l, kv);
  printf("tail %016lx:%016lx\n", *((u64*)(tail->kv)), *((u64*)(tail->kv+sizeof(u64))));
  skiplist_destroy(l);
}

  int
main(int argc, char ** argv)
{
  struct tests {
    char * name;
    void (*func)(void);
  };
#define TESTCASE(name) { #name, test_ ## name, }
  struct tests tests[] = {
    TESTCASE(misc),
    TESTCASE(gdb),
    TESTCASE(signal),
    TESTCASE(hash),
    TESTCASE(segv),
    TESTCASE(gen),
    TESTCASE(gentrace),
    TESTCASE(zipf),
    TESTCASE(asyncgen),
    TESTCASE(spinlock),
    TESTCASE(mutexlock),
    TESTCASE(rwlock),
    TESTCASE(rcu),
    TESTCASE(rcache),
    TESTCASE(oalloc),
    TESTCASE(gcache),
    TESTCASE(malloc),
    TESTCASE(perf),
    TESTCASE(str),
    TESTCASE(damp),
    TESTCASE(shownuma),
    TESTCASE(forkjoin),
    TESTCASE(xlog),
    TESTCASE(kviter),
    TESTCASE(kvu64),
    {NULL, NULL, },
  };
#undef TESTCASE
  if (argc == 1) {
    printf("TEST: ");
    for (int i = 0; tests[i].name; i++) {
      printf(" %s", tests[i].name);
    }
    printf("\n");
  }
  for (int i = 1; i < argc; i++) {
    for (int j = 0; tests[j].name; j++) {
      if (0 == strcmp(tests[j].name, argv[i])) {
        tests[j].func();
      }
    }
  }
  exit(0);
}
