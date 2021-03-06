#pragma once

#include <string>
#include <vector>
#include <functional>
#include <ostream>

#if __SSE2__
    #include <emmintrin.h>
#endif

#include <city.h>

#include <Core/Types.h>
#include <Common/unaligned.h>


/// The thing to avoid creating strings to find substrings in the hash table.
struct StringRef
{
    const char * data = nullptr;
    size_t size = 0;

    StringRef(const char * data_, size_t size_) : data(data_), size(size_) {}
    StringRef(const unsigned char * data_, size_t size_) : data(reinterpret_cast<const char *>(data_)), size(size_) {}
    StringRef(const std::string & s) : data(s.data()), size(s.size()) {}
    StringRef() = default;

    std::string toString() const { return std::string(data, size); }

    explicit operator std::string() const { return toString(); }
};

using StringRefs = std::vector<StringRef>;

using UInt64 = DB::UInt64;

#if __SSE2__

/** Compare strings for equality.
  * The approach is controversial and does not win in all cases.
  * For more information, see hash_map_string_2.cpp
  */

inline bool compareSSE2(const char * p1, const char * p2)
{
    return 0xFFFF == _mm_movemask_epi8(_mm_cmpeq_epi8(
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1)),
        _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2))));
}

inline bool compareSSE2x4(const char * p1, const char * p2)
{
    return 0xFFFF == _mm_movemask_epi8(
        _mm_and_si128(
            _mm_and_si128(
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1)),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2))),
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 1),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 1))),
            _mm_and_si128(
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 2),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 2)),
                _mm_cmpeq_epi8(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p1) + 3),
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(p2) + 3)))));
}

inline bool memequalSSE2Wide(const char * p1, const char * p2, size_t size)
{
    while (size >= 64)
    {
        if (compareSSE2x4(p1, p2))
        {
            p1 += 64;
            p2 += 64;
            size -= 64;
        }
        else
            return false;
    }

    switch ((size % 64) / 16)
    {
        case 3: if (!compareSSE2(p1 + 32, p2 + 32)) return false;
        case 2: if (!compareSSE2(p1 + 16, p2 + 16)) return false;
        case 1: if (!compareSSE2(p1     , p2     )) return false;
        case 0: break;
    }

    p1 += (size % 64) / 16 * 16;
    p2 += (size % 64) / 16 * 16;

    switch (size % 16)
    {
        case 15: if (p1[14] != p2[14]) return false;
        case 14: if (p1[13] != p2[13]) return false;
        case 13: if (p1[12] != p2[12]) return false;
        case 12: if (reinterpret_cast<const uint32_t *>(p1)[2] == reinterpret_cast<const uint32_t *>(p2)[2]) goto l8; else return false;
        case 11: if (p1[10] != p2[10]) return false;
        case 10: if (p1[9] != p2[9]) return false;
        case 9:  if (p1[8] != p2[8]) return false;
        l8: case 8:  return reinterpret_cast<const UInt64 *>(p1)[0] == reinterpret_cast<const UInt64 *>(p2)[0];
        case 7:  if (p1[6] != p2[6]) return false;
        case 6:  if (p1[5] != p2[5]) return false;
        case 5:  if (p1[4] != p2[4]) return false;
        case 4:  return reinterpret_cast<const uint32_t *>(p1)[0] == reinterpret_cast<const uint32_t *>(p2)[0];
        case 3:  if (p1[2] != p2[2]) return false;
        case 2:  return reinterpret_cast<const uint16_t *>(p1)[0] == reinterpret_cast<const uint16_t *>(p2)[0];
        case 1:  if (p1[0] != p2[0]) return false;
        case 0:  break;
    }

    return true;
}

#endif


inline bool operator== (StringRef lhs, StringRef rhs)
{
    if (lhs.size != rhs.size)
        return false;

    if (lhs.size == 0)
        return true;

#if __SSE2__
    return memequalSSE2Wide(lhs.data, rhs.data, lhs.size);
#else
    return 0 == memcmp(lhs.data, rhs.data, lhs.size);
#endif
}

inline bool operator!= (StringRef lhs, StringRef rhs)
{
    return !(lhs == rhs);
}

inline bool operator< (StringRef lhs, StringRef rhs)
{
    int cmp = memcmp(lhs.data, rhs.data, std::min(lhs.size, rhs.size));
    return cmp < 0 || (cmp == 0 && lhs.size < rhs.size);
}

inline bool operator> (StringRef lhs, StringRef rhs)
{
    int cmp = memcmp(lhs.data, rhs.data, std::min(lhs.size, rhs.size));
    return cmp > 0 || (cmp == 0 && lhs.size > rhs.size);
}


/** Hash functions.
  * You can use either CityHash64,
  *  or a function based on the crc32 statement,
  *  which is obviously less qualitative, but on real data sets,
  *  when used in a hash table, works much faster.
  * For more information, see hash_map_string_3.cpp
  */

struct StringRefHash64
{
    size_t operator() (StringRef x) const
    {
        return CityHash64(x.data, x.size);
    }
};

#if __SSE4_2__

#ifdef __SSE4_1__
#include <smmintrin.h>
#else

inline UInt64 _mm_crc32_u64(UInt64 crc, UInt64 value)
{
    asm("crc32q %[value], %[crc]\n" : [crc] "+r" (crc) : [value] "rm" (value));
    return crc;
}

#endif

/// Parts are taken from CityHash.

inline UInt64 hashLen16(UInt64 u, UInt64 v)
{
    return Hash128to64(uint128(u, v));
}

inline UInt64 shiftMix(UInt64 val)
{
    return val ^ (val >> 47);
}

inline UInt64 rotateByAtLeast1(UInt64 val, int shift)
{
    return (val >> shift) | (val << (64 - shift));
}

inline size_t hashLessThan8(const char * data, size_t size)
{
    static constexpr UInt64 k2 = 0x9ae16a3b2f90404fULL;
    static constexpr UInt64 k3 = 0xc949d7c7509e6557ULL;

    if (size >= 4)
    {
        UInt64 a = unalignedLoad<uint32_t>(data);
        return hashLen16(size + (a << 3), unalignedLoad<uint32_t>(data + size - 4));
    }

    if (size > 0)
    {
        uint8_t a = data[0];
        uint8_t b = data[size >> 1];
        uint8_t c = data[size - 1];
        uint32_t y = static_cast<uint32_t>(a) + (static_cast<uint32_t>(b) << 8);
        uint32_t z = size + (static_cast<uint32_t>(c) << 2);
        return shiftMix(y * k2 ^ z * k3) * k2;
    }

    return k2;
}

inline size_t hashLessThan16(const char * data, size_t size)
{
    if (size > 8)
    {
        UInt64 a = unalignedLoad<UInt64>(data);
        UInt64 b = unalignedLoad<UInt64>(data + size - 8);
        return hashLen16(a, rotateByAtLeast1(b + size, size)) ^ b;
    }

    return hashLessThan8(data, size);
}

struct CRC32Hash
{
    size_t operator() (StringRef x) const
    {
        const char * pos = x.data;
        size_t size = x.size;

        if (size == 0)
            return 0;

        if (size < 8)
        {
            return hashLessThan8(x.data, x.size);
        }

        const char * end = pos + size;
        size_t res = -1ULL;

        do
        {
            UInt64 word = unalignedLoad<UInt64>(pos);
            res = _mm_crc32_u64(res, word);

            pos += 8;
        } while (pos + 8 < end);

        UInt64 word = unalignedLoad<UInt64>(end - 8);    /// I'm not sure if this is normal.
        res = _mm_crc32_u64(res, word);

        return res;
    }
};

struct StringRefHash : CRC32Hash {};

#else

struct StringRefHash : StringRefHash64 {};

#endif


namespace std
{
    template <>
    struct hash<StringRef> : public StringRefHash {};
}


namespace ZeroTraits
{
    inline bool check(StringRef x) { return 0 == x.size; }
    inline void set(StringRef & x) { x.size = 0; }
};


inline bool operator==(StringRef lhs, const char * rhs)
{
    for (size_t pos = 0; pos < lhs.size; ++pos)
        if (!rhs[pos] || lhs.data[pos] != rhs[pos])
            return false;

    return true;
}

inline std::ostream & operator<<(std::ostream & os, const StringRef & str)
{
    if (str.data)
        os.write(str.data, str.size);

    return os;
}
