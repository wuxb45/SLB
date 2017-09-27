/*
 * Copyright (c) 2016  Wu, Xingbo <wuxb45@gmail.com>
 *
 * All rights reserved. No warranty, explicit or implicit, provided.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <infiniband/verbs.h>

struct ib_port;
struct ib_mr;
struct ib_socket;

struct ib_remote {
  int lid;
  int qpn;
};

struct ib_memory {
  void * ptr;
  size_t size;
  u32 key;
};

  extern struct ib_port *
ib_port_create(const u64 index);

  extern void
ib_port_destroy(struct ib_port * const port);

  extern struct ibv_mr *
ib_mr_helper(struct ib_port *port, void *addr, size_t length);

  extern void *
ib_sge_refill_off(struct ibv_sge * const sg, struct ibv_mr * const mr, const size_t offset, const size_t size);

  extern u64
ib_sge_refill_ptr(struct ibv_sge * const sg, struct ibv_mr * const mr, void * const ptr, const size_t size);

  extern struct ib_socket *
ib_socket_create(struct ib_port * const port, const u64 depth);

  extern bool
ib_socket_export(struct ib_socket * const s, struct ib_remote * const r);

  extern bool
ib_socket_connect(struct ib_socket * const s, struct ib_remote * const r);

  extern bool
ib_socket_connect_stream2(struct ib_socket * const s, struct stream2 * const s2);

  extern bool
ib_socket_post_send(struct ib_socket * const s, struct ibv_sge * const sg, const bool signaled);

  extern bool
ib_socket_post_recv(struct ib_socket * const s, struct ibv_sge * const sg);

  extern bool
ib_socket_flush_send(struct ib_socket * const s);

  extern bool
ib_socket_flush_recv(struct ib_socket * const s);

  extern bool
ib_socket_poll_send(struct ib_socket * const s, u64 * const nr);

  extern bool
ib_socket_poll_recv(struct ib_socket * const s, u64 * const nr);

  extern void
ib_socket_destroy(struct ib_socket * const s);

#ifdef __cplusplus
}
#endif
// vim:fdm=marker
