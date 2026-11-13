/** @file chorus.h
 *  @brief A Juno-60-style chorus: mono or stereo in, stereo out. Off, I, II,
 *  I + II, or I | II (I and II in parallel).
 *
 *  A triangle LFO sweeps two short bucket-brigade delays, the right side's
 *  inverted. Each side is the dry plus its delayed copy.
 *
 *    mode    LFO        delay sweep     out
 *    I       0.513 Hz   1.66 - 5.35 ms  stereo
 *    II      0.863 Hz   1.66 - 5.35 ms  stereo
 *    I + II  9.75 Hz    3.3 - 3.7 ms    mono (fast vibrato)
 *    I | II  both sweeps at -3 dB each
 *
 *  Around each delay: 2-pole low-passes before and after, soft saturation
 *  going in, and a little hiss.
 */
#pragma once
#include "dsp_util.h" // PHYMO_HOT
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace phymo
{

class Chorus
{
  public:
    enum class Mode { Off, I, II, Both, Parallel };

    static constexpr int   kLen    = 512; // > 5.35 ms at 48 kHz, a power of 2
    static constexpr float kFilter = 9000.f; // BBD filter cutoff, Hz
    static constexpr float kHiss   = 0.00015f; // ~-80 dB after the filters

    void Init(float sr)
    {
        sr_ = sr;
        pre_l_.Set(kFilter, sr), pre_r_.Set(kFilter, sr);
        post_l_.Set(kFilter, sr), post_r_.Set(kFilter, sr);
        Reset();
    }

    void Reset()
    {
        for(int i = 0; i < kLen; i++)
            line_l_[i] = line_r_[i] = 0.f;
        write_ = 0;
        phase_ = phase2_ = 0.f;
        wet_   = 0.f;
        level_ = 1.f;
        rng_   = 0x2545F491u;
        pre_l_.Clear(), pre_r_.Clear(), post_l_.Clear(), post_r_.Clear();
        SetMode(Mode::Off);
    }

    void SetMode(Mode m)
    {
        mode_ = m;
        // LFO rate, and the sweep's centre and half-width in samples
        const float ms = sr_ * 0.001f;
        if(m == Mode::Both)
        {
            inc_   = 9.75f / sr_;
            mid_   = 3.5f * ms;
            depth_ = 0.2f * ms;
        }
        else
        {
            inc_   = (m == Mode::II ? 0.863f : 0.513f) / sr_;
            inc2_  = 0.863f / sr_; // the parallel mode's second sweep
            mid_   = 3.505f * ms;  // (1.66 + 5.35) / 2
            depth_ = 1.845f * ms;  // (5.35 - 1.66) / 2
        }
    }

    Mode GetMode() const { return mode_; }

    /** Chorus level, 0 (dry) to 1. */
    void SetLevel(float v) { level_ = v; }

    /** Mono `in` to stereo `l` / `r`, `n` samples. `in` may be `l`. */
    void Process(const float* in, float* l, float* r, size_t n) { Process(in, in, l, r, n); }

    /** Stereo in to stereo out, `n` samples; in place is fine. */
    PHYMO_HOT void Process(const float* inl, const float* inr, float* l, float* r, size_t n)
    {
        // on / off fades the wet over ~10 ms
        const float wet_target = mode_ == Mode::Off ? 0.f : 1.f;
        const bool  same       = mode_ == Mode::Both;
        const bool  parallel   = mode_ == Mode::Parallel;
        for(size_t i = 0; i < n; i++)
        {
            const float xl = inl[i], xr = inr[i];
            wet_ += (wet_target - wet_) * 0.002f;
            if(wet_ < 1e-4f && wet_target == 0.f)
            {
                // off: dry only
                l[i] = xl, r[i] = xr;
                Write(0.f, 0.f);
                continue;
            }

            // triangle LFO, -1 to 1
            phase_ += inc_;
            if(phase_ >= 1.f)
                phase_ -= 1.f;
            const float tri = phase_ < 0.5f ? 4.f * phase_ - 1.f : 3.f - 4.f * phase_;

            // in: filtered, soft-saturated
            Write(Saturate(pre_l_.Process(xl)), Saturate(pre_r_.Process(xr)));

            // out: right side inverted; I + II sweeps both sides together
            float wl, wr;
            if(parallel)
            {
                // I | II: both sweeps from each side's line
                phase2_ += inc2_;
                if(phase2_ >= 1.f)
                    phase2_ -= 1.f;
                const float tri2 = phase2_ < 0.5f ? 4.f * phase2_ - 1.f : 3.f - 4.f * phase2_;
                const float a = Read(line_l_, mid_ + depth_ * tri), b = Read(line_l_, mid_ + depth_ * tri2);
                const float c = Read(line_r_, mid_ - depth_ * tri), d = Read(line_r_, mid_ - depth_ * tri2);
                wl = post_l_.Process(kMinus3dB * (a + b) + Hiss());
                wr = post_r_.Process(kMinus3dB * (c + d) + Hiss());
            }
            else if(same)
            {
                const float h = Hiss();
                wl            = post_l_.Process(Read(line_l_, mid_ + depth_ * tri) + h);
                wr            = post_r_.Process(Read(line_r_, mid_ + depth_ * tri) + h);
            }
            else
            {
                wl = post_l_.Process(Read(line_l_, mid_ + depth_ * tri) + Hiss());
                wr = post_r_.Process(Read(line_r_, mid_ - depth_ * tri) + Hiss());
            }

            // dry + wet
            const float w = wet_ * level_;
            const float d = 1.f - w * (1.f - kMix);
            l[i]          = d * xl + w * kMix * wl;
            r[i]          = d * xr + w * kMix * wr;
        }
    }

  private:
    // wet level, keeping the overall level about the same on and off
    static constexpr float kMix = 0.6f;
    static constexpr float kMinus3dB = 0.70710678f; // parallel mode: each sweep's share

    /** 2-pole low-pass (TPT SVF), Q 0.707. */
    struct Lp2
    {
        float a1 = 0.f, a2 = 0.f, a3 = 0.f, ic1 = 0.f, ic2 = 0.f;
        void  Set(float hz, float sr)
        {
            const float g = TanCycles(0.5f * hz / sr), k = 1.41421356f; // tan(π hz / sr)
            a1            = 1.f / (1.f + g * (g + k));
            a2            = g * a1;
            a3            = g * a2;
        }
        void  Clear() { ic1 = ic2 = 0.f; }
        float Process(float x)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1            = 2.f * v1 - ic1;
            ic2            = 2.f * v2 - ic2;
            return v2;
        }
    };

    void Write(float l, float r)
    {
        line_l_[write_] = l;
        line_r_[write_] = r;
        write_          = (write_ + 1) & (kLen - 1);
    }

    /** `d` samples back, interpolated. */
    float Read(const float* line, float d) const
    {
        const int   whole = static_cast<int>(d);
        const float frac  = d - static_cast<float>(whole);
        const int   a     = (write_ - 1 - whole) & (kLen - 1);
        const int   b     = (a - 1) & (kLen - 1);
        return line[a] + (line[b] - line[a]) * frac;
    }

    float Hiss()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return kHiss * (static_cast<float>(rng_) * (2.f / 4294967296.f) - 1.f);
    }

    /** Soft saturation towards full scale. */
    static float Saturate(float x)
    {
        if(x > 1.5f)
            return 1.f;
        if(x < -1.5f)
            return -1.f;
        return x - (4.f / 27.f) * x * x * x; // flat at ±1.5, slope 1 at 0
    }

    float    sr_;
    Mode     mode_;
    float    inc_, phase_;    // the LFO
    float    inc2_, phase2_; // the parallel mode's second LFO
    float    mid_, depth_;    // the delay sweep, samples
    float    wet_; // 0 to 1, fading
    float    level_; // 0 to 1, chorus level
    float    line_l_[kLen], line_r_[kLen];
    int      write_;
    uint32_t rng_;
    Lp2      pre_l_, pre_r_, post_l_, post_r_;
};

} // namespace phymo
