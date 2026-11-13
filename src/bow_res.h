/** @file bow_res.h
 *  @brief The Bowed model's string: a digital waveguide split at the bow
 *  (Smith 1986), with friction after McIntyre, Schumacher & Woodhouse 1983.
 *
 *  Two delay lines run from the bow to the nut and to the bridge; the bridge
 *  end has the loss filter. Friction is a smooth function of the bow-string
 *  velocity difference (`slope` its steepness), producing Helmholtz
 *  stick-slip motion.
 *
 *  Notes start stuck (static friction) until the string breaks free, so the
 *  motion builds in a few periods. Pressure sets the grip, limited so the
 *  corner can always break it free; the bow position stays a little off the
 *  bridge. The e-bow (`magnet`) feeds back the string's own motion instead.
 *
 *  Borrows the second string's memory (kLen floats).
 */
#pragma once
#include "dsp_util.h"
#include "string_res.h"
#include <cmath>

namespace phymo
{

class BowRes
{
  public:
    static constexpr int kLen    = StringRes::kLen;
    static constexpr int kBridge = 512; // a quarter of the longest period
    static constexpr int kNeck   = kLen - kBridge;

    void Init(float sr, float* mem)
    {
        sr_  = sr;
        mem_ = mem;
        Reset();
    }

    /** Clears the neck line's last `span` samples and the bridge line. */
    void Reset(int span = kNeck)
    {
        span = span < 0 ? 0 : span > kNeck ? kNeck : span;
        for(int i = kNeck - span; i < kLen; i++) // the neck's tail, then the bridge line
            mem_[i] = 0.f;
        wn_ = wb_ = 0;
        ln_       = 100.f;
        lb_       = 20;
        k_ = lp_ = 0.f;
        g_       = 0.f;
        beta_    = 0.1f;
        break_   = kBreak / beta_;
        set_[0] = set_[1] = set_[2] = set_[3] = -1.f;
        stuck_   = true;
        held_ = slid_ = 0;
        rng_     = 0x6c8e9cf5u;
        v_       = 0.f;
        hi_ = lo_ = 0.f;
        touch_m_ = touch_to_ = 0.f, touch_d_ = 50.f, touch_sq_ = false;
        swing_k_  = 0.01f;
        dig_      = 0.f;
    }

    /** Note (Hz), T60 and highs' T60 (s), bow position 0.04 (bridge) to 0.25. */
    void Set(float freq, float t60, float t60_hi, float beta)
    {
        // unchanged since last time: skip
        if(fabsf(freq - set_[0]) < 1e-6f * freq && fabsf(t60 - set_[1]) < 1e-4f * t60
           && fabsf(t60_hi - set_[2]) < 1e-4f * t60_hi && fabsf(beta - set_[3]) < 1e-6f)
            return;
        set_[0] = freq, set_[1] = t60, set_[2] = t60_hi, set_[3] = beta;
        const Loss l = LossFilter(freq, sr_, t60, t60_hi);
        k_           = l.k;
        const float g = l.g_lo / l.gain;
        g_            = g > 0.9999f ? 0.9999f : g;
        float total   = sr_ / freq - l.delay; // both lines together
        total         = total < 8.f ? 8.f : total > kNeck + kBridge - 4.f ? kNeck + kBridge - 4.f : total;
        beta_         = beta;
        break_        = kBreak / beta;
        int b         = static_cast<int>(beta * total + 0.5f);
        b             = b < 2 ? 2 : b > kBridge - 2 ? kBridge - 2 : b;
        lb_           = b;
        float n       = total - static_cast<float>(b);
        ln_           = n > kNeck - 3.f ? kNeck - 3.f : n;
        swing_k_      = 1.f / (kSwingPeriods * total); // swing follower: about kSwingPeriods periods
    }

    /** One sample. `vb` bow speed, `contact` 0 to 1, `stick` the initial
     *  catch's break-away threshold, `slope` friction steepness, `magnet`
     *  e-bow share, `push` added at the bow, `full` the target bow speed.
     *  Returns the signal at the bridge. */
    float Process(float vb, float contact, float stick, float slope, float magnet, float push = 0.f, float full = 0.f)
    {
        // waves arriving from the nut and the bridge; a light finger touch at
        // 1/n of the string combs the neck wave to play harmonics
        touch_m_ += (touch_to_ - touch_m_) * kTouchGlide;
        float nut = Neck(ln_);
        if(touch_m_ > 1e-4f)
        {
            const float n1 = Neck(ln_ - touch_d_);
            const float t  = touch_sq_ ? 0.25f * (nut + 2.f * n1 + Neck(ln_ - 2.f * touch_d_)) : 0.5f * (nut + n1);
            nut            = (1.f - touch_m_) * nut + touch_m_ * t;
        }
        nut = -nut;
        lp_             = (1.f - k_) * mem_[kNeck + ((wb_ - lb_) & (kBridge - 1))] + k_ * lp_;
        const float br  = -g_ * lp_;
        v_              = nut + br; // string speed under the bow
        // string swing, tracked over a couple of periods
        hi_ = v_ > hi_ ? v_ : hi_ - (hi_ - v_) * swing_k_;
        lo_ = v_ < lo_ ? v_ : lo_ + (v_ - lo_) * swing_k_;
        const float dv  = vb - v_;
        const float spd = (full > fabsf(vb) ? full : fabsf(vb)) + 1e-5f;
        const float lim = break_; // the corner can always break free
        const float brk = (stick < lim ? stick : lim) * spd; // grip limit
        // the catch holds until break-away, or at most kCatchMax periods
        stuck_ = stuck_ && fabsf(dv) <= brk && ++held_ <= static_cast<int>(kCatchMax * (ln_ + static_cast<float>(lb_)));
        // a long slide re-catches, but only while the string is quiet
        if(stuck_ || fabsf(dv) < kNear * spd)
            slid_ = 0;
        else if(++slid_ > static_cast<int>(kCatchMax * (ln_ + static_cast<float>(lb_))) && contact > 0.5f)
        {
            if(hi_ - lo_ < kQuiet * spd * (1.f + kDigQuiet * dig_)) // more re-catching while digging in
                stuck_ = true, held_ = 0;
            slid_ = 0;
        }
        float grip = kStuckGrip;
        if(!stuck_)
        {
            const float a = fabsf(dv * slope) + 0.75f;
            grip          = 1.f / (a * a * a * a);
            grip          = grip > kStuckGrip ? kStuckGrip : grip < kSlipGrip ? kSlipGrip : grip;
        }
        float add        = contact * (1.f - magnet) * grip * dv + push;
        if(magnet > 0.f)
        {
            const float lvl = 0.6f * fabsf(vb) + 1e-4f; // just under the friction's level
            add += magnet * contact * lvl * (0.1f * SoftClip(v_ / lvl) + 0.01f * Noise());
        }
        // write both lines
        mem_[wn_]                = br + add;
        mem_[kNeck + wb_]        = nut + add;
        wn_                      = wn_ + 1 >= kNeck ? 0 : wn_ + 1;
        wb_                      = (wb_ + 1) & (kBridge - 1);
        return br;
    }

    /** Flageolet touch at 1/`n` of the string, amount `a`. */
    void SetTouch(int n, float a)
    {
        n         = n < 2 ? 2 : n;
        touch_d_  = (ln_ + static_cast<float>(lb_)) / static_cast<float>(n); // a period over n
        touch_d_  = touch_d_ > ln_ - 1.f ? ln_ - 1.f : touch_d_;
        touch_sq_ = n >= 3 && 2.f * touch_d_ < ln_ - 1.f;
        touch_to_ = a < 0.f ? 0.f : a > 1.f ? 1.f : a;
    }

    /** Digging in at the start (0 to 1): re-catches regardless of swing. */
    void SetDig(float dig) { dig_ = dig; }

    /** Note on: the bow starts stuck. */
    void Catch() { stuck_ = true, held_ = 0, slid_ = 0; }

    /** String speed under the bow, last sample. */
    float Speed() const { return v_; }
    /** Still in the initial catch. */
    bool Stuck() const { return stuck_; }

    // grip limits when stuck and slipping, for a steadier period
    static constexpr float kStuckGrip = 0.99f, kSlipGrip = 0.005f;
    static constexpr float kBreak = 0.9f;
    static constexpr float kCatchMax = 2.f; // maximum catch, periods; also the re-catch slide time
    static constexpr float kQuiet    = 0.5f; // re-catch only below this swing (share of bow speed)
    static constexpr float kTouchGlide = 0.002f; // touch glide per sample (~10 ms)
    static constexpr float kDigQuiet = 40.f; // extra re-catching while digging in
    static constexpr float kSwingPeriods = 2.f; // swing tracking, periods
    static constexpr float kNear     = 0.15f; // near enough to the bow speed to count as caught

  private:
    /** The neck line, `d` samples back, interpolated. */
    float Neck(float d) const
    {
        const int   i = static_cast<int>(d);
        const float f = d - static_cast<float>(i);
        int         a = wn_ - i, b = a - 1;
        a += a < 0 ? kNeck : 0;
        b += b < 0 ? kNeck : 0;
        return mem_[a] + (mem_[b] - mem_[a]) * f;
    }

    float Noise()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * 4.6566129e-10f;
    }

    float*   mem_; // kLen floats: neck line, then bridge line
    int      wn_, wb_; // write positions
    float    ln_; // neck delay, samples (fractional)
    int      lb_; // bridge delay, whole samples
    float    k_, lp_, g_; // loss filter coefficient, state, loop gain
    float    v_; // string speed under the bow
    int      held_; // samples in the catch
    int      slid_; // samples sliding since the last grip
    float    beta_; // bow position, share of the string
    float    break_; // kBreak / beta_
    float    set_[4]; // the last Set's arguments
    bool     stuck_; // in the initial catch
    float    hi_, lo_; // swing: tracked highs and lows
    float    swing_k_; // swing follower coefficient
    float    dig_;        // the bow digging in (SetDig)
    float    touch_m_, touch_to_, touch_d_; // touch amount, target, and delay
    bool     touch_sq_; // squared comb (from 1/3)
    uint32_t rng_;
    float    sr_;
};

} // namespace phymo
