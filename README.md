##Dependencies
Our code runs on Ubuntu 14.04.

1. sudo aptitude update && sudo aptitude -y upgrade && sudo aptitude -y install build-essential git make automake pkg-config libbsd-dev

##Compilation
1. make && make test - builds the library and subsequently builds the test binary

2. make clean - cleans only the dsm lib binaries

3. make superclean - cleans everything include nanomsg binaries

##USAGE
Code should compile and run on the vagrant virtual machine used for lab2. Copy the zip file to the VM, unzip it and execute the following commands - 

#Three node test
1. make && make test
2. ./bin/test -m -p 8000 > a & ./bin/test -p 8001 > b & ./bin/test -p 8002 > c &

#Two node test
1. make && make test
2. ./bin/test -m -p 8000 > a & ./bin/test -p 8001 > b &

After executing the test, please look at the output in the files a, b, c in combination with the test cases in test/src/main.c. 

File a: generated by master; function test/src/main.c:test_dsm_master() runs master
File b: generated by client 1; function test/src/main.c:test_dsm_client1() runs client1
File c: generated by client 2; function test/src/main.c:test_dsm_client2() runs client2

## DSM tests
# multiple writes
1. make
2. ./bin/test1 1 localhost 8000 & ./bin/test1 0 localhost 8001 & ./bin/test1 0 localhost 8002 &

# read-write ping-pong
1. make
2. ./bin/test2 1 localhost 8000 & ./bin/test2 0 localhost 8001 & ./bin/test2 0 localhost 8002 &

# Read replication and write invalidation
1. make
2. ./bin/test3 1 localhost 8000 & ./bin/test3 0 localhost 8001 & ./bin/test3 0 localhost 8002 &

# read/write fault profiling
1. make
2. ./bin/fault 1 localhost 8000 & ./bin/fault 0 localhost 8001 &

## Matrix multiplication
1. make
2. ./bin/matrix_gen 1000 1000 1000 1000        # generate 2 1000x1000 matrices and matrix_ans.txt
3. . run_matrix_mul_4.sh                       # run matrix multiplication with 4 clients
4. diff matrix_ans.txt matrix_pmul.txt         # check if multiplication is correct
5. diff matrix_ans.txt matrix_pmul.txt | wc -l # check diff in rows

For matrix multiplication with 8 clients
1. Set MAX_CLIENTS from test/src/matrix_mul.c to 8
2. make
3. . run_matrix_mul_8.sh                       # run matrix multiplication with 8 clients

## Notes
- NUM_LIMIT in matrix_gen controls the limit on size of numbers.
- As NUM_CHUNKS = 32, those many distinct DSMs can be created.
- As MAP_SIZE = (1024*1024), 1024*1024*4096 total bytes of matrix data can be handled by DSM.
- As an unsigned long (8 bytes) holds a matrix element, total no. of elements handled by DSM are 1024*1024*4096/8 i.e. 536870912.
- So any matrix cannot be bigger than that m_row * m_col <= 536870912
