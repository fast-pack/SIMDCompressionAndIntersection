.SUFFIXES:
#
.SUFFIXES: .cpp .o .c .h
# replace the CXX variable with a path to a C++11 compatible compiler.
# to build an aligned version, add -DUSE_ALIGNED=1
ifeq ($(INTEL), 1)
# if you wish to use the Intel compiler, please do "make INTEL=1".
    CXX ?= /opt/intel/bin/icpc
    CC ?= /opt/intel/bin/icpc
ifeq ($(DEBUG),1)
    CXXFLAGS = -fpic -std=c++11 -O3 -Wall -ansi -xAVX -DDEBUG=1 -D_GLIBCXX_DEBUG   -ggdb
    CCFLAGS = -fpic -std=c99 -O3 -Wall  -ansi -xAVX -DDEBUG=1 -D_GLIBCXX_DEBUG   -ggdb
else
    CXXFLAGS = -fpic -std=c++11 -O2 -Wall  -ansi -xAVX -DNDEBUG=1  -ggdb
    CCFLAGS = -fpic -std=c99 -O2 -Wall   -ansi -xAVX -DNDEBUG=1  -ggdb
endif # debug
else #intel
    CXX ?= g++-4.7
# On x86 we target AVX; on ARM (AArch64) NEON is baseline, so no -m flag is
# needed (the SSE intrinsics are mapped to NEON via include/neon_sse.h).
    ARCH := $(shell uname -m)
ifneq (,$(filter x86_64 amd64 i386 i686,$(ARCH)))
    SIMDFLAGS ?= -mavx
else
    SIMDFLAGS ?=
endif
ifeq ($(DEBUG),1)
    CXXFLAGS = -fpic $(SIMDFLAGS) -std=c++11  -Weffc++ -pedantic -ggdb -DDEBUG=1 -D_GLIBCXX_DEBUG -Wall  -Wextra -Wextra -Wsign-compare  -Wwrite-strings -Wpointer-arith -Winit-self  -Wno-sign-conversion
    CCFLAGS = -fpic $(SIMDFLAGS) -std=c99  -pedantic -ggdb -DDEBUG=1 -D_GLIBCXX_DEBUG -Wall  -Wextra -Wsign-compare -Wwrite-strings -Wpointer-arith -Winit-self  -Wno-sign-conversion
else
    CXXFLAGS = -fpic $(SIMDFLAGS) -std=c++11  -Weffc++ -pedantic -O3 -DNDEBUG=1 -Wall  -Wextra -Wsign-compare  -Wwrite-strings -Wpointer-arith -Winit-self  -Wno-sign-conversion
    CCFLAGS = -fpic $(SIMDFLAGS) -std=c99 -pedantic -O3 -DNDEBUG=1 -Wall -Wextra -Wsign-compare -Wwrite-strings -Wpointer-arith -Winit-self -Wno-sign-conversion
endif #debug
endif #intel





HEADERS= $(shell ls include/*h)

all: unit  testcodecs  testintegration  advancedbenchmarking benchintersection benchsearch ramtocache libSIMDCompressionAndIntersection.a
	echo "please run unit tests by running the unit executable"


advancedbenchmarking: simplesynth compress uncompress budgetedtest entropy compflatstat

codecfactory.o: include/codecfactory.h src/codecfactory.cpp
	$(CXX) $(CXXFLAGS) -c src/codecfactory.cpp -Iinclude

bitpacking.o: include/bitpacking.h src/bitpacking.cpp
	$(CXX) $(CXXFLAGS) -c src/bitpacking.cpp -Iinclude

intersection.o: include/intersection.h src/intersection.cpp
	$(CXX) $(CXXFLAGS) -c src/intersection.cpp -Iinclude


benchintersection: intersection.o src/benchintersection.cpp include/synthetic.h include/timer.h
	$(CXX) $(CXXFLAGS) -o benchintersection src/benchintersection.cpp intersection.o -Iinclude

likwidintersection: intersection.o src/benchintersection.cpp include/synthetic.h include/timer.h
	$(CXX) $(CXXFLAGS) -DLIKWID_MARKERS -pthread -o likwidintersection src/benchintersection.cpp intersection.o -Iinclude -llikwid 

integratedbitpacking.o: include/integratedbitpacking.h src/integratedbitpacking.cpp 
	$(CXX) $(CXXFLAGS) -c src/integratedbitpacking.cpp -Iinclude

simdpackedsearch.o:  src/simdpackedsearch.c
	$(CC) $(CCFLAGS) -c src/simdpackedsearch.c -Iinclude

simdpackedselect.o:  src/simdpackedselect.c
	$(CC) $(CCFLAGS) -c src/simdpackedselect.c -Iinclude



streamvbyte.o:  src/streamvbyte.c
	$(CC) $(CCFLAGS) -c src/streamvbyte.c -Iinclude

varintdecode.o:  src/varintdecode.c
	$(CC) $(CCFLAGS) -c src/varintdecode.c -Iinclude

simdbitpacking.o: include/simdbitpacking.h src/simdbitpacking.cpp
	$(CXX) $(CXXFLAGS) -c src/simdbitpacking.cpp -Iinclude

usimdbitpacking.o: include/usimdbitpacking.h src/usimdbitpacking.cpp
	$(CXX) $(CXXFLAGS) -c src/usimdbitpacking.cpp -Iinclude

frameofreference.o: src/frameofreference.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c src/frameofreference.cpp -Iinclude

for.o: src/for.c $(HEADERS)
	$(CC) $(CCFLAGS) -c src/for.c -Iinclude


simdintegratedbitpacking.o: include/simdintegratedbitpacking.h include/delta.h src/simdintegratedbitpacking.cpp
	$(CXX) $(CXXFLAGS) -c src/simdintegratedbitpacking.cpp -Iinclude



UNAME := $(shell uname)


OBJECTS= codecfactory.o bitpacking.o integratedbitpacking.o simdbitpacking.o usimdbitpacking.o    simdintegratedbitpacking.o   intersection.o  varintdecode.o streamvbyte.o simdpackedsearch.o simdpackedselect.o frameofreference.o for.o

benchsearch: $(HEADERS) src/benchsearch.cpp  $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o benchsearch src/benchsearch.cpp  $(OBJECTS) -Iinclude


unit: $(HEADERS)   src/unit.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o unit src/unit.cpp  $(OBJECTS) -Iinclude

testcodecs: $(HEADERS) src/testcodecs.cpp   $(OBJECTS)
	$(CXX) $(CXXFLAGS) -Iinclude -o testcodecs src/testcodecs.cpp   $(OBJECTS)



example:  $(HEADERS) example.cpp  $(OBJECTS)
	$(CXX) $(CXXFLAGS)  -o example example.cpp  $(OBJECTS) -Iinclude



testintegration:  bitpacking.o simdbitpacking.o usimdbitpacking.o integratedbitpacking.o     simdintegratedbitpacking.o src/testintegration.cpp  $(HEADERS)
	$(CXX) $(CXXFLAGS) -Iinclude -o testintegration src/testintegration.cpp   bitpacking.o integratedbitpacking.o  simdbitpacking.o usimdbitpacking.o     simdintegratedbitpacking.o

libSIMDCompressionAndIntersection.a: $(OBJECTS)
	ar rvs $@ $^

libSIMDCompressionAndIntersection.so: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $^


clean:
	rm -f *.o unit benchsearch testintegration testcodecs   simplesynth  compress uncompress budgetedtest ramtocache  entropy example benchintersection libSIMDCompressionAndIntersection.so libSIMDCompressionAndIntersection.a compflatstat






BENCHHEADERS= $(shell ls advancedbenchmarking/include/*h)

simplesynth: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/simplesynth.cpp 
	$(CXX) $(CXXFLAGS) -o simplesynth advancedbenchmarking/src/simplesynth.cpp  -Iinclude -Iadvancedbenchmarking/include


compress: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/compress.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o compress advancedbenchmarking/src/compress.cpp $(OBJECTS) -Iinclude -Iadvancedbenchmarking/include

budgetedtest: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/budgetedtest.cpp $(OBJECTS) 
	$(CXX) $(CXXFLAGS) -o budgetedtest advancedbenchmarking/src/budgetedtest.cpp $(OBJECTS)  -Iinclude -Iadvancedbenchmarking/include

ramtocache: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/ramtocache.cpp $(OBJECTS) 
	$(CXX) $(CXXFLAGS) -o ramtocache advancedbenchmarking/src/ramtocache.cpp $(OBJECTS)  -Iinclude -Iadvancedbenchmarking/include


entropy: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/entropy.cpp $(OBJECTS) 
	$(CXX) $(CXXFLAGS) -o entropy advancedbenchmarking/src/entropy.cpp $(OBJECTS)  -Iinclude -Iadvancedbenchmarking/include


uncompress: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/uncompress.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o uncompress advancedbenchmarking/src/uncompress.cpp $(OBJECTS) -Iinclude -Iadvancedbenchmarking/include 

compflatstat: $(HEADERS) $(BENCHHEADERS) advancedbenchmarking/src/compflatstat.cpp $(OBJECTS) 
	$(CXX) $(CXXFLAGS) -o compflatstat advancedbenchmarking/src/compflatstat.cpp $(OBJECTS)  -Iinclude -Iadvancedbenchmarking/include

astyle:
	astyle --options=astyle.conf --recursive "*.cpp" "*.h"

.PHONY: all clean astyle
