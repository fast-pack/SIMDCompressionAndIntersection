Advanced benchmarking
=====================


The goal of this subdirectory is to provide some utilities that are 
useful for "advanced benchmarking".


* simplesynth is a utility that generates a file containing randomly generated arrays.
* compress is a utility that takes a file containing uncompressed (regular) arrays and generates compressed arrays. The expected input format is (length L of array as an unsigned 32-bit integer)(L consecutive 32-bit unsigned integers in increasing order) (repeat as many times as desired) -- integers must be stored using the default endianess of the machine (typically little endian)
* uncompress is a utility that takes a file containing compressed arrays and generates uncompressed arrays.
* budgetedtest is a program to benchmark decompression speed together with fast intersections
* entropy can be used to compute the entropy of the deltas given a set of (uncompressed) posting lists

Sample usage
-------------

go to root directory in the project
$ make compress uncompress simplesynth
$ ./simplesynth -m 100 -N 10000 -R 20 test.bin
$ ./compress -s varint test.bin ctest.bin
$ ./uncompress ctest.bin recovered.bin


Working with intersections
--------------------------

First, you must get the clueweb09 dataset (or a similar data set). 


You need a posting list file in a flat format: a sequence of arrays stored as a 32-bit unsigned integer
indicating length followed by a corresponding sequence of 32-bit unsigned integers.

You can quickly generate a test file from a synthetic model:

./simplesynth  -N 100000 -m 100 test.bin

You can construct such a data file from the clueweb09 dataset made available by Leonid
Boytsov (http://boytsov.info/datasets/clueweb09gap/). For Nathan and Leo, this 
is available on the test machine at /home/data/clueweb/Uncompressed/flat.bin (October 2013).

You also need a query log file. It needs to be a text file where each row contains
a sequence of integers (separated by white space). Each row corresponds to a query: each
integer is a the index of the corresponding posting list. For example, the row "0 1 2" corresponds
to the first 3 posting lists. Leonid Boytsov prepared such a query log from the infamous
AOL web query log file, matching the clueweb09 data set. (By convention, the number "1000000"
corresponds to an "unknown" posting list because there are 1000000 different terms in the
clueweb09 data set that Leo indexed: a more general convention is that an out-of-range
index can be ignored.) For Nathan and Leo, these files
are available under /home/data/AOL/ in the test machine (October 2013).

If you want, you can just make one up by creating a text file and entering, on each line, a series of distinct integers (say between 0 and 100) separated by spaces.
 
You can run a test as follows: 

./budgetedtest /home/data/clueweb/Uncompressed/flat.bin /home/data/AOL/user-ct-test-collection-01.id 
 -i simd -b 4  -s varint 
-s specify the compression scheme (if none: default  on uncompressed)
-b is is the memory budget in gigabytes (e.g., half of your RAM). 
-i allows you to specify an intersection routine  (default on a fast SIMD-based intersection routine).
-o is to include one-word queries (they don't participate in intersections, only in decoding)
-q allows you to specify that you just want the final report (warning:  the program can take a long time to complete)
-l allows you to limit the number of queries (e.g., -l 1000)
-p allows you to partition the postings during compression and decompression (param: number of partitions)
-k   to test Skipping (must provide a gap size param between 1 and 31)
-B   to activate bitmap mode (recommended values are in the range 8 to  32), when set to 0 in conjunction with compression, it means 'automatic'
-d   to dump complete statistics


Similarly, if you want the corresponding entropy, you can run

./entropy  /home/data/clueweb/Uncompressed/flat.bin /home/data/AOL/user-ct-test-collection-01.id

It has similar flags as budgetedtest.
