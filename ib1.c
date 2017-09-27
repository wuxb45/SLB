/*
 * Copyright (c) 2016  Wu, Xingbo <wuxb45@gmail.com>
 *
 * All rights reserved. No warranty, explicit or implicit, provided.
 */
#define _GNU_SOURCE

#include "lib1.h"
#include "ib1.h"

// define/struct {{{
#define IB1_QUEUE_DEPTH ((128))
#define IB1_SGE_DEPTH ((1))
#define IB1_SOCKET_QP_INIT ((IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
#define IB1_DATAGRAM_QP_INIT ((IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY))
#define IB1_SOCKET_QP_RTR_UC ((IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN))
#define IB1_SOCKET_QP_RTR_RC ((IB1_SOCKET_QP_RTR_UC | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
#define IB1_SOCKET_QP_RTS_UC ((IBV_QP_STATE | IBV_QP_SQ_PSN))
#define IB1_SOCKET_QP_RTS_RC ((IB1_SOCKET_QP_RTS_UC | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC))
#define IB1_MR_FLAGS ((IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC))
#define IB1_QKEY ((12345))
#define IB1_PSN  ((1000))

struct ib_port {
  struct ibv_context * ctx; // context of device
  u8 port_id;
  struct ibv_pd * pd; // protection domain
};

struct ib_mr {
  void * ptr;
  size_t size;
  struct ibv_mr * mr;
};

struct ib_socket {
  struct ib_port * port;
  struct ibv_qp * qp; // queue pair 0 (send for UD)
  struct ibv_cq * cqs; // send cq
  struct ibv_cq * cqr; // recv cq
  struct ibv_wc * wc; // completion status
  struct ibv_send_wr swr; // send work request
  struct ibv_recv_wr rwr; // recv work request
  u64 depth;
  u64 wid;
  u64 nr_sending;
  u64 nr_recving;
};
// }}} define/struct

// port/memory {{{
  struct ib_port *
ib_port_create(const u64 index)
{
  int num = 0;
  struct ibv_device ** const list = ibv_get_device_list(&num);
  u64 base = 0;
  for (int i = 0; i < num; i++) {
    struct ibv_context * const ctx = ibv_open_device(list[i]);
    if (ctx == NULL) {
      ibv_free_device_list(list);
      return NULL;
    }
    struct ibv_device_attr attr = {};
    const int q = ibv_query_device(ctx, &attr);
    debug_assert(q == 0);

    const u8 np = attr.phys_port_cnt;
    if ((base + np) > index) {
      struct ib_port * const port = (typeof(port))malloc(sizeof(*port));
      debug_assert(port);
      port->ctx = ctx;
      port->port_id = index - base + 1;
      port->pd = ibv_alloc_pd(ctx);
      debug_assert(port->pd);
      ibv_free_device_list(list);
      return port;
    }
    ibv_close_device(ctx);
    base += np;
  }
  ibv_free_device_list(list);
  return NULL;
}

  void
ib_port_destroy(struct ib_port * const port)
{
  ibv_dealloc_pd(port->pd);
  ibv_close_device(port->ctx);
  free(port);
}

  struct ibv_mr *
ib_mr_helper(struct ib_port *port, void *addr, size_t length)
{
  return ibv_reg_mr(port->pd, addr, length, IB1_MR_FLAGS);
}

  void *
ib_sge_refill_off(struct ibv_sge * const sg, struct ibv_mr * const mr, const size_t offset, const size_t size)
{
  debug_assert((offset + size) <= mr->length);
  sg->addr = ((u64)mr->addr) + offset;
  sg->length = size;
  sg->lkey = mr->lkey;
  return (void *)sg->addr;
}

  u64
ib_sge_refill_ptr(struct ibv_sge * const sg, struct ibv_mr * const mr, void * const ptr, const size_t size)
{
  debug_assert((((u64)ptr) + size - ((u64)mr->addr)) <= mr->length);
  debug_assert(ptr >= mr->addr);
  sg->addr = (u64)ptr;
  sg->length = size;
  sg->lkey = mr->lkey;
  return ptr - mr->addr;
}
// }}} port/memory

// socket {{{
  struct ib_socket *
ib_socket_create(struct ib_port * const port, const u64 depth)
{
  struct ib_socket * const s = (typeof(s))calloc(1, sizeof(*s));
  debug_assert(s);
  s->port = port;

  struct ibv_cq * const cqs = ibv_create_cq(port->ctx, depth, NULL, NULL, 0);
  struct ibv_cq * const cqr = ibv_create_cq(port->ctx, depth, NULL, NULL, 0);
  debug_assert(cqs);
  debug_assert(cqr);
  s->cqs = cqs;
  s->cqr = cqr;

  // qp
  struct ibv_qp_init_attr iattr = {};
  iattr.send_cq = cqs;
  iattr.recv_cq = cqr;
  iattr.qp_type = IBV_QPT_RC;
  iattr.sq_sig_all = 0;

  iattr.cap.max_send_wr = depth;
  iattr.cap.max_recv_wr = depth;
  iattr.cap.max_send_sge = IB1_SGE_DEPTH;
  iattr.cap.max_recv_sge = IB1_SGE_DEPTH;
  iattr.cap.max_inline_data = 60;

  s->qp = ibv_create_qp(port->pd, &iattr);
  debug_assert(s->qp);

  // qp init state
  struct ibv_qp_attr qattr = {};
  qattr.qp_state = IBV_QPS_INIT;
  qattr.pkey_index = 0;
  qattr.port_num = port->port_id;
  qattr.qp_access_flags = IB1_MR_FLAGS;
  const int r = ibv_modify_qp(s->qp, &qattr, IB1_SOCKET_QP_INIT);
  debug_assert(r == 0);

  // wc
  s->wc = (typeof(s->wc))calloc(depth, sizeof(s->wc[0]));
  debug_assert(s->wc);

  // wr/sg
  s->swr.num_sge = 1;
  s->rwr.num_sge = 1;
  s->depth = depth;
  return s;
}

  bool
ib_socket_export(struct ib_socket * const s, struct ib_remote * const r)
{
  struct ibv_port_attr attr = {};
  if (ibv_query_port(s->port->ctx, s->port->port_id, &attr) != 0) {
    return false;
  }
  r->lid = attr.lid;
  r->qpn = s->qp->qp_num;
  return true;
}

  bool
ib_socket_connect(struct ib_socket * const s, struct ib_remote * const r)
{
  // RTR
  struct ibv_qp_attr qattr = {};
  qattr.qp_state = IBV_QPS_RTR;
  qattr.path_mtu = IBV_MTU_4096;
  qattr.dest_qp_num = r->qpn;
  qattr.rq_psn = IB1_PSN;

  qattr.ah_attr.is_global = 0;
  qattr.ah_attr.dlid = r->lid;
  qattr.ah_attr.sl = 0;
  qattr.ah_attr.src_path_bits = 0;
  qattr.ah_attr.port_num = s->port->port_id;

  // RC only
  qattr.max_dest_rd_atomic = 16;
  qattr.min_rnr_timer = 12;
  const int mask1 = IB1_SOCKET_QP_RTR_RC;
  const int r1 = ibv_modify_qp(s->qp, &qattr, mask1);
  if (r1 != 0) return false;

  // RTS
  qattr.qp_state = IBV_QPS_RTS;
  qattr.sq_psn = IB1_PSN;
  // RC only
  qattr.timeout = 14;
  qattr.retry_cnt = 7;
  qattr.rnr_retry = 7;
  qattr.max_rd_atomic = 16;
  qattr.max_dest_rd_atomic = 16;
  const int mask2 = IB1_SOCKET_QP_RTS_RC;
  const int r2 = ibv_modify_qp(s->qp, &qattr, mask2);
  if (r2 != 0) return false;
  return true;
}

  bool
ib_socket_connect_stream2(struct ib_socket * const s, struct stream2 * const s2)
{
  struct ib_remote local = {};
  if (ib_socket_export(s, &local) == false) {
    fprintf(stderr, "export failed\n");
    return false;
  }
  fwrite(&local, sizeof(local), 1, s2->w);
  fflush(s2->w);

  struct ib_remote remote = {};
  if (1 != fread(&remote, sizeof(remote), 1, s2->r)) {
    fprintf(stderr, "read remote failed\n");
    return false;
  }

  const bool rc = ib_socket_connect(s, &remote);
  if (rc == false) {
    fprintf(stderr, "connect failed\n");
  }
  return rc;
}

  bool
ib_socket_post_send(struct ib_socket * const s, struct ibv_sge * const sg, const bool signaled)
{
  if (s->nr_sending >= s->depth) return false;
  struct ibv_send_wr * const wr = &(s->swr);
  wr->wr_id = s->wid++;
  wr->sg_list = sg;
  wr->opcode = IBV_WR_SEND;
  wr->send_flags = signaled ? IBV_SEND_SIGNALED : 0;
  if (signaled) s->nr_sending++;
  struct ibv_send_wr * bad_wr = NULL;
  const int r = ibv_post_send(s->qp, wr, &bad_wr);
  return r == 0 ? true : false;
}

  bool
ib_socket_post_recv(struct ib_socket * const s, struct ibv_sge * const sg)
{
  if (s->nr_recving >= s->depth) return false;
  struct ibv_recv_wr * const wr = &(s->rwr);
  wr->wr_id = s->wid++;
  wr->sg_list = sg;
  struct ibv_recv_wr * bad_wr = NULL;
  const int r = ibv_post_recv(s->qp, wr, &bad_wr);
  s->nr_recving++;
  return r == 0 ? true : false;
}

  bool
ib_socket_flush_send(struct ib_socket * const s)
{
  while (s->nr_sending) {
    const int n = ibv_poll_cq(s->cqs, s->depth, s->wc);
    if (n < 0) {
      return false;
    }
    if (n == 0) continue;
    for (int i = 0; i < n; i++) {
      if (s->wc[i].status != IBV_WC_SUCCESS) {
        return false;
      }
    }
    s->nr_sending -= n;
  }
  return true;
}

  bool
ib_socket_flush_recv(struct ib_socket * const s)
{
  while (s->nr_recving) {
    const int n = ibv_poll_cq(s->cqr, s->depth, s->wc);
    if (n < 0) {
      return false;
    }
    if (n == 0) continue;
    for (int i = 0; i < n; i++) {
      if (s->wc[i].status != IBV_WC_SUCCESS) {
        return false;
      }
    }
    s->nr_recving -= n;
  }
  return true;
}

  bool
ib_socket_poll_send(struct ib_socket * const s, u64 * const nr)
{
  const int n = ibv_poll_cq(s->cqs, s->depth, s->wc);
  if (n >= 0) {
    *nr = n;
    s->nr_sending -= n;
    return true;
  } else {
    return false;
  }
}

  bool
ib_socket_poll_recv(struct ib_socket * const s, u64 * const nr)
{
  const int n = ibv_poll_cq(s->cqr, s->depth, s->wc);
  if (n >= 0) {
    *nr = n;
    s->nr_recving -= n;
    return true;
  } else {
    return false;
  }
}

  void
ib_socket_destroy(struct ib_socket * const s)
{
  free(s->wc);
  ibv_destroy_qp(s->qp);
  ibv_destroy_cq(s->cqr);
  ibv_destroy_cq(s->cqs);
  free(s);
}
// }}} socket

// vim:fdm=marker
