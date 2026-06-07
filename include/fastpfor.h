/*
 * This is the non-SIMD version of FastPFOR.
 * It is not recommended per se, only provided for
 * comparison purposes.
 */

#ifndef SIMDCompressionAndIntersection_FASTPFOR_H_
#define SIMDCompressionAndIntersection_FASTPFOR_H_

#include "common.h"
#include "codecs.h"
#include "bitpackinghelpers.h"
#include "util.h"
#include "delta.h"

namespace SIMDCompressionLib {

class ScalarSortedBitPacker {
public:
  enum { DEFAULTSIZE = 128 };
  uint32_t buffer[32];

  ScalarSortedBitPacker() {
    for (uint32_t i = 0; i < 32; ++i) {
      data[i] = new uint32_t[DEFAULTSIZE];
      memset(data[i], 0, DEFAULTSIZE * sizeof(uint32_t));
      actualsizes[i] = DEFAULTSIZE;
    }
    clear();
  }

  void reset() {
    for (uint32_t i = 0; i < 32; ++i) {
      delete[] data[i];
      data[i] = new uint32_t[DEFAULTSIZE];
      memset(data[i], 0, DEFAULTSIZE * sizeof(uint32_t));
      actualsizes[i] = DEFAULTSIZE;
    }
    clear();
  }

  ~ScalarSortedBitPacker() { free(); }
  void free() {
    clear();
    for (uint32_t i = 0; i < 32; ++i)
      if (data[i] != NULL) {
        delete[] data[i];
        data[i] = NULL;
        actualsizes[i] = 0;
      }
  }
  void directAppend(uint32_t i, uint32_t val) { data[i][sizes[i]++] = val; }

  const uint32_t *get(int i) { return data[i]; }

  void ensureCapacity(int i, uint32_t datatoadd) {
    if (sizes[i] + datatoadd > actualsizes[i]) {
      actualsizes[i] = (sizes[i] + datatoadd + 127) / 128 * 128 * 2;
      uint32_t *tmp = new uint32_t[actualsizes[i]];
      for (uint32_t j = 0; j < sizes[i]; ++j)
        tmp[j] = data[i][j];
      delete[] data[i];
      data[i] = tmp;
    }
  }

  void clear() {
    for (uint32_t i = 0; i < 32; ++i)
      sizes[i] = 0; // memset "might" be faster.
  }

  uint32_t *write(uint32_t *out) {
    uint32_t bitmap = 0;
    for (uint32_t k = 1; k < 32; ++k) {
      if (sizes[k] != 0)
        bitmap |= (1U << k);
    }
    *(out++) = bitmap;

    for (uint32_t k = 1; k < 32; ++k) {
      if (sizes[k] != 0) {
        *out = sizes[k];
        out++;
        uint32_t j = 0;
        for (; j < sizes[k]; j += 32) {
          BitPackingHelpers::fastpackwithoutmask(&data[k][j], out, k + 1);
          out += k + 1;
        }
        out -= (j - sizes[k]) * (k + 1) / 32;
      }
    }
    return out;
  }

  const uint32_t *read(const uint32_t *in) {
    clear();
    const uint32_t bitmap = *(in++);

    for (uint32_t k = 1; k < 32; ++k) {
      if ((bitmap & (1U << k)) != 0) {
        sizes[k] = *in++;
        if (actualsizes[k] < sizes[k]) {
          delete[] data[k];
          actualsizes[k] = (sizes[k] + 31) / 32 * 32;
          data[k] = new uint32_t[actualsizes[k]];
        }
        uint32_t j = 0;
        for (; j + 31 < sizes[k]; j += 32) {
          BitPackingHelpers::fastunpack(in, &data[k][j], k + 1);
          in += k + 1;
        }
        uint32_t remaining = sizes[k] - j;
        memcpy(buffer, in, (remaining * (k + 1) + 31) / 32 * sizeof(uint32_t));
        uint32_t *bpointer = buffer;
        in += ((sizes[k] + 31) / 32 * 32 - j) / 32 * (k + 1);
        for (; j < sizes[k]; j += 32) {
          BitPackingHelpers::fastunpack(bpointer, &data[k][j], k + 1);
          bpointer += k + 1;
        }
        in -= (j - sizes[k]) * (k + 1) / 32;
      }
    }
    return in;
  }

private:
  uint32_t *data[32];
  uint32_t sizes[32];
  uint32_t actualsizes[32];

  // we don't want anyone to start copying this class
  ScalarSortedBitPacker(const ScalarSortedBitPacker &);
  ScalarSortedBitPacker &operator=(const ScalarSortedBitPacker &);
};

/**
 * FastPFor
 *
 * Reference and documentation:
 *
 * Daniel Lemire and Leonid Boytsov, Decoding billions of integers per second
 * through vectorization
 * http://arxiv.org/abs/1209.2137
 *
 * Designed by D. Lemire with ideas from Leonid Boytsov. This scheme is NOT
 * patented.
 *
 */
template <uint32_t BlockSizeInUnitsOfPackSize = 8,
          bool useDelta = true> // BlockSizeInUnitsOfPackSize should be 4 or 8
class FastPFor : public IntegerCODEC {
public:
  /**
   * ps (page size) should be a multiple of BlockSize, any "large"
   * value should do.
   */
  FastPFor(uint32_t ps = 65536)
      : PageSize(ps), bitsPageSize(gccbits(PageSize)), bpacker(),
        bytescontainer(PageSize + 3 * PageSize / BlockSize) {
    assert(ps / BlockSize * BlockSize == ps);
    assert(gccbits(BlockSizeInUnitsOfPackSize * PACKSIZE - 1) <= 8);
  }
  enum {
    PACKSIZE = 32,
    overheadofeachexcept = 8,
    overheadduetobits = 8,
    overheadduetonmbrexcept = 8,
    BlockSize = BlockSizeInUnitsOfPackSize * PACKSIZE
  };

  const uint32_t PageSize;
  const uint32_t bitsPageSize;
  ScalarSortedBitPacker bpacker;
  vector<uint8_t> bytescontainer;

  const uint32_t *decodeArray(const uint32_t *in, const size_t length,
                              uint32_t *out, size_t &nvalue) {
    const uint32_t *const initin(in);
    const size_t mynvalue = *in;
    ++in;
    if (mynvalue > nvalue)
      throw NotEnoughStorage(mynvalue);
    nvalue = mynvalue;
    const uint32_t *const finalout(out + nvalue);
    uint32_t prev = 0;
    while (out != finalout) {
      size_t thisnvalue(0);
      size_t thissize = static_cast<size_t>(
          finalout > PageSize + out ? PageSize : (finalout - out));

      __decodeArray(in, thisnvalue, out, thissize, prev);
      in += thisnvalue;
      out += thissize;
    }
    assert(initin + length >= in);
    (void)initin;
    (void)length;
    bpacker.reset(); // if you don't do this, the codec has a "memory".
    return in;
  }

  /**
   * If you save the output and recover it in memory, you are
   * responsible to ensure that the alignment is preserved.
   *
   * The input size (length) should be a multiple of
   * BlockSizeInUnitsOfPackSize * PACKSIZE. (This was done
   * to simplify slightly the implementation.)
   */
  void encodeArray(uint32_t *in, const size_t length, uint32_t *out,
                   size_t &nvalue) {
    checkifdivisibleby(length, BlockSize);
    const uint32_t *const initout(out);
    const uint32_t *const finalin(in + length);

    *out++ = static_cast<uint32_t>(length);
    const size_t oldnvalue = nvalue;
    nvalue = 1;
    uint32_t prev = 0;
    while (in != finalin) {
      size_t thissize = static_cast<size_t>(
          finalin > PageSize + in ? PageSize : (finalin - in));
      size_t thisnvalue(0);
      __encodeArray(in, thissize, out, thisnvalue, prev);
      nvalue += thisnvalue;
      out += thisnvalue;
      in += thissize;
    }
    assert(out == nvalue + initout);
    (void)initout;
    if (oldnvalue < nvalue)
			std::cerr
					<< "It is possible we have a buffer overrun. You reported having allocated "
					<< oldnvalue * sizeof(uint32_t)
					<< " bytes for the compressed data but we needed "
					<< nvalue * sizeof(uint32_t)
					<< " bytes. Please increase the available memory"
							" for compressed data or check the value of the last parameter provided "
							" to the encodeArray method." << std::endl;
    bpacker.reset(); // if you don't do this, the buffer has a memory
  }

  void getBestBFromData(const uint32_t *in, uint8_t &bestb,
                        uint8_t &bestcexcept, uint8_t &maxb) {
    uint32_t freqs[33];
    for (uint32_t k = 0; k <= 32; ++k)
      freqs[k] = 0;
    for (uint32_t k = 0; k < BlockSize; ++k) {
      freqs[asmbits(in[k])]++;
    }
    bestb = 32;
    while (freqs[bestb] == 0)
      bestb--;
    maxb = bestb;
    uint32_t bestcost = bestb * BlockSize;
    uint32_t cexcept = 0;
    bestcexcept = static_cast<uint8_t>(cexcept);
    for (uint32_t b = bestb - 1; b < 32; --b) {
      cexcept += freqs[b + 1];
      uint32_t thiscost = cexcept * overheadofeachexcept +
                          cexcept * (maxb - b) + b * BlockSize +
                          8; // the  extra 8 is the cost of storing maxbits
      if (bestb - b == 1)
        thiscost -= cexcept;
      if (thiscost < bestcost) {
        bestcost = thiscost;
        bestb = static_cast<uint8_t>(b);
        bestcexcept = static_cast<uint8_t>(cexcept);
      }
    }
  }

  void __encodeArray(uint32_t *in, const size_t length, uint32_t *out,
                     size_t &nvalue, uint32_t &prev) {
    uint32_t *const initout = out; // keep track of this
    checkifdivisibleby(length, BlockSize);
    uint32_t *const headerout = out++; // keep track of this
    bpacker.clear();
    uint8_t *bc = bytescontainer.data();
    for (const uint32_t *const final = in + length; (in + BlockSize <= final);
         in += BlockSize) {
      uint8_t bestb, bestcexcept, maxb;
      if (useDelta) {
        uint32_t nextprev = in[BlockSize - 1];
        delta(prev, in, BlockSize);
        prev = nextprev;
      }
      getBestBFromData(in, bestb, bestcexcept, maxb);
      *bc++ = bestb;
      *bc++ = bestcexcept;
      if (bestcexcept > 0) {
        *bc++ = maxb;
        bpacker.ensureCapacity(maxb - bestb - 1, bestcexcept);
        const uint32_t maxval = 1U << bestb;
        for (uint32_t k = 0; k < BlockSize; ++k) {
          if (in[k] >= maxval) {
            bpacker.directAppend(maxb - bestb - 1, in[k] >> bestb);
            *bc++ = static_cast<uint8_t>(k);
          }
        }
      }
      for (size_t k = 0; k < BlockSizeInUnitsOfPackSize; ++k) {
        BitPackingHelpers::fastpack(in + k * 32, out + k * bestb, bestb);
      }
      out += BlockSizeInUnitsOfPackSize * bestb;
    }
    headerout[0] = static_cast<uint32_t>(out - headerout);
    const uint32_t bytescontainersize =
        static_cast<uint32_t>(bc - bytescontainer.data());
    *(out++) = bytescontainersize;
    memcpy(out, bytescontainer.data(), bytescontainersize);
    out += (bytescontainersize + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    const uint32_t *const lastout = bpacker.write(out);
    nvalue = lastout - initout;
  }

  void __decodeArray(const uint32_t *in, size_t &length, uint32_t *out,
                     const size_t nvalue, uint32_t &prev) {
    const uint32_t *const initin = in;
    const uint32_t *const headerin = in++;
    const uint32_t wheremeta = headerin[0];
    const uint32_t *inexcept = headerin + wheremeta;
    const uint32_t bytesize = *inexcept++;
    const uint8_t *bytep = reinterpret_cast<const uint8_t *>(inexcept);

    inexcept += (bytesize + sizeof(uint32_t) - 1) / sizeof(uint32_t);
    inexcept = bpacker.read(inexcept);
    length = inexcept - initin;
    const uint32_t *unpackpointers[32 + 1];
    for (uint32_t k = 1; k <= 32; ++k) {
      unpackpointers[k] = bpacker.get(k - 1);
    }
    for (uint32_t run = 0; run < nvalue / BlockSize; ++run, out += BlockSize) {
      const uint8_t b = *bytep++;
      const uint8_t cexcept = *bytep++;
      for (size_t k = 0; k < BlockSizeInUnitsOfPackSize; ++k) {
        BitPackingHelpers::fastunpack(in + k * b, out + k * 32, b);
      }
      in += BlockSizeInUnitsOfPackSize * b;
      if (cexcept > 0) {
        const uint8_t maxbits = *bytep++;
        if (maxbits - b == 1) {
          for (uint32_t k = 0; k < cexcept; ++k) {
            const uint8_t pos = *(bytep++);
            out[pos] |= static_cast<uint32_t>(1) << b;
          }
        } else {
          const uint32_t *vals = unpackpointers[maxbits - b];
          unpackpointers[maxbits - b] += cexcept;
          for (uint32_t k = 0; k < cexcept; ++k) {
            const uint8_t pos = *(bytep++);
            out[pos] |= vals[k] << b;
          }
        }
      }
      if (useDelta) {
        inverseDelta(prev, out, BlockSize);
        prev = out[BlockSize - 1];
      }
    }

    assert(in == headerin + wheremeta);
  }

  string name() const { return string("FastPFor") + (useDelta ? "Delta" : ""); }
};

} // namespace SIMDCompressionLib

#endif /* SIMDCompressionAndIntersection_FASTPFOR_H_ */
