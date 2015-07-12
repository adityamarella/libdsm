##Random Notes
* to remove the lock around entire locatepage_internal function, first there should be a retry incase getpage fails. Why would getpage fail? If multiple nodes call locate page at the same time, some of the nodes could receive old/stale owner information. This gets me back to the very important detail of what to do when getpage fails? Current plan is to bail out after some retries. But what happens on the node which bails out?
* Timeout because of a possible deadlock? Here is the scenario
  - Node 1 -> lock(l); getpage(p1).. blocks until Node2 responds; unlock(l) {p1 is on Node 2}
  - Node 2 -> lock(l); getpage(p2).. blocks until Node1 responds; unlock(l) {p2 is on Node 1}
  - Low recv timeout was hiding this issue all the while
  - Possible fix? Add locatepage request; never block during getpage; 
    * this changes requires every node to be connected to every other node
    * otherwise connection creation overhead will make this horribly slow
    * call locatepage before calling getpage; locatepage will always be received by the master node
    * when to update the pagetable in this case? Only soln - during locatepage
    * so the requirement is locatepage should always be followed by getpage? Is this a good design?
    * When/how to call invalidate page? Right now master is sending invalidatepage to all the nodes which are accessing the page. 
* TODO: what to do when getpage fails? 
* TODO: Cannot make synchronous networking calls from signal handler!
  - make async call and wait on conditional lock on the page if that is allowed
* Test code is extremely sensitive to compiler optimizations O2 works, O0 doesn't work; O2 doesn't with bigger matrices.

##Old
##Architecture
One master and several nodes. Master is also one of the nodes in the system and acts as a central manager controlling access to the shared memory. Shared memory is allocated and exposed to the user in chunks. The internal system divides these chunks into small 4k byte pages and atomically transfers these pages across nodes on demand. Each page has a owner and only the owner has write access to the page. Rest of the nodes can read the page if it is not marked dirty. Read-replication, write-invalidation are used to ensure consistency.

##Implementation
The SIGSEGV fault handler and DSM daemon form the core of DSM. On page fault, the fault handler sends a request to the master asking for the page while the DSM Daemon thread listens for incoming requests from other nodes. Following sections give more details on the implementation - 

###SIGSEGV Handler
When a new chunk is allocated, libdsm restricts access to this chunk using mprotect flags PROT_NONE, PROT_READ, PROT_WRITE. If protection bit is set to PROT_NONE then a read or a write from process can result in SIGSEGV signal. If protection bit is set to PROT_READ then a write operation results in SIGSEGV signal. 

The first node to call alloc becomes the owner of the chunk. The owner of the chunk can read or write to any page in the chunk, while others have to contact the master before reading or writing the page for the first time. When other nodes access the page for the first time sends a request to DSM master node to fetch the page content. After locating appropriate owner of this page, DSM master node fetches content of remote page and sends it back. 

If the SIGSEGV signal is generated when protection bit is set to PROT_NONE then PROT_READ is set by the handler to mark this as a readable page and control is returned back to process. If this was a read operation then process will continue its normal operation. If this was a write operation, then DSM SIGSEGV handler changes protection bits from PROT_NONE to PROT_READ first and then PROT_READ to PROT_WRITE. This is done to differentiate read-only accesses from write accesses. An effect of this is that it results in two page faults i.e. two getpage requests for a single write access.

###Data Consistency
DSM uses read replication and write invalidation to maintain data consistency. When a getpage request is sent by SIGSEGV handler, it tags the request with READ or WRITE flag to indicate type of operation. Using this flag, the DSM master allows the owner of a page relinquish or retain ownership if the flag is READ only. Only content of requested page are given to SIGSEGV handler. This allows replication of read-only pages within DSM.

On a WRITE operation, the DSM master first fetches content of page from the owner node and then sends an invalidate request (set protection bit to PROT_NONE) to all nodes which are using that page. Finally, the requester node is made the new owner of the page.

This exclusive write access is taken back on first READ or WRITE request for that page from any other node. In this case, the page is given to the requestor node and the page is invalidated from all other nodes.


##Internode communication

###Based on nanomsg
* DSM daemon runs on each node and handles incoming requests
* Each non-master node opens a connection to the master
* Master opens connections to all other nodes
* REQ-REP pattern is used for communication

###Requests & Replies
* ALLOCCHUNK: allocates shared memory, updates page table on the master and returns whether the requestor node is the owner of the allocated memory. The first node to call allocchunk becomes the owner of the chunk. The alloc request from the rest of the nodes will increment the reference counter for that chunk on the master. Reference counting allows some of the nodes to still operate on the shared memory when other nodes have released it.
* FREECHUNK: if reference counter becomes zero the shared memory is deallocated, otherwise the reference counter is decremented. Also, the corresponding page table entries and owner mappings are updated 
* GETPAGE:  if the page is owned by node it is returned directly, otherwise master locates the page owner, sends a getpage request to the page owner, and sends it back to the requestor.
* INVALIDATEPAGE: when a write happens on page; master sends invalidatepage requests to all the nodes using that page.


##Dependencies
Our code runs on Ubuntu 14.04.

1. sudo aptitude update && sudo aptitude -y upgrade && sudo aptitude -y install build-essential git make automake pkg-config libbsd-dev

##Compilation
1. make && make test - builds the library and subsequently builds the test binary

2. make clean - cleans only the dsm lib binaries

3. make superclean - cleans everything include nanomsg binaries

##USAGE

###Three node test
1. make && make test
2. ./bin/test -m -p 8000 > a & ./bin/test -p 8001 > b & ./bin/test -p 8002 > c &

###Two node test
1. make && make test
2. ./bin/test -m -p 8000 > a & ./bin/test -p 8001 > b &

After executing the test, please look at the output in the files a, b, c in combination with the test cases in test/src/main.c. 

File a: generated by master; function test/src/main.c:test_dsm_master() runs master
File b: generated by client 1; function test/src/main.c:test_dsm_client1() runs client1
File c: generated by client 2; function test/src/main.c:test_dsm_client2() runs client2

## DSM tests
### multiple writes
1. make
2. ./bin/test1 1 localhost 8000 & ./bin/test1 0 localhost 8001 & ./bin/test1 0 localhost 8002 &

### read-write ping-pong
1. make
2. ./bin/test2 1 localhost 8000 & ./bin/test2 0 localhost 8001 & ./bin/test2 0 localhost 8002 &

### Read replication and write invalidation
1. make
2. ./bin/test3 1 localhost 8000 & ./bin/test3 0 localhost 8001 & ./bin/test3 0 localhost 8002 &

### read/write fault profiling
1. make
2. ./bin/fault 1 localhost 8000 & ./bin/fault 0 localhost 8001 &

## Matrix multiplication
1. make
2. ./bin/matrix_gen 1000 1000 1000 1000        # generate 2 1000x1000 matrices and matrix_ans.txt
3. . run_matrix_mul_4.sh                       # run matrix multiplication with 4 clients
4. diff matrix_ans.txt matrix_pmul.txt         # check if multiplication is correct
5. diff matrix_ans.txt matrix_pmul.txt | wc -l # check diff in rows
