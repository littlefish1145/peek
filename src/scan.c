#include "scan.h"
#include "platform.h"

#if (defined(__x86_64__) || defined(__i386__)) && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define SCAN_X86 1
#include <immintrin.h>
#elif defined(__aarch64__)
#define SCAN_NEON 1
#include <arm_neon.h>
#endif

#define SCAN_LT '<'
#define SCAN_AMP '&'
#define SCAN_CR '\r'

uint64_t scan_mask64_ref(const char *p) {
    uint64_t m = 0;
    for (int i = 0; i < 64; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == SCAN_LT || c == SCAN_AMP || c == SCAN_CR || c == '\0')
            m |= (uint64_t)1 << i;
    }
    return m;
}

#ifdef SCAN_X86

__attribute__((target("avx2")))
static uint64_t scan_mask64_avx2(const char *p) {
    const __m256i t = _mm256_setr_epi8(
        '\0', 0, 0, 0, 0, 0, '&', 0, 0, 0, 0, 0, '<', '\r', 0, 0,
        '\0', 0, 0, 0, 0, 0, '&', 0, 0, 0, 0, 0, '<', '\r', 0, 0);
    const __m256i lf = _mm256_set1_epi8(0x0f);
    const __m256i v0 = _mm256_loadu_si256((const __m256i *)p);
    const __m256i v1 = _mm256_loadu_si256((const __m256i *)(p + 32));
    const __m256i e0 = _mm256_cmpeq_epi8(_mm256_shuffle_epi8(t, _mm256_and_si256(v0, lf)), v0);
    const __m256i e1 = _mm256_cmpeq_epi8(_mm256_shuffle_epi8(t, _mm256_and_si256(v1, lf)), v1);
    uint32_t l0 = (uint32_t)_mm256_movemask_epi8(e0);
    uint32_t l1 = (uint32_t)_mm256_movemask_epi8(e1);
    return (uint64_t)l0 | ((uint64_t)l1 << 32);
}

static int scan_have_avx2(void) { return pk_has_avx2(); }

#elif defined(SCAN_NEON)

static const uint8_t NIBTBL[16] = {
    '\0', 0, 0, 0, 0, 0, '&', 0, 0, 0, 0, 0, '<', '\r', 0, 0
};
static const uint8_t BITTBL[16] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
};

static uint64_t scan_mask64_neon(const char *p) {
    const uint8x16_t t = vld1q_u8(NIBTBL);
    const uint8x16_t bm = vld1q_u8(BITTBL);
    const uint8x16_t lf = vdupq_n_u8(0x0f);
    uint8x16_t d0 = vld1q_u8((const uint8_t *)p);
    uint8x16_t d1 = vld1q_u8((const uint8_t *)p + 16);
    uint8x16_t d2 = vld1q_u8((const uint8_t *)p + 32);
    uint8x16_t d3 = vld1q_u8((const uint8_t *)p + 48);
    uint8x16_t q0 = vceqq_u8(vqtbl1q_u8(t, vandq_u8(d0, lf)), d0);
    uint8x16_t q1 = vceqq_u8(vqtbl1q_u8(t, vandq_u8(d1, lf)), d1);
    uint8x16_t q2 = vceqq_u8(vqtbl1q_u8(t, vandq_u8(d2, lf)), d2);
    uint8x16_t q3 = vceqq_u8(vqtbl1q_u8(t, vandq_u8(d3, lf)), d3);
    uint8x16_t s0 = vpaddq_u8(vandq_u8(q0, bm), vandq_u8(q1, bm));
    uint8x16_t s1 = vpaddq_u8(vandq_u8(q2, bm), vandq_u8(q3, bm));
    s0 = vpaddq_u8(s0, s1);
    s0 = vpaddq_u8(s0, s0);
    return vgetq_lane_u64(vreinterpretq_u64_u8(s0), 0);
}

#endif

uint64_t scan_mask64(const char *p) {
#ifdef SCAN_X86
    static int has = -1;
    if (has < 0) has = scan_have_avx2();
    if (has) return scan_mask64_avx2(p);
#elif defined(SCAN_NEON)
    return scan_mask64_neon(p);
#endif
    return scan_mask64_ref(p);
}

char *scan_next(const char *p, const char *end, int *has_amp) {
    int amp = 0;
    while (p + 64 <= end) {
        uint64_t m = scan_mask64(p);
        while (m) {
            int i = (int)pk_ctz64(m);
            if (p[i] == SCAN_LT) { *has_amp = amp; return (char *)(p + i); }
            amp |= (p[i] == SCAN_AMP);
            m &= m - 1;
        }
        p += 64;
    }
    for (; p < end; p++) {
        if (*p == SCAN_LT) { *has_amp = amp; return (char *)p; }
        amp |= (*p == SCAN_AMP);
    }
    *has_amp = amp;
    return (char *)end;
}
