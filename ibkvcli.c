#include "lib1.h"
#include "ib1.h"

#define XSA ((0))
#define XDA ((1))
#define XGA ((2))
#define XPA ((3))
//#define XSS ((4))
//#define XDS ((5))
//#define XGS ((6))
//#define XPS ((7))

struct worker_info {
  struct ib_port * port;
  char * host;
  int pn;
  u64 depth;
  u64 batch;
};

  static u64
fill_requests(u8 * const sbuf, struct maptest_worker_info * const mi, const u64 batch, const u64 bufsize)
{
  u8 * p = sbuf;
  rgen_next_func next = mi->rgen_next;
  struct vctr * const v = mi->vctr;

  for (u64 i = 0; i < batch; i++) {
    const u64 x = next(mi->gen);
    const u64 y = random_u64() % 100lu;
    if (y < mi->pset) {
      p[0] = 'S';
      p += 4;
      kv_refill((struct kv *)(p), &x, sizeof(x), mi->priv, mi->vlen);
      p += kv_size_align((struct kv *)(p), 4);
      vctr_add1(v, XSA);
    } else {
      if (y < mi->pdel) {
        p[0] = 'D';
        vctr_add1(v, XDA);
      } else if (y < mi->pget) {
        p[0] = 'G';
        vctr_add1(v, XGA);
      } else {
        p[0] = 'P';
        vctr_add1(v, XPA);
      }
      p += 4;
      kv_refill((struct kv *)(p), &x, sizeof(x), "", 0);
      p += key_size_align((struct kv *)(p), 4);
    }
  }

  p[0] = '\0';
  p += 4;
  const u64 reqsize = p - sbuf;
  debug_assert(reqsize <= bufsize);
  return reqsize;
}

  static u64
fill_shutdown(u8 * const sbuf)
{
  sbuf[0] = 'X';
  sbuf[4] = '\0';
  return 8;
}

  static bool
ibkv_analyze(struct vctr * const va, const double dt, struct damp * const damp, char * const out)
{
  u64 v[8];
  for (u64 i = 0; i < 4; i++) {
    v[i] = vctr_get(va, i);
  }

  const u64 nrop = v[XSA] + v[XDA] + v[XGA] + v[XPA];
  const double mops = ((double)nrop) / (dt * 1.0e6);
  const bool done = damp_add_test(damp, mops);
  const double havg = damp_average(damp);
  const char * const pat = " set %"PRIu64" del %"PRIu64" get %"PRIu64" pro %"PRIu64" mops %.2lf havg %.2lf\n";
  sprintf(out, pat, v[XSA], v[XDA], v[XGA], v[XPA], mops, havg);
  return done;
}

  static u8 *
__bufptr(u8 * const base, const u64 unit, const u64 i)
{
  return &(base[unit * i]);
}

  static void *
ibkv_worker(void * const ptr)
{
  struct maptest_worker_info * const mi = (typeof(mi))ptr;
  struct worker_info * const wi = (typeof(wi))mi->api;
  const u64 depth = wi->depth;
  const u64 batch = wi->batch;
  mi->priv = malloc(4096);
  memset(mi->priv, 'x', 4096);

  // depth * 2MB
  const u64 allocunit = UINT64_C(1) << 21;
  u64 memsize = 0;
  u8 * const mem = pages_alloc_best(depth * allocunit, true, &memsize);
  debug_assert(mem);
  struct ibv_mr * const mr = ib_mr_helper(wi->port, mem, memsize);
  debug_assert(mr);
  struct ib_socket * const s = ib_socket_create(wi->port, depth);
  debug_assert(s);
  struct stream2 * const s2 = stream2_create(wi->host, wi->pn);
  debug_assert(s2);
  const bool rc = ib_socket_connect_stream2(s, s2);
  stream2_destroy(s2);
  debug_assert(rc);

  const u64 bufsize = (allocunit - (4*4096));
  const u64 half = bufsize >> 1;
  u64 buf_head = 0;
  u64 buf_tail = 0;
  u64 active = 0;
  const u64 max_active = depth >> 1;
  const u64 send_polls = depth >> 2;
  struct ibv_sge sg;
  u64 send_seq = 0;
  u64 ns, nr;
  bool done = false;
  do {
    // queue requests
    while (active < max_active) {
      // send
      u8 * sbuf = __bufptr(mem, bufsize, buf_tail);
      const u64 reqsize = fill_requests(sbuf, mi, batch, half);
      (void)ib_sge_refill_ptr(&sg, mr, sbuf, reqsize);
      ib_socket_post_send(s, &sg, true);

      // recv
      u8 * const rbuf = sbuf + half;
      (void)ib_sge_refill_ptr(&sg, mr, rbuf, half);
      ib_socket_post_recv(s, &sg);
      buf_tail = (buf_tail + 1) % depth;
      active++;
      send_seq++;
      if (send_seq % send_polls == 0) {
        ib_socket_poll_send(s, &ns);
      }
      if ((mi->end_type == MAPTEST_END_COUNT) && ((send_seq * batch) >= mi->end_magic)) {
        done = true;
        break;
      }
    }

    if (ib_socket_poll_recv(s, &nr)) {
      for (u64 i = 0; i < nr; i++) {
        buf_head = (buf_head + 1) % depth;
        active--;
      }
    }
    if ((mi->end_type == MAPTEST_END_TIME) && (time_nsec() >= mi->end_magic)) done = true;
  } while (done == false);
  u8 * const shutbuf = __bufptr(mem, bufsize, buf_tail);
  fill_shutdown(shutbuf);
  (void)ib_sge_refill_ptr(&sg, mr, shutbuf, 8);
  ib_socket_post_send(s, &sg, true);
  ib_socket_flush_send(s);
  ib_socket_flush_recv(s);

  ib_socket_destroy(s);
  ibv_dereg_mr(mr);
  pages_unmap(mem, memsize);
  return NULL;
}

  static void
ibkv_help_message(void)
{
  fprintf(stderr, "Usage: <stdout> <stderr> api <ib-port> <host> <port> <depth> <batch> {rgen ... {pass ... }}\n");
  maptest_passes_message();
  fprintf(stderr, "set MAP_TEST_PERF=y to enable perf recording\n");
}

  int
test_ibkv(const int argc, char ** const argv)
{
  if (argc < 6 || (strcmp(argv[0], "api") != 0)) {
    return -1;
  }

  const u64 pid = strtoull(argv[1], NULL, 10);
  char * const host = argv[2];
  const int pn = atoi(argv[3]);
  const u64 depth = strtoull(argv[4], NULL, 10);
  const u64 batch = strtoull(argv[5], NULL, 10);

  char *pref[64] = {};
  for (int i = 0; i < 6; i++) {
    pref[i] = argv[i];
  }
  pref[6] = NULL;

  struct worker_info wi;
  wi.port = ib_port_create(pid);
  wi.host = host;
  wi.pn = pn;
  wi.depth = depth;
  wi.batch = batch;

  struct pass_info pi = {};
  pi.api = &wi;
  pi.vctr_size = 4;
  pi.wf = ibkv_worker;
  pi.af = ibkv_analyze;
  const int n2 = maptest_passes(argc - 6, argv + 6, pref, &pi);
  ib_port_destroy(wi.port);
  if (n2 < 0) {
    return n2;
  }
  return 6 + n2;
}

  int
main(int argc, char ** argv)
{
  const bool r = maptest_main(argc, argv, test_ibkv);
  if (r == false) ibkv_help_message();
  return 0;
}
