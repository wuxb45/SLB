#include "lib1.h"

  int
main(int argc, char ** argv)
{
  struct tcpmap * map = NULL;
  if (argc < 3) {
    printf("Usage: %s <host> <port>\n", argv[0]);
    printf("Use default setting: localhost 9999\n");
    map = tcpmap_create("localhost", 9999);
  } else {
    map = tcpmap_create(argv[1], atoi(argv[2]));
  }

  if (map == NULL) {
    printf("failed\n");
    exit(0);
  }

  struct kv * const kv = malloc(1024);
  struct kv * const out = malloc(1024);

  kv_refill_str(kv, "abc", "123");
  printf("set %c/t\n", tcpmap_set(map, kv) ? 't': 'f');

  kv_refill_str(kv, "hello", "45678");
  printf("set %c/t\n", tcpmap_set(map, kv) ? 't': 'f');

  kv_refill_str(kv, "notthere", "");
  printf("get %c/f\n", tcpmap_get(map, kv, out) ? 't':'f');
  printf("probe %c/f\n", tcpmap_probe(map, kv) ? 't':'f');

  kv_refill_str(kv, "hello", "");
  printf("get %c/t\n", tcpmap_get(map, kv, out) ? 't':'f');

  kv_refill_str(kv, "abc", "");
  printf("get %c/t\n", tcpmap_get(map, kv, out) ? 't':'f');

  kv_refill_str(kv, "abc", "");
  printf("del %c/t\n", tcpmap_del(map, kv) ? 't':'f');

  kv_refill_str(kv, "abc", "");
  printf("get %c/f\n", tcpmap_get(map, kv, out) ? 't':'f');
  free(kv);
  free(out);

  tcpmap_destroy(map);
  return 0;
}
