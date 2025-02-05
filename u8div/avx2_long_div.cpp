void avx2_long_div_u8(const uint8_t* A, const uint8_t* B, uint8_t* C, size_t n) {
    const __m256i msb  = _mm256_set1_epi8(int8_t(0x80));
    const __m256i zero = _mm256_set1_epi8(0x00);

    for (size_t i=0; i < n; i += 32) {
        __m256i aa = _mm256_loadu_si256((__m256i*)(&A[i]));

        const __m256i b   = _mm256_loadu_si256((__m256i*)(&B[i]));
        __m256i b_shifted = _mm256_xor_si256(b, msb);

        __m256i bit = _mm256_set1_epi8((int8_t)0x80);
        __m256i a   = _mm256_set1_epi16(0);
        __m256i c   = _mm256_set1_epi16(0);
        for (int j=0; j < 8; j++) {
            {
                const __m256i t0 = _mm256_cmpgt_epi8(zero, aa);
                aa = _mm256_add_epi8(aa, aa);
                a  = _mm256_sub_epi8(a, t0);
            }

            const __m256i a_shifted = _mm256_xor_si256(a, msb);
            const __m256i gt = _mm256_cmpgt_epi8(b_shifted, a_shifted);

            const __m256i b0 = _mm256_andnot_si256(gt, b);
            const __m256i c0 = _mm256_andnot_si256(gt, bit);

            a = _mm256_sub_epi16(a, b0);
            c = _mm256_or_si256(c, c0);

            bit = _mm256_srli_epi32(bit, 1);
            a = _mm256_add_epi32(a, a);
        }

        _mm256_storeu_si256((__m256i*)(&C[i]), c);
    }
}
