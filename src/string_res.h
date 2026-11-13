/** @file string_res.h
 *  @brief A string: a one-period delay line with a loss filter in its loop
 *  (Karplus & Strong 1983; Jaffe & Smith 1983; loss filter after Välimäki
 *  et al. 1996).
 *
 *    x[n] = in[n] + g · (LP(AP(x[n − i])) − k · MA(x)), soft-clipped
 *
 *  - LP, the loss filter, is set by two decay times (the fundamental's and
 *    the 8th harmonic's), so the timbre holds across the keyboard.
 *  - MA, the loop's mean over exactly one period, is zero at every harmonic;
 *    subtracting a share of it each trip removes DC without the low-frequency
 *    wobble a high-pass in the loop would cause. Its sum leaks slightly.
 *  - AP, a first-order allpass, tunes the fractional period.
 *  - Tuning accounts for the phase delays of AP, LP, MA and the stiffness
 *    allpasses at the note; g gives the fundamental its T60.
 *  - Stiffness: four first-order allpasses (Van Duyne & Smith 1994) stretch
 *    the upper partials sharp, scaled to the note.
 *  - The soft clip keeps hard strikes in check.
 *
 *  Set() is costly, so voices call it every few samples. Follow() sets a
 *  detuned second string from the first's filters.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>

namespace phymo
{

/** The loss filter LP, y = (1 - k) x + k y1: its coefficient, phase delay
 *  and gain at the note, and the loop gain for the fundamental's T60. */
struct Loss
{
    float k, delay, gain, g_lo;
};
/** LP's maximum coefficient while held (~75 Hz cutoff). */
static constexpr float kLossMax = 0.99f;
/** LP's maximum once released (~800 Hz cutoff), so the lows fade with the note. */
static constexpr float kDampLossMax = 0.9f;

inline Loss LossFilter(float freq, float sr, float t60, float t60_hi, float k_max = kLossMax)
{
    static constexpr float kHiHarm = 8.f; // the harmonic `t60_hi` applies to
    t60          = t60 < 0.01f ? 0.01f : t60;
    t60_hi       = t60_hi < 0.001f ? 0.001f : t60_hi > t60 ? t60 : t60_hi;
    const float cyc = freq / sr, w = 6.2831853f * cyc;
    const float c1 = SinCycles(cyc + 0.25f), s1 = SinCycles(cyc);
    // (1 - k) / (1 - k e^-jw): gain 1 at DC, r at the high harmonic; solve for k
    Loss        l;
    const float trips = freq; // trips round the loop a second
    l.g_lo            = Exp2Fast(-9.9657843f / (trips * t60)); // 0.001 ^ (1 / trips)
    const float g_hi  = Exp2Fast(-9.9657843f / (trips * t60_hi));
    const float r2    = (g_hi / l.g_lo) * (g_hi / l.g_lo);
    float       wh    = kHiHarm * w;
    wh                = wh > 3.1415927f ? 3.1415927f : wh;
    if(r2 > 0.99999f)
        l.k = 0.f;
    else
    {
        const float b = 1.f - r2 * SinCycles(wh * 0.15915494f + 0.25f), q = 1.f - r2;
        l.k           = (b - sqrtf(b * b - q * q)) / q;
        l.k           = l.k < k_max ? l.k : k_max;
    }
    const float lre = 1.f - l.k * c1, lim = l.k * s1;
    l.delay         = Atan2Fast(lim, lre) / w;
    l.gain          = (1.f - l.k) / sqrtf(lre * lre + lim * lim);
    return l;
}

class StringRes
{
  public:
    static constexpr int   kLen  = 2048; // longest period: ~23 Hz at 48 kHz
    static constexpr float kMaxG = 0.9999f; // maximum loop gain
    static constexpr float kMa     = 0.25f; // DC removed per trip
    static constexpr float kMaLeak = 1.f - 1.f / 65536.f; // running sum leak per sample (~1.4 s)
    static constexpr float kApLo = 0.618f; // the allpass covers kApLo to kApLo + 1 samples
    static constexpr int   kDisp = 4; // stiffness allpasses
    static constexpr float kDispHarm  = 3.f; // at full stiffness, the bend is around this harmonic
    static constexpr float kDispRange = 4.f; // octaves the bend moves up as stiffness falls

    /** The delay line's memory, kLen floats; set before Init. */
    void SetMemory(float* m) { buf_ = m; }

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    /** Clears the line's last `span` samples (all by default). */
    void Reset(int span = kLen)
    {
        span = span < 0 ? 0 : span > kLen ? kLen : span;
        for(int i = kLen - span; i < kLen; i++)
            buf_[i] = 0.f;
        w_ = 0;
        i_ = 100;
        c_ = 0.f;
        ap_ = 0.f;
        k_ = lp_ = 0.f;
        ma_sum_ = ma_last_ = 0.f, ma_n_ = 0, ma_frac_ = 0.f, ma_inv_ = 1.f, ma_out_ = 1.f;
        g_ = 0.f;
        fb_ = 0.f;
        disp_a_ = 0.f;
        fixed_  = 0.f;
        freq_   = 440.f;
        set_[0] = set_[1] = set_[2] = set_[3] = set_[4] = -1.f; // force the next Set to recompute
        for(int k = 0; k < kDisp; k++)
            disp_x_[k] = disp_y_[k] = 0.f;
    }

    static constexpr float kHiHarm = 8.f; // the harmonic `t60_hi` applies to

    /** Note (Hz), T60 (s), the highs' T60 (s), stiffness (0 to 1), and LP's
     *  maximum coefficient. */
    PHYMO_HOT void Set(float freq, float t60, float t60_hi, float stiff = 0.f, float k_max = kLossMax)
    {
        // unchanged since last time: skip
        if(fabsf(freq - set_[0]) < 1e-6f * freq && fabsf(t60 - set_[1]) < 1e-4f * t60
           && fabsf(t60_hi - set_[2]) < 1e-4f * t60_hi && fabsf(stiff - set_[3]) < 1e-5f && k_max == set_[4])
            return;
        set_[0] = freq, set_[1] = t60, set_[2] = t60_hi, set_[3] = stiff, set_[4] = k_max;
        const float cyc = freq / sr_; // cycles per sample
        const float w   = 6.2831853f * cyc;
        const float c1 = SinCycles(cyc + 0.25f), s1 = SinCycles(cyc);
        const Loss  loss = LossFilter(freq, sr_, t60, t60_hi, k_max);
        k_                   = loss.k;
        const float g_lo     = loss.g_lo;
        const float lp_delay = loss.delay;
        const float lp_gain  = loss.gain;

        // stiffness allpasses (a + z^-1) / (1 + a z^-1), limited to fit the period
        const float period = sr_ / freq;
        const float slack  = stiff > 0.f ? 1.f - stiff : 1.f;
        const float bend   = w * kDispHarm * Exp2Fast(kDispRange * slack * slack); // 1 + a
        float       a      = stiff > 0.f && bend < 1.f ? bend - 1.f : 0.f, disp_delay = 0.f;
        for(int tries = 0; tries < 8; tries++)
        {
            const float ph = Atan2Fast(-s1, a + c1) - Atan2Fast(-a * s1, 1.f + a * c1); // one allpass's phase
            disp_delay     = -static_cast<float>(kDisp) * ph / w;
            if(disp_delay < period - lp_delay - 3.f)
                break;
            a *= 0.6f;
        }
        disp_a_ = a;

        // MA's response at the note: phase into the tuning, gain into the loop
        const int   n  = period < 1.f ? 1 : period > kLen - 4.f ? kLen - 4 : static_cast<int>(period);
        const float f  = period - static_cast<float>(n);
        const float m1 = SinCycles(0.5f * static_cast<float>(n) * cyc) / (SinCycles(0.5f * cyc) * period);
        const float a1 = cyc * (period - 0.5f * static_cast<float>(n + 3));
        const float m2 = f / period, a2 = cyc * (f - 2.f);
        const float ma_re = m1 * SinCycles(a1 + 0.25f) + m2 * SinCycles(a2 + 0.25f);
        const float ma_im = m1 * SinCycles(a1) + m2 * SinCycles(a2);
        const float lp_g = lp_gain > 1e-6f ? lp_gain : 1e-6f; // guard against LP's gain reaching 0
        const float z_re = 1.f - kMa * ma_re / lp_g, z_im = -kMa * ma_im / lp_g;
        const float ma_delay = -Atan2Fast(z_im, z_re) / w;
        const float ma_gain  = sqrtf(z_re * z_re + z_im * z_im);

        // integer delay, and the allpass for the rest
        fixed_ = lp_delay + disp_delay + ma_delay;
        freq_  = freq;
        SetDelay(cyc);

        const float g = g_lo / (lp_gain * ma_gain); // make up LP's and MA's loss at the note
        g_      = g > kMaxG ? kMaxG : g;
    }

    /** A second string at `ratio` times `first`'s pitch, sharing its filters.
     *  Call after `first`'s Set. */
    void Follow(const StringRes& first, float ratio)
    {
        set_[0] = -1.f; // force the next Set to recompute
        k_      = first.k_;
        g_      = first.g_;
        disp_a_ = first.disp_a_;
        fixed_  = first.fixed_;
        freq_   = first.freq_ * ratio;
        SetDelay(freq_ / sr_);
    }

    float Process(float in)
    {
        const float fb = Next();
        Write(in);
        return fb;
    }

    /** Two-step processing for the bow: Next() returns the string's output,
     *  then Write() adds the input. */
    float Next()
    {
        // allpass: c · x[n-i] + x[n-i-1] − c · y[n-1]
        const float x  = Tap(i_);
        float       ap = c_ * x + Tap(i_ + 1) - c_ * ap_;
        ap_            = ap;
        for(int k = 0; k < kDisp; k++) // stiffness
        {
            const float y = disp_a_ * ap + disp_x_[k] - disp_a_ * disp_y_[k];
            disp_x_[k] = ap, disp_y_[k] = y, ap = y;
        }
        lp_            = (1.f - k_) * ap + k_ * lp_;
        const float ma = (ma_sum_ + ma_frac_ * Tap(ma_n_ + 2)) * ma_inv_; // the loop's mean over a period, a sample back
        fb_ = SoftClip(g_ * (lp_ - kMa * ma));
        return fb_;
    }
    void Write(float in)
    {
        const float x = in + fb_;
        buf_[w_]      = x;
        w_            = (w_ + 1) & (kLen - 1);
        ma_sum_       = kMaLeak * ma_sum_ + ma_last_ - ma_out_ * Tap(ma_n_ + 2); // running sum: newest in, oldest out, leaked
        ma_last_      = x;
    }

    /** The loop's output `d` samples back, interpolated (for the pickup). */
    float Past(float d) const
    {
        const int   i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        const float a = Tap(i + 1), b = Tap(i + 2);
        return a + (b - a) * f;
    }

    /** The delay line's memory (the Bowed model borrows the second string's). */
    float* Memory() { return buf_; }

  private:
    float Tap(int delay) const { return buf_[(w_ - delay) & (kLen - 1)]; }

    /** Integer delay and allpass for the rest of the period: c =
     *  sin(w(1-d)/2) / sin(w(1+d)/2) gives a phase delay of d at w. */
    void SetDelay(float cyc)
    {
        // MA's window: one period, its oldest sample fractional
        const float p = 1.f / cyc;
        int         n = static_cast<int>(p);
        n             = n < 1 ? 1 : n > kLen - 4 ? kLen - 4 : n;
        if(ma_n_ == 0) // cleared: start the sum at once
            ma_n_ = n;
        for(; ma_n_ < n; ma_n_++)
            ma_sum_ += Tap(ma_n_ + 2);
        for(; ma_n_ > n; ma_n_--)
            ma_sum_ -= Tap(ma_n_ + 1);
        ma_frac_ = p - static_cast<float>(n);
        ma_frac_ = ma_frac_ > 1.f ? 1.f : ma_frac_;
        ma_inv_  = 1.f / (static_cast<float>(n) + ma_frac_);
        ma_out_  = Exp2Fast(static_cast<float>(n) * -2.2014e-5f); // kMaLeak ^ n

        float rest = 1.f / cyc - fixed_;
        rest       = rest < 2.f ? 2.f : rest > kLen - 3.f ? kLen - 3.f : rest;
        i_         = static_cast<int>(rest - kApLo);
        const float d = rest - static_cast<float>(i_);
        c_            = SinCycles(0.5f * cyc * (1.f - d)) / SinCycles(0.5f * cyc * (1.f + d));
    }

    float* buf_ = nullptr; // kLen floats
    float set_[5]; // the last Set's arguments
    int   w_; // write position
    int   i_; // integer delay, samples
    float c_, ap_; // allpass coefficient and last output
    float k_, lp_; // loss filter coefficient and state
    float ma_sum_; // MA's running sum (leaking)
    float ma_last_; // newest sample, not yet in the sum
    int   ma_n_; // MA window, whole samples (0: cleared)
    float ma_frac_; // fraction of the oldest sample
    float ma_inv_; // 1 / window
    float ma_out_; // kMaLeak ^ ma_n_
    float disp_a_; // stiffness allpass coefficient
    float disp_x_[kDisp], disp_y_[kDisp];
    float g_; // loop gain
    float fb_; // Next()'s output, for Write()
    float fixed_; // filter delays at the note, samples
    float freq_; // Hz
    float sr_;
};

} // namespace phymo
