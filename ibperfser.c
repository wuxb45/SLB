#include "lib1.h"
#include "ib1.h"

struct worker_info {
  struct ib_port * port;
  u64 depth;
  u64 unit;
};

  static void *
perf_worker(void * const ptr)
{
  struct server_wi * const si = (typeof(si))ptr;
  struct worker_info * const wi = (typeof(wi))server_wi_private(si);
  const u64 depth = wi->depth;
  const u64 unit = wi->unit;

  // depth * 1MB
  u64 memsize = 0;
  u8 * const mem = pages_alloc_best(depth * unit, true, &memsize);
  debug_assert(mem);
  memset(mem, 0, memsize);
  struct ibv_mr * const mr = ib_mr_helper(wi->port, mem, memsize);
  debug_assert(mr);
  struct ib_socket * const s = ib_socket_create(wi->port, wi->depth);
  debug_assert(s);
  const bool rc = ib_socket_connect_stream2(s, server_wi_stream2(si));
  debug_assert(rc);

  u64 *bufs[depth];
  for (u64 i = 0; i < depth; i++) {
    bufs[i] = (u64 *)(&(mem[unit * i]));
  }

  u64 buf_head = 0;
  u64 buf_tail = 0;
  u64 active = 0;
  const u64 max_active = depth >> 1;
  const u64 send_polls = depth >> 2;
  struct ibv_sge sg;
  bool running = true;
  u64 send_seq = 0;
  u64 ns, nr;
  do {
    // prepared for receive requests
    while (active < max_active) {
      ib_sge_refill_ptr(&sg, mr, bufs[buf_tail], unit);
      ib_socket_post_recv(s, &sg);
      buf_tail = (buf_tail + 1) % depth;
      active++;
    }

    // handle requests
    if (ib_socket_poll_recv(s, &nr)) {
      for (u64 i = 0; i < nr; i++) {
        if (bufs[buf_head][0] == 0) {
          running = false;
          break;
        }
        const u64 respsize = bufs[buf_head][0];
        ib_sge_refill_ptr(&sg, mr, bufs[buf_head], respsize);
        ib_socket_post_send(s, &sg, true);
        send_seq++;
        if (send_seq % send_polls == 0) {
          ib_socket_poll_send(s, &ns);
        }
        buf_head = (buf_head + 1) % depth;
        debug_assert(active);
        active--;
      }
    }
  } while (running);
  ib_socket_flush_send(s);
  ib_socket_destroy(s);
  ibv_dereg_mr(mr);
  pages_unmap(mem, memsize);
  server_wi_destroy(si);
  return NULL;
}

  int
main(int argc, char ** argv)
{
  if (argc < 6) {
    fprintf(stderr, "Usage: %s <ib-port> <host> <port> <depth> <max-unit>\n", argv[0]);
    exit(0);
  }

  struct ib_port * const port = ib_port_create(atoi(argv[1]));
  if (port == NULL) {
    fprintf(stderr, "ib_port_create() failed\n");
    exit(0);
  }

  const char * const host = argv[2];
  const int pn = atoi(argv[3]);
  const u64 depth = strtoull(argv[4], NULL, 10);
  const u64 unit = strtoull(argv[5], NULL, 10);

  struct worker_info wi;
  wi.port = port;
  wi.depth = depth;
  wi.unit = unit;
  struct server * const ser = server_create(host, pn, perf_worker, &wi);
  if (ser == NULL) {
    fprintf(stderr, "server_create() failed\n");
    exit(0);
  }

  server_wait(ser);
  return 0;
}
