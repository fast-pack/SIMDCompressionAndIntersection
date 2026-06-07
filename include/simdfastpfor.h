/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 * (c) Daniel Lemire, http://lemire.me/en/
 */
#ifndef SIMDCompressionAndIntersection_SIMDFASTPFOR_H_
#define SIMDCompressionAndIntersection_SIMDFASTPFOR_H_

#include "common.h"
#include "codecs.h"
#include "sortedbitpacking.h"
#include "simdbitpacking.h"
#include "util.h"
#include "delta.h"

namespace SIMDCompressionLib {

/**
 * SIMDFastPFor
 *
 * Reference and documentation:
 *
 * Daniel Lemire and Leonid Boytsov, Decoding billions of integers per second
 * through vectorization
 * http://arxiv.org/abs/1209.2137
 *
 * Note that this implementation is slightly improved compared to the version
 * presented
 * in the paper.
 *
 * Designed by D. Lemire with ideas from Leonid Boytsov. This scheme is NOT
 * patented.
 *
 */
template <uint32_t BlockSizeInUnitsOfPackSize = 8, class DeltaHelper = NoDelta,
          class SortedBitPacker = BasicSortedBitPacker,
          bool arraydispatch = true>
class SIMDFastPFor : public IntegerCODEC {
public:
  /**
   * ps (page size) should be a multiple of BlockSize, any "large"
   * value should do.
   */
  SIMDFastPFor(uint32_t ps = 65536)
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
  SortedBitPacker bpacker;
  vector<uint8_t> bytescontainer;

  const uint32_t *decodeArray(const uint32_t *in, const size_t length,
                              uint32_t *out, size_t &nvalue) {
#ifdef USE_ALIGNED
    if (needPaddingTo128Bits(out) or needPaddingTo128Bits(in))
      throw std::runtime_error(
          "alignment issue: pointers should be aligned on 128-bit boundaries");
#endif
    const uint32_t *const initin(in);
    const size_t mynvalue = *in;
    ++in;
    if (mynvalue > nvalue)
      throw NotEnoughStorage(mynvalue);
    nvalue = mynvalue;
    const uint32_t *const finalout(out + nvalue);
    __m128i prev = _mm_set1_epi32(0);
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
#ifdef USE_ALIGNED
    if (needPaddingTo128Bits(out) or needPaddingTo128Bits(in))
      throw std::runtime_error(
          "alignment issue: pointers should be aligned on 128-bit boundaries");
#endif
    checkifdivisibleby(length, BlockSize);
    const uint32_t *const initout(out);
    const uint32_t *const finalin(in + length);

    *out++ = static_cast<uint32_t>(length);
    const size_t oldnvalue = nvalue;
    nvalue = 1;
    __m128i prev = _mm_set1_epi32(0);
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

  void
  __encodeArray(uint32_t *in, const size_t length, uint32_t *out,
                size_t &nvalue,
                __m128i &prev) {   //         =  _mm_set1_epi32 (0);// for delta
    uint32_t *const initout = out; // keep track of this
    checkifdivisibleby(length, BlockSize);
    uint32_t *const headerout = out++; // keep track of this
    bpacker.clear();
    uint8_t *bc = bytescontainer.data();
#ifdef USE_ALIGNED
    out = padTo128bits(out);
    if (needPaddingTo128Bits(in))
      throw std::runtime_error("alignment bug");
#endif
    for (const uint32_t *const final = in + length; (in + BlockSize <= final);
         in += BlockSize) {
      uint8_t bestb, bestcexcept, maxb;

      const __m128i nextprev =
          MM_LOAD_SI_128(reinterpret_cast<const __m128i *>(in + BlockSize - 4));
      SIMDDeltaProcessor<DeltaHelper, BlockSize>::runDelta(prev, in);
      prev = nextprev;

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
      for (int k = 0; k < BlockSize; k += 128) {
        simdpack(in + k, reinterpret_cast<__m128i *>(out), bestb);
        out += 4 * bestb;
      }
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
                     const size_t nvalue, __m128i &prev) {
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
#ifdef USE_ALIGNED
    in = padTo128bits(in);
    assert(!needPaddingTo128Bits(out));
#endif
    for (uint32_t run = 0; run < nvalue / BlockSize; ++run, out += BlockSize) {
      const uint8_t b = *bytep++;
      const uint8_t cexcept = *bytep++;
      for (int k = 0; k < BlockSize; k += 128) {
        if (arraydispatch)
          simdunpack(reinterpret_cast<const __m128i *>(in), out + k, b);
        else
          ArrayDispatch::SIMDunpack(reinterpret_cast<const __m128i *>(in),
                                    out + k, b);
        in += 4 * b;
      }
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
      prev =
          SIMDDeltaProcessor<DeltaHelper, BlockSize>::runPrefixSum(prev, out);
    }

    assert(in == headerin + wheremeta);
  }

  string name() const { return string("SIMDFastPFor") + DeltaHelper::name(); }
};

} // namespace SIMDCompressionLib

#endif /* SIMDCompressionAndIntersection_SIMDFASTPFOR_H_ */
