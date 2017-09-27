/*
 * Copyright (c) 2016  Wu, Xingbo <wuxb45@gmail.com>
 *
 * All rights reserved. No warranty, explicit or implicit, provided.
 */
#include "lib1.h"

#define XSA ((0))
#define XSS ((1))
#define XDA ((2))
#define XDS ((3))
#define XGA ((4))
#define XGS ((5))
#define XPA ((6))
#define XPS ((7))

  static bool
kvmap_analyze(struct vctr * const va, const double dt, struct damp * const damp, char * const out)
{
  u64 v[8];
  for (u64 i = 0; i < 8; i++) {
    v[i] = vctr_get(va, i);
  }

  const u64 nrop = v[XSA] + v[XDA] + v[XGA] + v[XPA];
  const double mops = ((double)nrop) / (dt * 1.0e6);
  const bool done = damp_add_test(damp, mops);
  const double havg = damp_average(damp);
  const char * const pat = " set %"PRIu64" %"PRIu64" del %"PRIu64" %"PRIu64
      " get %"PRIu64" %"PRIu64" pro %"PRIu64" %"PRIu64" mops %.4lf havg %.4lf\n";
  sprintf(out, pat, v[XSA], v[XSS], v[XDA], v[XDS], v[XGA], v[XGS], v[XPA], v[XPS], mops, havg);
  return done;
}

  static void
kvmap_mix_batch(const struct maptest_worker_info * const info, const u64 nr)
{
  struct kvmap_api * const api = (typeof(api))info->api;
  struct kv * const kv = (typeof(kv))info->priv;
  struct kv * const out = (typeof(out))(((u8*)info->priv)+384);
  struct vctr * const v = info->vctr;
  rgen_next_func next = info->rgen_next;

  for (u64 i = 0; i < nr; i++) {
    const u64 x = next(info->gen);
    kv_refill(kv, &x, sizeof(x), "", 0);
    const u64 y = random_u64() % 100lu;
    if (y < info->pset) { // SET
      vctr_add1(v, XSA);
      kv->vlen = info->vlen;
      if (api->set(api->map, kv)) vctr_add1(v, XSS);
    } else if (y < info->pdel) { // DEL
      vctr_add1(v, XDA);
      if (api->del(api->map, kv)) vctr_add1(v, XDS);
    } else if (y < info->pget) { // GET
      vctr_add1(v, XGA);
      if (api->get(api->map, kv, out)) vctr_add1(v, XGS);
    } else { // PROBE
      vctr_add1(v, XPA);
      if (api->probe(api->map, kv)) vctr_add1(v, XPS);
    }
  }
}

  static void *
kvmap_worker(void * const ptr)
{
  struct maptest_worker_info * const info = (typeof(info))ptr;
  const u64 end_magic = info->end_magic;
  info->priv = malloc(2048);
  srandom_u64(info->seed);
  if (info->end_type == MAPTEST_END_TIME) {
    do {
      kvmap_mix_batch(info, 4096);
    } while (time_nsec() < end_magic);
  } else if (info->end_type == MAPTEST_END_COUNT) {
    u64 count = 0;
    do {
      const u64 nr = (end_magic - count) > 4096 ? 4096 : (end_magic - count);
      kvmap_mix_batch(info, nr);
      count += nr;
    } while (count < end_magic);
  }
  free(info->priv);
  return NULL;
}

  static void
kvmap_help_message(void)
{
  fprintf(stderr, "%s Usage: <stdout> <stderr> {api ... {rgen ... {pass ...}}}\n", __func__);
  kvmap_api_helper_message();
  maptest_passes_message();
  fprintf(stderr, "set MAP_TEST_PERF=y to enable perf recording\n");
}

  static int
test_kvmap(const int argc, char ** const argv)
{
  struct oalloc * const oa = oalloc_create();
  debug_assert(oa);
  struct kvmap_mm mm = { .af = (kv_alloc_func)oalloc_alloc, .ap = oa, };
  //struct kvmap_mm mm = { .af = kv_alloc_malloc, .rf = kv_retire_free, };
  struct kvmap_api * api = NULL;
  const int n1 = kvmap_api_helper(argc, argv, &api, &mm, false);
  if (n1 < 0) {
    kvmap_help_message();
    oalloc_destroy(oa);
    return n1;
  }

  char *pref[64] = {};
  for (int i = 0; i < n1; i++) {
    pref[i] = argv[i];
  }
  pref[n1] = NULL;

  struct pass_info pi = {};
  pi.api = api;
  pi.vctr_size = 8;
  pi.wf = kvmap_worker;
  pi.af = kvmap_analyze;
  const int n2 = maptest_passes(argc - n1, argv + n1, pref, &pi);

  kvmap_api_destroy(api);
  oalloc_destroy(oa);
  if (n2 < 0) {
    kvmap_help_message();
    return n2;
  } else {
    return n1 + n2;
  }
}

  int
main(int argc, char ** argv)
{
  const bool r = maptest_main(argc, argv, test_kvmap);
  if (r == false) kvmap_help_message();
  return 0;
}
