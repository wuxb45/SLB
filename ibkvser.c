#include "lib1.h"
#include "ib1.h"

struct worker_info {
  struct kvmap_api * api;
  struct ib_port * port;
  u64 depth;
};

  static u64
batch_process(u8 * const rbuf, u8 * const sbuf, const u64 bufsize, struct kvmap_api * const api)
{
  u8 * rp = rbuf;
  u8 * sp = sbuf;
  u8 * rprefetch = rp;
  u8 * sprefetch = sp;
  u64 batch = 0;
  while (rp[0] != '\0') {
    while ((rp + 64) < rprefetch) {
      __builtin_prefetch(rprefetch, 0, 0);
      rprefetch += 64;
    }
    while ((sp + 64) < sprefetch) {
      __builtin_prefetch(sprefetch, 1, 0);
      sprefetch += 64;
    }
    batch++;
    const char opcode = rp[0];
    sp[0] = rp[0];
    struct kv * const kv =  (typeof(kv))(&(rp[4]));
    struct kv * const out = (typeof(kv))(&(sp[4]));
    rp += 4;
    switch(opcode) {
      case 'G':
        {
          struct kv * const ret = api->get(api->map, kv, out);
          sp[1] = ret ? 'Y' : 'N';
          rp += key_size_align(kv, 4);
          sp += 4;
          if (ret) {
            sp += kv_size_align(ret, 4);
          }
        }
        break;
      case 'P':
        {
          const bool r = api->probe(api->map, kv);
          sp[1] = r ? 'Y' : 'N';
          rp += key_size_align(kv, 4);
          sp += 4;
        }
        break;
      case 'S':
        {
          const bool r = api->set(api->map, kv);
          sp[1] = r ? 'Y' : 'N';
          rp += kv_size_align(kv, 4);
          sp += 4;
        }
        break;
      case 'D':
        {
          const bool r = api->del(api->map, kv);
          sp[1] = r ? 'Y' : 'N';
          rp += key_size_align(kv, 4);
          sp += 4;
        }
        break;
      case 'C':
        {
          api->clean(api->map);
          sp += 4;
        }
        break;
      case 'X': // zero response for shut down
        {
          return 0;
        }
        break;
      default:
        {
          sp += 4;
          return sp - sbuf;
        }
        break;
    }
  }
  sp[0] = '\0';
  sp += 4;
  const u64 respsize = sp - sbuf;
  debug_assert(respsize <= bufsize);
  return respsize;
}

  static u8 *
__bufptr(u8 * const base, const u64 unit, const u64 i)
{
  return &(base[unit * i]);
}

  static void *
kv_worker(void * const ptr)
{
  struct server_wi * const si = (typeof(si))ptr;
  struct worker_info * const wi = (typeof(wi))server_wi_private(si);
  const u64 depth = wi->depth;
  struct kvmap_api * const api = wi->api;

  // depth * 1MB
  const u64 allocunit = UINT64_C(1) << 21;
  u64 memsize = 0;
  u8 * const mem = pages_alloc_best(depth * allocunit, true, &memsize);
  debug_assert(mem);
  struct ibv_mr * const mr = ib_mr_helper(wi->port, mem, memsize);
  debug_assert(mr);
  struct ib_socket * const s = ib_socket_create(wi->port, wi->depth);
  debug_assert(s);
  const bool rc = ib_socket_connect_stream2(s, server_wi_stream2(si));
  debug_assert(rc);

  const u64 bufsize = (allocunit - (4*4096));
  const u64 half = bufsize >> 1;
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
      u8 * const rbuf = __bufptr(mem, bufsize, buf_tail);
      ib_sge_refill_ptr(&sg, mr, rbuf, half);
      ib_socket_post_recv(s, &sg);
      buf_tail = (buf_tail + 1) % depth;
      active++;
    }

    // handle requests
    if (ib_socket_poll_recv(s, &nr)) {
      for (u64 i = 0; i < nr; i++) {
        u8 * const rbuf = __bufptr(mem, bufsize, buf_head);
        u8 * const sbuf = rbuf + half;
        const u64 rsize = batch_process(rbuf, sbuf, half, api);
        if (rsize == 0) { // shutdown and no response
          running = false;
          break;
        }
        ib_sge_refill_ptr(&sg, mr, sbuf, rsize);
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
  ib_socket_poll_recv(s, &nr);
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
    fprintf(stderr, "Usage: %s <ib-port> <host> <port> <depth> api ...\n", argv[0]);
    kvmap_api_helper_message();
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
  struct kvmap_api * api = NULL;
  struct oalloc * const oa = oalloc_create();
  struct kvmap_mm mm = { .af = (kv_alloc_func)oalloc_alloc, .ap = oa, };
  const int n1 = kvmap_api_helper(argc - 5, argv + 5, &api, &mm, false);
  if (n1 < 0 || api == NULL) {
    fprintf(stderr, "kvmap_api_helper() failed\n");
    exit(0);
  }
  struct worker_info wi;
  wi.api = api;
  wi.port = port;
  wi.depth = depth;
  struct server * const ser = server_create(host, pn, kv_worker, &wi);
  if (ser == NULL) {
    fprintf(stderr, "server_create() failed\n");
    exit(0);
  }
  server_wait(ser);
  kvmap_api_destroy(api);
  oalloc_destroy(oa);
  return 0;
}
