# Search Lookaside Buffer (SLB)

This repo contains code of [Search Lookaside Buffer](http://omega.uta.edu/~xxw4571/papers/slb.pdf), by Xingbo Wu, Fan Ni, and Song Jiang, published on ACM SoCC'17.

The bibtex entry of this paper can be found [here](https://dl.acm.org/citation.cfm?id=3127483).

## Implementation
There are three SLB implementations:

1. icache: The most general SLB cache implementation. It wraps around index data structures using KVMAP API (see struct kvmap\_api).
Currently there are several index data structures available in the code base, including Chaining hash table, Cuckoo hash table, Skip list, and B+-tree.
To use SLB with another data structure, users need to wrap it with the KVMAP API.
kvmap\_api\_helper() is a single API that can be used to create any integrated index with flexible configurability.
icache is thread safe.

2. ucache: It is almost identical to icache but it removes the locking in SLB. For single-threaded use ucache has less overhead than icache.
Please be ware that it is unsafe to use ucache in a multi-threaded environment even for read-only operations.

3. rcache: It is an icache slightly tailored for integration with LMDB.

## Build
Just make it. IB/RDMA supports might be installed for compilation of the ib module.
SLB uses xxhash in its code. It will be automatically downloaded by a few rules in Makefile.local at the first build.

## Run
Run the binaries without arguments to see usage information.
