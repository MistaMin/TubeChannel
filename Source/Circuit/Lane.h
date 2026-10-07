#pragma once

// Two values processed together, one per audio channel (SIMD). The solvers are written once for double and for V2.

#include <algorithm>
#include <cmath>

#if defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define TC_LANES_NEON 1
#else
#include <emmintrin.h>
#define TC_LANES_NEON 0
#endif

namespace tubechannel::circuit
{
#if TC_LANES_NEON
struct V2
{
    float64x2_t v;
    V2() : v(vdupq_n_f64(0.0)) {}
    V2(double a) : v(vdupq_n_f64(a)) {}
    V2(double a, double b) { const double t[2] = { a, b }; v = vld1q_f64(t); }
    explicit V2(float64x2_t x) : v(x) {}
    double lane0() const { return vgetq_lane_f64(v, 0); }
    double lane1() const { return vgetq_lane_f64(v, 1); }
    friend V2 operator+(V2 a, V2 b) { return V2(vaddq_f64(a.v, b.v)); }
    friend V2 operator-(V2 a, V2 b) { return V2(vsubq_f64(a.v, b.v)); }
    friend V2 operator*(V2 a, V2 b) { return V2(vmulq_f64(a.v, b.v)); }
    friend V2 operator/(V2 a, V2 b) { return V2(vdivq_f64(a.v, b.v)); }
    V2& operator+=(V2 b) { v = vaddq_f64(v, b.v); return *this; }
    V2& operator-=(V2 b) { v = vsubq_f64(v, b.v); return *this; }
};
inline V2 fabsR(V2 a) { return V2(vabsq_f64(a.v)); }
#else
struct V2
{
    __m128d v;
    V2() : v(_mm_setzero_pd()) {}
    V2(double a) : v(_mm_set1_pd(a)) {}
    V2(double a, double b) : v(_mm_set_pd(b, a)) {}
    explicit V2(__m128d x) : v(x) {}
    double lane0() const { return _mm_cvtsd_f64(v); }
    double lane1() const { return _mm_cvtsd_f64(_mm_unpackhi_pd(v, v)); }
    friend V2 operator+(V2 a, V2 b) { return V2(_mm_add_pd(a.v, b.v)); }
    friend V2 operator-(V2 a, V2 b) { return V2(_mm_sub_pd(a.v, b.v)); }
    friend V2 operator*(V2 a, V2 b) { return V2(_mm_mul_pd(a.v, b.v)); }
    friend V2 operator/(V2 a, V2 b) { return V2(_mm_div_pd(a.v, b.v)); }
    V2& operator+=(V2 b) { v = _mm_add_pd(v, b.v); return *this; }
    V2& operator-=(V2 b) { v = _mm_sub_pd(v, b.v); return *this; }
};
inline V2 fabsR(V2 a) { return V2(_mm_andnot_pd(_mm_set1_pd(-0.0), a.v)); }
#endif
inline double fabsR(double a) { return std::fabs(a); }
// the smaller of the lanes (for pivot checks)
inline double minLane(double a) { return a; }
inline double minLane(V2 a) { return std::min(a.lane0(), a.lane1()); }
inline double clampPivot(double a) { return std::fabs(a) < 1e-300 ? 1e-300 : a; }
inline double lane(double a, int) { return a; }
inline double lane(V2 a, int k) { return k == 0 ? a.lane0() : a.lane1(); }
inline V2 clampPivot(V2 a) { return V2(clampPivot(a.lane0()), clampPivot(a.lane1())); }
} // namespace tubechannel::circuit
