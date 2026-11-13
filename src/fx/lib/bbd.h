/** @file bbd.h
 *  @brief A bucket-brigade delay, mono.
 *
 *  Models the clock (bandwidth falls as the delay lengthens), sample-and-hold
 *  aliasing, 2-pole anti-aliasing and reconstruction filters at 0.4 of the
 *  clock, soft saturation, and compander noise that breathes with the signal.
 *  After Holters & Parker, "A combined model for a bucket brigade device and
 *  its input and output filters" (DAFx-18), simplified.
 */
#pragma once
#include "../effect.h"
#include "../../dsp_util.h" // TanCycles

namespace fx
{

/** 2-pole low-pass (TPT state-variable filter), stable under modulation. */
struct Lp2
{
    float a1 = 0.f, a2 = 0.f, a3 = 0.f, ic1 = 0.f, ic2 = 0.f;
    void  Set(float hz, float sr, float q = .707f)
    {
        const float g = phymo::TanCycles(0.5f * Clamp(hz, 10.f, .45f * sr) / sr), k = 1.f / q; // tan(π hz / sr)
        a1            = 1.f / (1.f + g * (g + k));
        a2            = g * a1;
        a3            = g * a2;
    }
    void  Clear() { ic1 = ic2 = 0.f; }
    float Process(float x)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1            = 2.f * v1 - ic1;
        ic2            = 2.f * v2 - ic2;
        return v2;
    }
};

class Bbd
{
  public:
    /** `line`: `len` floats, longer than the longest delay; `stages`: the
     *  chip length; `max_hz`: the filters' upper limit. */
    void Init(float sr, float* line, size_t len, int stages, float max_hz, float noise_db)
    {
        sr_     = sr;
        line_   = line;
        len_    = len;
        stages_ = static_cast<float>(stages);
        max_hz_ = max_hz;
        noise_  = powf(10.f, noise_db / 20.f);
    }

    void Reset()
    {
        for(size_t i = 0; i < len_; i++)
            line_[i] = 0.f;
        write_ = 0;
        phase_ = 0.f;
        held_  = 0.f;
        env_   = 0.f;
        in_.Clear(), out_.Clear();
        rng_.Seed(0x2545F491u);
        SetClock(sr_ * .01f);
    }

    /** The filters' upper limit, Hz. Call SetClock after. */
    void SetMaxHz(float hz) { max_hz_ = hz; }

    /** Once a block: the clock and filters for a delay of `d` samples. */
    void SetClock(float d)
    {
        const float clock = stages_ / (2.f * Clamp(d, 1.f, float(len_ - 2)) / sr_);
        clock_inc_        = clock / sr_;
        const float hz    = .4f * clock < max_hz_ ? .4f * clock : max_hz_;
        in_.Set(hz, sr_);
        out_.Set(hz, sr_);
    }

    /** One sample in; the sample `d` back out. */
    float Process(float x, float d)
    {
        // input: band-limited, compander envelope, soft saturation
        x = in_.Process(x);
        const float ax = fabsf(x);
        env_ += (ax - env_) * (ax > env_ ? .01f : .0005f);
        x = SoftClip(x * kHeadroom) * (1.f / kHeadroom);
        line_[write_] = x;

        // a new sample from the buckets on each clock tick, held between
        phase_ += clock_inc_;
        if(phase_ >= 1.f)
        {
            phase_ -= phase_ >= 2.f ? phase_ : 1.f;
            held_ = Read(d);
        }
        write_ = write_ + 1 < len_ ? write_ + 1 : 0;

        // output: reconstructed, plus noise that follows the signal
        const float hiss = noise_ * (rng_.Uniform() * 2.f - 1.f) * (.15f + Clamp(env_ * 6.f, 0.f, 1.f));
        return out_.Process(held_ + hiss);
    }

  private:
    static constexpr float kHeadroom = .8f; // soft saturation starts just below full scale

    float Read(float d) const
    {
        d                 = Clamp(d, 1.f, float(len_ - 2));
        const size_t whole = static_cast<size_t>(d);
        const float  frac  = d - static_cast<float>(whole);
        const size_t a     = (write_ + len_ - whole) % len_;
        const size_t b     = (a + len_ - 1) % len_;
        return line_[a] + (line_[b] - line_[a]) * frac;
    }

    float* line_ = nullptr;
    size_t len_ = 0, write_ = 0;
    float  sr_ = 48000.f, stages_ = 4096.f, max_hz_ = 15000.f, noise_ = 0.f;
    float  clock_inc_ = 1.f, phase_ = 0.f, held_ = 0.f, env_ = 0.f;
    Lp2    in_, out_;
    Random rng_;
};

} // namespace fx
