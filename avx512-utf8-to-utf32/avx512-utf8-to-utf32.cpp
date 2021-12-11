#include "avx512-utf8-to-utf32.h"

#include <cstdio>
#include "uint32-accumulator.h"

__m512i avx512_utf8_to_utf32__aux__version1(__m512i utf8);
__m512i avx512_utf8_to_utf32__aux__version2(__m512i utf8);

template <typename EXPANDFN>
size_t avx512_utf8_to_utf32(EXPANDFN expandfn, const char* str, size_t len, uint32_t* dwords) {
    const char* ptr = str;
    const char* end = ptr + len;

    uint32_t* output = dwords;

    while (ptr + 16 + 3 < end) {
        /* 1. Load all possible 4-byte substring into an AVX512 register
              For example if we have bytes abcdefgh... we create following 32-bit lanes

              [abcd|bcde|cdef|defg|efgh|...]
               ^                          ^
               byte 0 of reg              byte 63 of reg

              It means, we are touching 16+3 = 19 bytes
        */
        const __m512i input = expandfn(ptr);

        /*
            2. Classify which words contain valid UTF-8 characters.
               We test if the 0th byte is not a continuation byte (0b10xxxxxx) */
        __mmask16 valid;
        {
            const __m512i t0 = _mm512_and_si512(input, _mm512_set1_epi32(0xc0));
            valid = _mm512_cmpneq_epu32_mask(t0, _mm512_set1_epi32(0x80));
        }
        const int valid_count = __builtin_popcount(valid);

        // 3. Convert words into UCS-32
        //    (XXX: would passing `valid` mask speed things up?
        //     answer: no)
        const __m512i utf32 = avx512_utf8_to_utf32__aux__version1(input);

        // 4. Pack only valid words
        const __m512i out = _mm512_mask_compress_epi32(_mm512_setzero_si512(), valid, utf32);

        // 5. Store them
        _mm512_storeu_si512((__m512i*)output, out);

        output += valid_count;
        ptr += 16;
    }

    // TODO: process the tail

    return output - dwords;
}

size_t avx512_utf8_to_utf32__version1(const char* str, size_t len, uint32_t* dwords) {
    return avx512_utf8_to_utf32(avx512_expand__version1, str, len, dwords);
}

size_t avx512_utf8_to_utf32__version2(const char* str, size_t len, uint32_t* dwords) {
    return avx512_utf8_to_utf32(avx512_expand__version2, str, len, dwords);
}

size_t avx512_utf8_to_utf32__version3(const char* str, size_t len, uint32_t* dwords) {
    const char* ptr = str;
    const char* end = ptr + len;

    uint32_t* output = dwords;

    if (len > 64) {

        // load 16 bytes: [abcdefghijklmnop]
        __m512i prev = _mm512_broadcast_i64x2(_mm_loadu_si128((const __m128i*)ptr));
        ptr += 16;

        while (ptr + 16 < end) {
            // load next 16 bytes: [qrstu....]
            const __m512i curr = _mm512_broadcast_i64x2(_mm_loadu_si128((const __m128i*)ptr));

            /* 1. Load all possible 4-byte substring into an AVX512 register
                  from bytes a..p (16) and q..t (4). For these bytes
                  we create following 32-bit lanes

                  [abcd|bcde|cdef|defg|efgh|...]
                   ^                          ^
                   byte 0 of reg              byte 63 of reg */

            const __m512i merged = _mm512_mask_mov_epi32(prev, 0x1000, curr);
            const __m512i expand_ver2 = _mm512_setr_epi64(
                0x0403020103020100,
                0x0605040305040302,
                0x0807060507060504,
                0x0a09080709080706,
                0x0c0b0a090b0a0908,
                0x0e0d0c0b0d0c0b0a,
                0x000f0e0d0f0e0d0c,
                0x0201000f01000f0e
            );

            const __m512i input = _mm512_shuffle_epi8(merged, expand_ver2);
            prev = curr;

            /*
                2. Classify which words contain valid UTF-8 characters.
                   We test if the 0th byte is not a continuation byte (0b10xxxxxx) */
            __mmask16 valid;
            {
                const __m512i t0 = _mm512_and_si512(input, _mm512_set1_epi32(0xc0));
                valid = _mm512_cmpneq_epu32_mask(t0, _mm512_set1_epi32(0x80));
            }
            const int valid_count = __builtin_popcount(valid);

            // 3. Convert words into UCS-32
            const __m512i utf32 = avx512_utf8_to_utf32__aux__version1(input);

            // 4. Pack only valid words
            const __m512i out = _mm512_mask_compress_epi32(_mm512_setzero_si512(), valid, utf32);

            // 5. Store them
            _mm512_storeu_si512((__m512i*)output, out);

            output += valid_count;
            ptr += 16;
        }
    }

    // TODO: process the tail

    return output - dwords;
}

size_t avx512_utf8_to_utf32__version4(const char* str, size_t len, uint32_t* dwords) {
    const char* ptr = str;
    const char* end = ptr + len;

    uint32_t* output = dwords;

    if (len > 64) {

        // load 16 bytes: [abcdefghijklmnop]
        __m512i prev = _mm512_broadcast_i64x2(_mm_loadu_si128((const __m128i*)ptr));
        ptr += 16;

        RegisterAccumulator acc;

        auto convert_to_utf32 = [&output](__m512i utf8, int count) {
            // 3. Convert words into UCS-32
            const __m512i utf32 = avx512_utf8_to_utf32__aux__version1(utf8);

            // 4. Store them
            _mm512_storeu_si512((__m512i*)output, utf32);

            output += count;
        };

        while (ptr + 16 < end) {
            // load next 16 bytes: [qrstu....]
            const __m512i curr = _mm512_broadcast_i64x2(_mm_loadu_si128((const __m128i*)ptr));

            /* 1. Load all possible 4-byte substring into an AVX512 register
                  from bytes a..p (16) and q..t (4). For these bytes
                  we create following 32-bit lanes

                  [abcd|bcde|cdef|defg|efgh|...]
                   ^                          ^
                   byte 0 of reg              byte 63 of reg */

            const __m512i merged = _mm512_mask_mov_epi32(prev, 0x1000, curr);
            const __m512i expand_ver2 = _mm512_setr_epi64(
                0x0403020103020100,
                0x0605040305040302,
                0x0807060507060504,
                0x0a09080709080706,
                0x0c0b0a090b0a0908,
                0x0e0d0c0b0d0c0b0a,
                0x000f0e0d0f0e0d0c,
                0x0201000f01000f0e
            );

            const __m512i input = _mm512_shuffle_epi8(merged, expand_ver2);
            prev = curr;

            /*
                2. Classify which words contain valid UTF-8 characters.
                   We test if the 0th byte is not a continuation byte (0b10xxxxxx) */
            __mmask16 valid;
            {
                const __m512i t0 = _mm512_and_si512(input, _mm512_set1_epi32(0xc0));
                valid = _mm512_cmpneq_epu32_mask(t0, _mm512_set1_epi32(0x80));
            }

            acc.add(input, valid, convert_to_utf32);
            ptr += 16;
        }

        acc.flush(convert_to_utf32);
    }

    // TODO: process the tail

    return output - dwords;
}

__m512i avx512_expand__version1(const char* ptr) {
    const __m512i indices = _mm512_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    return _mm512_i32gather_epi32(indices, ptr, 1);
}

__m512i avx512_expand__version2(const char* ptr) {
    // load bytes 0..15 (16)
    const __m128i tmp0 = _mm_loadu_si128((const __m128i*)ptr);
    const __m512i t0 = _mm512_broadcast_i64x2(tmp0);

    // load bytes 16..19 (4)
    const uint32_t tmp1 = *(uint32_t*)(ptr + 16);
    const __m512i t1 = _mm512_set1_epi32(tmp1);

    // In the last lane we need bytes 13..19, so we're placing
    // 32-bit word from t1 at 0th position of the lane

    const __m512i t2 = _mm512_mask_mov_epi32(t0, 0x1000, t1);

    /*
    lane{0,1,2} have got bytes: [  0,  1,  2,  3,  4,  5,  6,  8,  9, 10, 11, 12, 13, 14, 15]
    lane3 has got bytes:        [ 16, 17, 18, 19,  4,  5,  6,  8,  9, 10, 11, 12, 13, 14, 15]
    expand_ver2 = [
        # lane 0:
        0, 1, 2, 3,
        1, 2, 3, 4,
        2, 3, 4, 5,
        3, 4, 5, 6,
        # lane 1:
        4, 5, 6, 7,
        5, 6, 7, 8,
        6, 7, 8, 9,
        7, 8, 9, 10,
        # lane 2:
         8,  9, 10, 11,
         9, 10, 11, 12,
        10, 11, 12, 13,
        11, 12, 13, 14,

        # lane 3 order: 13, 14, 15, 16 14, 15, 16, 17, 15, 16, 17, 18, 16, 17, 18, 19
        12, 13, 14, 15,
        13, 14, 15,  0,
        14, 15,  0,  1,
        15,  0,  1,  2,
    ] */
    const __m512i expand_ver2 = _mm512_setr_epi64(
        0x0403020103020100,
        0x0605040305040302,
        0x0807060507060504,
        0x0a09080709080706,
        0x0c0b0a090b0a0908,
        0x0e0d0c0b0d0c0b0a,
        0x000f0e0d0f0e0d0c,
        0x0201000f01000f0e
    );

    return _mm512_shuffle_epi8(t2, expand_ver2);
}

__m512i avx512_utf8_to_utf32__aux__version1(__m512i utf8) {
    /*
        Input:
        - utf8: bytes stored at separate 32-bit words
        - valid: which words have valid UTF-8 characters

        Bit layout of single word. We show 4 cases for each possible
        UTF-8 character encoding. The `?` denotes bits we must not
        assume their value.

        |10dd.dddd|10cc.cccc|10bb.bbbb|1111.0aaa| 4-byte char
        |????.????|10cc.cccc|10bb.bbbb|1110.aaaa| 3-byte char
        |????.????|????.????|10bb.bbbb|110a.aaaa| 2-byte char
        |????.????|????.????|????.????|0aaa.aaaa| ASCII char
          byte 3    byte 2    byte 1     byte 0
    */

    /* 1. Swap bytes within the 32-bit lanes

        |1111.0aaa|10bb.bbbb|10cc.cccc|10dd.dddd|
        |1110.aaaa|10bb.bbbb|10cc.cccc|????.????|
        |110a.aaaa|10bb.bbbb|????.????|????.????|
        |0aaa.aaaa|????.????|????.????|????.????| */
    __m512i values;
    // 4 * [3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12]
    const __m512i bswap_lookup = _mm512_setr_epi64(
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b
    );

    values = _mm512_shuffle_epi8(utf8, bswap_lookup);

    /* 2. Shift continuation byte B (byte #2) left by 2 and merge with the value

        |1111.0aaa|bbbb.bb??|10cc.cccc|10dd.dddd|
        |1110.aaaa|bbbb.bb??|10cc.cccc|????.????|
        |110a.aaaa|bbbb.bb??|????.????|????.????|
        |0aaa.aaaa|????.????|????.????|????.????| */
    const uint8_t merge_function = 0xca; // ((mask and value) or (not mask and field))
    {
        const __m512i mask = _mm512_set1_epi32(0x00fc0000);
        __m512i B = _mm512_slli_epi32(values, 2);
        values = _mm512_ternarylogic_epi32(mask, B, values, merge_function);
    }

    /* 3. Shift continuation byte C (byte #1) left by 4 and merge with the value

        |1111.0aaa|bbbb.bbcc|cccc.????|10dd.dddd|
        |1110.aaaa|bbbb.bbcc|cccc.????|????.????|
        |110a.aaaa|bbbb.bb??|????.????|????.????|
        |0aaa.aaaa|????.????|????.????|????.????| */
    {
        const __m512i mask = _mm512_set1_epi32(0x0003f000);
        __m512i C = _mm512_slli_epi32(values, 4);
        values = _mm512_ternarylogic_epi32(mask, C, values, merge_function);
    }

    /* 4. Shift continuation byte D (byte #0) left by 6 and merge with the value

        |1111.0aaa|bbbb.bbcc|cccc.dddd|dd??.????|
        |1110.aaaa|bbbb.bbcc|cccc.????|????.????|
        |110a.aaaa|bbbb.bb??|????.????|????.????|
        |0aaa.aaaa|????.????|????.????|????.????| */
    {
        const __m512i mask = _mm512_set1_epi32(0x00000fc0);
        __m512i D = _mm512_slli_epi32(values, 6);
        values = _mm512_ternarylogic_epi32(mask, D, values, merge_function);
    }

    /* 5. Get the 4 most significant bits from byte #0 -- these values
          will be used by to distinguish character classes

        |0000.0000|0000.0000|0000.0000|0000.1111|
        |0000.0000|0000.0000|0000.0000|0000.1110|
        |0000.0000|0000.0000|0000.0000|0000.110a|
        |0000.0000|0000.0000|0000.0000|0000.0aaa| */
    const __m512i char_type = _mm512_and_si512(_mm512_srli_epi32(utf8, 4),
                                               _mm512_set1_epi32(0x0000000f));

    /* 6. Shift left the values by variable amounts to reset highest UTF-8 bits

        |aaab.bbbb|bccc.cccd|dddd.d???|???0.0000| shift left by 5
        |aaaa.bbbb|bbcc.cccc|????.????|????.0000| shift left by 4
        |aaaa.abbb|bbb?.????|????.????|????.?000| shift left by 3
        |aaaa.aaa?|????.????|????.????|????.???0| shift left by 1 */
    {
        // 4 * [1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 3, 3, 4, 5]
        const __m512i shift_left = _mm512_setr_epi64(
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_left, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_sllv_epi32(values, shift);
    }

    /* 7. Shift right the values by variable amounts to reset lowest bits

        |0000.0000|000a.aabb|bbbb.cccc|ccdd.dddd| shift right by 11
        |0000.0000|0000.0000|aaaa.bbbb|bbcc.cccc| shift right by 16
        |0000.0000|0000.0000|0000.0aaa|aabb.bbbb| shift right by 21
        |0000.0000|0000.0000|0000.0000|0aaa.aaaa| shift right by 25 */
    {
        // 4 * [25, 25, 25, 25, 25, 25, 25, 25, 0, 0, 0, 0, 21, 21, 16, 11]
        const __m512i shift_right = _mm512_setr_epi64(
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_right, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_srlv_epi32(values, shift);
    }

    /*
        Note about shifts: since the left shift max is 5, and right shift max is 25,
        we may pack them in single lookup: 3 bits for left, and 5 bits for right shift.
        Then we would need only one _mm512_shuffle_epi8(). But still we need
        _mm512_and_si512() to convert the lookup result into 32-bit numbers. It would
        look like:

        __m512i shift = _mm512_shuffle_epi8(shift_left_right, char_type);

        __m512i shift_left = _mm512_and_si512(shift, _mm512_set1_epi32(0x03));
        __m512i shift_right = _mm512_srli_epi32(shift, 3);
        shift_right _mm512_and_si512(shift_right, _mm512_set1_epi32(0x1f));

        values = _mm512_sllv_epi32(values, shift_left);
        values = _mm512_srlv_epi32(values, shift_right);

        We would replace _mm512_shuffle_epi8() with _mm512_srli_epi32().
    */

    return values;
}

__m512i avx512_utf8_to_utf32__aux__version2(__m512i utf8) {
    /*
        Input:
        - utf8: bytes stored at separate 32-bit words
        - valid: which words have valid UTF-8 characters

        Bit layout of single word. We show 4 cases for each possible
        UTF-8 character encoding. The `?` denotes bits we must not
        assume their value.

        |10dd.dddd|10cc.cccc|10bb.bbbb|1111.0aaa| 4-byte char
        |????.????|10cc.cccc|10bb.bbbb|1110.aaaa| 3-byte char
        |????.????|????.????|10bb.bbbb|110a.aaaa| 2-byte char
        |????.????|????.????|????.????|0aaa.aaaa| ASCII char
          byte 3    byte 2    byte 1     byte 0
    */

    /* 1. Swap bytes within the 32-bit lanes

        |1111.0aaa|10bb.bbbb|10cc.cccc|10dd.dddd|
        |1110.aaaa|10bb.bbbb|10cc.cccc|????.????|
        |110a.aaaa|10bb.bbbb|????.????|????.????|
        |0aaa.aaaa|????.????|????.????|????.????| */
    __m512i values;
    // 4 * [3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12]
    const __m512i bswap_lookup = _mm512_setr_epi64(
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b,
        0x0405060700010203,
        0x0c0d0e0f08090a0b
    );

    values = _mm512_shuffle_epi8(utf8, bswap_lookup);
    /* 2. Shift continuation byte B (byte #2) left by 2 and isolate the field

        |0000.0000|bbbb.bb00|0000.0000|0000.0000|
        |0000.0000|bbbb.bb00|0000.0000|0000.0000|
        |0000.0000|bbbb.bb00|0000.0000|0000.0000|
        |0000.0000|????.??00|0000.0000|0000.0000| */

    __m512i B;
    B = _mm512_slli_epi32(values, 2);
    B = _mm512_and_si512(B, _mm512_set1_epi32(0x00fc0000));

    /* 3. Shift continuation byte C (byte #1) left by 4 and isolate the field

        |0000.0000|0000.00cc|cccc.0000|0000.0000|
        |0000.0000|0000.00cc|cccc.0000|0000.0000|
        |0000.0000|0000.00??|????.0000|0000.0000|
        |0000.0000|0000.00??|????.0000|0000.0000| */
    __m512i C;
    C = _mm512_slli_epi32(values, 4);
    C = _mm512_and_si512(C, _mm512_set1_epi32(0x0003f000));

    /* 4. Shift continuation byte D (byte #0) left by 6 and isolate the field

        |0000.0000|0000.0000|0000.dddd|dd00.0000|
        |0000.0000|0000.0000|0000.????|??00.0000|
        |0000.0000|0000.0000|0000.????|??00.0000|
        |0000.0000|0000.0000|0000.????|??00.0000| */
    __m512i D;
    D = _mm512_slli_epi32(values, 6);
    D = _mm512_and_si512(D, _mm512_set1_epi32(0x00000fc0));

    // 5. Merge fields B, C and D
    const uint8_t or_all = 0xfe; // B | C | D
    __m512i BCD;
    BCD = _mm512_ternarylogic_epi32(B, C, D, or_all);

    // 6. Put merged field into values
    {
        const __m512i mask = _mm512_set1_epi32(0x00ffffc0);
        const uint8_t merge_function = 0xca; // ((mask and value) or (not mask and field))
        values = _mm512_ternarylogic_epi32(mask, BCD, values, merge_function);
    }

    /* 7. Get the 4 most significant bits from byte #0 -- these values
          will be used by to distinguish character classes

        |0000.0000|0000.0000|0000.0000|0000.1111|
        |0000.0000|0000.0000|0000.0000|0000.1110|
        |0000.0000|0000.0000|0000.0000|0000.110a|
        |0000.0000|0000.0000|0000.0000|0000.0aaa| */
    const __m512i char_type = _mm512_and_si512(_mm512_srli_epi32(utf8, 4),
                                               _mm512_set1_epi32(0x0000000f));

    /* 8. Shift left the values by variable amounts to reset highest UTF-8 bits

        |aaab.bbbb|bccc.cccd|dddd.d???|???0.0000| shift left by 5
        |aaaa.bbbb|bbcc.cccc|????.????|????.0000| shift left by 4
        |aaaa.abbb|bbb?.????|????.????|????.?000| shift left by 3
        |aaaa.aaa?|????.????|????.????|????.???0| shift left by 1 */
    {
        // 4 * [1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 3, 3, 4, 5]
        const __m512i shift_left = _mm512_setr_epi64(
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000,
            0x0101010101010101,
            0x0504030300000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_left, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_sllv_epi32(values, shift);
    }

    /* 9. Shift right the values by variable amounts to reset lowest bits

        |0000.0000|000a.aabb|bbbb.cccc|ccdd.dddd| shift right by 11
        |0000.0000|0000.0000|aaaa.bbbb|bbcc.cccc| shift right by 16
        |0000.0000|0000.0000|0000.0aaa|aabb.bbbb| shift right by 21
        |0000.0000|0000.0000|0000.0000|0aaa.aaaa| shift right by 25 */
    {
        // 4 * [25, 25, 25, 25, 25, 25, 25, 25, 0, 0, 0, 0, 21, 21, 16, 11]
        const __m512i shift_right = _mm512_setr_epi64(
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_right, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_srlv_epi32(values, shift);
    }

    return values;
}


__m512i avx512_utf8_to_utf32__aux__version3(__m512i utf8) {
    /*
        Input:
        - utf8: bytes stored at separate 32-bit words
        - valid: which words have valid UTF-8 characters

        Bit layout of single word. We show 4 cases for each possible
        UTF-8 character encoding. The `?` denotes bits we must not
        assume their value.

        |10dd.dddd|10cc.cccc|10bb.bbbb|1111.0aaa| 4-byte char
        |????.????|10cc.cccc|10bb.bbbb|1110.aaaa| 3-byte char
        |????.????|????.????|10bb.bbbb|110a.aaaa| 2-byte char
        |????.????|????.????|????.????|0aaa.aaaa| ASCII char
          byte 3    byte 2    byte 1     byte 0
    */

    /* 1. Reset control bits of continuation bytes and the MSB
          of the leading byte; this makes all bytes unsigned (and
          does not alter ASCII char).

        |00dd.dddd|00cc.cccc|00bb.bbbb|0111.0aaa| 4-byte char
        |00??.????|00cc.cccc|00bb.bbbb|0110.aaaa| 3-byte char
        |00??.????|00??.????|00bb.bbbb|010a.aaaa| 2-byte char
        |00??.????|00??.????|00??.????|0aaa.aaaa| ASCII char
         ^^        ^^        ^^        ^
    */
    __m512i values;
    values = _mm512_and_si512(utf8, _mm512_set1_epi32(0x3f3f3f7f));

    /* 2. Swap and join fields A-B and C-D

        |0000.cccc|ccdd.dddd|0001.110a|aabb.bbbb| 4-byte char
        |0000.cccc|cc??.????|0001.10aa|aabb.bbbb| 3-byte char
        |0000.????|????.????|0001.0aaa|aabb.bbbb| 2-byte char
        |0000.????|????.????|000a.aaaa|aa??.????| ASCII char */
    values = _mm512_maddubs_epi16(values, _mm512_set1_epi32(0x01400140));

    /* 3. Swap and join field AB & CD

        |0000.0001|110a.aabb|bbbb.cccc|ccdd.dddd| 4-byte char
        |0000.0001|10aa.aabb|bbbb.cccc|cc??.????| 3-byte char
        |0000.0001|0aaa.aabb|bbbb.????|????.????| 2-byte char
        |0000.000a|aaaa.aa??|????.????|????.????| ASCII char */
    values = _mm512_madd_epi16(values, _mm512_set1_epi32(0x00011000));

    /* 4. Get the 4 most significant bits from byte #0 -- these values
          will be used by to distinguish character classes

        |0000.0000|0000.0000|0000.0000|0000.1111|
        |0000.0000|0000.0000|0000.0000|0000.1110|
        |0000.0000|0000.0000|0000.0000|0000.110a|
        |0000.0000|0000.0000|0000.0000|0000.0aaa| */
    const __m512i char_type = _mm512_and_si512(_mm512_srli_epi32(utf8, 4),
                                               _mm512_set1_epi32(0x0000000f));

    /* 5. Shift left the values by variable amounts to reset highest UTF-8 bits
        |aaab.bbbb|bccc.cccd|dddd.d000|0000.0000| 4-byte char -- by 11
        |aaaa.bbbb|bbcc.cccc|????.??00|0000.0000| 3-byte char -- by 10
        |aaaa.abbb|bbb?.????|????.???0|0000.0000| 2-byte char -- by 9
        |aaaa.aaa?|????.????|????.????|?000.0000| ASCII char -- by 7 */
    {
        // 4 * [7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 9, 9, 10, 11]
        const __m512i shift_left_v3 = _mm512_setr_epi64(
            0x0707070707070707,
            0x0b0a090900000000,
            0x0707070707070707,
            0x0b0a090900000000,
            0x0707070707070707,
            0x0b0a090900000000,
            0x0707070707070707,
            0x0b0a090900000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_left_v3, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_sllv_epi32(values, shift);
    }

    /* 5. Shift right the values by variable amounts to reset lowest bits
        |aaab.bbbb|bccc.cccd|dddd.d000|0000.0000| 4-byte char -- by 11
        |aaaa.bbbb|bbcc.cccc|????.??00|0000.0000| 3-byte char -- by 16
        |aaaa.abbb|bbb?.????|????.???0|0000.0000| 2-byte char -- by 21
        |aaaa.aaa?|????.????|????.????|?000.0000| ASCII char -- by 25 */
    {
        // 4 * [25, 25, 25, 25, 25, 25, 25, 25, 0, 0, 0, 0, 21, 21, 16, 11]
        const __m512i shift_right = _mm512_setr_epi64(
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000,
            0x1919191919191919,
            0x0b10151500000000
        );

        __m512i shift = _mm512_shuffle_epi8(shift_right, char_type);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0xff));
        values = _mm512_srlv_epi32(values, shift);
    }

    return values;
}


__m512i avx512_utf8_to_utf32__aux__version4(__m512i utf8) {
    /*
        Input:
        - utf8: bytes stored at separate 32-bit words
        - valid: which words have valid UTF-8 characters

        Bit layout of single word. We show 4 cases for each possible
        UTF-8 character encoding. The `?` denotes bits we must not
        assume their value.

        |10dd.dddd|10cc.cccc|10bb.bbbb|1111.0aaa| 4-byte char
        |????.????|10cc.cccc|10bb.bbbb|1110.aaaa| 3-byte char
        |????.????|????.????|10bb.bbbb|110a.aaaa| 2-byte char
        |????.????|????.????|????.????|0aaa.aaaa| ASCII char
          byte 3    byte 2    byte 1     byte 0
    */

    /* 1. Reset control bits of continuation bytes and the MSB
          of the leading byte; this makes all bytes unsigned (and
          does not alter ASCII char).

        |00dd.dddd|00cc.cccc|00bb.bbbb|0111.0aaa| 4-byte char
        |00??.????|00cc.cccc|00bb.bbbb|0110.aaaa| 3-byte char
        |00??.????|00??.????|00bb.bbbb|010a.aaaa| 2-byte char
        |00??.????|00??.????|00??.????|0aaa.aaaa| ASCII char
         ^^        ^^        ^^        ^
    */
    __m512i values;
    values = _mm512_and_si512(utf8, _mm512_set1_epi32(0x3f3f3f7f));

    /* 2. Swap and join fields A-B and C-D

        |0000.cccc|ccdd.dddd|0001.110a|aabb.bbbb| 4-byte char
        |0000.cccc|cc??.????|0001.10aa|aabb.bbbb| 3-byte char
        |0000.????|????.????|0001.0aaa|aabb.bbbb| 2-byte char
        |0000.????|????.????|000a.aaaa|aa??.????| ASCII char */
    values = _mm512_maddubs_epi16(values, _mm512_set1_epi32(0x01400140));

    /* 3. Swap and join field AB & CD

        |0000.0001|110a.aabb|bbbb.cccc|ccdd.dddd| 4-byte char
        |0000.0001|10aa.aabb|bbbb.cccc|cc??.????| 3-byte char
        |0000.0001|0aaa.aabb|bbbb.????|????.????| 2-byte char
        |0000.000a|aaaa.aa??|????.????|????.????| ASCII char */
    values = _mm512_madd_epi16(values, _mm512_set1_epi32(0x00011000));

    /* 4. Get the 4 most significant bits from byte #0 -- these values
          will be used by to distinguish character classes

        |0000.0000|0000.0000|0000.0000|0000.1111|
        |0000.0000|0000.0000|0000.0000|0000.1110|
        |0000.0000|0000.0000|0000.0000|0000.110a|
        |0000.0000|0000.0000|0000.0000|0000.0aaa| */
    const __m512i char_type = _mm512_and_si512(_mm512_srli_epi32(utf8, 4),
                                               _mm512_set1_epi32(0x0000000f));

    /* 5. Get packed shift amounts to use in the following steps */
    // 4 * [200, 200, 200, 200, 200, 200, 200, 200, 0, 0, 0, 0, 170, 170, 131, 92]
    const __m512i shift_left_right_v4 = _mm512_setr_epi64(
        0xc8c8c8c8c8c8c8c8,
        0x5c83aaaa00000000,
        0xc8c8c8c8c8c8c8c8,
        0x5c83aaaa00000000,
        0xc8c8c8c8c8c8c8c8,
        0x5c83aaaa00000000,
        0xc8c8c8c8c8c8c8c8,
        0x5c83aaaa00000000
    );
    const __m512i shifts = _mm512_shuffle_epi8(shift_left_right_v4, char_type);

    /* 5. Shift left the values by variable amounts to reset highest UTF-8 bits
        |aaab.bbbb|bccc.cccd|dddd.d000|0000.0000| 4-byte char -- by 11
        |aaaa.bbbb|bbcc.cccc|????.??00|0000.0000| 3-byte char -- by 10
        |aaaa.abbb|bbb?.????|????.???0|0000.0000| 2-byte char -- by 9
        |aaaa.aaa?|????.????|????.????|?000.0000| ASCII char -- by 7 */
    {
        const __m512i c7 = _mm512_set1_epi32(7);
        __m512i shift = _mm512_and_si512(shifts, c7);
        shift = _mm512_add_epi32(shift, c7);
        values = _mm512_sllv_epi32(values, shift);
    }

    /* 5. Shift right the values by variable amounts to reset lowest bits
        |aaab.bbbb|bccc.cccd|dddd.d000|0000.0000| 4-byte char -- by 11
        |aaaa.bbbb|bbcc.cccc|????.??00|0000.0000| 3-byte char -- by 16
        |aaaa.abbb|bbb?.????|????.???0|0000.0000| 2-byte char -- by 21
        |aaaa.aaa?|????.????|????.????|?000.0000| ASCII char -- by 25 */
    {
        __m512i shift = _mm512_srli_epi32(shifts, 3);
        shift = _mm512_and_si512(shift, _mm512_set1_epi32(0x1f));
        values = _mm512_srlv_epi32(values, shift);
    }

    return values;
}