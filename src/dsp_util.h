/** @file dsp_util.h
 *  @brief Small DSP pieces more than one block uses.
 *
 *  Plain C++, no hardware; everything set in Reset().
 */
#pragma once
#include <cmath>
#include <cstdint>

namespace phymo
{

/** Hot loops: placed in ITCM on the CHOMPI (chompi_sram.lds). */
#if defined(PHYMO_ITCM) && defined(__arm__)
#define PHYMO_HOT __attribute__((section(".itcmram"), noinline))
#else
#define PHYMO_HOT
#endif
/** Not inlined: called from several places, and code memory is tight. */
#define PHYMO_ONCE __attribute__((noinline))

/** tanh-like soft clip (Padé), ±1 from ±3. */
inline float SoftClip(float x)
{
    const float u = x * x;
    if(u < 0.25f)
        return x * (1.f + u * (-8.f / 27.f + u * (8.f / 81.f + u * (-8.f / 243.f + u * (8.f / 729.f)))));
    x = x > 3.f ? 3.f : x < -3.f ? -3.f : x;
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

/** Fast 2^x (6th-order series), within 2e-7. */
inline float Exp2Fast(float x)
{
    x            = x < -126.f ? -126.f : x > 126.f ? 126.f : x;
    const float n = roundf(x), f = x - n; // f: -0.5 .. 0.5
    const float p = 1.f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
    union
    {
        float   f;
        int32_t i;
    } u;
    u.i = (static_cast<int32_t>(n) + 127) << 23;
    return p * u.f;
}

/** Fast log2(x), x > 0, within 1e-6. */
inline float Log2Fast(float x)
{
    union
    {
        float   f;
        int32_t i;
    } u;
    u.f   = x;
    int e = ((u.i >> 23) & 0xff) - 127;
    u.i   = (u.i & 0x007fffff) | 0x3f800000; // mantissa, 1 to 2
    float m = u.f;
    if(m > 1.4142136f)
        m *= 0.5f, e++;
    const float t = (m - 1.f) / (m + 1.f), t2 = t * t;
    return static_cast<float>(e) + 2.8853901f * t * (1.f + t2 * (1.f / 3.f + t2 * (0.2f + t2 * (1.f / 7.f))));
}

/** sin(2π t), t in cycles (9th-order series), within 4e-6. */
inline float SinCycles(float t)
{
    t -= floorf(t + 0.5f); // -0.5 .. 0.5
    t = t > 0.25f ? 0.5f - t : t < -0.25f ? -0.5f - t : t; // -0.25 .. 0.25
    const float x = 6.2831853f * t, x2 = x * x;
    return x * (1.f + x2 * (-1.f / 6.f + x2 * (1.f / 120.f + x2 * (-1.f / 5040.f + x2 * (1.f / 362880.f)))));
}

/** tan(2π t) for filter prewarping, t under 0.25. */
inline float TanCycles(float t) { return SinCycles(t) / SinCycles(t + 0.25f); }

/** Fast atan2(y, x), within 2e-7 radians. */
inline float Atan2Fast(float y, float x)
{
    const float ax = fabsf(x), ay = fabsf(y);
    const float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
    if(mx == 0.f)
        return 0.f;
    const float a = mn / mx, s = a * a;
    float       r = a * (0.99999934f + s * (-0.33329862f + s * (0.19946599f + s * (-0.13908803f + s * (0.09642664f + s * (-0.05591895f + s * (0.02186770f + s * -0.00405592f)))))));
    if(ay > ax)
        r = 1.5707963f - r;
    if(x < 0.f)
        r = 3.1415927f - r;
    return y < 0.f ? -r : r;
}

/** SoftClip's slope at `x`, inside ±3. */
inline float SoftClipSlope(float x)
{
    const float d = 27.f + 9.f * x * x;
    return ((27.f + 3.f * x * x) * d - x * (27.f + x * x) * 18.f * x) / (d * d);
}

/** TPT state-variable filter (Zavalishin, "The Art of VA Filter Design"):
 *  low-, band- and high-pass outputs. Set() is costly: call it on changes. */
class Svf
{
  public:
    void Reset() { ic1_ = ic2_ = 0.f; }

    void Set(float hz, float q, float sr)
    {
        const float nyq = 0.49f * sr;
        hz              = hz < 10.f ? 10.f : hz > nyq ? nyq : hz;
        const float g   = TanCycles(0.5f * hz / sr); // tan(π hz / sr)
        k_              = 1.f / q;
        a1_             = 1.f / (1.f + g * (g + k_));
        a2_             = g * a1_;
        a3_             = g * a2_;
    }

    /** Runs a sample; read Lp(), Bp(), Hp() after. */
    void Process(float x)
    {
        const float v3 = x - ic2_;
        const float v1 = a1_ * ic1_ + a2_ * v3;
        const float v2 = ic2_ + a2_ * ic1_ + a3_ * v3;
        ic1_           = 2.f * v1 - ic1_;
        ic2_           = 2.f * v2 - ic2_;
        lp_ = v2, bp_ = v1, hp_ = x - k_ * v1 - v2;
    }
    float Lp() const { return lp_; }
    /** Band-pass, unity gain at the centre. */
    float Bp() const { return k_ * bp_; }
    float Hp() const { return hp_; }

  private:
    float a1_ = 0.f, a2_ = 0.f, a3_ = 0.f, k_ = 1.f;
    float ic1_ = 0.f, ic2_ = 0.f;
    float lp_ = 0.f, bp_ = 0.f, hp_ = 0.f;
};

/** DC blocker: y = x - x1 + R y1. */
struct DcBlock
{
    float x1 = 0.f, y1 = 0.f;
    void  Reset() { x1 = y1 = 0.f; }
    float Process(float x, float r)
    {
        y1 = x - x1 + r * y1;
        x1 = x;
        return y1;
    }
};

} // namespace phymo
