/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */
#
#include <unistd.h>
#include <unordered_map>
#include <memory>
#include <sstream>

#include "common.h"
#include "util.h"
#include "timer.h"
#include "maropuparser.h"
#include "codecfactory.h"
#include "budgetedpostingcollector.h"
#include "skipping.h"
#include "hybm2.h"
#include "statisticsrecorder.h"

using namespace SIMDCompressionLib;

uint32_t FakeCheckSum(const uint32_t *p, size_t qty) {
  return std::accumulate(p, p + qty, 0);
}

void printusage(char *prog) {
  cout << "Usage: " << prog
       << " <uncompressed postings in the flat format> <converted log file> -b "
          "<memory budget> -s <codec>  -l <max # of log lines to process> -i "
          "<intersection function> -o "
       << endl;
  cout << "       -s   specify compression scheme ( ";
  for (string s : CODECFactory::allNames())
    cout << s << " ";
  cout << ")" << endl;
  cout << " (If no compression scheme is specified, we fall back on "
          "uncompressed postings.)"
       << endl;
  cout << "       -i   specify intersection function ( ";
  for (string s : IntersectionFactory::allNames())
    cout << s << " ";
  cout << ")" << endl;
  cout
      << "       -o   include one-word queries (default is do not include them)"
      << endl;
  cout << "       -q   quiet mode, runs entire test before outputting results "
          "(default is verbose)"
       << endl;
  cout << "       -l   allow you to specify a limit on the number of queries"
       << endl;
  cout << "       -p   partition the postings during compression and "
          "decompression (param: number of partitions)"
       << endl;
  cout << "       -k   to test Skipping (must provide a gap size param between "
          "1 and 31)"
       << endl;
  cout << "       -B   to activate bitmap mode (recommended values are in the "
          "range 8 to  32), when set to 0 in conjunction with compression, it "
          "means 'automatic' "
       << endl;
  cout << "       -d   to dump complete statistics " << endl;
  cout << "       -b   memory budget in GB (recommended: less than half of "
          "your available memory) "
       << endl;
}

/**
* This is a convenience class to run tests and recover statistics.
* Basically, given a collection of queries, it will compress the relevant
* posting lists... then uncompress them as needed and compute the intersections.
* It can then provide a statistical report of the performance.
*/
class TestHelper {
public:
  TestHelper(IntegerCODEC &scheme, intersectionfunction Inter)
      : scheme(scheme), CompressedSizeDuringPacking(0), packVolume(0),
        unpackVolume(0), packTime(0), unpackTime(0), interTime(0), Inter(Inter),
        CoarsePackTime(0), CoarseUnpackInterTime(0),
        modifiesinput(CODECFactory::modifiesInputDuringCompression(scheme)),
        MaxRecoveryBuffer(0), SR() {}

  /*
  * The class reports cumulative results, but if you ever
  * want to "zero" the statistics, you can call this method.
  */
  void reset() {
    CompressedSizeDuringPacking = 0;
    packVolume = 0;
    unpackVolume = 0;
    packTime = 0;
    unpackTime = 0;
    interTime = 0;
    CoarsePackTime = 0;
    CoarseUnpackInterTime = 0;
    MaxRecoveryBuffer = 0;
    SR.reset();
  }

  /**
   * This is the main testing function. Posting lists are compressed, then
   * uncompressed to RAM
   * and intersected.
   */
  void test(BudgetedPostingCollector &uncompPosts,
            const vector<vector<uint32_t>> &allPostIds) {
    unordered_map<uint32_t, vector<uint32_t>> compPostings; // we use a hash
                                                            // table to store
                                                            // the postings,
                                                            // ought to fit in
                                                            // RAM
    size_t MaxPostingSize(0);

    vector<uint32_t> dirtyCopy;
    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (compPostings.find(id) !=
            compPostings
                .end()) // these hits could potentially slow us down a bit
          continue;     // was compressed earlier
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        size_t qty = onePost.size();
        vector<uint32_t> compressedBuffer(
            qty); // this is potential a bit expensive: memory allocation
        if (MaxPostingSize < qty) {
          MaxPostingSize = qty;
        }
        if (modifiesinput) // if true there is a copy which might be expensive
          dirtyCopy = onePost; // some schemes might modify the input, hence it
                               // is necessary to make a copy in general
        size_t nvalue = compressedBuffer.size();
        //////////////
        // BEGIN performance-sensitive section for *compression*
        // (we don't care that much about it).
        /////////////
        z.reset();
        if (modifiesinput)
          scheme.encodeArray(dirtyCopy.data(), dirtyCopy.size(),
                             compressedBuffer.data(), nvalue);
        else
          scheme.encodeArray(onePost.data(), onePost.size(),
                             compressedBuffer.data(), nvalue);
        packTime += static_cast<double>(z.split());
        /////////////
        // END performance-sensitive code for *compression*
        /////////////
        assert(nvalue <= compressedBuffer.size());
        packVolume += qty;
        CompressedSizeDuringPacking += nvalue;
        compressedBuffer.resize(nvalue);
        compressedBuffer.shrink_to_fit(); // this may or may not be useful
        compPostings.emplace(id, compressedBuffer); // with some luck,
                                                    // compressedBuffer is
                                                    // *moved* to the container,
                                                    // not copied
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());

    vector<uint32_t> recoveryBuffer(MaxPostingSize + 1024);
    if (MaxRecoveryBuffer < recoveryBuffer.size())
      MaxRecoveryBuffer = recoveryBuffer.size();
    vector<uint32_t> intersection_result(recoveryBuffer.size());
    // pre2: verify results
    /**
     * We first test that the compressed version can be uncompressed back
     * to the original (duh!)
     */
    for (unordered_map<uint32_t, vector<uint32_t>>::iterator i =
             compPostings.begin();
         i != compPostings.end(); ++i) {
      const vector<uint32_t> &compressed = i->second;
      const vector<uint32_t> &uncompressed = uncompPosts.getOnePost(i->first);
      vector<uint32_t> recbuffer(uncompressed.size() + 1024);
      size_t recoveredsize = recbuffer.size();
      scheme.decodeArray(compressed.data(), compressed.size(), recbuffer.data(),
                         recoveredsize);
      recbuffer.resize(recoveredsize);
      if (recbuffer.size() != uncompressed.size()) {
        cout << "Original array had size " << uncompressed.size() << endl;
        cout << "Compressed size was " << compressed.size() << endl;
        cout << "After decoding, the size was " << recoveredsize << endl;
        throw runtime_error("Bug: don't even have same size!?!");
      }
      if (recbuffer != uncompressed) {
        cout << "size = " << uncompressed.size() << endl;
        int display = 10;
        for (size_t ii = 0; ii < uncompressed.size(); ++ii) {
          if (uncompressed[ii] != recbuffer[ii]) {
            cout << " i = " << ii << " expected " << uncompressed[ii]
                 << " but got " << recbuffer[ii] << endl;
            display--;
            if (display == 0)
              break;
          }
        }

        throw runtime_error("Bug: somehow, we can't recover the original.");
      }
    }
    /**
    * The next loop is just meant to test intersections
    */
    for (const vector<uint32_t> &qids : allPostIds) {
      // we begin by recoving references to the posting lists.
      vector<pair<size_t, const vector<uint32_t> *>> queryCompPost;
      for (uint32_t id : qids) {
        const vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        queryCompPost.emplace_back(
            make_pair(onePost.size(), &compPostings[id]));
      }
      assert(!queryCompPost.empty());
      // Sort in the order of increasing posting size
      // Daniel: this is to make the SvS intersection faster...
      sort(queryCompPost.begin(), queryCompPost.end());
      // the first posting list is a particular case, so we do it separately.
      size_t intersectioncardinality = intersection_result.size();
      scheme.decodeArray(queryCompPost.front().second->data(),
                         queryCompPost.front().second->size(),
                         intersection_result.data(), intersectioncardinality);
      assert(intersectioncardinality <= intersection_result.size());
      for (size_t i = 1;
           (intersectioncardinality > 0) && (i < queryCompPost.size()); ++i) {
        size_t recoveredsize = recoveryBuffer.size();
        scheme.decodeArray(queryCompPost[i].second->data(),
                           queryCompPost[i].second->size(),
                           recoveryBuffer.data(), recoveredsize);
        assert(recoveredsize <= recoveryBuffer.size());
        intersectioncardinality = Inter(
            intersection_result.data(), intersectioncardinality,
            recoveryBuffer.data(), recoveredsize, intersection_result.data());
      }
      vector<uint32_t> trueintersection =
          uncompPosts.computeIntersection(qids, onesidedgallopingintersection);
      if (trueintersection.size() != intersectioncardinality) {
        cout << "expected cardinality: " << trueintersection.size() << endl;
        cout << "actual cardinality: " << intersectioncardinality << endl;
        for (auto v : queryCompPost)
          cout << v.first << endl;
        throw runtime_error("Intersection bug with write-back: you don't even "
                            "have the correct cardinality.");
      }
      if (!equal(trueintersection.begin(), trueintersection.end(),
                 intersection_result.begin())) {
        cout << "intersectioncardinality=" << intersectioncardinality << endl;
        cout << "number of lists=" << qids.size() << endl;
        for (auto v : queryCompPost)
          cout << v.first << endl;
        throw runtime_error(
            "you have a bug with intersections using write-backs!");
      }
    }
    /**
     * End of testing
     */
    // if these tests passed, then we should have the right answer.
    coarsez.reset();
    // 2. Test full cycle (decompression + intersection)
    for (const vector<uint32_t> &qids : allPostIds) {
      SR.prepareQuery();
      // we begin by recoving references to the posting lists.
      vector<pair<size_t, const vector<uint32_t> *>> queryCompPost;
      for (uint32_t id : qids) {
        const vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        queryCompPost.emplace_back(
            make_pair(onePost.size(), &compPostings[id]));
      }
      assert(!queryCompPost.empty());
      // Sort in the order of increasing posting size
      // Daniel: this is to make the SvS intersection faster...
      // we don't time this operation
      sort(queryCompPost.begin(), queryCompPost.end());
      // the first posting list is a particular case, so we do it separately.

      // we use pointers explicitely to me it easier for non-STL folks
      const uint32_t *input = queryCompPost.front().second->data();
      size_t inputsize = queryCompPost.front().second->size();
      uint32_t *const intersectionbuffer = intersection_result.data();

      //////////////////////
      // BEGIN performance-sensitive section
      // Note that we possibly uncompress to RAM, and not to cache.
      // Moreover, input might not be in cache.
      //////////////////////
      z.reset();
      size_t intersectioncardinality = intersection_result.size();
      scheme.decodeArray(input, inputsize, intersectionbuffer,
                         intersectioncardinality);
      unpackTime += static_cast<double>(z.split());
      /////////////////////
      // END performance-sensitive section
      /////////////////////
      assert(intersectioncardinality <= intersection_result.size());
      unpackVolume += intersectioncardinality;
      uint32_t *const recoverybuffer = recoveryBuffer.data();
      for (size_t i = 1;
           (intersectioncardinality > 0) && (i < queryCompPost.size()); ++i) {
        size_t recoveredsize = recoveryBuffer.size();
        // again we use explicit pointers to make it easier for non-STL people
        input = queryCompPost[i].second->data();
        inputsize = queryCompPost[i].second->size();
        /////////////////////////
        // BEGIN performance-sensitive section
        // Note that input might not be in cache, and that
        // output might be to RAM
        ////////////////////////
        z.reset();
        scheme.decodeArray(input, inputsize, recoverybuffer, recoveredsize);
        unpackTime += static_cast<double>(z.split());
        /////////////////////////
        // END performance-sensitive section
        /////////////////////////
        assert(recoveredsize <= recoveryBuffer.size());
        unpackVolume += recoveredsize;
        /////////////////////////
        // BEGIN performance-sensitive section for intersections.
        // Both inputs could be in RAM, not in cache.
        ////////////////////////
        z.reset();
        intersectioncardinality =
            Inter(intersectionbuffer, intersectioncardinality, recoverybuffer,
                  recoveredsize, intersectionbuffer);
        interTime += static_cast<double>(z.split());
        ////////////////////////////////
        // END performance-sensitive section for intersections.
        ///////////////////////////////
      }
      SR.endQuery(intersectioncardinality, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: "
         << (FakeCheckSum(recoveryBuffer.data(), recoveryBuffer.size()) +
             FakeCheckSum(intersection_result.data(),
                          intersection_result.size()))
         << endl;
  }

  /**
   * This is a version of "tests" where compressed posting lists are divided it
   * up in NumberOfPartitions partitions.
   * If NumberOfPartitions is large enough, uncompressed posting lists will
   * reside in cache, not in RAM.
   */

  void splittedtest(int NumberOfPartitions,
                    BudgetedPostingCollector &uncompPosts,
                    const vector<vector<uint32_t>> &allPostIds) {
    // we index on posting ID, and point to a vector of compressed postings
    unordered_map<uint32_t, vector<vector<uint32_t>>> compPostings;
    unordered_map<uint32_t, vector<uint32_t>> uncompsizes;

    pair<uint32_t, uint32_t> range =
        uncompPosts.findDocumentIDRange(allPostIds);
    vector<uint32_t> bounds(NumberOfPartitions + 1);
    for (int part = 0; part < NumberOfPartitions; ++part)
      bounds[part] = range.first +
                     part * (range.second - range.first) /
                         NumberOfPartitions; // slightly uneven
    bounds[NumberOfPartitions] = range.second + 1;
    vector<uint32_t> maxSize(NumberOfPartitions); // will contain the max size
                                                  // of a partition of a posting
                                                  // list
    size_t TotalMaxPostingSize = 0;
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        TotalMaxPostingSize = max(TotalMaxPostingSize, onePost.size());
        vector<uint32_t>::iterator i = onePost.begin();
        for (int part = 0; part < NumberOfPartitions; ++part) {
          vector<uint32_t>::iterator j =
              lower_bound(i, onePost.end(), bounds[part + 1]);
          uint32_t thissize = static_cast<uint32_t>(j - i);
          if (thissize > maxSize[part])
            maxSize[part] = thissize;
          i = j;
        }
      }
    }
    uint32_t AbsoluteMaxSize = *max_element(maxSize.begin(), maxSize.end());
    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (compPostings.find(id) !=
            compPostings
                .end()) // these hits could potentially slow us down a bit
          continue;     // was compressed earlier
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        //////////////
        // BEGIN performance-sensitive section for *compression*
        // (we don't care that much about it).
        /////////////
        z.reset();

        vector<uint32_t>::iterator i = onePost.begin();
        vector<vector<uint32_t>> subposts(NumberOfPartitions);
        vector<uint32_t> subsizes(NumberOfPartitions);
        uint32_t sanitycheck = 0;
        for (int part = 0; (i != onePost.end()) && (part < NumberOfPartitions);
             ++part) {
          vector<uint32_t> compressedBuffer;
          vector<uint32_t>::iterator j =
              lower_bound(i, onePost.end(), bounds[part + 1]);
          uint32_t thissize = static_cast<uint32_t>(j - i);
          if (j != onePost.end())
            assert(*j >= bounds[part + 1]);
          assert(*i >= bounds[part]);
          subsizes[part] = thissize;
          sanitycheck += thissize;
          if (thissize != 0) {
            compressedBuffer.resize(thissize + 128);
            size_t nvalue = compressedBuffer.size();
            vector<uint32_t> dirtyCopy(i, j); // we make a copy because (1) some
                                              // schemes modify input (2) we
                                              // need 128-bit alignment
            scheme.encodeArray(dirtyCopy.data(), thissize,
                               compressedBuffer.data(), nvalue);
            compressedBuffer.resize(nvalue);
            packVolume += thissize;
            CompressedSizeDuringPacking += nvalue;
          }
          subposts[part] = compressedBuffer;
          i = j;
        }
        assert(i == onePost.end());
        assert(sanitycheck == onePost.size());
        (void)sanitycheck;
        compPostings.emplace(id, subposts);
        uncompsizes.emplace(id, subsizes);
        packTime += static_cast<double>(z.split());
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());
    vector<uint32_t> recoveryBuffer(AbsoluteMaxSize);
    if (MaxRecoveryBuffer < AbsoluteMaxSize)
      MaxRecoveryBuffer = AbsoluteMaxSize;
    vector<uint32_t> total_intersection_result(TotalMaxPostingSize + 1024);
    vector<vector<uint32_t>> intersection_result;
    for (uint32_t size : maxSize) {
      vector<uint32_t> intersectionbuffer(size + 128);
      intersection_result.emplace_back(intersectionbuffer);
    }

    // 2. Test full cycle (decompression + intersection)
    // if 0 == phase, we test the correctness of the algorithm.
    // if 1 == phase, we test performance
    for (int phase = 0; phase < 2; ++phase) {
      if (1 == phase)
        coarsez.reset();

      for (const vector<uint32_t> &qids : allPostIds) {
        if (1 == phase)
          SR.prepareQuery();
        size_t totalintercardinality = 0;
        for (int part = 0; part < NumberOfPartitions; ++part) {
          vector<pair<size_t, const vector<uint32_t> *>> queryCompPost;
          for (uint32_t id : qids) {
            uint32_t myuncompsize = uncompsizes[id][part];
            const vector<uint32_t> *compressedposting = &compPostings[id][part];
            queryCompPost.emplace_back(
                make_pair(myuncompsize, compressedposting));
          }
          sort(queryCompPost.begin(), queryCompPost.end());
          const uint32_t *input = queryCompPost.front().second->data();
          size_t inputsize = queryCompPost.front().second->size();
          uint32_t *const intersectionbuffer = intersection_result[part].data();
          if (1 == phase)
            z.reset();
          size_t intersectioncardinality = intersection_result[part].size();
          if (inputsize == 0)
            intersectioncardinality = 0;
          else
            scheme.decodeArray(input, inputsize, intersectionbuffer,
                               intersectioncardinality);
          if (1 == phase) {
            unpackTime += static_cast<double>(z.split());
            unpackVolume += intersectioncardinality;
          }
          assert(intersectioncardinality <= intersection_result[part].size());
          uint32_t *const recoverybuffer = recoveryBuffer.data();
          for (size_t i = 1;
               (intersectioncardinality > 0) && (i < queryCompPost.size());
               ++i) {
            // again we use explicit pointers to make it easier for non-STL
            // people
            size_t recoveredsize = recoveryBuffer.size();
            input = queryCompPost[i].second->data();
            inputsize = queryCompPost[i].second->size();
            /////////////////////////
            // BEGIN performance-sensitive section
            // Note that input might not be in cache, and that
            // output might be to RAM
            ////////////////////////
            if (1 == phase)
              z.reset();
            scheme.decodeArray(input, inputsize, recoverybuffer, recoveredsize);
            if (1 == phase) {
              unpackTime += static_cast<double>(z.split());
              unpackVolume += recoveredsize;
            }
            assert(recoveredsize <= recoveryBuffer.size());
            /////////////////////////
            // END performance-sensitive section
            /////////////////////////
            /////////////////////////
            // BEGIN performance-sensitive section for intersections.
            // Both inputs could be in RAM, not in cache.
            ////////////////////////
            if (1 == phase)
              z.reset();
            intersectioncardinality =
                Inter(intersectionbuffer, intersectioncardinality,
                      recoverybuffer, recoveredsize, intersectionbuffer);
            if (1 == phase)
              interTime += static_cast<double>(z.split());
            ////////////////////////////////
            // END performance-sensitive section for intersections.
            ///////////////////////////////
          }
          // Saving the result
          copy(intersectionbuffer, intersectionbuffer + intersectioncardinality,
               total_intersection_result.begin() + totalintercardinality);
          totalintercardinality += intersectioncardinality;
        }
        if (1 == phase)
          SR.endQuery(totalintercardinality, uncompPosts, qids);
        if (0 == phase) {
          vector<uint32_t> trueintersection = uncompPosts.computeIntersection(
              qids, onesidedgallopingintersection);
          if (trueintersection.size() != totalintercardinality) {
            stringstream err;
            err << "Different cardinality, expected: "
                << trueintersection.size() << " got: " << totalintercardinality;
            throw runtime_error(err.str());
          }
          for (uint32_t k = 0; k < trueintersection.size(); ++k) {
            if (trueintersection[k] != total_intersection_result[k]) {
              throw runtime_error("intersection bug");
            }
          }
        }
      }
      if (1 == phase)
        CoarseUnpackInterTime += static_cast<double>(coarsez.split());
    }

    // Prevent from optimizing out
    cout << "### Ignore: " << FakeCheckSum(total_intersection_result.data(),
                                           total_intersection_result.size())
         << endl;
  }

  void skippingtest(int SkipLog, BudgetedPostingCollector &uncompPosts,
                    const vector<vector<uint32_t>> &allPostIds) {
    unordered_map<uint32_t, shared_ptr<Skipping>> compPostings; // we use a hash
                                                                // table to
                                                                // store the
                                                                // postings,
                                                                // ought to fit
                                                                // in RAM
    size_t MaxPostingSize(0);

    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (compPostings.find(id) !=
            compPostings
                .end()) // these hits could potentially slow us down a bit
          continue;     // was compressed earlier
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        z.reset();
        compPostings.emplace(id, shared_ptr<Skipping>(new Skipping(
                                     SkipLog, onePost.data(), (uint32_t) onePost.size())));
        packTime += static_cast<double>(z.split());
        size_t qty = onePost.size();
        if (MaxPostingSize < qty) {
          MaxPostingSize = qty;
        }
        packVolume += qty;
        CompressedSizeDuringPacking +=
            (compPostings[id]->storageInBytes() + sizeof(uint32_t) - 1) /
            sizeof(uint32_t);
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());
    vector<uint32_t> recoveryBuffer(MaxPostingSize + 1024);
    if (MaxRecoveryBuffer < recoveryBuffer.size())
      MaxRecoveryBuffer = recoveryBuffer.size();
    vector<uint32_t> intersection_result(recoveryBuffer.size());
    coarsez.reset();
    for (vector<uint32_t> qids : allPostIds) {
      if (qids.empty())
        continue; // odd but could happen?
      SR.prepareQuery();
      vector<pair<uint32_t, uint32_t>> sizeids;
      for (uint32_t i : qids) {
        sizeids.emplace_back(make_pair(compPostings[i]->Length, i));
      }
      sort(sizeids.begin(), sizeids.end());
      size_t intersize;
      if (sizeids.size() == 1) {
        intersize = compPostings[sizeids.front().second]->decompress(
            intersection_result.data());
        unpackVolume += intersize;
      } else {
        assert(compPostings.size() >= 2);
        intersize = compPostings[sizeids[0].second]->intersect(
            *compPostings[sizeids[1].second], intersection_result.data());
        unpackVolume += compPostings[sizeids[0].second]->Length;
        unpackVolume += compPostings[sizeids[1].second]->Length;
        for (size_t k = 2; (intersize > 0) && (k < sizeids.size()); ++k) {
          unpackVolume += compPostings[sizeids[k].second]->Length;
          intersize = compPostings[sizeids[k].second]->intersect(
              intersection_result.data(), (uint32_t) intersize,
              intersection_result.data());
        }
      }
      SR.endQuery(intersize, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: "
         << (FakeCheckSum(recoveryBuffer.data(), recoveryBuffer.size()) +
             FakeCheckSum(intersection_result.data(),
                          intersection_result.size()))
         << endl;
  }

  /*
   * If NumberOfPartitions == 0, we call a well-tested function with no
   * partitions
   */
  void bitmaptest(int NumberOfPartitions, uint32_t th,
                  BudgetedPostingCollector &uncompPosts,
                  const vector<vector<uint32_t>> &allPostIds) {
    if (NumberOfPartitions > 1) {
      bitmaptestsplit(NumberOfPartitions, th, uncompPosts, allPostIds);
    } else {
      bitmaptestnosplit(th, uncompPosts, allPostIds);
    }
  }

  void bitmaptestsplit(int NumberOfPartitions, uint32_t th,
                       BudgetedPostingCollector &uncompPosts,
                       const vector<vector<uint32_t>> &allPostIds) {
    pair<uint32_t, uint32_t> range =
        uncompPosts.findDocumentIDRange(allPostIds);
    vector<uint32_t> recovbufferHybrid;
    vector<unique_ptr<HybM2>> hybridPart(NumberOfPartitions);
    vector<size_t> MaxRecoveryBufferPart(NumberOfPartitions + 1);

    vector<uint32_t> bounds(NumberOfPartitions + 1);

    for (int part = 0; part < NumberOfPartitions; ++part) {
      bounds[part] = range.first +
                     part * (range.second - range.first) /
                         NumberOfPartitions; // slightly uneven
    }
    bounds[NumberOfPartitions] = range.second + 1;

    for (int i = 0; i < NumberOfPartitions; ++i) {
      hybridPart[i] = unique_ptr<HybM2>(new HybM2(
          scheme, Inter, bounds[i + 1] - bounds[i], recovbufferHybrid, th));
    }

    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about

    size_t TotalMaxPostingSize = 0;
    size_t MaxPostingSizePart = 0;

    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);

        if (TotalMaxPostingSize < onePost.size()) {
          TotalMaxPostingSize = onePost.size();
        }

        vector<uint32_t>::iterator i = onePost.begin();
        uint32_t sanitycheck = 0;
        for (int part = 0; (part < NumberOfPartitions); ++part) {
          if (i == onePost.end()) {
            /*
             * We need to load the empty posting or the intersection
             * will fail
             */
            if (!hybridPart[part]->hasBeenLoaded(
                    id)) {                       // wasn't compressed earlier
              static vector<uint32_t> emptyPost; // thread-safe in C++ 11

              z.reset(); // actually this should be very quick
              CompressedSizeDuringPacking += hybridPart[part]->load(
                  id, emptyPost.data(), (uint32_t) emptyPost.size());
              packTime += static_cast<double>(z.split());

              packVolume += 0;
            }
            continue;
          }
          vector<uint32_t>::iterator j =
              lower_bound(i, onePost.end(), bounds[part + 1]);
          uint32_t thissize = static_cast<uint32_t>(j - i);
          if (MaxPostingSizePart < thissize) {
            MaxPostingSizePart = thissize;
          }
          if (j != onePost.end())
            assert(*j >= bounds[part + 1]);
          assert(*i >= bounds[part]);
          sanitycheck += thissize;

          if (!hybridPart[part]->hasBeenLoaded(
                  id)) { // wasn't compressed earlier
            //////////////
            // BEGIN performance-sensitive section for *compression*
            // (we don't care that much about it).
            /////////////

            z.reset();
            /*
             * We make a copy because
             * (1) we need to subtract bounds[part]
             * (2) some schemes modify input
             * (3) we need 128-bit alignment
             */
            vector<uint32_t> dirtyCopy(i, j);

            /*
             * This might be a bit suboptimal.
             * However Leo expects that GCC 4.7
             * would vectorize this operation.
             * Anyways, we don't care much
             * about compression speeds.
             */

            size_t minId = bounds[part];
            size_t qty = dirtyCopy.size();

            for (size_t idx = 0; idx < qty; ++idx) {
              dirtyCopy[idx] -= minId;
            }

            CompressedSizeDuringPacking +=
                hybridPart[part]->load(id, dirtyCopy.data(), (uint32_t) dirtyCopy.size());
            packTime += static_cast<double>(z.split());

            packVolume += thissize;
            //////////////
            // END performance-sensitive section for *compression*
            //////////////
          }
          i = j;

          if (MaxRecoveryBufferPart[part] <
              hybridPart[part]->sizeOfRecoveryBufferInWords())
            MaxRecoveryBufferPart[part] =
                hybridPart[part]->sizeOfRecoveryBufferInWords();
        }
        assert(i == onePost.end());
        assert(sanitycheck == onePost.size());
        (void)sanitycheck;
        packTime += static_cast<double>(z.split());
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());

    MaxRecoveryBuffer = *max_element(MaxRecoveryBufferPart.begin(),
                                     MaxRecoveryBufferPart.end());

    vector<uint32_t> intersection_result(MaxPostingSizePart + 1024);
    vector<uint32_t> total_intersection_result(TotalMaxPostingSize + 1024);

#if 1
    // testing round, does the algorithm have errors?
    for (vector<uint32_t> qids : allPostIds) {
      size_t total_sizeout = 0;

      for (int part = 0; part < NumberOfPartitions; ++part) {
        size_t sizeout = intersection_result.size();
        hybridPart[part]->intersect(qids, intersection_result.data(), sizeout);

        for (size_t k = total_sizeout; k < total_sizeout + sizeout; ++k)
          total_intersection_result[k] =
              intersection_result[k - total_sizeout] + bounds[part];
        total_sizeout += sizeout;
      }

      vector<uint32_t> trueintersection =
          uncompPosts.computeIntersection(qids, onesidedgallopingintersection);
      if (trueintersection.size() != total_sizeout) {
        stringstream err;
        err << "Different cardinality, expected: " << trueintersection.size()
            << " got: " << total_sizeout;
        throw runtime_error(err.str());
      }
      for (uint32_t k = 0; k < trueintersection.size(); ++k) {
        if (trueintersection[k] != total_intersection_result[k]) {
          throw runtime_error("intersection bug");
        }
      }
    }
#endif

    coarsez.reset();

    // 2. Finally benchmarking
    for (vector<uint32_t> qids : allPostIds) {
      size_t total_sizeout = 0;

      SR.prepareQuery();
      for (int part = 0; part < NumberOfPartitions; ++part) {
        size_t sizeout = 0;

        // Intersect will fail if there is an empty list
        sizeout = intersection_result.size();
        unpackVolume += hybridPart[part]->intersect(
            qids, intersection_result.data(), sizeout);
        for (size_t k = total_sizeout; k < total_sizeout + sizeout; ++k)
          total_intersection_result[k] =
              intersection_result[k - total_sizeout] + bounds[part];
        total_sizeout += sizeout;
      }
      SR.endQuery(total_sizeout, uncompPosts, qids);
    }

    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: " << FakeCheckSum(total_intersection_result.data(),
                                           total_intersection_result.size())
         << endl;
  }

  void bitmaptestnosplit(uint32_t th, BudgetedPostingCollector &uncompPosts,
                         const vector<vector<uint32_t>> &allPostIds) {
    uint32_t MaxId = uncompPosts.findDocumentIDRange(allPostIds).second;
    size_t MaxPostingSize(0);
    vector<uint32_t> recovbufferHybrid;
    HybM2 hybrid(scheme, Inter, MaxId, recovbufferHybrid, th);
    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (hybrid.hasBeenLoaded(id))
          continue;
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        z.reset();
        CompressedSizeDuringPacking +=
            hybrid.load(id, onePost.data(), (uint32_t) onePost.size());
        packTime += static_cast<double>(z.split());
        size_t qty = onePost.size();
        packVolume += qty;
        if (MaxPostingSize < qty) {
          MaxPostingSize = qty;
        }
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());
    vector<uint32_t> intersection_result(MaxPostingSize + 1024);
    if (MaxRecoveryBuffer < hybrid.sizeOfRecoveryBufferInWords())
      MaxRecoveryBuffer = hybrid.sizeOfRecoveryBufferInWords();
    // testing round
    for (vector<uint32_t> qids : allPostIds) {
      size_t sizeout = intersection_result.size();
      hybrid.intersect(qids, intersection_result.data(), sizeout);
      vector<uint32_t> trueintersection =
          uncompPosts.computeIntersection(qids, onesidedgallopingintersection);
      if (trueintersection.size() != sizeout)
        throw runtime_error("not even same cardinality");
      for (uint32_t k = 0; k < sizeout; ++k)
        if (trueintersection[k] != intersection_result[k])
          throw runtime_error("intersection bug");
    }

    coarsez.reset();
    for (vector<uint32_t> qids : allPostIds) {
      SR.prepareQuery();
      size_t sizeout = intersection_result.size();
      unpackVolume +=
          hybrid.intersect(qids, intersection_result.data(), sizeout);
      SR.endQuery(sizeout, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: "
         << FakeCheckSum(intersection_result.data(), intersection_result.size())
         << endl;
  }

  void bitmapskippingtest(uint32_t BS, uint32_t th,
                          BudgetedPostingCollector &uncompPosts,
                          const vector<vector<uint32_t>> &allPostIds) {
    uint32_t MaxId = uncompPosts.findDocumentIDRange(allPostIds).second;
    size_t MaxPostingSize(0);
    SkippingHybM2 hybrid(MaxId, th, BS);
    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (hybrid.hasBeenLoaded(id))
          continue;
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        z.reset();
        CompressedSizeDuringPacking +=
            hybrid.load(id, onePost.data(), (uint32_t) onePost.size());
        packTime += static_cast<double>(z.split());
        size_t qty = onePost.size();
        packVolume += qty;
        if (MaxPostingSize < qty) {
          MaxPostingSize = qty;
        }
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());
    vector<uint32_t> intersection_result(MaxPostingSize + 1024);
    // testing round
    for (vector<uint32_t> qids : allPostIds) {
      size_t sizeout = intersection_result.size();
      hybrid.intersect(qids, intersection_result.data(), sizeout);
      vector<uint32_t> trueintersection =
          uncompPosts.computeIntersection(qids, onesidedgallopingintersection);
      if (trueintersection.size() != sizeout)
        throw runtime_error("not even same cardinality");
      for (uint32_t k = 0; k < sizeout; ++k)
        if (trueintersection[k] != intersection_result[k])
          throw runtime_error("intersection bug");
    }

    coarsez.reset();
    for (vector<uint32_t> qids : allPostIds) {
      SR.prepareQuery();
      size_t sizeout = intersection_result.size();
      unpackVolume +=
          hybrid.intersect(qids, intersection_result.data(), sizeout);
      SR.endQuery(sizeout, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: "
         << FakeCheckSum(intersection_result.data(), intersection_result.size())
         << endl;
  }

  void uncompressedbitmaptest(uint32_t th,
                              BudgetedPostingCollector &uncompPosts,
                              const vector<vector<uint32_t>> &allPostIds) {
    uint32_t MaxId = uncompPosts.findDocumentIDRange(allPostIds).second;
    size_t MaxPostingSize(0);
    UncompressedHybM2 hybrid(Inter, MaxId, th);
    WallClockTimer z;       // this is use only to time what we care
    WallClockTimer coarsez; // for "coarse" timings, it includes many things we
                            // don't care about
    // 1. Benchmark only compression
    for (const vector<uint32_t> &qids : allPostIds) {
      for (uint32_t id : qids) {
        if (hybrid.hasBeenLoaded(id))
          continue;
        vector<uint32_t> &onePost = uncompPosts.getOnePost(id);
        z.reset();
        CompressedSizeDuringPacking +=
            hybrid.load(id, onePost.data(), (uint32_t) onePost.size());
        packTime += static_cast<double>(z.split());
        size_t qty = onePost.size();
        packVolume += qty;
        if (MaxPostingSize < qty) {
          MaxPostingSize = qty;
        }
      }
    }
    CoarsePackTime += static_cast<double>(coarsez.split());
    vector<uint32_t> intersection_result(MaxPostingSize + 1024);
    // testing round
    for (vector<uint32_t> qids : allPostIds) {
      size_t sizeout = intersection_result.size();
      hybrid.intersect(qids, intersection_result.data(), sizeout);
      vector<uint32_t> trueintersection =
          uncompPosts.computeIntersection(qids, onesidedgallopingintersection);
      if (trueintersection.size() != sizeout)
        throw runtime_error("not even same cardinality");
      for (uint32_t k = 0; k < sizeout; ++k)
        if (trueintersection[k] != intersection_result[k])
          throw runtime_error("intersection bug");
    }

    coarsez.reset();
    for (vector<uint32_t> qids : allPostIds) {
      SR.prepareQuery();
      size_t sizeout = intersection_result.size();
      unpackVolume +=
          hybrid.intersect(qids, intersection_result.data(), sizeout);
      SR.endQuery(sizeout, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());

    // Prevent from optimizing out
    cout << "### Ignore: "
         << FakeCheckSum(intersection_result.data(), intersection_result.size())
         << endl;
  }

  /**
   * This runs a a posting-list intersection tests without compression.
   */
  void testUncompressed(BudgetedPostingCollector &uncompPosts,
                        const vector<vector<uint32_t>> &allPostIds) {
    size_t MaxPostingSize = uncompPosts.findMaxPostingSize(allPostIds);
    vector<uint32_t> inter(MaxPostingSize);
    WallClockTimer coarsez;
    for (vector<uint32_t> qids : allPostIds) {
      SR.prepareQuery();
      if (qids.empty())
        continue; // odd but could happen?
      vector<pair<uint32_t, uint32_t>> sizeids;
      for (uint32_t i : qids) {
        sizeids.emplace_back(make_pair(uncompPosts.getOnePost(i).size(), i));
      }
      sort(sizeids.begin(), sizeids.end());
      vector<uint32_t> *answer =
          &uncompPosts.getOnePost(sizeids.front().second);
      size_t intersize = answer->size();
      unpackVolume += intersize;
      for (size_t k = 1; (intersize > 0) && (k < sizeids.size()); ++k) {
        vector<uint32_t> &nextone = uncompPosts.getOnePost(sizeids[k].second);
        unpackVolume += nextone.size();
        intersize = Inter(answer->data(), intersize, nextone.data(),
                          nextone.size(), inter.data());
        answer = &inter;
      }
      SR.endQuery(intersize, uncompPosts, qids);
    }
    CoarseUnpackInterTime += static_cast<double>(coarsez.split());
    // Prevent from optimizing out
    cout << "### Ignore: " << FakeCheckSum(inter.data(), inter.size()) << endl;
  }

  void printNumbers(bool detailed) const {
    cout << endl;
    if (!detailed) {
      cout << "#  coarse total speed (mis) + avg (ms/query) + median "
              "(ms/query) + 90perc (ms/query)"
           << endl;
      cout << setw(10) << setprecision(4)
           << static_cast<double>(unpackVolume) / (CoarseUnpackInterTime);
      cout << setw(10) << setprecision(4) << SR.averageTimeInMS();
      cout << setw(10) << setprecision(4) << SR.medianTimeInMS();
      cout << setw(10) << setprecision(4) << SR.ninetypercentileTimeInMS()
           << endl;

      return;
    }
    cout << "# max recovery buffer = " << setprecision(4)
         << (static_cast<double>(MaxRecoveryBuffer * sizeof(uint32_t)) /
             (1024.0 * 1024.0))
         << "MB" << endl;

    if (packVolume > 0) {
      cout << "# compression: number of integers + bits/int  + speed (mis) + "
              "coarse speed (mis) "
           << endl;
      cout << setw(20) << packVolume << setw(20) << setprecision(4)
           << static_cast<double>(CompressedSizeDuringPacking *
                                  sizeof(uint32_t)) *
                  8.0 / static_cast<double>(packVolume)
           << setw(20) << setprecision(4)
           << static_cast<double>(packVolume) / packTime << setw(20)
           << setprecision(4)
           << static_cast<double>(packVolume) / CoarsePackTime << endl;
    };
    if (unpackVolume > 0) {
      cout << "# decompression: number of integers  + ";
      if (unpackTime > 0)
        cout << " decompression speed (mis) +";
      if (interTime > 0)
        cout << " intersection speed (mis) +";
      if ((unpackTime + interTime) > 0)
        cout << " total speed (mis) + ";
      cout << "coarse total speed (mis) +";
      cout << "avg (ms/query)  + median (ms/query) + 90perc (ms/query)" << endl;
      cout << setw(10) << unpackVolume;
      if (unpackTime > 0)
        cout << setw(10) << setprecision(4)
             << static_cast<double>(unpackVolume) / unpackTime;
      if (interTime > 0)
        cout << setw(10) << setprecision(4)
             << static_cast<double>(unpackVolume) / interTime;
      if ((unpackTime + interTime) > 0)
        cout << setw(10) << setprecision(4)
             << static_cast<double>(unpackVolume) / (unpackTime + interTime);
      cout << setw(10) << setprecision(4)
           << static_cast<double>(unpackVolume) / (CoarseUnpackInterTime);
      cout << setw(10) << setprecision(4) << SR.averageTimeInMS();
      cout << setw(10) << setprecision(4) << SR.medianTimeInMS();
      cout << setw(10) << setprecision(4) << SR.ninetypercentileTimeInMS()
           << endl;
    };

    cout << endl;
  }

  StatisticsRecorder &getStatisticsRecorder() { return SR; }

private:
  IntegerCODEC &scheme;
  size_t CompressedSizeDuringPacking;
  size_t packVolume;
  size_t unpackVolume;
  double packTime;
  double unpackTime;
  double interTime;
  intersectionfunction Inter;
  double CoarsePackTime;
  double CoarseUnpackInterTime;
  const bool
      modifiesinput; // whether codec modifies the input during compression
  size_t MaxRecoveryBuffer;
  StatisticsRecorder SR;
};

int main(int argc, char **argv) {
  std::string InterName = "galloping";
  bool bIncludeOnePostQuery = false;
  bool quiet = false;
  size_t maxLinesToProcess = numeric_limits<size_t>::max();

  bool useCompression = false;
  int SkipLog = 0;
  int th = -1;

  string scheme = "copy";
  int partitions = 1;
  bool dumpcompletestats = false;

  size_t memBudget =
      1024ULL * 1024 * 1024 * 4; // 4GB seems like a better default than 16GB

  int c;
  while ((c = getopt(argc, argv, "i:os:b:hl:p:qk:B:d")) != -1) {
    switch (c) {
    case 'd':
      dumpcompletestats = true;
      break;
    case 'i':
      InterName = optarg;
      if (!IntersectionFactory::valid(InterName)) {
        cerr << "I don't recognize the intersection scheme '" << InterName
             << "' " << endl;
        printusage(argv[0]);
        return -1;
      }
      break;
    case 'B':
      th = atoi(optarg);
      if (th < 0) {
        cerr << "th param needs to be within [0,infty)." << endl;
        printusage(argv[0]);
        return -1;
      }
      break;
    case 'k':
      SkipLog = atoi(optarg);
      if ((SkipLog < 1) || (SkipLog > 31)) {
        cerr << "Skip param needs to be within [1,31]." << endl;
        printusage(argv[0]);
        return -1;
      }
      break;
    case 'p':
      partitions = atoi(optarg);
      if (partitions < 0) {
        printusage(argv[0]);
        return -1;
      }
      break;
    case 'q':
      quiet = true;
      break;
    case 'o':
      bIncludeOnePostQuery = true;
      break;
    case 's':
      scheme = optarg;
      useCompression = true;
      break;
    case 'b':
      memBudget = 1024ULL * 1024 * 1024 * atol(optarg);
      break;
    case 'l':
      maxLinesToProcess = atol(optarg);
      break;
    case 'h':
      printusage(argv[0]);
      return 0;
    default:
      printusage(argv[0]);
      return -1;
    }
  }
  if (optind + 1 >= argc) {
    printusage(argv[0]);
    return -1;
  }
  if (!CODECFactory::valid(scheme)) {
    cout << "Compression scheme " << scheme
         << " is unknown. Try one of these: " << endl;
    for (string n : CODECFactory::allNames())
      cout << n << endl;
    return -1;
  }
  if ((SkipLog > 0) && (useCompression)) {
    cout << "conflicting options. You cannot mix skipping and compression."
         << endl;
    printusage(argv[0]);
    return -1;
  }
  const bool modifiesinput = CODECFactory::modifiesInputDuringCompression(
      *CODECFactory::getFromName(scheme));

  cout << "# Memory budget for uncompressed postings: " << setprecision(2)
       << static_cast<double>(memBudget) / 1024.0 / 1024.0 / 1024.0 << "GB"
       << endl;
  cout << "# Maximum number of queries: " << maxLinesToProcess << endl;
  if (SkipLog != 0) {
    cout << "# testing skipping with block size: " << (1 << SkipLog) << endl;
    if (th >= 0) {
      cout << "# bitmap threshold: " << th << endl;
    }
  } else if (useCompression) {
    cout << "# Compression scheme: " << scheme << endl;
    cout << "# Does it modify inputs during compression? : "
         << (modifiesinput ? "yes" : "no") << endl;
    cout << "# Intersection proc: " << InterName << endl;
    if (th >= 0) {
      if (th == 0)
        cout << "# bitmap threshold: automatic (0)" << endl;
      else
        cout << "# bitmap threshold: " << th << endl;
    }
    if (partitions > 1) {
      cout << "# number of partitions: " << partitions << endl;
    }
  } else {
    cout << "# Compression is deactivated (use -s followed by scheme to enable)"
         << endl;
    cout << "# Intersection proc: " << InterName << endl;
    if (th >= 0)
      cout << "# bitmap threshold: " << th << endl;
  }
  cout << "# Do we include one-word queries: "
       << (bIncludeOnePostQuery ? "yes" : "no") << endl;

  cout << "# Quiet: " << (quiet ? "yes" : "no") << endl;

  try {
    string postFileName = argv[optind];
    string logFileName = argv[optind + 1];

    ifstream logFile(logFileName.c_str());
    if (!logFile.is_open()) {
      cerr << " Couldn't open query log file " << logFileName << endl;
      printusage(argv[0]);
      return -1;
    }
    cout << "# Parsing query file " << logFileName << endl;

    logFile.exceptions(ios::badbit); // will throw an exception if something
                                     // goes wrong, saves us the trouble of
                                     // checking the IO status

    TestHelper testBed(*CODECFactory::getFromName(scheme),
                       IntersectionFactory::getFromName(InterName));

    cout << "# Loading posting lists from " << postFileName
         << "... please be patient." << endl;
    BudgetedPostingCollector uncompPosts(postFileName, memBudget);
    cout << "# Found " << uncompPosts.getMaxPostQty() << " posting lists."
         << endl;
    if (!quiet)
      cout << "# After each block, we output the *cumulative* results. Expect "
              "convergence. "
           << endl;
    string line;
    line.reserve(1024);

    vector<vector<uint32_t>>
        allPostIds; // this a buffer where queries are stored.

    size_t skippedQty = 0, lineQty = 0, randPickQty = 0;

    bool detailedDisplay = useCompression || (SkipLog != 0) || (th >= 0);

    vector<uint32_t> oneQueryPostIds; // buffer where a single query is stored
    for (; (static_cast<size_t>(lineQty - skippedQty) < maxLinesToProcess) &&
           logFile && getline(logFile, line);
         ++lineQty) {
      stringstream lineStr(line);
      oneQueryPostIds.clear();

      {
        uint32_t id;
        while (lineStr >> id) {
          if (uncompPosts.valid(id))
            oneQueryPostIds.emplace_back(id);
        }
      }
      if (oneQueryPostIds.empty() ||
          ((!bIncludeOnePostQuery) && (oneQueryPostIds.size() == 1))) {
        skippedQty++;
        continue;
      }
      if (!uncompPosts.loadPostings(
              oneQueryPostIds)) { // we couldn't load them all in
        if (SkipLog != 0) {
          if (th >= 0) {
            testBed.bitmapskippingtest(SkipLog, th, uncompPosts, allPostIds);
          } else {
            testBed.skippingtest(SkipLog, uncompPosts, allPostIds);
          }
        } else if (useCompression) {
          if (th >= 0)
            testBed.bitmaptest(partitions, th, uncompPosts, allPostIds);
          else if (partitions > 1)
            testBed.splittedtest(partitions, uncompPosts, allPostIds);
          else
            testBed.test(uncompPosts, allPostIds);
        } else {
          if (th >= 0)
            testBed.uncompressedbitmaptest(th, uncompPosts, allPostIds);
          else
            testBed.testUncompressed(uncompPosts, allPostIds);
        }
        if (!quiet)
          cout << "# queries processed so far: " << (lineQty - skippedQty)
               << endl;
        if (!quiet)
          testBed.printNumbers(detailedDisplay);
        uncompPosts.clear();
        allPostIds.clear();
        assert(uncompPosts.getMemUsed() == 0);
        if (!uncompPosts.loadPostings(oneQueryPostIds)) {
          cerr << "Cannot load postings for the query '" << line
               << "' after all postings are deleted. Perhaps, the memory "
                  "budget is too small, or keepQtyPost is too large."
               << endl;
          cerr << "Aborting!" << endl;
          return -1;
        }
      }
      allPostIds.emplace_back(oneQueryPostIds);
    }
    logFile.close(); // we are done

    // final report
    cout << "### FINAL REPORT: " << endl;
    if (SkipLog != 0) {
      if (th >= 0) {
        testBed.bitmapskippingtest(SkipLog, th, uncompPosts, allPostIds);
      } else {
        testBed.skippingtest(SkipLog, uncompPosts, allPostIds);
      }
    } else if (useCompression) {
      if (th >= 0)
        testBed.bitmaptest(partitions, th, uncompPosts, allPostIds);
      else if (partitions > 1)
        testBed.splittedtest(partitions, uncompPosts, allPostIds);
      else
        testBed.test(uncompPosts, allPostIds);
    } else {
      if (th >= 0)
        testBed.uncompressedbitmaptest(th, uncompPosts, allPostIds);
      else
        testBed.testUncompressed(uncompPosts, allPostIds);
    }
    testBed.printNumbers(detailedDisplay);

    cout << "# Total lines: " << lineQty
         << "  processed: " << (lineQty - skippedQty)
         << " skipped: " << skippedQty << " assigned randomly: " << randPickQty
         << endl;
    if (dumpcompletestats) {
      cout << "####### Complete stats:" << endl;
      testBed.getStatisticsRecorder().output(cout);
    }

  } catch (const exception &e) {
    cerr << "Run-time error: " << e.what() << endl;
    return -1;
  } catch (...) {
    cerr << "Unknown exception caught, terminating..." << endl;
    return -1;
  }

  return 0;
}
