/** @file digital_delay.h
 *  @brief A 16-bit stereo delay, up to ~2 s, with ping-pong. Fully wet.
 *
 *  The feedback path has a gentle low-pass (~14 kHz), high-pass (~40 Hz)
 *  and soft clip. Ping-pong 0 is straight stereo; 1 feeds the mono input
 *  left and crosses each repeat to the other side.
 *
 *  Time changes either crossfade between two read heads over kFade (no pitch
 *  bend) or glide one head (~50 ms), bending the pitch like tape.
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace phymo
{

class DigitalDelay
{
  public:
    static constexpr size_t kLen  = 98304; // per side: 2 s at 48 kHz, plus room
    static constexpr float  kFade = 0.08f; // time-change crossfade, s

    /** `mem`: 2 x kLen 16-bit words (SDRAM on the CHOMPI). Clears it. */
    void Init(float sr, int16_t* mem)
    {
        sr_   = sr;
        line_ = mem;
        for(size_t i = 0; i < 2 * kLen; i++)
            line_[i] = 0;
        write_ = 0;
        lp_l_ = lp_r_ = hp_l_ = hp_r_ = 0.f;
        lp_k_ = 1.f - expf(-6.2831853f * 14000.f / sr);
        hp_k_ = 1.f - expf(-6.2831853f * 40.f / sr);
        SetTime(0.25f);
        time_ = time_b_ = target_;
        fading_    = false;
        fade_      = 0.f;
        fade_inc_  = 1.f / (kFade * sr);
        glide_     = false;
        SetFeedback(0.4f);
        SetPingPong(1.f);
    }

    /** Seconds, ~10 ms to ~2 s. */
    void SetTime(float s)
    {
        const float d = s * sr_;
        target_       = d < 480.f ? 480.f : d > float(kLen - 4) ? float(kLen - 4) : d;
    }
    /** 0 to ~1.05; past 1 the repeats swell into the soft clip. */
    void SetFeedback(float f) { fb_ = f; }
    /** 0 straight stereo, 1 ping-pong. */
    void SetPingPong(float p) { pp_ = p; }
    /** false: time changes crossfade; true: they glide. */
    void SetGlide(bool g) { glide_ = g; }
    bool Glide() const { return glide_; }

    /** Adds `gain` times the delay's output to `outl` / `outr`. */
    void Process(const float* inl, const float* inr, float* outl, float* outr, size_t n, float gain)
    {
        for(size_t i = 0; i < n; i++)
        {
            float yl, yr;
            if(glide_)
            {
                fading_ = false;
                time_ += (target_ - time_) * 0.0004f; // ~50 ms
                yl = Read(0, time_), yr = Read(kLen, time_);
            }
            else
            {
                // new time and no crossfade running: start one
                if(!fading_ && fabsf(target_ - time_) > 0.5f)
                {
                    time_b_ = target_;
                    fading_ = true;
                    fade_   = 0.f;
                }
                yl = Read(0, time_), yr = Read(kLen, time_);
                if(fading_)
                {
                    // equal-power crossfade
                    fade_ += fade_inc_;
                    if(fade_ >= 1.f)
                    {
                        time_   = time_b_;
                        fading_ = false;
                        yl = Read(0, time_), yr = Read(kLen, time_);
                    }
                    else
                    {
                        const float a  = 1.5707963f * fade_;
                        const float go = cosf(a), come = sinf(a);
                        yl             = go * yl + come * Read(0, time_b_);
                        yr             = go * yr + come * Read(kLen, time_b_);
                    }
                }
            }

            // feedback tone
            lp_l_ += (yl - lp_l_) * lp_k_;
            lp_r_ += (yr - lp_r_) * lp_k_;
            hp_l_ += (lp_l_ - hp_l_) * hp_k_;
            hp_r_ += (lp_r_ - hp_r_) * hp_k_;
            const float fl = lp_l_ - hp_l_, fr = lp_r_ - hp_r_;

            // feedback: straight stereo to ping-pong
            const float mono = 0.5f * (inl[i] + inr[i]);
            const float in_l = inl[i] + pp_ * (mono - inl[i]);
            const float in_r = inr[i] * (1.f - pp_);
            const float wl   = in_l + fb_ * (fl + pp_ * (fr - fl));
            const float wr   = in_r + fb_ * (fr + pp_ * (fl - fr));
            line_[write_]        = ToWord(SoftClip(wl));
            line_[kLen + write_] = ToWord(SoftClip(wr));
            write_               = write_ + 1 < kLen ? write_ + 1 : 0;

            outl[i] += gain * yl;
            outr[i] += gain * yr;
        }
    }

  private:
    /** Side `base` (0 or kLen), `d` samples back, interpolated. */
    float Read(size_t base, float d) const
    {
        const size_t whole = static_cast<size_t>(d);
        const float  frac  = d - static_cast<float>(whole);
        const size_t a     = (write_ + kLen - whole) % kLen;
        const size_t b     = (a + kLen - 1) % kLen;
        const float  x     = line_[base + a] * (1.f / 32768.f);
        const float  y     = line_[base + b] * (1.f / 32768.f);
        return x + (y - x) * frac;
    }

    static int16_t ToWord(float x)
    {
        x = x > 1.f ? 1.f : x < -1.f ? -1.f : x;
        return static_cast<int16_t>(x * 32767.f);
    }

    /** Soft clip near full scale. */
    static float SoftClip(float x)
    {
        if(x > 3.f)
            return 1.f;
        if(x < -3.f)
            return -1.f;
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

    float    sr_;
    int16_t* line_;
    size_t   write_;
    float    time_, target_; // samples: current and target
    float    time_b_; // the second head during a crossfade
    bool     fading_, glide_;
    float    fade_, fade_inc_;
    float    fb_, pp_;
    float    lp_l_, lp_r_, hp_l_, hp_r_, lp_k_, hp_k_;
};

} // namespace phymo
