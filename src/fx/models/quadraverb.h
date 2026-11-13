/** @file quadraverb.h
 *  @brief The reverb: a Griesinger / Dattorro plate tank.
 *
 *  Pre-delay with three early reflections a side, band-limited input,
 *  a modulated first diffuser, four input allpasses and a damped,
 *  chorused figure-eight loop. 16-bit tank memory.
 *
 *    Amount       dry / wet crossfade
 *    1 Pre-delay  23 to 175 ms
 *    2 Early      early reflections level
 *    3 Decay      tail length
 *    4 Damping    high-frequency damping
 *    CHOMPI + 4   Dark: darker loop and output, lower input high-pass
 *    Freeze       input off, infinite decay
 *
 *  The tank: Copyright 2014 Emilie Gillet, MIT license (full notice in
 *  lib/fx_engine.h).
 */
#pragma once
#include "../../dsp_util.h" // PHYMO_HOT
#include "../effect.h"
#include "../lib/fx_engine.h"
#include "../lib/bbd.h" // Lp2

namespace fx
{

/** PHYMO_REVERB: 0 full rate; 1 half rate, linear interpolation; 2 half
 *  rate, 4th-order filters (default). */
#ifndef PHYMO_REVERB
#define PHYMO_REVERB 2
#endif
static constexpr int kTankDiv = PHYMO_REVERB ? 2 : 1;

/** Tank memory, in 16-bit words. */
static constexpr size_t kTankWords = 32768 / kTankDiv;

/** The amount knob's dry / wet curve. */
inline float SendCurve(float s)
{
    s = Clamp(s, 0.f, 1.f);
    if(s < .5f)
    {
        const float u = 2.f * s;
        return .5f * u * sqrtf(u);
    }
    const float u = 2.f * (1.f - s);
    return 1.f - .5f * u * sqrtf(u);
}
inline float SinQuarter(float g) // sin(pi / 2 g), g 0 to 1
{
    if(g >= 1.f)
        return 1.f;
    const float x = 1.5707963f * g, x2 = x * x;
    return x * (1.f - x2 * (1.f / 6.f) + x2 * x2 * (1.f / 120.f));
}
inline float SendToLevel(float s) { return 1.4f * SinQuarter(SendCurve(s)); }
inline float SendToDry(float s) { return SinQuarter(1.f - SendCurve(s)); }

class Quadraverb : public Effect
{
  public:
    void SetMemory(uint16_t* tank) { tank_ = tank; }

    unsigned ParamMask() const override { return 0xF | (1u << ShiftParam(3)); }
    float    ParamDefault(int i) const override { return i == 0 || i == ShiftParam(3) ? 0.f : (i == 3 ? .4f : .5f); }

    void Reset() override
    {
        engine_.Init(tank_);
        engine_.Clear();
        engine_.SetLFOFrequency(daisysp::LFO_1, 0.5f / Half());
        engine_.SetLFOFrequency(daisysp::LFO_2, 0.3f / Half());
        lp_decay_1_ = lp_decay_2_ = lp_in_ = hp_in_ = hp_1_ = hp_2_ = 0.f;
        dark_l_.Clear(), dark_r_.Clear();
        // half-rate anti-aliasing filters
        down_a_.Clear(), down_b_.Clear(), up_l_.Clear(), up_r_.Clear(), up_l2_.Clear(), up_r2_.Clear();
        if(PHYMO_REVERB == 2) // 4th-order Butterworth
        {
            down_a_.Set(kHalfHz, sr_, .5412f), down_b_.Set(kHalfHz, sr_, 1.3066f);
            up_l_.Set(kHalfHz, sr_, .5412f), up_l2_.Set(kHalfHz, sr_, 1.3066f);
            up_r_.Set(kHalfHz, sr_, .5412f), up_r2_.Set(kHalfHz, sr_, 1.3066f);
        }
        else
            down_a_.Set(kHalfHz, sr_), up_l_.Set(kHalfHz, sr_), up_r_.Set(kHalfHz, sr_);
        odd_ = false, was_l_ = was_r_ = now_l_ = now_r_ = 0.f;
        frz_        = 0.f;
        time_       = .3f + .68f * param_[2];
        pre_        = PreDelay();
    }

    PHYMO_HOT void Process(float* l, float* r, size_t n) override
    {
        const float send = SendToLevel(amount_) * kInputGain;
        const float dry  = SendToDry(amount_);
        const float time = .3f + .68f * param_[2];
        const float pre  = PreDelay();
        er_              = 2.f * param_[1] * (1.f - frz_);
        const float dark = param_[ShiftParam(3)];
        klp_             = (1.f - .5f * param_[3] * (1.f - frz_)) * (1.f - kDarkLoop * dark * (1.f - frz_));
        dark_l_.Set(16000.f * powf(3000.f / 16000.f, dark), sr_);
        dark_r_.Set(16000.f * powf(3000.f / 16000.f, dark), sr_);
        const bool darken = dark > .01f;
        khp_in_           = Halved(.026f * powf(.25f, dark));
        klp_              = Halved(klp_);
        for(size_t i = 0; i < n; i++)
        {
            FreezeGlide(frz_, freeze_);
            // glide decay and pre-delay
            time_ += (time + (1.f - time) * frz_ - time_) * .001f;
            const float step = (pre - pre_) * .0003f;
            pre_ += step > .1f ? .1f : (step < -.1f ? -.1f : step);
            float wl = (l[i] + r[i]) * send * (1.f - frz_), wr = 0.f;
            if(PHYMO_REVERB == 0)
                Tank(&wl, &wr);
            else
            {
                // decimate by 2, run the tank, interpolate back
                wl = down_a_.Process(wl);
                if(PHYMO_REVERB == 2)
                    wl = down_b_.Process(wl);
                if(odd_)
                {
                    Tank(&wl, &wr);
                    was_l_ = now_l_, was_r_ = now_r_, now_l_ = wl, now_r_ = wr;
                    if(PHYMO_REVERB == 2)
                        wl *= 2.f, wr *= 2.f;
                }
                else if(PHYMO_REVERB == 2)
                    wl = wr = 0.f;
                else
                    wl = 0.5f * (was_l_ + now_l_), wr = 0.5f * (was_r_ + now_r_);
                odd_ = !odd_;
                wl = up_l_.Process(wl), wr = up_r_.Process(wr);
                if(PHYMO_REVERB == 2)
                    wl = up_l2_.Process(wl), wr = up_r2_.Process(wr);
            }
            if(darken)
                wl = dark_l_.Process(wl), wr = dark_r_.Process(wr);
            const float d = FrozenDry(dry, frz_);
            l[i]          = l[i] * d + wl * kReturn;
            r[i]          = r[i] * d + wr * kReturn;
        }
    }

  private:
    static constexpr float kInputGain = .18f, kReturn = 1.08f;
    static constexpr float kDarkLoop  = .55f; // loop damping at full Dark
    static constexpr float kHalfHz = PHYMO_REVERB == 2 ? 11000.f : 10500.f;
    float Half() const { return sr_ / kTankDiv; } // the tank's sample rate
    /** A full-rate one-pole coefficient, converted to the tank's rate. */
    static float Halved(float k) { return kTankDiv == 1 ? k : 1.f - (1.f - k) * (1.f - k); }
    /** Full-rate lengths in the tank's samples. */
    static constexpr int L(int n) { return (n + kTankDiv - 1) / kTankDiv; }
    static constexpr float F(float x) { return x / kTankDiv; }

    /** The pre-delay in the tank's samples: 23 to 175 ms. */
    float PreDelay() const { return Clamp((23.f + 152.f * param_[0]) * .001f * Half(), F(1100.f), F(8400.f)); }

    /** One tank sample, all wet. */
    PHYMO_HOT void Tank(float* left, float* right)
    {
        typedef E::Reserve<L(9700),
                E::Reserve<L(150),
                E::Reserve<L(214),
                E::Reserve<L(319),
                E::Reserve<L(527),
                E::Reserve<L(2182),
                E::Reserve<L(2690),
                E::Reserve<L(4501),
                E::Reserve<L(2525),
                E::Reserve<L(2197),
                E::Reserve<L(6312)> > > > > > > > > > > Memory;
        E::DelayLine<Memory, 0>  pre;
        E::DelayLine<Memory, 1>  ap1;
        E::DelayLine<Memory, 2>  ap2;
        E::DelayLine<Memory, 3>  ap3;
        E::DelayLine<Memory, 4>  ap4;
        E::DelayLine<Memory, 5>  dap1a;
        E::DelayLine<Memory, 6>  dap1b;
        E::DelayLine<Memory, 7>  del1;
        E::DelayLine<Memory, 8>  dap2a;
        E::DelayLine<Memory, 9>  dap2b;
        E::DelayLine<Memory, 10> del2;
        E::Context               c;

        const float kap = .7f; // diffusion
        constexpr float kLpIn   = kTankDiv == 1 ? .75f : .9375f;
        constexpr float kHpLoop = kTankDiv == 1 ? .006f : .011964f;
        const float klp = klp_;
        const float krt = time_;

        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;
        float wet;
        float apout = 0.0f;
        float er_l, er_r;
        engine_.Start(&c);

        // modulate the first diffuser
        c.Interpolate(ap1, F(30.0f), daisysp::LFO_1, F(25.0f), 1.0f);
        c.Write(ap1, L(100), 0.0f);

        // band-limited input into the pre-delay
        c.Read(*left + *right, 1.f);
        c.Lp(lp_in_, kLpIn);
        c.Hp(hp_in_, khp_in_); // ~200 Hz, lower with Dark
        c.Write(pre, 0.0f);

        // early reflections, three a side
        const float pd = pre_;
        c.InterpolateFrom(pre, pd, -770 / kTankDiv, .45f);
        c.InterpolateFrom(pre, pd, -450 / kTankDiv, -.3f);
        c.InterpolateFrom(pre, pd, -10 / kTankDiv, .25f);
        c.Write(er_l, 0.0f);
        c.InterpolateFrom(pre, pd, -630 / kTankDiv, .45f);
        c.InterpolateFrom(pre, pd, -270 / kTankDiv, -.3f);
        c.InterpolateFrom(pre, pd, 170 / kTankDiv, .25f);
        c.Write(er_r, 0.0f);

        // input diffusion
        c.InterpolateFrom(pre, pd, 0, 1.0f);
        c.Read(ap1 TAIL, kap);
        c.WriteAllPass(ap1, -kap);
        c.Read(ap2 TAIL, kap);
        c.WriteAllPass(ap2, -kap);
        c.Read(ap3 TAIL, kap);
        c.WriteAllPass(ap3, -kap);
        c.Read(ap4 TAIL, kap);
        c.WriteAllPass(ap4, -kap);
        c.Write(apout);

        // the loop
        c.Load(apout);
        c.Interpolate(del2, F(6190.0f), daisysp::LFO_2, F(60.0f), krt);
        c.Lp(lp_1, klp);
        // ~45 Hz high-pass in the loop, off while frozen
        hp_1_ *= 1.f - frz_;
        hp_2_ *= 1.f - frz_;
        c.Hp(hp_1_, kHpLoop * (1.f - frz_));
        c.Read(dap1a TAIL, -kap);
        c.WriteAllPass(dap1a, kap);
        c.Read(dap1b TAIL, kap);
        c.WriteAllPass(dap1b, -kap);
        c.Write(del1, 2.0f);
        c.Write(wet, 0.0f);
        *left = wet + er_l * er_;

        c.Load(apout);
        c.Interpolate(del1, F(4400.0f), daisysp::LFO_1, F(45.0f), krt);
        c.Lp(lp_2, klp);
        c.Hp(hp_2_, kHpLoop * (1.f - frz_));
        c.Read(dap2a TAIL, kap);
        c.WriteAllPass(dap2a, -kap);
        c.Read(dap2b TAIL, -kap);
        c.WriteAllPass(dap2b, kap);
        c.Write(del2, 2.0f);
        c.Write(wet, 0.0f);
        *right = wet + er_r * er_;

        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
    }

    typedef daisysp::FxEngine<kTankWords, daisysp::FORMAT_16_BIT> E;
    E         engine_;
    uint16_t* tank_ = nullptr;
    float     lp_decay_1_ = 0.f, lp_decay_2_ = 0.f, lp_in_ = 0.f, hp_in_ = 0.f, hp_1_ = 0.f, hp_2_ = 0.f;
    float     time_ = .5f, pre_ = 1100.f, er_ = 1.f, klp_ = .8f, frz_ = 0.f;
    Lp2       dark_l_, dark_r_; // Dark's output low-pass
    Lp2       down_a_, down_b_, up_l_, up_r_, up_l2_, up_r2_; // half-rate filters
    bool      odd_ = false;
    float     was_l_ = 0.f, was_r_ = 0.f, now_l_ = 0.f, now_r_ = 0.f;
    float     khp_in_ = .026f;
};

} // namespace fx
