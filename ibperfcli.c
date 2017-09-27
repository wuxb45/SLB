#include "lib1.h"
#include "ib1.h"

struct worker_info {
  struct ib_port * port;
  char * host;
  int pn;
  u64 depth;
  u64 unit;
};

  static bool
ibperf_analyze(struct vctr * const va, const double dt, struct damp * const damp, char * const out)
{
  const u64 nb = vctr_get(va, 0);
  const double mbps = ((double)nb) / (dt * (1024.0*1024.0));
  const bool done = damp_add_test(damp, mbps);
  const double havg = damp_average(damp);
  const char * const pat = " bytes %"PRIu64" mbps %.4lf havg %.4lf\n";
  sprintf(out, pat, nb, mbps, havg);
  return done;
}

  static void *
ibperf_worker(void * const ptr)
{
  struct maptest_worker_info * const mi = (typeof(mi))ptr;
  struct worker_info * const wi = (typeof(wi))mi->api;
  const u64 depth = wi->depth;
  const u64 unit = wi->unit;
  debug_assert(mi->vlen <= unit);

  // depth * 1MB
  u64 memsize = 0;
  u8 * const mem = pages_alloc_best(depth * unit, true, &memsize);
  debug_assert(mem);
  memset(mem, 0, memsize);
  struct ibv_mr * const mr = ib_mr_helper(wi->port, mem, memsize);
  debug_assert(mr);
  struct ib_socket * const s = ib_socket_create(wi->port, depth);
  debug_assert(s);
  struct stream2 * const s2 = stream2_create(wi->host, wi->pn);
  debug_assert(s2);
  const bool rc = ib_socket_connect_stream2(s, s2);
  stream2_destroy(s2);
  debug_assert(rc);

  u64 *bufs[depth];
  for (u64 i = 0; i < depth; i++) {
    bufs[i] = (u64 *)(&(mem[unit * i]));
  }
  const u64 vlen = mi->vlen;

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
      bufs[buf_tail][0] = vlen;
      (void)ib_sge_refill_ptr(&sg, mr, bufs[buf_tail], vlen);
      ib_socket_post_send(s, &sg, true);
      (void)ib_sge_refill_ptr(&sg, mr, bufs[buf_tail], unit);
      ib_socket_post_recv(s, &sg);
      buf_tail = (buf_tail + 1) % depth;
      active++;
      send_seq++;
      vctr_add(mi->vctr, 0, vlen);
      if (send_seq % send_polls == 0) {
        ib_socket_poll_send(s, &ns);
      }
      if ((mi->end_type == MAPTEST_END_COUNT) && (send_seq >= mi->end_magic)) {
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
  ib_socket_flush_recv(s);
  memset(mem, 0, 8);
  ib_sge_refill_ptr(&sg, mr, mem, 8);
  ib_socket_post_send(s, &sg, true);
  ib_socket_flush_send(s);

  ib_socket_destroy(s);
  ibv_dereg_mr(mr);
  pages_unmap(mem, memsize);
  return NULL;
}

  static void
ibperf_help_message(void)
{
  fprintf(stderr, "Usage: <stdout> <stderr> api <ib-port> <host> <port> <depth> <max-unit> {rgen ... {pass ... }}\n");
  fprintf(stderr, "rgen: not used. passes: pset/pdel/pget ignored. rgenopt should be 0.\n");
  maptest_passes_message();
  fprintf(stderr, "set MAP_TEST_PERF=y to enable perf recording\n");
}

  int
test_ibperf(const int argc, char ** const argv)
{
  if (argc < 6 || (strcmp(argv[0], "api") != 0)) {
    return -1;
  }
  const u64 pid = strtoull(argv[1], NULL, 10);
  char * const host = argv[2];
  const int pn = atoi(argv[3]);
  const u64 depth = strtoull(argv[4], NULL, 10);
  const u64 unit = strtoull(argv[5], NULL, 10);

  char *pref[64] = {};
  for (int i = 0; i < 6; i++) {
    pref[i] = argv[i];
  }
  pref[6] = NULL;

  struct worker_info wi = {};
  wi.port = ib_port_create(pid);
  wi.host = host;
  wi.pn = pn;
  wi.depth = depth;
  wi.unit = unit;

  struct pass_info pi = {};
  pi.api = &wi;
  pi.vctr_size = 1;
  pi.wf = ibperf_worker;
  pi.af = ibperf_analyze;

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
  const bool r = maptest_main(argc, argv, test_ibperf);
  if (r == false) ibperf_help_message();
  return 0;
}
