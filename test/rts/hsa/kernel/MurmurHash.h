#pragma once
#include "Types.h"

const uint64_t murmurHash64a_m = 0xc6a4a7935bd1e995;
const int32_t murmurHash64a_r = 47;
const uint64_t murmurHash64a_h = 0x8445d61a4e774912 ^ (8 * murmurHash64a_m);

inline uint64_t murmurHash64a(uint64_t k) {
   uint64_t h = murmurHash64a_h;
   k *= murmurHash64a_m;
   k ^= k >> murmurHash64a_r;
   k *= murmurHash64a_m;
   h ^= k;
   h *= murmurHash64a_m;
   h ^= h >> murmurHash64a_r;
   h *= murmurHash64a_m;
   h ^= h >> murmurHash64a_r;
   return h;
}

const uint32_t murmurhash1_m = 0xc6a4a793;
const int32_t murmurhash1_r = 16;
const uint32_t murmurhash1_h = 0x4e774912 ^ (4 * murmurhash1_m);
inline uint32_t murmurhash1(uint32_t k) {
      uint32_t h = murmurhash1_h;
      h += k;
      h *= murmurhash1_m;
      h ^= h >> 16;
      h *= murmurhash1_m;
      h ^= h >> 10;
      h *= murmurhash1_m;
      h ^= h >> 17;
      return h;
}
