// Smoke test for an installed SIMDCompressionAndIntersection: round-trips a
// vector through a SIMD codec and runs a SIMD intersection, using only the
// installed (namespaced) headers and imported target.
#include <codecfactory.h>
#include <intersection.h>

#include <cstdint>
#include <iostream>
#include <vector>

using namespace SIMDCompressionLib;

int main() {
  IntegerCODEC &codec = *CODECFactory::getFromName("s4-bp128-d4");

  std::vector<uint32_t> data;
  for (uint32_t i = 0; i < 100000; i++)
    data.push_back(i * 3 + (i % 7));

  std::vector<uint32_t> compressed(data.size() + 1024);
  size_t compressedsize = compressed.size();
  codec.encodeArray(data.data(), data.size(), compressed.data(),
                    compressedsize);

  std::vector<uint32_t> recovered(data.size());
  size_t recoveredsize = recovered.size();
  codec.decodeArray(compressed.data(), compressedsize, recovered.data(),
                    recoveredsize);

  const bool roundtrip_ok =
      (recoveredsize == data.size()) && (recovered == data);

  const std::vector<uint32_t> a = {1, 3, 5, 7, 9, 11};
  const std::vector<uint32_t> b = {3, 4, 5, 9, 12};
  std::vector<uint32_t> out(a.size());
  const size_t intersize =
      SIMDintersection(a.data(), a.size(), b.data(), b.size(), out.data());

  std::cout << "roundtrip " << (roundtrip_ok ? "OK" : "FAIL")
            << ", compressed " << compressedsize << " words, intersection size "
            << intersize << "\n";

  return (roundtrip_ok && intersize == 3) ? 0 : 1;
}
