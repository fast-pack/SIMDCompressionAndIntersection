/*
 * varintgb.h
 *
 *  Created on: Jul 25, 2013
 *      Author: lemire
 */

#ifndef SIMDCompressionAndIntersection_VARINTGB_H_
#define SIMDCompressionAndIntersection_VARINTGB_H_

#include "common.h"
#include "codecs.h"
#include "variablebyte.h"

namespace SIMDCompressionLib {

using namespace std;

static uint8_t group_size[] = {
    4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9,  10, 5,  6,  7,
    8,  6,  7,  8,  9,  7,  8,  9,  10, 8,  9,  10, 11, 6,  7,  8,  9,  7,  8,
    9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 7,  8,  9,  10, 8,  9,  10, 11, 9,
    10, 11, 12, 10, 11, 12, 13, 5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9,  10,
    8,  9,  10, 11, 6,  7,  8,  9,  7,  8,  9,  10, 8,  9,  10, 11, 9,  10, 11,
    12, 7,  8,  9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 8,  9,
    10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 6,  7,  8,  9,  7,
    8,  9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 7,  8,  9,  10, 8,  9,  10, 11,
    9,  10, 11, 12, 10, 11, 12, 13, 8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12,
    13, 11, 12, 13, 14, 9,  10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13,
    14, 15, 7,  8,  9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 8,
    9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 9,  10, 11, 12,
    10, 11, 12, 13, 11, 12, 13, 14, 12, 13, 14, 15, 10, 11, 12, 13, 11, 12, 13,
    14, 12, 13, 14, 15, 13, 14, 15, 16};

/**
 * Group VarInt.
 *
 * Implemented and designed by D. Lemire based on a talk by Jeff Dean (Google),
 * optimized by N. Kurz.
 */

template <bool delta = false> class VarIntGB : public IntegerCODEC {
public:
  void encodeArray(uint32_t *in, const size_t length, uint32_t *out,
                   size_t &nvalue) {
    uint8_t *bout = reinterpret_cast<uint8_t *>(out);
    const uint8_t *const initbout = reinterpret_cast<uint8_t *>(out);
    *out = static_cast<uint32_t>(length);
    bout += 4;
    bout = headlessEncode(in, length, 0, bout);

    while (needPaddingTo32Bits(bout)) {
      *bout++ = 0;
    }
    const size_t storageinbytes = bout - initbout;
    assert((storageinbytes % 4) == 0);
    nvalue = storageinbytes / 4;
  }

  void encodeToByteArray(uint32_t *in, const size_t length, uint8_t *bout,
                         size_t &nvalue) {
    const uint8_t *const initbout = bout;
    *(uint32_t *)bout = static_cast<uint32_t>(length);
    bout += 4;
    bout = headlessEncode(in, length, 0, bout);
    nvalue = bout - initbout;
  }

  const uint32_t *decodeArray(const uint32_t *in, const size_t length,
                              uint32_t *out, size_t &nvalue) {
    if (length == 0) {
      nvalue = 0;
      return in;
    }
    const uint8_t *inbyte = reinterpret_cast<const uint8_t *>(in);
    nvalue = *in;
    inbyte += 4;
    size_t decoded =
        headlessDecode(inbyte, (length - 1) * sizeof(uint32_t), 0, out, nvalue);
    assert(decoded == nvalue);
    (void)decoded;
    return in + length;
  }

  const uint8_t *decodeFromByteArray(const uint8_t *inbyte, const size_t length,
                                     uint32_t *out, size_t &nvalue) {
    if (length == 0) {
      nvalue = 0;
      return inbyte;
    }
    nvalue = *(uint32_t *)inbyte;
    inbyte += 4;
    size_t decoded = headlessDecode(inbyte, length - 4, 0, out, nvalue);
    assert(decoded == nvalue);
    (void)decoded;
    return inbyte + length;
  }

  // appends a key
  // return the new size in bytes
  size_t appendToByteArray(uint8_t *in, const size_t length, uint32_t previous,
                           uint32_t value) {
    uint32_t num_ints = *(uint32_t *)in;
    uint8_t *bout = reinterpret_cast<uint8_t *>(in + 4);
    uint8_t *bend = in + (length == 0 ? 4 : length);

    if (delta)
      value -= previous;

    uint8_t *keyp;
    int shift;

    // fast-forward to the last block
    if (num_ints % 4 != 0) {
      uint32_t size = 0;
      do {
        bout += size;
        size = 1 + group_size[*bout];
      } while (bout + size < bend);
      keyp = bout;
      bout = bend;
      shift = (num_ints % 4) * 2;
    } else {
      keyp = bend;
      bout = keyp + 1;
      *keyp = 0;
      shift = 0;
    }

    if (value < (1U << 8)) {
      *bout++ = static_cast<uint8_t>(value);
    } else if (value < (1U << 16)) {
      *bout++ = static_cast<uint8_t>(value);
      *bout++ = static_cast<uint8_t>(value >> 8);
      *keyp |= (1 << shift);
    } else if (value < (1U << 24)) {
      *bout++ = static_cast<uint8_t>(value);
      *bout++ = static_cast<uint8_t>(value >> 8);
      *bout++ = static_cast<uint8_t>(value >> 16);
      *keyp |= (2 << shift);
    } else {
      // the compiler will do the right thing
      *reinterpret_cast<uint32_t *>(bout) = value;
      bout += 4;
      *keyp |= (3 << shift);
    }

    *(uint32_t *)in = num_ints + 1;
    return bout - in;
  }

  // insert the key in sorted order. We assume that there is enough room and
  // that delta encoding was used.
  size_t insert(uint32_t *in, const size_t length, uint32_t key) {
    size_t bytesize = length * 4;
    uint8_t *bytein = (uint8_t *)in;
    uint8_t *byteininit = bytein;
    size_t bl = insertInByteArray(bytein, bytesize, key);
    bytein += bl;

    while (needPaddingTo32Bits(bytein)) {
      *bytein++ = 0;
    }
    size_t storageinbytes = bytein - byteininit;
    assert((storageinbytes % 4) == 0);
    return storageinbytes / 4;
  }

  // insert the key in sorted order. We assume that there is enough room and
  // that delta encoding was used.
  // the new size is returned (in bytes)
  size_t insertInByteArray(uint8_t *inbyte, size_t length, uint32_t key) {
    if (length == 0) {
      *((uint32_t *)inbyte) = 0;
      length = 4;
    }
    uint8_t *finalinbyte = inbyte + length;
    const uint8_t *const initinbyte = inbyte;
    uint32_t nvalue = *((uint32_t *)inbyte);
    *((uint32_t *)inbyte) = nvalue + 1; // incrementing
    inbyte += 4;                        // skip nvalue
    assert(delta);
    uint32_t initial = 0;
    size_t i = 0;
    while (i + 3 < nvalue) {
      uint32_t copyinitial = initial;
      const uint8_t *const newinbyte =
          scanGroupVarIntDelta(inbyte, &copyinitial);
      if (copyinitial >= key) {
        goto finish;
      }
      inbyte = (uint8_t *)newinbyte;
      initial = copyinitial;
      i += 4;
    }
  finish:
    assert(finalinbyte >= inbyte);
    assert(i <= nvalue);
    static const int REASONABLEBUFFER = 256;
    if (nvalue - i + 1 > REASONABLEBUFFER) {
      if (nvalue == i) { // straight append
        const uint8_t *const newfinalinbyte =
            headlessEncode(&key, 1, initial, inbyte);
        return newfinalinbyte - initinbyte;
      }
      if (nvalue - i <= 4) {
        // easy case
        uint32_t tmpbuffer[5];
        tmpbuffer[0] = key;
        size_t decoded = headlessDecode(inbyte, finalinbyte - inbyte, initial,
                                        tmpbuffer + 1, nvalue - i);
        assert(decoded == nvalue - i);
        (void)decoded;
        sortinfirstvalue(tmpbuffer, nvalue - i);
        const uint8_t *const newfinalinbyte =
            headlessEncode(tmpbuffer, nvalue - i + 1, initial, inbyte);
        return newfinalinbyte - initinbyte;
      }
      // harder case
      // this part is a bit complicated since we need to merge in the key
      uint32_t readinitial = initial;
      uint32_t tmpbuffer[5];
      tmpbuffer[0] = key;
      const uint8_t *readinginbyte =
          decodeGroupVarIntDelta(inbyte, &readinitial, tmpbuffer + 1);
      assert(tmpbuffer[4] >= key);
      assert(readinginbyte > inbyte);

      sortinfirstvalue(tmpbuffer, nvalue - i);
      i += 4;

      // initialize blocks

      Block b1, b2;

      Block *block1 = &b1;
      Block *block2 = &b2;
      Block *blocktmp;

      // load block1

      uint8_t *fb = encodeGroupVarIntDelta(block1->data, initial, tmpbuffer);

      block1->length = static_cast<uint32_t>(fb - block1->data);
      uint32_t nextval = tmpbuffer[4] - tmpbuffer[3];
      uint32_t newsel = getByteLength(nextval) - 1;
      // everything after that is just going to be shifting
      while (nvalue - i >= 4) {

        // load block 2
        assert(readinginbyte >= inbyte);
        readinginbyte = loadblock(block2, readinginbyte);
        i += 4;
        // shift in block 1
        shiftin(block2, &nextval, &newsel);
        // write block1
        memcpy(inbyte, block1->data, block1->length);
        inbyte += block1->length;
        // block1 = block2
        blocktmp = block1;
        block1 = block2;
        block2 = blocktmp;
      }
      if (nvalue != i) {
        readinginbyte = loadblockcarefully(block2, readinginbyte, nvalue - i);
        finalshiftin(block2, nextval, newsel,
                     nvalue - i + 1); // nextval is useless here
        memcpy(inbyte, block1->data, block1->length);
        inbyte += block1->length;
        memcpy(inbyte, block2->data, block2->length);
        inbyte += block2->length;
        return inbyte - initinbyte;
      } else {
        memcpy(inbyte, block1->data, block1->length);
        inbyte += block1->length;
        inbyte[0] = (uint8_t)newsel;
        inbyte++;
        memcpy(inbyte, &nextval, newsel + 1);
        inbyte += newsel + 1;
        return inbyte - initinbyte;
      }
      // we are using brute force here, by decoding everything to a buffer and
      // then reencoding.
    } else {
      uint32_t tmpbuffer[REASONABLEBUFFER];
      assert(tmpbuffer);
      tmpbuffer[0] = key;
      if (nvalue != i) {
        size_t decoded = headlessDecode(inbyte, finalinbyte - inbyte, initial,
                                        tmpbuffer + 1, nvalue - i);
        assert(decoded == nvalue - i);
        (void)decoded;
        sortinfirstvalue(tmpbuffer, nvalue - i);
      }
      const uint8_t *const newfinalinbyte =
          headlessEncode(tmpbuffer, nvalue - i + 1, initial, inbyte);
      return newfinalinbyte - initinbyte;
    }
  }

  // Performs a lower bound find in the encoded array.
  // Returns the index
  // assumes delta coding was used
  size_t findLowerBound(const uint32_t *in, const size_t length, uint32_t key,
                        uint32_t *presult) {
    const uint8_t *inbyte = reinterpret_cast<const uint8_t *>(in);
    uint32_t out[4] = {0};
    assert(delta);
    size_t i = 0;
    uint32_t initial = 0;
    uint32_t nvalue = *in;

    inbyte += 4; // skip nvalue

    const uint8_t *const endbyte =
        reinterpret_cast<const uint8_t *>(in + length);
    while (i + 3 < nvalue) {
      uint32_t gap1, gap2, gap3, gap4;
      uint32_t gap12, gap34;

      const uint32_t sel = *inbyte++;
      if (sel == 0) {
        gap1 = static_cast<uint32_t>(inbyte[0]);
        gap2 = static_cast<uint32_t>(inbyte[1]);
        gap12 = gap1 + gap2;
        gap3 = static_cast<uint32_t>(inbyte[2]);
        gap4 = static_cast<uint32_t>(inbyte[3]);
        gap34 = gap3 + gap4;
        inbyte += 4;
      } else {
        const uint32_t sel1 = (sel & 3);
        gap1 = *(reinterpret_cast<const uint32_t *>(inbyte)) & mask[sel1];
        inbyte += sel1 + 1;
        const uint32_t sel2 = ((sel >> 2) & 3);
        gap2 = *(reinterpret_cast<const uint32_t *>(inbyte)) & mask[sel2];
        gap12 = gap1 + gap2;
        inbyte += sel2 + 1;
        const uint32_t sel3 = ((sel >> 4) & 3);
        gap3 = *(reinterpret_cast<const uint32_t *>(inbyte)) & mask[sel3];
        inbyte += sel3 + 1;
        const uint32_t sel4 = (sel >> 6);
        gap4 = *(reinterpret_cast<const uint32_t *>(inbyte)) & mask[sel4];
        gap34 = gap3 + gap4;
        inbyte += sel4 + 1;
      }
      initial += gap12 + gap34;
      if (key <= initial) {
        if (key <= initial - gap34 - gap2) {
          *presult = initial - gap34 - gap2;
          return (i + 0);
        }
        if (key <= initial - gap34) {
          *presult = initial - gap34;
          return (i + 1);
        }
        if (key <= initial - gap4) {
          *presult = initial - gap4;
          return (i + 2);
        }
        *presult = initial;
        return (i + 3);
      }
      i += 4;
    }
    if (endbyte > inbyte && nvalue > i) {
      uint32_t tnvalue = static_cast<uint32_t>(nvalue - 1 - i);
      inbyte = decodeCarefully(inbyte, &initial, out, tnvalue);
      assert(inbyte <= endbyte);
      if (key <= out[0]) {
        *presult = out[0];
        return (i + 0);
      }
      if (tnvalue > 0 && key <= out[1]) {
        *presult = out[1];
        return (i + 1);
      }
      if (tnvalue > 1 && key <= out[2]) {
        *presult = out[2];
        return (i + 2);
      }
      if (tnvalue > 2 && key <= out[3]) {
        *presult = out[3];
        return (i + 3);
      }
    }
    assert(false);
    *presult = key + 1;
    return (i);
  }

  // Returns a decompressed value in an encoded array
  // This code has been optimized for delta-encoded arrays (TODO: optimize for
  // the regular case).
  uint32_t select(uint32_t *in, size_t index) {
    const uint8_t *inbyte = reinterpret_cast<const uint8_t *>(in);
    uint32_t out[4];
    out[0] = 0;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    size_t i = 0;
    uint32_t initial = 0;
    uint32_t nvalue = *in;
    inbyte += 4; // skip nvalue
    if (index + 3 <
        nvalue) { // this common case can be done with fewer branches
      while (i + 4 <= index) {
        inbyte = delta ? scanGroupVarIntDelta(inbyte, &initial)
                       : scanGroupVarInt(inbyte); // note: delta known at
                                                  // compile time: this is not a
                                                  // branch
        i += 4;
      }
      inbyte = delta ? decodeGroupVarIntDelta(inbyte, &initial, out)
                     : decodeGroupVarInt(inbyte, out); // note: delta known at
                                                       // compile time: this is
                                                       // not a branch
      return (out[index - i]);
    } // else
    // we finish with the uncommon case
    while (i + 3 < index) { // a single branch will do for this case (bulk of
                            // the computation)
      inbyte = delta ? scanGroupVarIntDelta(inbyte, &initial)
                     : scanGroupVarInt(inbyte);
      i += 4;
    }
    // lots of branching ahead...
    while (i + 3 < nvalue) {
      inbyte = delta ? decodeGroupVarIntDelta(inbyte, &initial, out)
                     : decodeGroupVarInt(inbyte, out);
      i += 4;
      if (i > index)
        return (out[index - (i - 4)]);
    }
    {
      nvalue = static_cast<uint32_t>(nvalue - i);
      inbyte = decodeCarefully(inbyte, &initial, out, nvalue);
      if (index == i)
        return (out[0]);
      if (nvalue > 1 && index == i + 1)
        return (out[1]);
      if (nvalue > 2 && index == i + 2)
        return (out[2]);
      if (nvalue > 3 && index == i + 3)
        return (out[3]);
    }
    assert(false); // we should never get here
    return (0);
  }

  string name() const {
    if (delta)
      return "varintgbdelta";
    else
      return "varintgb";
  }

  uint8_t *headlessEncode(uint32_t *in, const size_t length, uint32_t prev,
                          uint8_t *bout) {
    size_t k = 0;
    for (; k + 3 < length; k += 4) {
      uint8_t *keyp = bout++;
      *keyp = 0;
      {
        const uint32_t val = delta ? in[k] - prev : in[k];
        if (delta)
          prev = in[k];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t>(val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *keyp = static_cast<uint8_t>(1);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *bout++ = static_cast<uint8_t>(val >> 16);
          *keyp = static_cast<uint8_t>(2);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *>(bout) = val;
          bout += 4;
          *keyp = static_cast<uint8_t>(3);
        }
      }
      {
        const uint32_t val = delta ? in[k + 1] - prev : in[k + 1];
        if (delta)
          prev = in[k + 1];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t>(val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *keyp |= static_cast<uint8_t>(1 << 2);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *bout++ = static_cast<uint8_t>(val >> 16);
          *keyp |= static_cast<uint8_t>(2 << 2);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *>(bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << 2);
        }
      }
      {
        const uint32_t val = delta ? in[k + 2] - prev : in[k + 2];
        if (delta)
          prev = in[k + 2];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t>(val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *keyp |= static_cast<uint8_t>(1 << 4);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *bout++ = static_cast<uint8_t>(val >> 16);
          *keyp |= static_cast<uint8_t>(2 << 4);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *>(bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << 4);
        }
      }
      {
        const uint32_t val = delta ? in[k + 3] - prev : in[k + 3];
        if (delta)
          prev = in[k + 3];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t>(val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *keyp |= static_cast<uint8_t>(1 << 6);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *bout++ = static_cast<uint8_t>(val >> 16);
          *keyp |= static_cast<uint8_t>(2 << 6);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *>(bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << 6);
        }
      }
    }
    if (k < length) {
      uint8_t *keyp = bout++;
      *keyp = 0;
      for (int j = 0; k < length && j < 8; j += 2, ++k) {
        const uint32_t val = delta ? in[k] - prev : in[k];
        if (delta)
          prev = in[k];
        if (val < (1U << 8)) {
          *bout++ = static_cast<uint8_t>(val);
        } else if (val < (1U << 16)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *keyp |= static_cast<uint8_t>(1 << j);
        } else if (val < (1U << 24)) {
          *bout++ = static_cast<uint8_t>(val);
          *bout++ = static_cast<uint8_t>(val >> 8);
          *bout++ = static_cast<uint8_t>(val >> 16);
          *keyp |= static_cast<uint8_t>(2 << j);
        } else {
          // the compiler will do the right thing
          *reinterpret_cast<uint32_t *>(bout) = val;
          bout += 4;
          *keyp |= static_cast<uint8_t>(3 << j);
        }
      }
    }
    return bout;
  }

  // returns how many values were decoded to out, will try to decode
  // desirednumber
  // if input allows.
  size_t headlessDecode(const uint8_t *inbyte, const size_t length,
                        uint32_t prev, uint32_t *out,
                        const size_t desirednumber) {

    uint32_t *initout = out;
    const uint32_t *const endout = out + desirednumber;
    const uint8_t *const endbyte = inbyte + length;
    uint32_t val;
    while ((endbyte > inbyte + 4 * 4)) { //&& (endout > out + 3)
      inbyte = delta ? decodeGroupVarIntDelta(inbyte, &prev, out)
                     : decodeGroupVarInt(inbyte, out);
      out += 4;
    }
    while (endbyte > inbyte + 1) {
      uint8_t key = *inbyte++;
      // printf("last key is %u \n",key);
      for (int k = 0; (k < 4) && (endout > out); k++) {
        const uint32_t howmanybyte = key & 3;
        // printf("last key is %u howmanybyte = %u \n",key, howmanybyte+1);

        key = static_cast<uint8_t>(key >> 2);
        val = static_cast<uint32_t>(*inbyte++);
        if (howmanybyte >= 1) {
          val |= (static_cast<uint32_t>(*inbyte++) << 8);
          if (howmanybyte >= 2) {
            val |= (static_cast<uint32_t>(*inbyte++) << 16);
            if (howmanybyte >= 3) {
              val |= (static_cast<uint32_t>(*inbyte++) << 24);
            }
          }
        }
        // printf("decoded %u\n",val);
        prev = (delta ? prev : 0) + val;
        // printf("writing %u\n",prev);

        *out++ = prev;
      }
      assert(inbyte <= endbyte);
    }
    return out - initout;
  }

private:
  const uint32_t mask[4] = {0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF};

  void sortinfirstvalue(uint32_t *tmpbuffer, size_t length) {
    size_t top = length < 4 ? length : 4;

    for (size_t j = 0; j < top; ++j) {
      if (tmpbuffer[j] > tmpbuffer[j + 1]) {
        uint32_t t = tmpbuffer[j + 1];
        tmpbuffer[j + 1] = tmpbuffer[j];
        tmpbuffer[j] = t;
      }
    }
  }

  const uint8_t *decodeGroupVarInt(const uint8_t *in, uint32_t *out) {
    const uint32_t sel = *in++;
    if (sel == 0) {
      out[0] = static_cast<uint32_t>(in[0]);
      out[1] = static_cast<uint32_t>(in[1]);
      out[2] = static_cast<uint32_t>(in[2]);
      out[3] = static_cast<uint32_t>(in[3]);
      return in + 4;
    }
    const uint32_t sel1 = (sel & 3);
    *out++ = *((uint32_t *)(in)) & mask[sel1];
    in += sel1 + 1;
    const uint32_t sel2 = ((sel >> 2) & 3);
    *out++ = *((uint32_t *)(in)) & mask[sel2];
    in += sel2 + 1;
    const uint32_t sel3 = ((sel >> 4) & 3);
    *out++ = *((uint32_t *)(in)) & mask[sel3];
    in += sel3 + 1;
    const uint32_t sel4 = (sel >> 6);
    *out++ = *((uint32_t *)(in)) & mask[sel4];
    in += sel4 + 1;
    return in;
  }

  const uint8_t *decodeGroupVarIntDelta(const uint8_t *in, uint32_t *val,
                                        uint32_t *out) {
    const uint32_t sel = *in++;
    if (sel == 0) {
      out[0] = (*val += static_cast<uint32_t>(in[0]));
      out[1] = (*val += static_cast<uint32_t>(in[1]));
      out[2] = (*val += static_cast<uint32_t>(in[2]));
      out[3] = (*val += static_cast<uint32_t>(in[3]));
      return in + 4;
    }
    const uint32_t sel1 = (sel & 3);
    *val += *((uint32_t *)(in)) & mask[sel1];
    *out++ = *val;
    in += sel1 + 1;
    const uint32_t sel2 = ((sel >> 2) & 3);
    *val += *((uint32_t *)(in)) & mask[sel2];
    *out++ = *val;
    in += sel2 + 1;
    const uint32_t sel3 = ((sel >> 4) & 3);
    *val += *((uint32_t *)(in)) & mask[sel3];
    *out++ = *val;
    in += sel3 + 1;
    const uint32_t sel4 = (sel >> 6);
    *val += *((uint32_t *)(in)) & mask[sel4];
    *out++ = *val;
    in += sel4 + 1;
    return in;
  }

  const uint8_t *decodeCarefully(const uint8_t *inbyte, uint32_t *initial,
                                 uint32_t *out, uint32_t count) {
    uint32_t val;
    uint32_t k, key = *inbyte++;
    for (k = 0; k < count && k < 4; k++) {
      const uint32_t howmanybyte = key & 3;
      key = static_cast<uint8_t>(key >> 2);
      val = static_cast<uint32_t>(*inbyte++);
      if (howmanybyte >= 1) {
        val |= (static_cast<uint32_t>(*inbyte++) << 8);
        if (howmanybyte >= 2) {
          val |= (static_cast<uint32_t>(*inbyte++) << 16);
          if (howmanybyte >= 3) {
            val |= (static_cast<uint32_t>(*inbyte++) << 24);
          }
        }
      }
      if (delta) {
        *initial += val;
        *out = *initial;
      } else {
        *out = val;
      }
      out++;
    }
    return (inbyte);
  }

  const uint8_t *scanGroupVarIntDelta(const uint8_t *in, uint32_t *val) {
    const uint32_t sel = *in++;
    if (sel == 0) {
      *val += static_cast<uint32_t>(in[0]);
      *val += static_cast<uint32_t>(in[1]);
      *val += static_cast<uint32_t>(in[2]);
      *val += static_cast<uint32_t>(in[3]);
      return in + 4;
    }
    const uint32_t sel1 = (sel & 3);
    *val += *(reinterpret_cast<const uint32_t *>(in)) & mask[sel1];
    in += sel1 + 1;
    const uint32_t sel2 = ((sel >> 2) & 3);
    *val += *(reinterpret_cast<const uint32_t *>(in)) & mask[sel2];
    in += sel2 + 1;
    const uint32_t sel3 = ((sel >> 4) & 3);
    *val += *(reinterpret_cast<const uint32_t *>(in)) & mask[sel3];
    in += sel3 + 1;
    const uint32_t sel4 = (sel >> 6);
    *val += *(reinterpret_cast<const uint32_t *>(in)) & mask[sel4];
    in += sel4 + 1;
    return in;
  }

  const uint8_t *scanGroupVarInt(const uint8_t *in) {
    const uint32_t sel = *in++;
    if (sel == 0) {
      return in + 4;
    }
    const uint32_t sel1 = (sel & 3);
    in += sel1 + 1;
    const uint32_t sel2 = ((sel >> 2) & 3);
    in += sel2 + 1;
    const uint32_t sel3 = ((sel >> 4) & 3);
    in += sel3 + 1;
    const uint32_t sel4 = (sel >> 6);
    in += sel4 + 1;
    return in;
  }

  // encode 4 integers
  uint8_t *encodeGroupVarIntDelta(uint8_t *bout, uint32_t prev, uint32_t *in) {
    uint8_t *keyp = bout++;
    *keyp = 0;
    {
      const uint32_t val = in[0] - prev;
      prev = in[0];
      if (val < (1U << 8)) {
        *bout++ = static_cast<uint8_t>(val);
      } else if (val < (1U << 16)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *keyp = static_cast<uint8_t>(1);
      } else if (val < (1U << 24)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *bout++ = static_cast<uint8_t>(val >> 16);
        *keyp = static_cast<uint8_t>(2);
      } else {
        // the compiler will do the right thing
        *reinterpret_cast<uint32_t *>(bout) = val;
        bout += 4;
        *keyp = static_cast<uint8_t>(3);
      }
    }
    {
      const uint32_t val = in[1] - prev;
      prev = in[1];
      if (val < (1U << 8)) {
        *bout++ = static_cast<uint8_t>(val);
      } else if (val < (1U << 16)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *keyp |= static_cast<uint8_t>(1 << 2);
      } else if (val < (1U << 24)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *bout++ = static_cast<uint8_t>(val >> 16);
        *keyp |= static_cast<uint8_t>(2 << 2);
      } else {
        // the compiler will do the right thing
        *reinterpret_cast<uint32_t *>(bout) = val;
        bout += 4;
        *keyp |= static_cast<uint8_t>(3 << 2);
      }
    }
    {
      const uint32_t val = in[2] - prev;
      prev = in[2];
      if (val < (1U << 8)) {
        *bout++ = static_cast<uint8_t>(val);
      } else if (val < (1U << 16)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *keyp |= static_cast<uint8_t>(1 << 4);
      } else if (val < (1U << 24)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *bout++ = static_cast<uint8_t>(val >> 16);
        *keyp |= static_cast<uint8_t>(2 << 4);
      } else {
        // the compiler will do the right thing
        *reinterpret_cast<uint32_t *>(bout) = val;
        bout += 4;
        *keyp |= static_cast<uint8_t>(3 << 4);
      }
    }
    {
      const uint32_t val = in[3] - prev;
      prev = in[3];
      if (val < (1U << 8)) {
        *bout++ = static_cast<uint8_t>(val);
      } else if (val < (1U << 16)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *keyp |= static_cast<uint8_t>(1 << 6);
      } else if (val < (1U << 24)) {
        *bout++ = static_cast<uint8_t>(val);
        *bout++ = static_cast<uint8_t>(val >> 8);
        *bout++ = static_cast<uint8_t>(val >> 16);
        *keyp |= static_cast<uint8_t>(2 << 6);
      } else {
        // the compiler will do the right thing
        *reinterpret_cast<uint32_t *>(bout) = val;
        bout += 4;
        *keyp |= static_cast<uint8_t>(3 << 6);
      }
    }
    return bout;
  }

  uint32_t getByteLength(uint32_t val) {
    if (val < (1U << 8)) {
      return 1;
    } else if (val < (1U << 16)) {
      return 2;
    } else if (val < (1U << 24)) {
      return 3;
    } else {
      return 4;
    }
  }

  struct Block {                 // should fit in two cache lines.
    uint8_t data[4 * 4 + 1 + 3]; // the final +3 is a safety buffer
    uint32_t length;
  };

  uint8_t lengths[256] = {
      5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9,  10, 8,  9,  10, 11, 6,  7,
      8,  9,  7,  8,  9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 7,  8,  9,  10,
      8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 8,  9,  10, 11, 9,  10,
      11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 6,  7,  8,  9,  7,  8,  9,  10,
      8,  9,  10, 11, 9,  10, 11, 12, 7,  8,  9,  10, 8,  9,  10, 11, 9,  10,
      11, 12, 10, 11, 12, 13, 8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13,
      11, 12, 13, 14, 9,  10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13,
      14, 15, 7,  8,  9,  10, 8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13,
      8,  9,  10, 11, 9,  10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 9,  10,
      11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13, 14, 15, 10, 11, 12, 13,
      11, 12, 13, 14, 12, 13, 14, 15, 13, 14, 15, 16, 8,  9,  10, 11, 9,  10,
      11, 12, 10, 11, 12, 13, 11, 12, 13, 14, 9,  10, 11, 12, 10, 11, 12, 13,
      11, 12, 13, 14, 12, 13, 14, 15, 10, 11, 12, 13, 11, 12, 13, 14, 12, 13,
      14, 15, 13, 14, 15, 16, 11, 12, 13, 14, 12, 13, 14, 15, 13, 14, 15, 16,
      14, 15, 16, 17};

  const uint8_t *loadblock(Block *b, const uint8_t *readinginbyte) {
    b->length = lengths[readinginbyte[0]];
    memcpy(b->data, readinginbyte, b->length);
    return readinginbyte + b->length;
  }

  const uint8_t *loadblockcarefully(Block *b, const uint8_t *readinginbyte,
                                    size_t howmanyvals) {
    b->length = 1;
    for (size_t k = 0; k < howmanyvals; ++k)
      b->length += 1 + ((readinginbyte[0] >> (2 * k)) & 3);
    memcpy(b->data, readinginbyte, b->length);
    return readinginbyte + b->length;
  }

  void shiftin(Block *b, uint32_t *nextval, uint32_t *newsel) {
    uint32_t offsettolastval = lengths[b->data[0] & 63] - 1;
    uint32_t newnextsel = b->data[0] >> 6;
    uint32_t newnextval;
    memcpy(&newnextval, b->data + offsettolastval, 4);
    newnextval &= mask[newnextsel];
    // uint32_t newnextval =  *(reinterpret_cast<const uint32_t*>(b->data +
    // offsettolastval)) & mask[newnextsel];
    b->data[0] = uint8_t((b->data[0] << 2) | *newsel);
    std::memmove(b->data + 2 + *newsel, b->data + 1,
                 b->length - 1 - 1 - newnextsel);
    b->length = offsettolastval + 1 + *newsel;
    std::memcpy(b->data + 1, nextval, *newsel + 1);
    *nextval = newnextval;
    *newsel = newnextsel;
  }

  void finalshiftin(Block *b, uint32_t nextval, uint32_t newsel,
                    size_t howmany) {
    b->data[0] = uint8_t((b->data[0] << 2) | newsel);
    std::memmove(b->data + 2 + newsel, b->data + 1, b->length - 1);
    b->length = 1;
    for (size_t k = 0; k < howmany; ++k)
      b->length += 1 + ((b->data[0] >> (2 * k)) & 3);
    std::memcpy(b->data + 1, &nextval, newsel + 1);
  }
};

} // namespace SIMDCompressionLib

#endif /* SIMDCompressionAndIntersection_VARINTGB_H_ */
