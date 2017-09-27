#include "lib1.h"

  static struct kv *
__read_kv(struct stream2 * const s2, const bool read_kv)
{
  struct kv head;
  if (fread(&head, sizeof(head), 1, s2->r) != 1) {
    return NULL;
  }

  const size_t size = read_kv ? kv_size(&head) : key_size(&head);
  struct kv * const ret = (typeof(ret))malloc(size);
  memcpy(ret, &head, sizeof(head));
  if (fread(ret->kv, size - sizeof(head), 1, s2->r) != 1) {
    free(ret);
    return NULL;
  }
  return ret;
}

  static bool
__write_ack(struct stream2 * const s2, const bool ack)
{
  if (fwrite(&ack, sizeof(ack), 1, s2->w) != 1) {
    return false;
  }
  return true;
}

  static bool
kv_get(struct stream2 * const s2, struct kvmap_api * const api)
{
  struct kv * key = __read_kv(s2, false);
  if (key == NULL) return false;

  struct kv * const r = api->get(api->map, key, NULL);
  free(key);

  if (__write_ack(s2, r ? true : false) == false) {
    if (r) free(r);
    return false;
  }
  if (r) {
    if (fwrite(r, kv_size(r), 1, s2->w) != 1) {
      free(r);
      return false;
    }
    free(r);
  }
  fflush(s2->w);
  return true;
}

  static bool
kv_probe(struct stream2 * const s2, struct kvmap_api * const api)
{
  struct kv * key = __read_kv(s2, false);
  if (key == NULL) return false;

  const bool r = api->probe(api->map, key);
  free(key);

  if (__write_ack(s2, r) == false) {
    return false;
  }
  fflush(s2->w);
  return true;
}

  static bool
kv_set(struct stream2 * const s2, struct kvmap_api * const api)
{
  struct kv * kv = __read_kv(s2, true);
  if (kv == NULL) return false;

  const bool r = api->set(api->map, kv);
  free(kv);

  if (__write_ack(s2, r) == false) {
    return false;
  }
  fflush(s2->w);
  return true;
}

  static bool
kv_del(struct stream2 * const s2, struct kvmap_api * const api)
{
  struct kv * key = __read_kv(s2, false);
  if (key == NULL) return false;

  const bool r = api->del(api->map, key);
  free(key);

  if (__write_ack(s2, r) == false) {
    return false;
  }
  fflush(s2->w);
  return true;
}

  static bool
kv_clean(struct stream2 * const s2, struct kvmap_api * const api)
{
  api->clean(api->map);
  bool r = true;
  if (__write_ack(s2, r) == false) {
    return false;
  }
  fflush(s2->w);
  return true;
}

  static void *
kv_worker(void * const ptr)
{
  if (ptr == NULL) return NULL;
  struct server_wi * const wi = (typeof(wi))ptr;
  struct stream2 * const s2 = server_wi_stream2(wi);
  struct kvmap_api * const api = (typeof(api))server_wi_private(wi);
  while ((!feof(s2->r)) && (!ferror(s2->r)) && (!ferror(s2->w))) {
    char opcode;
    if (fread(&opcode, sizeof(opcode), 1, s2->r) != 1) break;
    bool r = false;
    switch(opcode) {
    case 'G':
      r = kv_get(s2, api); break;
    case 'P':
      r = kv_probe(s2, api); break;
    case 'S':
      r = kv_set(s2, api); break;
    case 'D':
      r = kv_del(s2, api); break;
    case 'C':
      r = kv_clean(s2, api); break;
    default:
      break;
    }
    if (r == false) break;
  }
  server_wi_destroy(wi);
  return NULL;
}

  int
main(int argc, char ** argv)
{
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <host> <port> api ...\n", argv[0]);
    kvmap_api_helper_message();
    exit(0);
  }

  const char * const host = argv[1];
  const int pn = atoi(argv[2]);
  struct kvmap_api * api = NULL;
  struct kvmap_mm mm = {};
  mm.af = kv_alloc_malloc;
  mm.rf = kv_retire_free;
  argc -= 3;
  argv += 3;
  const int n1 = kvmap_api_helper(argc, argv, &api, &mm, false);
  if (n1 < 0 || api == NULL) {
    fprintf(stderr, "kvmap_api_helper() failed\n");
    exit(0);
  }
  struct server * const ser = server_create(host, pn, kv_worker, api);
  if (ser == NULL) {
    fprintf(stderr, "server_create() failed\n");
    exit(0);
  }
  //fclose(stdin);
  //fclose(stdout);
  //fclose(stderr);
  server_wait(ser);
  return 0;
}
