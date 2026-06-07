/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 * (c) Daniel Lemire, http://lemire.me/en/
 */
#ifndef SIMDCompressionAndIntersection_COMMON_H_
#define SIMDCompressionAndIntersection_COMMON_H_

#include <errno.h>
#include <fcntl.h>
#include "platform.h" /* pulls in the SIMD intrinsics (SSE on x86, NEON on ARM) */
#include <iso646.h>
#include <limits.h>
#ifndef _MSC_VER
#include <sys/resource.h>
#endif
#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _MSC_VER
#include <sys/time.h>
#include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <string>
#include <stdexcept>
#include <sstream>
#include <memory>
#include <unordered_set>
#include <vector>

#include "platform.h"

#ifdef USE_ALIGNED
#define MM_LOAD_SI_128 _mm_load_si128
#define MM_STORE_SI_128 _mm_store_si128
#else
#define MM_LOAD_SI_128 _mm_loadu_si128
#define MM_STORE_SI_128 _mm_storeu_si128
#endif

namespace SIMDCompressionLib {} // namespace SIMDCompressionLib

#endif /* SIMDCompressionAndIntersection_COMMON_H_ */
