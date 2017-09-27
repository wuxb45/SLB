#include "lib1.h"
#include "ib1.h"

int
main(int argc, char ** argv)
{
  if (argc < 3) {
    printf("usage: %s <host> <port>\n", argv[0]);
    exit(1);
  }
  struct tcpmap * const map = tcpmap_create(argv[1], atoi(argv[2]));
  if (map == NULL) {
    fprintf(stderr, "connect to map failed\n");
    exit(1);
  }
  tcpmap_clean(map);
  tcpmap_destroy(map);
  return 0;
}
