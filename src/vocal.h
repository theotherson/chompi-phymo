/** @file vocal.h
 *  @brief The Vocal model's resonator: a source-filter singer (Klatt 1980).
 *
 *  - Source: a band-limited glottal pulse (polyBLEP), tilted by Tension and
 *    Effort, plus breath noise modulated by the glottal cycle.
 *  - Tract: five cascade formant resonators across eight vowels, back to
 *    front (u U aw ah ae eh ih ee); F1-F3 from Peterson & Barney (1952).
 *  - Size shifts all formants; sung high, F1 follows the note.
 *  - Section: two extra randomised, wandering, staggered voices.
 *  - Onset: a consonant attack crossfading into the vowel.
 *  - Level: a quick rise, settling to the hold level over the fall time;
 *    released, falling over the release time.
 */
#pragma once
#include "dsp_util.h"
#include <cmath>
#include <cstdint>

namespace phymo
{

static constexpr int kVowels   = 8;
static constexpr int kFormants = 5;

/** Vowel formants, Hz: u, U, aw, ah, ae, eh, ih, ee (back to front, so
 *  neighbours morph well). F1-F3 from Peterson & Barney. */
static const float kVowelHz[kVowels][kFormants] = {
    {300.f, 870.f, 2240.f, 3400.f, 4400.f},  // u (boot)
    {440.f, 1020.f, 2240.f, 3400.f, 4400.f}, // U (book)
    {570.f, 840.f, 2410.f, 3500.f, 4500.f},  // aw (bought)
    {730.f, 1090.f, 2440.f, 3500.f, 4500.f}, // ah (father)
    {660.f, 1720.f, 2410.f, 3500.f, 4500.f}, // ae (bat)
    {530.f, 1840.f, 2480.f, 3500.f, 4500.f}, // eh (bet)
    {390.f, 1990.f, 2550.f, 3600.f, 4600.f}, // ih (bit)
    {270.f, 2290.f, 3010.f, 3700.f, 4700.f}, // ee (beet)
};
/** Formant bandwidths, Hz. */
static const float kFormantBw[kFormants] = {70.f, 90.f, 130.f, 220.f, 280.f};

/** Voice settings, each UpdateMod. */
struct VocalSettings
{
    float freq    = 220.f; // Hz
    float vowel   = 3.f;   // 0 to kVowels - 1
    float size    = 0.f; // -0.5 (bass) to 0.5 (child): formants ±0.3 octave
    float tension = 0.5f;  // 0 lax to 1 pressed
    float breath  = 0.1f;  // 0 to 1
    float effort  = 0.7f; // 0 to 1: louder and brighter
    float release = 0.5f; // release T60, s
    float section = 0.f; // 0 one voice to 1: two more, detuned and wandering
    float hold    = 1.f; // held level after the attack (1: no fall)
    float fall    = 4.f; // time to reach it (T60), s
    float bright  = 0.f; // -0.5 to 0.5: all formants shift (kBrightShift)
    bool  gate    = false;
};

class VocalRes
{
  public:
    static constexpr float kLevel  = 0.11f; // output level (vocal_test)
    static constexpr float kOn     = 0.004f; // gate rise, s
    // section: per strike, two voices at random within ±kSecCents, each
    // wandering (kSecWander of the range) and entering after a random delay
    static constexpr float kSecCents = 12.f, kSecWander = 0.35f;
    static constexpr float kSecStagger = 0.025f, kSecIn = 0.012f; // entry spread and fade-in, s
    // onset: the consonant's layer (burst, aspiration, frication, murmur)
    // holds, then crossfades (equal power) into the ducked vowel
    static constexpr float kBurstLevel = 3.5f; // level at 1
    static constexpr float kFricLevel  = 2.2f; // level at 1 (vocal_test)
    static constexpr float kAspLevel   = 1.4f; // level at 1, into the tract
    static constexpr float kMurmLevel  = 1.f; // level at 1
    static constexpr float kMurmHz     = 350.f; // murmur low-pass, Hz
    // formant transitions: F1 from closed, F2 and F3 from towards the
    // consonant's loci, into the vowel
    static constexpr float kF1Closed = 250.f, kLocusPull = 0.75f;
    // brightness: all formants up or down, ±0.15 octave
    static constexpr float kBrightShift = 0.3f;

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        ph_ = 0.f, inc_ = 0.f;
        ph2_ = 0.37f, ph3_ = 0.71f, inc2_ = inc3_ = 0.f, sec_ = 0.f, sec_norm_ = 1.f; // out of phase with the first
        for(int i = 0; i < 2; i++)
            at_[i] = 0.f, wan_[i] = aim_[i] = 0.f, wan_left_[i] = 0, wait_[i] = 0, in_[i] = 1.f;
        in_k_ = 1.f;
        tilt_ = tilt_k_ = 0.f;
        hp_ = 0.f;
        amp_ = 0.f, amp_k_ = 0.f, amp_to_ = 0.f, fall_ = 1.f;
        hold_ = 1.f, hold_k_ = 0.f, settled_ = false;
        e_burst_ = k_burst_ = 0.f, c_asp_ = c_fric_ = c_murm_ = 0.f, murm_lp_ = 0.f;
        x_hold_ = 0, x_pos_ = 0.f, x_step_ = 0.f, x_c_ = 0.f, x_v_ = 1.f;
        murm_k_ = 1.f - Exp2Fast(-1.4426950f * 6.2831853f * kMurmHz / sr_);
        burst_bp_.Reset(), fric_bp_.Reset();
        breath_ = 0.f, voiced_ = 1.f, gain_ = 0.f;
        rng_ = 0x6d2b79f5u;
        for(int k = 0; k < kFormants; k++)
            y1_[k] = y2_[k] = 0.f, a_[k] = 1.f, b_[k] = c_[k] = 0.f;
    }

    /** Formant offset, octaves (per singer). */
    void SetScatter(float octaves) { scatter_ = octaves; }

    /** Note and level every UpdateMod; tilt, mix and formants when `full`. */
    PHYMO_ONCE void Set(const VocalSettings& s, bool full)
    {
        inc_ = s.freq / sr_;
        // section: two more voices, wandering slowly about their places
        sec_ = s.section;
        if(sec_ > 0.f)
        {
            const float d = kSecCents * sec_;
            for(int i = 0; i < 2; i++) // a new aim now and then, glided to
            {
                if((wan_left_[i] -= 16) <= 0)
                    aim_[i] = Rand(), wan_left_[i] = static_cast<int>((0.8f + 0.4f * Rand()) * sr_);
                wan_[i] += (aim_[i] - wan_[i]) * 0.004f; // ~0.25 s
            }
            inc2_     = inc_ * Exp2Fast(d * (at_[0] + kSecWander * wan_[0]) / 1200.f);
            inc3_     = inc_ * Exp2Fast(d * (at_[1] + kSecWander * wan_[1]) / 1200.f);
            sec_norm_ = 1.f / sqrtf(1.f + 2.f * sec_ * sec_); // keep three as loud as one
        }
        // level: quick rise on the gate; released, 60 dB over the release
        amp_to_ = s.gate ? 1.f : 0.f;
        amp_k_  = 1.f - Exp2Fast(-1.4426950f / (kOn * sr_));
        hold_   = s.hold < 0.f ? 0.f : s.hold > 1.f ? 1.f : s.hold;
        hold_k_ = 1.f - Exp2Fast(-9.9657843f / ((s.fall > 0.01f ? s.fall : 0.01f) * sr_));
        fall_   = Exp2Fast(-9.9657843f / ((s.release > 0.01f ? s.release : 0.01f) * sr_));
        tr_age_ += 16; // UpdateMod's samples
        const bool moving = tr_len_ > 0 && tr_age_ < tr_start_ + tr_len_ + 16;
        if(!full && !moving) // formants update every Set during a transition
            return;
        // source tilt: 400 Hz (lax, quiet) to 8 kHz (pressed, loud)
        const float bright = Clamp01(0.65f * s.tension + 0.35f * s.effort);
        const float fc     = 400.f * Exp2Fast(4.3f * bright);
        tilt_k_            = 1.f - Exp2Fast(-1.4426950f * 6.2831853f * fc / sr_);
        breath_            = 0.6f * s.breath;
        voiced_            = 1.f - 0.7f * s.breath * s.breath;
        gain_              = kLevel * (0.25f + 0.75f * s.effort);
        // formants between two vowels, in octaves; size; F1 follows a high note
        float v = s.vowel < 0.f ? 0.f : s.vowel > kVowels - 1 ? kVowels - 1 : s.vowel;
        int   a = static_cast<int>(v);
        a       = a > kVowels - 2 ? kVowels - 2 : a;
        const float f     = v - static_cast<float>(a);
        const float shift = Exp2Fast(0.6f * s.size + kBrightShift * s.bright + scatter_);
        const float top   = 0.45f * sr_;
        for(int k = 0; k < kFormants; k++)
        {
            float hz = kVowelHz[a][k] * Exp2Fast(f * Log2Fast(kVowelHz[a + 1][k] / kVowelHz[a][k])) * shift;
            if(k == 0 && hz < 1.1f * s.freq)
                hz = 1.1f * s.freq; // sung above F1: follow the note
            if(moving && k < 3) // consonant transition into the vowel
            {
                float e = static_cast<float>(tr_age_ - tr_start_) / static_cast<float>(tr_len_);
                e       = e < 0.f ? 0.f : e > 1.f ? 1.f : e;
                e       = e * e * (3.f - 2.f * e);
                const float from = k == 0 ? (tr_f1_ < hz ? tr_f1_ : hz) // F1 opening from closed
                                          : hz * Exp2Fast(kLocusPull * Log2Fast((k == 1 ? tr_f2_ : tr_f3_) / hz)); // F2, F3 from towards the locus
                hz = from * Exp2Fast(e * Log2Fast(hz / from));
            }
            hz             = hz > top ? top : hz;
            hz_[k]         = hz;
            const float bw = kFormantBw[k] * (k == 0 && hz > 600.f ? hz / 600.f : 1.f); // wider when raised
            const float r  = Exp2Fast(-1.4426950f * 3.1415927f * bw / sr_);
            c_[k]          = -r * r;
            b_[k]          = 2.f * r * SinCycles(hz / sr_ + 0.25f);
            a_[k]          = 1.f - b_[k] - c_[k];
        }
    }

    /** One sample. */
    float Process()
    {
        // level: rise, then settle to the hold; released, fall
        if(amp_to_ > 0.f && (!settled_ || hold_ >= 1.f))
        {
            amp_ += (1.f - amp_) * amp_k_;
            settled_ = amp_ > 0.995f;
        }
        else if(amp_to_ > 0.f)
            amp_ += (hold_ - amp_) * hold_k_;
        else
            amp_ *= fall_;
        // glottal pulse: a falling ramp, polyBLEP at the step
        ph_ += inc_;
        ph_ -= ph_ >= 1.f ? 1.f : 0.f;
        float g = 1.f - 2.f * ph_;
        g += Blep(ph_, inc_);
        if(sec_ > 0.f) // section voices into the same tract
        {
            ph2_ += inc2_, ph2_ -= ph2_ >= 1.f ? 1.f : 0.f;
            ph3_ += inc3_, ph3_ -= ph3_ >= 1.f ? 1.f : 0.f;
            const float g2 = 1.f - 2.f * ph2_ + Blep(ph2_, inc2_);
            const float g3 = 1.f - 2.f * ph3_ + Blep(ph3_, inc3_);
            for(int i = 0; i < 2; i++) // each enters after its wait, faded in
                if(wait_[i] > 0)
                    wait_[i]--;
                else
                    in_[i] += (1.f - in_[i]) * in_k_;
            g = (g + sec_ * (in_[0] * g2 + in_[1] * g3)) * sec_norm_;
        }
        tilt_ += (g - tilt_) * tilt_k_;
        // breath: high-passed noise, more while the folds are open
        rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
        const float w = static_cast<float>(static_cast<int32_t>(rng_)) * 4.6566129e-10f;
        hp_ += (w - hp_) * 0.25f;
        const float open = ph_ < 0.6f ? 1.f : 0.35f;
        // level applied before the tract; the onset bypasses it
        float x    = amp_ * (voiced_ * tilt_ + breath_ * open * (w - hp_));
        float post = 0.f; // the onset's post-tract part
        if(onset_)
            x = Onset(w, open, post);
        // the tract: cascade resonators
        for(int k = 0; k < kFormants; k++)
        {
            const float y = a_[k] * x + b_[k] * y1_[k] + c_[k] * y2_[k];
            y2_[k] = y1_[k], y1_[k] = y;
            x = y;
        }
        return gain_ * (x + post);
    }

    /** Note on: back to full, then the hold. `at` 0 (none) to 1 along kCons
     *  (none, d, k, h), blending between neighbours. */
    PHYMO_ONCE void Strike(float at = 0.f)
    {
        settled_ = false;
        // section voices: new positions and entries (from quiet only)
        const bool quiet = amp_ < 0.01f;
        for(int i = 0; i < 2; i++)
        {
            at_[i] = Rand(), wan_[i] = aim_[i] = 0.f, wan_left_[i] = 0;
            if(quiet)
                wait_[i] = static_cast<int>((0.5f + 0.5f * Rand()) * kSecStagger * sr_), in_[i] = 0.f;
        }
        in_k_ = 1.f - Exp2Fast(-1.4426950f / (kSecIn * sr_));
        at       = at < 0.f ? 0.f : at > 1.f ? 1.f : at;
        const float p = at * static_cast<float>(kConsN - 1);
        int         a = static_cast<int>(p);
        a             = a > kConsN - 2 ? kConsN - 2 : a;
        const float f = p - static_cast<float>(a);
        auto mix = [&](float Cons::*m) { return kCons[a].*m + (kCons[a + 1].*m - kCons[a].*m) * f; };
        const float burst = mix(&Cons::burst), asp = mix(&Cons::asp), fric = mix(&Cons::fric), murm = mix(&Cons::murm);
        const float hold = mix(&Cons::hold), xfade = mix(&Cons::xfade);
        onset_ = xfade > 0.f;
        tr_len_ = 0;
        if(!onset_)
            return;
        auto fade = [&](float ms) { return Exp2Fast(-1.4426950f / ((ms > 0.5f ? ms : 0.5f) * 0.001f * sr_)); };
        e_burst_ = kBurstLevel * burst, k_burst_ = fade(0.5f * mix(&Cons::burst_ms));
        c_asp_ = kAspLevel * asp, c_fric_ = kFricLevel * fric, c_murm_ = kMurmLevel * murm;
        // crossfade: consonant held, then into the vowel
        x_hold_ = static_cast<int>(hold * 0.001f * sr_), x_pos_ = 0.f, x_step_ = 1.f / (xfade * 0.001f * sr_);
        x_c_ = 1.f, x_v_ = 0.f;
        // transition from the loci into the vowel
        tr_f2_ = mix(&Cons::f2), tr_f3_ = mix(&Cons::f3), tr_f1_ = kF1Closed;
        tr_age_ = 0, tr_start_ = x_hold_;
        tr_len_ = tr_f2_ > 0.f ? static_cast<int>(xfade * 0.001f * sr_) : 0;
        burst_bp_.Set(mix(&Cons::burst_hz), mix(&Cons::burst_q), sr_);
        fric_bp_.Set(mix(&Cons::fric_hz), mix(&Cons::fric_q), sr_);
    }

    /** Formant `k`'s frequency, Hz. */
    float FormantHz(int k) const { return hz_[k]; }

    /** Sounding or still falling. */
    bool Sounding() const { return amp_to_ > 0.f || amp_ > 1e-4f; }

    /** A consonant: burst (level, band Hz, Q, ms), aspiration, frication
     *  (level, band Hz, Q), murmur, hold and crossfade times (ms), and F2/F3
     *  loci (Hz, 0 none). */
    struct Cons
    {
        float burst, burst_hz, burst_q, burst_ms;
        float asp, fric, fric_hz, fric_q, murm;
        float hold, xfade;
        float f2, f3;
    };
    static constexpr int kConsN = 4; // none, d, k, h

  private:
    static constexpr Cons kCons[kConsN] = {
        // burst                      asp   fric  band            murm   hold   xfade  loci
        {0.f, 1000.f, 1.f, 5.f, 0.f, 0.f, 3000.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f}, // none
        {1.8f, 3600.f, 1.2f, 6.f, 0.15f, 0.f, 4000.f, 1.f, 0.8f, 12.f, 35.f, 1800.f, 2700.f}, // d: quick, voiced
        {2.4f, 2100.f, 1.6f, 14.f, 5.f, 0.f, 2000.f, 1.f, 0.f, 45.f, 55.f, 2100.f, 2400.f}, // k: harder, aspirated
        {0.f, 2000.f, 1.f, 5.f, 1.f, 0.f, 2000.f, 1.f, 0.f, 60.f, 80.f, 0.f, 0.f}, // h: breath
    };

    /** Onset sample: the consonant layer crossfading into the ducked vowel
     *  source `x`; `post` gets the frication (after the tract). */
    float Onset(float w, float open, float& post)
    {
        const float n = w - hp_;
        burst_bp_.Process(w), fric_bp_.Process(w);
        const float v = voiced_ * tilt_;
        murm_lp_ += (v - murm_lp_) * murm_k_; // muffled voice
        const float c = x_c_;
        const float x = amp_ * (x_v_ * v + breath_ * open * n) + c * (c_asp_ * n + c_murm_ * amp_ * murm_lp_) + e_burst_ * burst_bp_.Bp();
        post          = c * c_fric_ * fric_bp_.Bp();
        e_burst_ *= k_burst_;
        if(x_hold_ > 0)
            x_hold_--;
        else if(x_pos_ < 1.f)
        {
            x_pos_ += x_step_;
            const float q = x_pos_ > 1.f ? 1.f : x_pos_;
            x_c_ = SinCycles(0.25f + 0.25f * q), x_v_ = SinCycles(0.25f * q); // equal power: cos and sin
        }
        onset_ = x_pos_ < 1.f || e_burst_ > 1e-4f;
        if(!onset_)
            x_c_ = 0.f, x_v_ = 1.f;
        return x;
    }

    /** polyBLEP correction at the ramp's step, `t` phase, `dt` increment. */
    static float Blep(float t, float dt)
    {
        if(t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.f;
        }
        if(t > 1.f - dt)
        {
            t = (t - 1.f) / dt;
            return t * t + t + t + 1.f;
        }
        return 0.f;
    }
    static float Clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }
    /** -1 to 1, uniform. */
    float Rand()
    {
        rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * 4.6566129e-10f;
    }

    float    sr_;
    float    ph_, inc_; // glottal phase and increment
    float    ph2_, ph3_, inc2_, inc3_; // section voices'
    float    sec_, sec_norm_; // section amount and level correction
    float    at_[2], wan_[2], aim_[2], in_[2], in_k_; // each: position (±1), wander, aim, level, fade-in rate
    int      wan_left_[2], wait_[2]; // each: samples to the next aim, and to entry
    float    tilt_, tilt_k_; // source tilt one-pole
    float    hp_; // breath high-pass state
    float    amp_, amp_k_, amp_to_, fall_; // level, rise rate, target, release fall
    float    hold_, hold_k_; // hold level and settle rate
    bool     onset_ = false; // onset sounding
    float    e_burst_, k_burst_; // burst level and decay
    float    c_asp_, c_fric_, c_murm_; // aspiration, frication, murmur levels
    float    murm_lp_, murm_k_; // murmur low-pass
    int      x_hold_; // crossfade: hold samples
    float    x_pos_, x_step_, x_c_, x_v_; // position, step, consonant and vowel gains
    float    tr_f1_, tr_f2_, tr_f3_; // transition start: F1, F2 and F3 loci
    int      tr_age_ = 0, tr_start_ = 0, tr_len_ = 0; // samples since the strike, transition start and length
    float    hz_[kFormants] = {}; // formant frequencies
    Svf      burst_bp_, fric_bp_; // burst and frication band-passes
    bool     settled_; // risen to full since the strike
    float    breath_, voiced_, gain_; // mix levels
    float    scatter_ = 0.f; // formant offset, octaves
    uint32_t rng_;
    float    y1_[kFormants], y2_[kFormants]; // resonator state
    float    a_[kFormants], b_[kFormants], c_[kFormants];
};

// out-of-class definition (C++14)
constexpr VocalRes::Cons VocalRes::kCons[VocalRes::kConsN];

} // namespace phymo
