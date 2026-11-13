/** @file voice.h
 *  @brief One voice: an exciter strikes a resonator, heard through a pickup.
 *  The resonator is the model's: a string, a bank of partials (Spectral),
 *  FM, a bowed string, or a singer (Vocal).
 *
 *  A voice keeps the model and pickup it was struck with until its next
 *  note, so ringing notes aren't cut when either changes.
 *
 *  - Morph: each note's spectrum starts `morph` along knob 1's range and
 *    glides back over the morph time.
 *  - The note rings and decays on its own; releasing brings the damper
 *    down unless hold is on. The ENV envelope is a modulation source only.
 *  - Modulation: the LFO and envelope each reach six destinations (Dest),
 *    depth -1 to 1, scaled by kLfoScale / kEnvScale.
 *  - Brightness: the highs' decay against the fundamental's (128x shorter
 *    at 0); the strike's transient opens it for the attack.
 *  - Key tracking: at 1, the decay halves each octave above middle C.
 *  - Velocity scales the strike's strength; Life randomises each strike.
 *  - Course (String): a detuned second string, or one tuned to an interval.
 *
 *  Shared settings arrive each block in VoiceParams; NoteOn requests a
 *  strike, which the next Process makes.
 */
#pragma once
#include "body.h"
#include "bow_res.h"
#include "env.h"
#include "exciter.h"
#include "lfo.h"
#include "fm_core.h"
#include "modal_res.h"
#include "vocal.h"
#include "string_res.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace phymo
{

/** What rings: a string struck, a bank of partials, FM, or a string bowed. */
enum class Model { String, Spectral, Fm, Bow, Vocal, Count };

/** A string material (String, knob 1): highs' decay (octaves), sustain
 *  (times the decay), stiffness, and pitch rise on a hard pluck (cents). */
struct Material
{
    float hi, decay, stiff, sharp;
};
static constexpr int      kMaterials = 5;
static const Material     kMaterial[kMaterials] = {
    {0.3f, 1.f, 0.25f, 2.f},     // steel: bright and long, a little stiff
    {-0.3f, 0.95f, 0.3f, 3.f},   // bronze (wound): warmer, a little stiffer
    {-1.4f, 0.85f, 0.08f, 9.f},  // nylon: round, shorter, soft
    {-2.f, 0.78f, 0.04f, 12.f},  // nylgut / perlon: plummier, stretchier
    {-2.6f, 0.7f, 0.02f, 10.f},  // gut: darkest and shortest
};

/** A bow (Bowed, knob 1): grip, grit (rosin noise), and e-bow share. */
struct BowKind
{
    float grip, grit, magnet;
};
static constexpr int  kBows = 4;
static const BowKind  kBow[kBows] = {
    {1.4f, 0.06f, 0.f}, // rosin: heavy grip, gritty
    {1.f, 0.015f, 0.f}, // horsehair: the classic
    {0.65f, 0.f, 0.f},  // synthetic: smooth, glassy
    {0.65f, 0.f, 1.f},  // e-bow: magnetic
};

/** LFO and envelope destinations. */
enum class Dest { Pitch, Bright, Morph, Exciter, Tone, Place, Count };
static constexpr int kDests = static_cast<int>(Dest::Count);
/** Each destination's range at depth 1: pitch in octaves, morph in tables,
 *  FM index in octaves, the rest as shares of their range. */
static constexpr float kLfoScale[kDests] = {1.f, 0.3f, 2.f, 1.f, 0.5f, 0.4f};
static constexpr float kEnvScale[kDests] = {1.f, 1.f, 4.f, 1.f, 0.5f, 1.f};

/** Shared voice settings, set by the synth from the panel. */
struct VoiceParams
{
    Model model = Model::String;
    // Spectral spectrum (before the morph)
    float table      = 0.f;  // 0 to kTables − 1
    float stretch    = 0.f;  // −0.5 to 0.5
    float snap       = 0.f;
    float partials   = 24.f;
    float morph      = 0.f; // each note's spectrum offset, tables (±)
    float morph_time = 0.5f; // s
    float partial_cap = 24.f; // max partials per voice (CPU budget)
    // FM
    int   fm_list    = 0; // ratio list (fm_core.h)
    float attack     = 0.f; // s: String/Spectral swell, FM/bow attack
    float sustain    = 0.f; // 0 (off) to 1: hold level while held
    float vib_hz      = 5.5f; // Bowed vibrato rate, Hz
    float vib_depth   = 0.f; // depth, ± semitones
    float vib_onset   = 0.5f; // onset, s
    float vib_decay   = -1.f; // decay, s (below 0: none)
    float stroke      = 0.f; // bow stroke length each way, s (0: one stroke)
    float weight      = 0.f; // 0 even to 1 heavier at the frog
    float ensemble   = 0.f; // Vocal: per-note pitch and formant spread, 0 to 1
    float tonebar    = 0.f; // Spectral tonebar, 0 to 1
    float tape       = 0.f; // Vocal tape: wow, flutter and reel length, 0 to 1
    float tape_speed = 1.f; // current tape speed (pitch)
    float fm_depth   = 3.f; // FM index at full brightness and strength, radians
    float fm_fb      = 0.f; // FM feedback, radians
    StrikeParams strike;
    float bright    = 0.5f; // the wheel: highs' decay, 0 (2^-7) to 1 (same)
    float decay     = 3.f; // T60 while held, s
    float damper    = 0.2f; // T60 once released, s
    float key_track = 0.5f; // decay key tracking, 0 to 1
    PickupMode pickup = PickupMode::Clean;
    float      drive  = 0.4f; // pickup amount, 0 to 1
    float      tone   = 0.5f; // pickup tone, 0 to 1
    float      place  = 0.f; // pickup position: 0 whole string, then bridge to middle
    float lfo_to[kDests] = {}; // LFO depth per destination, -1 to 1
    float env_to[kDests] = {}; // envelope depth per destination
    EnvTimes env; // envelope stages
    bool  lfo_voice  = false; // per-voice LFO (VOICE)
    float lfo_hz     = 5.f; // LFO rate (per-voice LFO)
    Lfo::Shape lfo_shape = Lfo::Shape::Triangle;
    float lfo_delay  = 0.f, lfo_fade = 0.f; // s
    float vel_amount = 0.8f; // velocity to strength, 0 to 1
    float life       = 0.f; // per-strike randomness, 0 to 1
    float course     = 0.f; // String second string, 0 to 1 (15 cents)
    float interval   = 0.f; // its interval, semitones (0: unison)
    int   flag_n     = 2; // Bowed flageolet harmonic (2 the octave)
    float flag_a     = 0.f; // and amount
    float bend      = 0.f;  // -1 to 1
    float glide     = 0.f; // portamento per UpdateMod (0: off)
    float lfo       = 0.f; // the synth LFO's value, -1 to 1
};

class Voice
{
  public:
    static constexpr int   kModEvery  = 16; // samples between UpdateMods
    static constexpr float kBendRange = 2.f;   // semitones at full bend
    static constexpr float kBrightOct = 7.f;   // at brightness 0, the highs ring 2^this times shorter than the note
    static constexpr float kOpen      = 0.6f; // transient brightness boost
    static constexpr float kSilent    = 1e-4f; // silence threshold (released)
    static constexpr float kFaded     = 0.016f; // latch loop restrikes this far below the peak (-36 dB)
    static constexpr int   kModalEvery = 4; // UpdateMods between Spectral/FM Sets (64 samples)
    static constexpr float kTiltMax   = 2.5f;  // the Spectral model's decay tilt at brightness 0
    static constexpr float kPast      = 0.05f; // the morph envelope aims this far past 0
    static constexpr float kFmLevel   = 0.17f; // FM level (fm_test)
    static constexpr float kFmTilt    = 2.f;   // FM's index tilt at brightness 0
    static constexpr float kCourseMax = 0.15f; // course spread at full, semitones
    static constexpr float kIntervalMin = 0.05f; // below this interval: unison
    static constexpr float kCourseLevel = 0.65f; // each string's level with a course
    static constexpr float kStringGlide = 0.005f; // string level glide, s
    static constexpr float kTension = 0.08f; // pluck sharpness settles over this, s
    static constexpr float kBowSpeed = 0.25f; // bow speed at full strength
    static constexpr float kBowLevel = 0.27f; // Bowed level (bow_test)
    static constexpr float kBowLift  = 0.03f; // bow lift on release, s
    static constexpr float kBite     = 1.5f; // bow bite at Type 0 (none from the middle up)
    static constexpr float kVoiceTilt  = 0.6f; // String material change: highs' gain, octaves per octave of `hi`
    static constexpr float kVoiceSplit = 2.5f; // split frequency, times the note
    static constexpr float kPlaceGain  = 2.5f; // pickup position: max level make-up
    static constexpr float kPlaceGlide = 0.15f; // its glide per UpdateMod (~2 ms)
    static constexpr float kVibWait  = 0.33f; // vibrato onset: waiting share
    static constexpr float kVibSlow  = 0.55f; // initial rate share during the build
    static constexpr float kVibRelease = 3.f; // vibrato release fade, s
    static constexpr float kVibDepthLife = 0.25f; // Life: per-note depth variation
    static constexpr float kTuneIn   = 0.08f; // Life (Bowed): initial tuning offset, semitones
    static constexpr float kTuneInTime = 0.12f; // its settle time, s
    static constexpr float kTuneDrift = 0.03f; // slow drift, semitones
    static constexpr float kPressGrip  = 1.15f; // max pressure x grip
    static constexpr float kPressFar   = 0.4f; // less over the fingerboard
    static constexpr float kCrunch     = 0.7f; // digging-in crunch past kPressGrip, at Hardness 1
    static constexpr float kCrunchTime = 0.12f; // crunch fade, s
    static constexpr float kScratchFrom = 0.8f; // Hardness above this: a held scratch
    static constexpr float kScratch     = 0.6f; // its size at Hardness 1
    static constexpr float kScratchGrit = 1.f; // its grit at Hardness 1
    static constexpr float kSpeedRef   = 0.84f; // friction curve reference speed
    static constexpr float kBowRingMin = 0.6f; // Bowed: minimum string ring, s
    static constexpr float kBowTilt    = 2.5f; // Bowed brightness tilt range
    static constexpr float kBowTiltMid = 0.6f; // flat at this brightness
    static constexpr float kTiltSplit  = 1500.f; // tilt split, Hz
    static constexpr float kDcPole   = 0.9987f; // Bowed DC blocker (~10 Hz)
    static constexpr float kBowBrightOct = 5.5f; // Bowed: highs' decay range (octaves)
    static constexpr float kTurnLift = 0.95f; // pressure eased through the turn
    static constexpr float kTurn     = 0.008f; // bow turn time, s
    static constexpr float kEaseDepth = 0.3f; // speed dip around each change
    static constexpr float kEaseLen   = 0.06f; // its length, s
    static constexpr float kEaseShare = 0.4f; // at most this share of a short stroke
    static constexpr float kEaseVary  = 0.f; // per-change variation at Life 0
    static constexpr float kEaseVaryLife = 1.f; // extra at Life 1
    static constexpr float kTurnShare = 0.3f; // turn at most this share of a short stroke
    static constexpr float kFullBow   = 1.f; // a full bow takes this long, s
    static constexpr float kTurnScrape = 0.45f; // scrape on re-gripping
    static constexpr float kWeight   = 0.5f; // weight at full: pressure ± this share
    static constexpr float kVibWander = 0.06f; // vibrato wander at Life 0
    static constexpr float kVibWanderLife = 0.12f; // extra at Life 1
    static constexpr float kTapeLen    = 8.f; // Vocal tape: reel length, s
    static constexpr float kTapeFade   = 0.4f; // then fades over this (T60), s
    static constexpr float kEnsCents   = 14.f; // Vocal ensemble pitch spread, cents
    static constexpr float kEnsFormant = 0.07f; // and formant spread, octaves
    static constexpr float kBowStart = 0.012f; // bow's own attack at most, s
    static constexpr float kGritK    = 0.18f; // grit low-pass (~1.5 kHz)
    static constexpr float kGritGain = 2.f; // grit make-up gain
    static constexpr float kGritHeld = 0.3f; // grit under a held tone
    static constexpr float kScrape   = 3.f; // more as the bow meets the string
    static constexpr float kScrapeTime = 0.04f; // scrape fade, s
    static constexpr float kBowTune  = 0.f; // bow pitch correction, cents
    static constexpr float kBetaMin  = 0.12f; // nearest the bridge, from C3 up
    static constexpr float kBetaLow  = 0.18f; // at C2 and below
    static constexpr float kBetaMax  = 0.3f; // furthest, over the fingerboard
    static constexpr float kGripMin  = 0.55f; // catch at the lightest pressure, x 1 / beta
    static constexpr float kGripRange = 0.2f; // more at the heaviest
    static constexpr float kSlope    = 25.f; // friction steepness, x beta
    static constexpr float kMorphLn  = 3.0445224f; // ln((1 + kPast) / kPast)
    static constexpr float kHold     = 600.f; // Sustain: ring time when holding, s
    static constexpr float kHoldOct  = 8.f; // Sustain's lowest level, octaves under the peak (48 dB)
    static constexpr float kKneeLow  = 3.f; // knee width at its lowest, octaves of level
    static constexpr float kKneeHigh = 0.25f; // at its highest
    static constexpr float kKneeEnd  = 0.2f; // final share held outright
    static constexpr float kChoke     = 0.025f; // choke fade, s
    static constexpr float kCycleMin  = 0.04f; // Cycle: minimum time between restrikes, s
    static constexpr float kDampEase  = 0.004f; // ring time eases to the damper's over this, s
    static constexpr float kHushSwell = 0.012f; // fade before a swell over a ringing note, s
    static constexpr float kHushMove  = 0.005f; // the same before a new string note
    static constexpr float kRinging   = 1e-3f; // ringing threshold for those fades

    /** The two strings' delay lines (StringRes::kLen floats each); before Init. */
    void SetMemory(float* a, float* b) { str_.SetMemory(a), str2_.SetMemory(b); }

    PHYMO_ONCE void Init(float sr)
    {
        sr_ = sr;
        exc_.Init(sr);
        str_.Init(sr); // memory first: SetMemory
        str2_.Init(sr);
        vocal_.Init(sr);
        bow_.Init(sr, str2_.Memory()); // borrows the second string's memory
        static uint32_t seed = 0x2545f491u; // each voice its own random sequence
        rng_ = seed, seed = seed * 1664525u + 1013904223u;
        modal_.Init(sr);
        fm_.Init(sr);
        pick_.Init(sr);
        lfo_.Init(sr);
        follow_k_ = expf(-1.f / (0.05f * sr)); // level follower fall: 50 ms
        glide_k_  = 1.f - expf(-1.f / (kStringGlide * sr));
        tens_k_   = expf(-static_cast<float>(kModEvery) / (kTension * sr));
        damp_k_   = 1.f - expf(-static_cast<float>(kModEvery) / (kDampEase * sr));
        bow_kr_   = 1.f - expf(-1.f / (kBowLift * sr));
        scrape_k_ = expf(-1.f / (kScrapeTime * sr));
        crunch_k_ = expf(-static_cast<float>(kModEvery) / (kCrunchTime * sr));
        tilt_k_   = 1.f - expf(-6.2831853f * kTiltSplit / sr);
        Reset();
    }

    /** Resets everything. `lines`: clear the delay lines too (otherwise the
     *  next note clears what it reads). */
    PHYMO_ONCE void Reset(bool lines = true)
    {
        exc_.Reset();
        str_.Reset(lines ? StringRes::kLen : 0);
        str2_.Reset(lines ? StringRes::kLen : 0);
        bow_.Reset(lines ? BowRes::kNeck : 0);
        stale_ = !lines;
        modal_.Reset();
        vocal_.Reset();
        fm_.Reset();
        pick_.Reset();
        env_.Reset();
        lfo_.Reset();
        tone_mod_    = 0.f;
        lfo_val_     = 0.f;
        place_ = place_to_ = 0.f, heard_d_ = heard_d_to_ = 1.f, heard_m_ = heard_m_to_ = 0.f, heard_g_ = heard_g_to_ = 1.f;
        ring_w_ = 0;
        voiced_ = true, hi_ref_ = 0.f, voice_g_ = voice_g_to_ = 1.f, voice_lp_ = 0.f, voice_k_ = 0.1f;
        swell_ = swell1_ = 1.f, swell_k_ = 1.f;
        hushing_ = moved_ = swells_ = false, hush_left_ = hush_n_ = 0, from_ = -1.f;
        choking_ = false, choke_left_ = choke_n_ = 1;
        since_   = 0;
        damp_    = 0.f;
        vib_ph_ = vib_t_ = vib_in_ = 0.f, vib_life_ = 1.f, vib_w_ = vib_w_to_ = 0.f, vib_w_left_ = 0;
        vib_rate_in_ = vib_held_ = 0.f, vib_dlife_ = 1.f, tune_in_ = 0.f, tune_w_ = tune_w_to_ = 0.f, tune_w_left_ = 0;
        stroke_t_ = 0.f, stroke_u_ = 0.f, stroke_dir_ = 1.f, stroke_f_ = stroke_f_to_ = 1.f, stroke_n_ = 0;
        stroke_ease_ = 0.f, ease_depth_ = kEaseDepth, ease_len_ = kEaseLen;
        in_scale_    = 0.f;
        vel_         = 1.f;
        strength_    = 0.f;
        life_det_    = 0.f;
        course_      = 0.f;
        interval_    = 0.f;
        g1_ = g1_to_ = 1.f;
        tens_ = 0.f;
        bv_ = 0.f, bow_ka_ = 1.f, bow_v_ = 0.f, bow_slope_ = 5.f, bow_grit_ = 0.f, bow_mag_ = 0.f, grit_ = 0.f, scrape_ = 0.f;
        bow_stick_ = 1.25f, bite_ = 0.f, crunch_ = 0.f, scratch_ = 0.f, dc_x_ = dc_y_ = 0.f;
        tilt_lp_ = 0.f, bow_tilt_ = tilt_lo_ = 1.f;
        g2_ = g2_to_ = 0.f;
        menv_        = 0.f;
        model_       = Model::String;
        pickup_      = PickupMode::Clean;
        modal_count_ = 0;
        note_      = -1;
        root_      = -1;
        pitch_     = 60.f;
        glide_own_ = -1.f;
        detune_    = 0.f;
        gate_      = false;
        strike_    = false;
        level_     = 0.f;
        peak_      = 0.f;
        mod_count_ = 0;
        age_       = 0;
        SetPan(0.f);
    }

    /** Starts `note`. `age`: the synth's note count (for stealing); `root`:
     *  the key that started it; `from`: the pitch to glide from (semitones,
     *  below 0 none); `vel`: velocity, 0 to 1. */
    PHYMO_ONCE void NoteOn(int note, uint32_t age, float from, int root, float vel = 1.f)
    {
        vel_       = vel;
        // a ringing note fades out first where a strike would click
        const bool ring  = level_ > kRinging;
        const bool moved = from < 0.f && fabsf(static_cast<float>(note) - pitch_) > 0.05f; // gliding from where it is: no jump
        const bool jumps = model_ == Model::String || model_ == Model::Bow;
        if(ring && (swells_ || (moved && jumps)))
            Hush(note, from, moved);
        else
            SetNote(note, from);
        fresh_     = !ring; // from quiet: may wait a block
        root_      = root;
        glide_own_ = -1.f; // the panel's portamento
        gate_      = true;
        strike_    = true;
        since_     = 0;
        age_       = age;
        level_     = 1.f; // sounding until the follower says otherwise
        damp_      = 0.f; // ring time at once
        peak_      = 0.f;
        mod_count_ = 0; // retune at once
        modal_count_ = 0;
        menv_      = 1.f; // morph starts at its setting
        voiced_    = false; // String: material voicing from the strike
        env_.Trigger();
        lfo_.Restart();
        vib_ph_ = vib_t_ = vib_in_ = vib_rate_in_ = 0.f, vib_held_ = 1.f; // restart the vibrato
        stroke_t_ = 0.f, stroke_f_ = stroke_f_to_ = 1.f, stroke_n_ = 0; // a new bow stroke
    }

    /** Release: the damper comes down. */
    void NoteOff() { gate_ = false; }

    /** Strikes its note again (the latch loop). */
    void Restrike()
    {
        strike_    = true;
        since_     = 0;
        if(level_ > kRinging && swells_)
            Hush(note_, pitch_, false);
        level_     = 1.f;
        peak_      = 0.f;
        menv_      = 1.f;
        voiced_    = false; // String: material voicing from the strike
        mod_count_ = 0, modal_count_ = 0;
        env_.Trigger();
        lfo_.Restart();
        vib_ph_ = vib_t_ = vib_in_ = vib_rate_in_ = 0.f, vib_held_ = 1.f;
        stroke_t_ = 0.f, stroke_f_ = stroke_f_to_ = 1.f, stroke_n_ = 0;
    }

    /** Held, ENV attack and decay done, kCycleMin since the last strike. */
    bool Cycled(const VoiceParams& p) const
    {
        return gate_ && !strike_ && !choking_ && env_.Cycled(p.env) && since_ >= static_cast<int>(kCycleMin * sr_);
    }

    /** Held, struck, and faded kFaded below its peak: ready to restrike. */
    bool Faded() const { return gate_ && !strike_ && peak_ > 0.f && level_ < kFaded * peak_ && !exc_.Busy(); }
    /** Level against its peak since the strike, 0 to 1 (LEDs). */
    float Fade() const { return peak_ > 0.f ? (level_ < peak_ ? level_ / peak_ : 1.f) : 0.f; }

    /** A new note without a strike, gliding from `from` (below 0: at once). */
    void SetNote(int note, float from)
    {
        note_  = note;
        pitch_ = from >= 0.f ? from : static_cast<float>(note);
    }

    /** Stereo position, -1 to 1 (equal power, unity in the middle). */
    void SetPan(float pos)
    {
        const float a = 0.78539816f * (pos + 1.f); // 0 to pi / 2
        pan_l_        = 1.41421356f * cosf(a);
        pan_r_        = 1.41421356f * sinf(a);
    }

    /** Adds `n` samples of this voice to `l` and `r`, times `gain`, panned. */
    PHYMO_HOT void Process(const VoiceParams& p, float* l, float* r, size_t n, float gain)
    {
        since_ += since_ < (1 << 30) ? static_cast<int>(n) : 0;
        swells_ = Swells(p, p.model);
        if(hushing_) // fading before a strike
        {
            const size_t m  = n < static_cast<size_t>(hush_left_) ? n : static_cast<size_t>(hush_left_);
            const bool   st = strike_;
            strike_         = false;
            Render(p, l, r, m, gain);
            strike_ = st;
            if(hush_left_ <= 0)
                Hushed();
            if(m == n)
                return;
            l += m, r += m, n -= m;
        }
        Render(p, l, r, n, gain);
    }

  private:
    /** Whether Attack swells this model. */
    static bool Swells(const VoiceParams& p, Model m)
    {
        return p.attack > 0.f
               && (m == Model::String || m == Model::Spectral || m == Model::Vocal || (m == Model::Bow && p.attack > kBowStart));
    }

    /** Fades the old note (kHushSwell or kHushMove), then starts `note` from
     *  `from`, clearing the string if `moved`. */
    void Hush(int note, float from, bool moved)
    {
        if(!hushing_)
            hushing_ = true, hush_n_ = hush_left_ = static_cast<int>((swells_ ? kHushSwell : kHushMove) * sr_);
        moved_ = moved_ || moved;
        note_  = note; // pitch_ stays the old note's during the fade
        from_  = from;
    }

    /** How much of the line a note-on clears: twice the longest period it
     *  reaches, plus a margin. */
    int ClearSpan() const
    {
        float low = static_cast<float>(note_);
        low       = from_ >= 0.f && from_ < low ? from_ : low;
        low       = pitch_ < low ? pitch_ : low; // where it starts, gliding from below
        const float hz  = 440.f * Exp2Fast((low - 69.f) / 12.f);
        return static_cast<int>(2.f * sr_ / (hz > 1.f ? hz : 1.f)) + 64;
    }

    /** Fade done: the new note's pitch, string cleared if it moved. */
    void Hushed()
    {
        hushing_ = false;
        pitch_   = from_ >= 0.f ? from_ : static_cast<float>(note_);
        if(moved_ && model_ == Model::String)
        {
            const int span = ClearSpan();
            str_.Reset(span), str2_.Reset(span);
        }
        else if(moved_ && model_ == Model::Bow)
            bow_.Reset(ClearSpan());
        moved_     = false;
        mod_count_ = 0;
    }

    PHYMO_HOT void Render(const VoiceParams& p, float* l, float* r, size_t n, float gain)
    {
        StrikeParams sp = p.strike;
        if(strike_) // the struck model and pickup stay until the next
        {
            // clear what this note will read of the lines
            const bool string = p.model != model_ && (p.model == Model::String || model_ == Model::String);
            if(stale_ || string)
                str_.Reset(ClearSpan()), ring_w_ = 0;
            if(p.model == Model::Bow && (model_ != Model::Bow || stale_))
                bow_.Reset(ClearSpan());
            stale_ = false;
            model_ = p.model, pickup_ = p.pickup;
            Strike(p, sp);
            // the swell (Attack), from silence
            swell_ = swell1_ = Swells(p, model_) ? 0.f : 1.f;
        }
        if(strike_ && model_ == Model::Fm)
        {
            fm_.Trigger(kFmLevel * (0.08f + 0.92f * strength_ * strength_));
            strike_ = false;
        }
        if(strike_ && model_ == Model::Vocal)
        {
            // Vocal: no strike; each note its own pitch and formants (ensemble)
            ens_cents_ = kEnsCents * p.ensemble * Rand();
            vocal_.SetScatter(kEnsFormant * p.ensemble * Rand());
            vocal_.Strike(p.strike.type); // Type: the onset consonant
            strike_ = false;
        }
        if(strike_ && model_ == Model::Bow)
        {
            // Bowed: no strike, but a gentle bite as it catches
            exc_.Strike(sr_ / Hz(pitch_), sp);
            bow_.Catch();
            scrape_ = 1.f; // the bow meets the string
            const float b = 1.f - 2.f * p.strike.type; // below Type's middle only
            bite_         = b > 0.f ? kBite * b * b : 0.f;
            crunch_       = p.strike.hardness * p.strike.hardness; // heavier with Hardness
            strike_ = false;
        }
        if(strike_)
        {
            exc_.Strike(sr_ / Hz(pitch_), sp);
            in_scale_ = 2.f / static_cast<float>(exc_.Length()); // pulse size at low partials
            strike_   = false;
        }
        pick_.Set(pickup_, p.drive, Clamp01(p.tone + tone_mod_));
        const float gl = gain * pan_l_, gr = gain * pan_r_;
        // in chunks between UpdateMods: resonator, then pickup and mix
        for(size_t i = 0; i < n;)
        {
            if(mod_count_ == 0)
            {
                UpdateMod(p);
                mod_count_ = kModEvery;
            }
            const int c = mod_count_ < static_cast<int>(n - i) ? mod_count_ : static_cast<int>(n - i);
            float     s[kModEvery];
            if(model_ == Model::Fm)
                for(int j = 0; j < c; j++)
                    s[j] = fm_.Process();
            else if(model_ == Model::Bow)
                for(int j = 0; j < c; j++)
                    s[j] = kBowLevel * Bowed();
            else if(model_ == Model::Vocal)
                for(int j = 0; j < c; j++)
                    s[j] = vocal_.Process();
            else if(model_ == Model::Spectral)
            {
                float e[kModEvery];
                for(int j = 0; j < c; j++)
                    e[j] = exc_.Process() * in_scale_, s[j] = 0.f;
                modal_.Process(e, s, c);
            }
            const bool  heard = model_ != Model::Fm && (heard_m_ > 0.f || heard_m_to_ > 0.f);
            const float kc    = 1.f / static_cast<float>(c);
            const float dd = (heard_d_to_ - heard_d_) * kc, dm = (heard_m_to_ - heard_m_) * kc,
                        dg = (heard_g_to_ - heard_g_) * kc;
            if(model_ == Model::String && (g2_ > 0.f || g2_to_ > 0.f)) // the course, or its tail
                for(int j = 0; j < c; j++)
                {
                    const float e = exc_.Process();
                    g1_ += (g1_to_ - g1_) * glide_k_;
                    g2_ += (g2_to_ - g2_) * glide_k_;
                    float a = str_.Process(e), b = str2_.Process(g2_to_ > 0.f ? e : 0.f);
                    if(heard)
                    {
                        heard_d_ += dd, heard_m_ += dm, heard_g_ += dg;
                        a = heard_g_ * (a - heard_m_ * str_.Past(heard_d_));
                        b = heard_g_ * (b - heard_m_ * str2_.Past(heard_d_));
                    }
                    s[j] = g1_ * a + g2_ * b;
                }
            else if(model_ == Model::String)
                for(int j = 0; j < c; j++)
                {
                    s[j] = str_.Process(exc_.Process());
                    if(heard)
                    {
                        heard_d_ += dd, heard_m_ += dm, heard_g_ += dg;
                        s[j] = heard_g_ * (s[j] - heard_m_ * str_.Past(heard_d_));
                    }
                }
            else if(model_ != Model::Fm) // Bowed and Spectral: output history in str_'s memory
            {
                float* h = str_.Memory();
                for(int j = 0; j < c; j++)
                {
                    h[ring_w_] = s[j];
                    if(heard)
                    {
                        heard_d_ += dd, heard_m_ += dm, heard_g_ += dg;
                        const float x = static_cast<float>(ring_w_) - heard_d_ + static_cast<float>(StringRes::kLen);
                        const int   k = static_cast<int>(x);
                        const float f = x - static_cast<float>(k);
                        const float a = h[k & (StringRes::kLen - 1)], b = h[(k + 1) & (StringRes::kLen - 1)];
                        s[j] = heard_g_ * (s[j] - heard_m_ * (a + (b - a) * f));
                    }
                    ring_w_ = (ring_w_ + 1) & (StringRes::kLen - 1);
                }
            }
            if(!heard)
                heard_d_ = heard_d_to_, heard_m_ = heard_m_to_, heard_g_ = heard_g_to_;
            // String material changed since the strike: highs lifted or cut
            if(model_ == Model::String && (voice_g_ != 1.f || voice_g_to_ != 1.f))
            {
                const float dv = (voice_g_to_ - voice_g_) * kc;
                for(int j = 0; j < c; j++)
                {
                    voice_g_ += dv;
                    voice_lp_ += (s[j] - voice_lp_) * voice_k_;
                    s[j] = voice_lp_ + voice_g_ * (s[j] - voice_lp_);
                }
                if(fabsf(voice_g_to_ - 1.f) < 1e-4f && fabsf(voice_g_ - 1.f) < 1e-4f)
                    voice_g_ = voice_g_to_ = 1.f;
            }
            else
                voice_lp_ = s[c - 1]; // ready, no step
            if(g2_to_ == 0.f && g2_ < 1e-3f)
                g2_ = 0.f, g1_ = g1_to_; // the tail's gone: one string
            for(int j = 0; j < c; j++)
            {
                const float a = fabsf(s[j]);
                level_        = a > level_ ? a : level_ * follow_k_;
                peak_         = a > peak_ ? a : peak_; // the signal's
                float g = swell_;
                if(choking_) // choking: raised-cosine fade
                {
                    g *= 0.5f + 0.5f * SinCycles(0.25f + 0.5f * (1.f - static_cast<float>(choke_left_) / static_cast<float>(choke_n_)));
                    choke_left_ -= choke_left_ > 0 ? 1 : 0;
                }
                if(hushing_)
                {
                    g *= 0.5f + 0.5f * SinCycles(0.75f - 0.5f * static_cast<float>(hush_left_) / static_cast<float>(hush_n_));
                    hush_left_ -= hush_left_ > 0 ? 1 : 0;
                }
                if(swell_ < 1.f)
                {
                    // two-stage S-curve, 95% in its time
                    swell1_ += (1.f - swell1_) * swell_k_;
                    swell_ += (swell1_ - swell_) * swell_k_;
                    swell_ = swell_ > 0.9995f ? 1.f : swell_;
                }
                const float x = pick_.Process(s[j] * g);
                l[i + j] += gl * x;
                r[i + j] += gr * x;
            }
            mod_count_ -= c;
            i += static_cast<size_t>(c);
        }
        if(choking_ && choke_left_ <= 0)
        {
            Reset(false); // silent; the next note clears its lines
            return;
        }
        if(!gate_ && !exc_.Busy() && level_ < kSilent)
            level_ = 0.f;
    }

  public:

    /** Its own glide (the sequencer's), per UpdateMod; below 0 none. */
    void SetGlideOwn(float k) { glide_own_ = k; }

    /** Its own pitch offset, semitones. */
    void     SetDetune(float semitones) { detune_ = semitones; }
    float    Detune() const { return detune_; }

    int      Note() const { return note_; }
    /** The key that started it. */
    int      Root() const { return root_; }
    /** Current pitch, semitones (MIDI). */
    float    Pitch() const { return pitch_; }
    bool     Gated() const { return gate_; }
    uint32_t Age() const { return age_; }
    /** Making sound: held or ringing. */
    bool Active() const { return gate_ || level_ > 0.f; }
    /** A strike or hushed note waiting for the next Process. */
    bool Striking() const { return strike_ || hushing_; }
    /** Vocal formant `k`, Hz. */
    float VocalFormant(int k) const { return vocal_.FormantHz(k); }
    /** A strike from quiet not yet processed: it can wait a block. */
    bool Waiting() const { return strike_ && fresh_ && !hushing_; }

    /** Out at once (CPU budget): a quick fade, then clear. */
    void Choke()
    {
        if(choking_ || !Active())
            return;
        choking_ = true, gate_ = false;
        choke_n_ = choke_left_ = static_cast<int>(kChoke * sr_);
    }
    bool Choking() const { return choking_; }
    /** Tone modulation, a share of the range. */
    float ToneMod() const { return tone_mod_; }
    /** Envelope value, 0 to 1. */
    float EnvValue() const { return env_.Value(); }
    /** Its own LFO's last value, -1 to 1. */
    float LfoValue() const { return lfo_val_; }

  private:
    float Hz(float semitones) const { return 440.f * Exp2Fast((semitones + detune_ + life_det_ - 69.f) / 12.f); }

    /** Bowed and Vocal: held while the key's down, with vibrato and drift. */
    bool Sung() const { return model_ == Model::Bow || model_ == Model::Vocal; }
    /** Life (none for Vocal, whose knob is tape). */
    float Life(const VoiceParams& p) const { return model_ == Model::Vocal ? 0.f : p.life; }

    /** -1 to 1, uniform. */
    float Rand()
    {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(rng_)) * 4.6566129e-10f;
    }

    static float Clamp01(float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; }

    /** Bow position from `position` (0 bridge, 1 fingerboard): kBetaMin to
     *  kBetaMax, further off for low notes. */
    float BowBeta(float position) const
    {
        float low = (60.f - 12.f - pitch_) / 12.f; // 0 at C3, 1 at C2
        low       = low < 0.f ? 0.f : low > 1.f ? 1.f : low;
        const float near = kBetaMin + (kBetaLow - kBetaMin) * low;
        return near + (kBetaMax - near) * position;
    }

    /** One bowed sample: the bow comes down over the attack, lifts on release. */
    float Bowed()
    {
        bv_ += ((gate_ ? 1.f : 0.f) - bv_) * (gate_ ? bow_ka_ : bow_kr_);
        // bow speed with low-passed rosin grit: strongest as the bow meets
        // the string, then a light trace
        grit_ += (Rand() - grit_) * kGritK;
        scrape_ *= scrape_k_;
        const float g    = bow_grit_ * (kGritHeld + kScrape * scrape_) + kScratchGrit * scratch_; // grit plus the held scratch
        stroke_f_ += (stroke_f_to_ - stroke_f_) * 0.25f; // smooth the stroke speed per sample
        const float vb   = stroke_f_ * bv_ * bow_v_ * (1.f + kGritGain * g * grit_);
        // run the pulse to its end, bite or not
        const float e    = exc_.Busy() ? exc_.Process() : 0.f;
        const float bite = bite_ != 0.f ? stroke_dir_ * bite_ * bow_v_ * e : 0.f; // in the bow's direction
        // pressure lightened through a turn
        const float sf      = stroke_f_ < 0.f ? -stroke_f_ : stroke_f_;
        const float contact = bv_ * (1.f - kTurnLift * (1.f - sf));
        float y = (1.f - 0.5f * bow_mag_) * bow_.Process(vb, contact, bow_stick_, bow_slope_, bow_mag_, bite, gate_ ? bow_v_ : 0.f);
        tilt_lp_ += (y - tilt_lp_) * tilt_k_; // brightness tilt around kTiltSplit
        y = tilt_lp_ * tilt_lo_ + bow_tilt_ * (y - tilt_lp_); // highs up as lows down
        // DC blocker, ~10 Hz
        dc_y_ = y - dc_x_ + kDcPole * dc_y_;
        dc_x_ = y;
        return dc_y_;
    }

    /** A note's strike into `sp`: velocity, Life's variation, and the course. */
    void Strike(const VoiceParams& p, StrikeParams& sp)
    {
        const float life = Life(p);
        sp.strength      = Clamp01(sp.strength * (1.f - p.vel_amount * (1.f - vel_)) * (1.f + 0.15f * life * Rand()));
        sp.position      = Clamp01(sp.position + 0.15f * life * Rand());
        sp.hardness      = Clamp01(sp.hardness + 0.1f * life * Rand());
        strength_        = sp.strength;
        tens_            = strength_ * strength_; // hard pluck: sharp, then settling
        life_det_        = 0.04f * life * Rand(); // ±4 cents
        vib_life_        = 1.f + 0.1f * life * Rand(); // vibrato rate ±10%
        vib_dlife_       = 1.f + kVibDepthLife * life * Rand(); // vibrato depth
        tune_in_         = kTuneIn * life * Rand(); // Bowed: initial tuning offset
        // alternate bow direction each note
        stroke_dir_ = -stroke_dir_;
        stroke_u_   = stroke_dir_ > 0.f ? 0.f : 1.f;
        menv_            = 1.f + 0.25f * life * Rand();
        const float was  = g2_to_;
        course_          = model_ == Model::String && p.course > 0.f
                               ? kCourseMax * p.course * p.course * (1.f + 0.3f * life * Rand())
                               : 0.f;
        interval_        = model_ == Model::String ? p.interval : 0.f;
        const bool two   = course_ > 0.f || interval_ > kIntervalMin;
        g2_to_ = two ? kCourseLevel : 0.f;
        g1_to_ = two ? kCourseLevel : 1.f;
        if(model_ != Model::String) // other models: second string off
            g2_ = 0.f, g1_ = 1.f;
        if(two && was == 0.f && g2_ == 0.f)
            str2_.Reset(ClearSpan()); // a clean second string
    }

    /** Every few samples: modulation, pitch, decay and brightness. */
    PHYMO_HOT void UpdateMod(const VoiceParams& p)
    {
        const float env = env_.Step(p.env, gate_);
        float       lfo = p.lfo;
        if(p.lfo_voice)
        {
            lfo_.SetRate(p.lfo_hz), lfo_.SetShape(p.lfo_shape), lfo_.SetDelay(p.lfo_delay), lfo_.SetFade(p.lfo_fade);
            lfo = lfo_val_ = lfo_.Advance(kModEvery);
        }
        float m[kDests];
        for(int d = 0; d < kDests; d++)
            m[d] = kLfoScale[d] * p.lfo_to[d] * lfo + kEnvScale[d] * p.env_to[d] * env;
        tone_mod_ = m[static_cast<int>(Dest::Tone)];
        const float exc = m[static_cast<int>(Dest::Exciter)];

        const float glide = glide_own_ >= 0.f ? glide_own_ : p.glide;
        if(hushing_)
            ; // the old note's, while fading
        else if(glide > 0.f)
            pitch_ += (static_cast<float>(note_) - pitch_) * glide;
        else
            pitch_ = static_cast<float>(note_);
        float freq = Hz(pitch_ + kBendRange * p.bend) * Exp2Fast(m[static_cast<int>(Dest::Pitch)]);
        // Bowed vibrato: a wait, then building into its depth; fading on
        // release; rate and depth wander slowly
        if(Sung())
        {
            // Life: starts a little off and settles, then drifts slowly
            const float dt = static_cast<float>(kModEvery) / sr_;
            tune_in_ -= tune_in_ * (1.f - Exp2Fast(-1.4426950f * dt / kTuneInTime));
            if(--tune_w_left_ <= 0)
                tune_w_to_ = Rand(), tune_w_left_ = static_cast<int>((1.2f + 0.6f * Rand()) * sr_ / kModEvery);
            tune_w_ += (tune_w_to_ - tune_w_) * (1.f - Exp2Fast(-1.4426950f * dt / 0.6f));
            freq *= Exp2Fast((tune_in_ + kTuneDrift * Life(p) * tune_w_) / 12.f);
        }
        if(Sung() && p.vib_depth > 0.f)
        {
            const float dt = static_cast<float>(kModEvery) / sr_;
            vib_t_ += dt;
            if(gate_)
            {
                if(vib_t_ >= kVibWait * p.vib_onset)
                {
                    // depth building, easing in
                    const float build = (1.f - kVibWait) * p.vib_onset + 1e-3f;
                    vib_in_ += (1.f - vib_in_) * (1.f - Exp2Fast(-1.4426950f * 3.f * dt / build));
                    vib_rate_in_ += (1.f - vib_rate_in_) * (1.f - Exp2Fast(-1.4426950f * 6.f * dt / build));
                }
                // vibrato decay after the onset
                if(p.vib_decay >= 0.f && vib_t_ >= p.vib_onset)
                    vib_held_ -= vib_held_ * (1.f - Exp2Fast(-1.4426950f * 3.f * dt / (p.vib_decay > kVibRelease ? p.vib_decay : kVibRelease)));
            }
            else
                vib_in_ -= vib_in_ * (1.f - Exp2Fast(-1.4426950f * 3.f * dt / kVibRelease));
            // wander: a new random aim every ~0.4 to 1.2 s
            if(--vib_w_left_ <= 0)
                vib_w_to_ = Rand(), vib_w_left_ = static_cast<int>((0.8f + 0.4f * Rand()) * sr_ / kModEvery);
            vib_w_ += (vib_w_to_ - vib_w_) * (1.f - Exp2Fast(-1.4426950f * dt / 0.3f));
            const float w = (kVibWander + kVibWanderLife * Life(p)) * vib_w_;
            vib_ph_ += p.vib_hz * vib_life_ * (kVibSlow + (1.f - kVibSlow) * vib_rate_in_) * (1.f + w) * dt;
            vib_ph_ -= vib_ph_ >= 1.f ? 1.f : 0.f;
            freq *= Exp2Fast(p.vib_depth * vib_dlife_ * vib_in_ * vib_held_ * (1.f + 2.f * w) * SinCycles(vib_ph_) / 12.f);
        }

        // pickup position: a comb (the wave less its reflection), faded in
        // over the first tenth, its level made up to at most kPlaceGain
        if(model_ != Model::Fm)
        {
            place_to_ = Clamp01(p.place + m[static_cast<int>(Dest::Place)]);
            place_ += (place_to_ - place_) * kPlaceGlide;
            const float x   = 0.5f * place_ + 0.002f;
            const float per = sr_ / freq;
            float       d   = x * per;
            heard_d_to_     = d > StringRes::kLen - 4.f ? StringRes::kLen - 4.f : d;
            heard_m_to_     = place_ < 0.1f ? 10.f * place_ : 1.f;
            const float c1 = SinCycles(x + 0.25f), mm = heard_m_to_;
            const float h1 = sqrtf(1.f - 2.f * mm * c1 + mm * mm); // comb at the fundamental
            const float r1 = sqrtf(h1); // half the fundamental's loss, in dB
            heard_g_to_    = r1 * kPlaceGain < 1.f ? kPlaceGain : 1.f / r1; // made-up level
        }

        float t60 = gate_ ? p.decay : (p.damper < p.decay ? p.damper : p.decay);
        damp_ = damp_ <= 0.f || model_ != Model::String ? t60 : damp_ * Exp2Fast(damp_k_ * Log2Fast(t60 / damp_)); // strikes at once; String eases to the damper
        t60   = damp_;
        t60 *= Exp2Fast(-p.key_track * (pitch_ - 60.f) / 12.f);
        // Sustain: the fall slows over a knee into the hold level, then holds
        float hold_by = 1.f; // ring stretch, 1 none
        if(gate_ && p.sustain > 0.f && model_ != Model::Bow && peak_ > 0.f)
        {
            const float thr   = peak_ * Exp2Fast(-kHoldOct * (1.f - p.sustain));
            const float above = level_ > thr ? Log2Fast(level_ / thr) : 0.f; // octaves above the hold level
            const float knee  = kKneeLow + (kKneeHigh - kKneeLow) * p.sustain;
            float       f     = above < knee ? (above / knee - kKneeEnd) / (1.f - kKneeEnd) : 1.f; // held outright at the end
            f                 = f < 0.f ? 0.f : f;
            hold_by           = f * kHold > t60 ? 1.f / f : kHold / t60; // 0: held
            hold_by           = hold_by > kHold / t60 ? kHold / t60 : hold_by;
        }
        const bool hold = hold_by > 1.f;
        if(p.attack > 0.f)
            swell_k_ = 1.f - Exp2Fast(-1.4426950f * 4.7f / (p.attack * sr_)); // two stages: 95% in the attack time

        float b = p.bright + (1.f - p.bright) * kOpen * exc_.Transient() + m[static_cast<int>(Dest::Bright)];
        b       = b < 0.f ? 0.f : b > 1.f ? 1.f : b;
        // morph envelope, aimed past 0 to land at morph_time
        if(menv_ > 0.f)
        {
            const float tau = p.morph_time / kMorphLn;
            menv_ += (-kPast - menv_) * (1.f - Exp2Fast(-1.4426950f * static_cast<float>(kModEvery) / (tau * sr_)));
            menv_ = menv_ < 0.f ? 0.f : menv_;
        }
        // position along knob 1's range: table, morph and modulation
        const float position = p.table + p.morph * menv_ + m[static_cast<int>(Dest::Morph)];
        // exciter modulation as Stretch (String, Spectral)
        float stretch = p.stretch + 0.5f * exc;
        stretch       = stretch < -0.5f ? -0.5f : stretch > 0.5f ? 0.5f : stretch;
        if(model_ == Model::String || model_ == Model::Bow)
        {
            const float span = static_cast<float>(kTables - 1);
            const float at   = Clamp01(position / span); // 0 to 1 along the materials or bows
            float       stiff = 2.f * stretch; // Stretch above its middle
            float       hi    = 0.f;
            if(model_ == Model::String)
            {
                interval_ = p.interval; // live: a ringing pair glides to it
                // between two materials
                const float x = at * (kMaterials - 1);
                int         m = static_cast<int>(x);
                m             = m > kMaterials - 2 ? kMaterials - 2 : m;
                const float f = x - static_cast<float>(m);
                const Material &u = kMaterial[m], &v = kMaterial[m + 1];
                hi    = u.hi + (v.hi - u.hi) * f;
                // material changes while ringing lift or cut the highs
                if(!voiced_)
                    hi_ref_ = hi, voiced_ = true;
                voice_g_to_ = Exp2Fast(kVoiceTilt * (hi - hi_ref_));
                {
                    const float fc = kVoiceSplit * freq < 4000.f ? kVoiceSplit * freq : 4000.f; // from the 3rd harmonic up
                    voice_k_       = 1.f - Exp2Fast(-1.4426950f * 6.2831853f * fc / sr_);
                }
                stiff = (stiff > 0.f ? stiff : 0.f) + u.stiff + (v.stiff - u.stiff) * f;
                t60 *= u.decay + (v.decay - u.decay) * f;
                freq *= Exp2Fast((u.sharp + (v.sharp - u.sharp) * f) * tens_ / 1200.f);
                tens_ *= tens_k_;
            }
            else
            {
                Stroke(p);
                // weight along the bow: heavier at the frog
                const float press = (p.strike.hardness + exc) * (1.f + kWeight * p.weight * (1.f - 2.f * stroke_u_));
                // crunch fades from the start; above kScratchFrom it holds
                crunch_ *= crunch_k_;
                {
                    const float h  = Clamp01(p.strike.hardness + exc);
                    const float sc = h > kScratchFrom ? (h - kScratchFrom) / (1.f - kScratchFrom) : 0.f;
                    scratch_         = sc * sc;
                }
                bow_.SetDig(crunch_); // start only


                SetBow(p, at, Clamp01(press), BowBeta(p.strike.position)); // exciter modulation: pressure
            }
            stiff              = stiff < -1.f ? -1.f : stiff > 1.f ? 1.f : stiff;
            // Bowed: a narrower brightness range
            float t60_hi = t60 * Exp2Fast(-(model_ == Model::Bow ? kBowBrightOct : kBrightOct) * (1.f - b) + hi);
            // Bowed: brightness also tilts the tone
            const float tilt = kBowTilt * ((b < 0.f ? 0.f : b > 1.f ? 1.f : b) - kBowTiltMid);
            bow_tilt_        = Exp2Fast(0.5f * tilt), tilt_lo_ = Exp2Fast(-0.5f * tilt); // half each way
            if(hold) // highs stretched too
                t60_hi *= hold_by, t60 *= hold_by;
            const float k_max = gate_ ? kLossMax : kDampLossMax; // released: lows fade with the note
            if(model_ == Model::Bow)
            {
                // held, the bow sustains: the string rings at least kBowRingMin
                if(gate_ && t60 < kBowRingMin)
                    t60_hi *= kBowRingMin / t60, t60 = kBowRingMin;
                bow_.Set(freq * Exp2Fast(kBowTune / 1200.f), t60, t60_hi, BowBeta(p.strike.position));
                bow_.SetTouch(p.flag_n, p.flag_a); // flageolet
            }
            else if(g2_ > 0.f || g2_to_ > 0.f) // the course: half its offset either side
            {
                str_.Set(freq * Exp2Fast(-course_ / 24.f), t60, t60_hi, stiff, k_max);
                if(interval_ > kIntervalMin) // an interval: its own filters
                    str2_.Set(freq * Exp2Fast((interval_ + course_ / 2.f) / 12.f), t60, t60_hi, stiff, k_max);
                else
                    str2_.Follow(str_, Exp2Fast(course_ / 12.f));
            }
            else
                str_.Set(freq, t60, t60_hi, stiff, k_max);
            return;
        }
        if(model_ == Model::Vocal)
        {
            // Vocal: vowel along knob 1, size (Stretch), tension (Hardness),
            // breath (Noise), effort (strength); release over the damper
            VocalSettings v;
            v.freq    = freq * Exp2Fast(ens_cents_ / 1200.f) * p.tape_speed;
            v.section = p.ensemble;
            v.vowel   = Clamp01(position / static_cast<float>(kTables - 1)) * static_cast<float>(kVowels - 1);
            v.size    = stretch;
            v.tension = Clamp01(p.strike.hardness + (b - 0.5f));
            v.breath  = p.strike.noise;
            v.effort  = strength_;
            v.release = p.damper; // released: the damper's time
            v.hold    = p.sustain; // hold level (Sustain)
            v.fall    = p.decay;
            v.bright  = b - 0.5f; // the wheel, about its middle
            v.gate    = gate_;
            if(p.tape > 0.f && since_ > static_cast<int>(kTapeLen * sr_)) // tape run out: fade quickly
                v.gate = false, v.release = v.release < kTapeFade ? v.release : kTapeFade;
            const bool full = modal_count_-- <= 0; // formants as often as Spectral's partials
            if(full)
                modal_count_ = kModalEvery - 1;
            vocal_.Set(v, full);
            return;
        }
        if(modal_count_-- > 0)
            return;
        modal_count_ = kModalEvery - 1;
        if(hold)
            t60 *= hold_by;
        if(model_ == Model::Fm)
        {
            FmSettings f;
            f.position = position * static_cast<float>(kFmEntries - 1) / static_cast<float>(kTables - 1); // the list's positions
            f.list     = p.fm_list;
            f.stretch  = p.stretch;
            f.snap     = p.snap;
            f.index    = p.fm_depth * (0.3f + 0.7f * b) * (0.5f + 0.5f * strength_) * Exp2Fast(2.f * exc); // exciter modulation: ±2 octaves of index
            f.tilt     = kFmTilt * (1.f - b);
            f.feedback = p.fm_fb;
            fm_.Set(freq, p.attack < 0.002f ? 0.002f : p.attack, t60, f, kModEvery * kModalEvery);
            return;
        }
        Shape s;
        s.table    = position;
        s.stretch  = stretch;
        s.snap     = p.snap;
        s.partials = p.partials < p.partial_cap ? p.partials : p.partial_cap;
        s.tilt     = kTiltMax * (1.f - b);
        s.bar      = p.tonebar;
        modal_.Set(freq, t60, s, kModEvery * kModalEvery);
    }

    /** The bow's stroke, per UpdateMod: along the bow at the note's speed,
     *  turning at each end through zero over kTurn and re-gripping with a
     *  scrape. No strokes (0) or the e-bow: one endless stroke. */
    void Stroke(const VoiceParams& p)
    {
        if(p.stroke <= 0.f || bow_mag_ > 0.5f || !gate_)
        {
            stroke_f_to_ = stroke_dir_; // full speed, its direction
            return;
        }
        const float dt   = static_cast<float>(kModEvery) / sr_;
        // turn at most kTurnShare of a short stroke
        const float turn = kTurn < kTurnShare * p.stroke ? kTurn : kTurnShare * p.stroke;
        const float half = 0.5f * turn;
        stroke_t_ += dt;
        if(stroke_t_ >= p.stroke + half) // the turn's middle: reverse
        {
            stroke_t_   = -half;
            stroke_dir_ = -stroke_dir_;
            stroke_n_++;
            crunch_ = 0.f; // the start's crunch is over
            // each change a little different, more with Life
            ease_depth_ = kEaseDepth * (1.f + (kEaseVary + kEaseVaryLife * p.life) * Rand());
            ease_len_   = kEaseLen * (1.f + 0.5f * (kEaseVary + kEaseVaryLife * p.life) * Rand());
            scrape_ = scrape_ > kTurnScrape ? scrape_ : kTurnScrape; // scrape on re-gripping
        }
        // speed: full along the stroke, easing to 0 at each turn
        float f = 1.f;
        if(stroke_t_ < half && stroke_n_ > 0)
            f = SinCycles(0.25f * (stroke_t_ + half) / turn); // 0 at -half to 1
        else if(stroke_t_ > p.stroke - half)
            f = SinCycles(0.25f * (p.stroke + half - stroke_t_) / turn);
        f = f < 0.f ? 0.f : f;
        // speed eased around each change, at most kEaseShare of the stroke
        {
            const float len = ease_len_ < kEaseShare * p.stroke ? ease_len_ : kEaseShare * p.stroke; // short strokes: barely eased
            const float to  = stroke_n_ > 0 ? stroke_t_ + half : 1e9f; // since the last change
            const float d   = to < p.stroke + half - stroke_t_ ? to : p.stroke + half - stroke_t_; // to the nearer change
            stroke_ease_    = d < 0.5f * len ? ease_depth_ * (0.5f + 0.5f * SinCycles(0.25f + d / len)) : 0.f;
        }
        f *= 1.f - stroke_ease_;
        stroke_f_to_ = stroke_dir_ * f;
        // position along the bow: a full bow from kFullBow up
        stroke_u_ += stroke_dir_ * f * dt / (p.stroke > kFullBow ? p.stroke : kFullBow);
        stroke_u_ = stroke_u_ < 0.f ? 0.f : stroke_u_ > 1.f ? 1.f : stroke_u_;
    }

    void SetBow(const VoiceParams& p, float at, float press, float beta)
    {
        const float x = at * (kBows - 1);
        int         m = static_cast<int>(x);
        m             = m > kBows - 2 ? kBows - 2 : m;
        const float f = x - static_cast<float>(m);
        const BowKind &u = kBow[m], &v = kBow[m + 1];
        const float grip = u.grip + (v.grip - u.grip) * f;
        // pressure capped at kPressGrip (less over the board); the start's
        // crunch can go past it
        const float held = kScratch * scratch_;
        const float dig  = 1.f + kCrunch * (crunch_ > held ? crunch_ : held);
        const float most = dig * kPressGrip * (1.f - kPressFar * (beta - kBetaMin) / (kBetaMax - kBetaMin));
        press            = press * dig * grip > most ? most / grip : press * dig;
        bow_stick_       = (kGripMin + kGripRange * press * grip) / beta; // catch: heavier and grippier holds longer
        bow_slope_       = kSlope * beta * (1.f - 0.6f * (press < 1.f ? press : 1.f)) / grip; // friction curve flatter with pressure
        bow_grit_        = u.grit + (v.grit - u.grit) * f + 0.2f * p.strike.noise;
        bow_mag_         = u.magnet + (v.magnet - u.magnet) * f;
        bow_v_           = kBowSpeed * (0.2f + 0.8f * strength_);
        // friction curve scaled to the bow speed
        bow_slope_ *= kSpeedRef / ((0.2f + 0.8f * strength_) * dig * dig); // digging in: flatter
        // the bow's own quick start; a longer Attack swells over it
        const float att  = p.attack < 0.002f ? 0.002f : p.attack > kBowStart ? kBowStart : p.attack;
        bow_ka_          = 1.f - Exp2Fast(-1.4426950f * 3.f / (att * sr_)); // there in the attack time
    }

    Exciter   exc_;
    StringRes str_, str2_; // the string, and the course's second
    BowRes    bow_; // Bowed string (on str2_'s memory)
    ModalRes  modal_;
    VocalRes  vocal_; // Vocal singer
    float     ens_cents_ = 0.f; // ensemble pitch offset, cents
    FmCore    fm_;
    Pickup    pick_;
    Env       env_; // ENV envelope
    Lfo       lfo_; // per-voice LFO
    float     tone_mod_; // tone modulation, share of range
    float     place_, place_to_; // pickup position (0 to 1), gliding
    float     heard_d_, heard_d_to_; // comb delay (samples), gliding
    float     heard_m_, heard_m_to_; // comb depth (0: whole string)
    float     heard_g_, heard_g_to_; // level make-up
    bool      voiced_; // String: material taken at the strike
    float     hi_ref_; // highs' decay then, octaves
    float     voice_g_, voice_g_to_; // highs' gain since, gliding
    float     voice_lp_, voice_k_; // split low-pass and coefficient
    int       ring_w_; // Bowed/Spectral output history position
    float     lfo_val_; // own LFO's last value
    float     swell_, swell1_, swell_k_; // swell gain (two stages) and rate
    bool      hushing_; // fading before a strike
    bool      choking_; // choking: fading, then clear
    int       choke_n_, choke_left_; // choke fade length and remaining samples
    bool      moved_; // to a new note
    bool      swells_; // a strike now would swell
    int       hush_left_, hush_n_; // hush samples left and total
    float     from_; // pitch to glide from (below 0: none)
    bool      stale_ = false; // choked: lines not yet cleared
    bool      fresh_ = false; // struck from quiet
    float     vib_ph_, vib_t_, vib_life_; // vibrato phase, time, Life rate
    float     vib_in_; // 0 to 1: onset and release
    float     vib_w_, vib_w_to_; // wander (-1 to 1) and target
    int       vib_w_left_; // UpdateMods to the next aim
    float     vib_rate_in_, vib_held_; // rate build (0 to 1); held decay (1 to 0)
    float     vib_dlife_; // Life: depth
    float     tune_in_; // Life (Bowed): initial offset, semitones
    float     tune_w_, tune_w_to_; // drift (-1 to 1) and target
    int       tune_w_left_;
    float     stroke_t_, stroke_u_, stroke_dir_; // stroke time (s), position (0 frog, 1 tip), direction (1 down)
    float     stroke_f_, stroke_f_to_; // speed (-1 to 1) and target
    float     stroke_ease_; // speed dip around a change
    float     ease_depth_, ease_len_; // next change's dip depth and length (s)
    int       stroke_n_; // turns since the note
    Model      model_; // struck model: kept until the next note
    PickupMode pickup_;
    float     in_scale_; // 2 / pulse length
    float     vel_;         // the note's velocity, 0 to 1
    float     strength_; // after velocity and Life
    float     life_det_; // Life's tuning, semitones
    float     course_; // string spread, semitones (0: one string)
    float     interval_; // second string's interval, semitones
    float     g1_, g1_to_, g2_, g2_to_; // string levels and targets
    float     glide_k_; // their glide per sample
    uint32_t  rng_; // random numbers
    float     tens_, tens_k_; // pluck sharpness and its settle rate
    float     bv_, bow_ka_, bow_kr_; // bow down (0 to 1), attack and lift rates
    float     bow_v_, bow_slope_, bow_grit_, bow_mag_; // bow speed, friction curve, grit, e-bow share
    float     grit_; // low-passed grit noise
    float     scrape_, scrape_k_; // contact scrape and its fade
    float     bow_stick_, bite_; // grip limit; bite size
    float     dc_x_, dc_y_; // DC blocker state
    float     tilt_lp_, tilt_k_, bow_tilt_, tilt_lo_; // tilt split low-pass, coefficient, highs' and lows' gains
    float     crunch_, crunch_k_; // digging-in crunch and its fade
    float     scratch_; // held scratch, 0 to 1
    float     menv_; // 1 at the strike, gliding to 0
    int       modal_count_; // UpdateMods to the next Spectral Set
    float     sr_;
    int       note_; // -1: never played
    int       root_;      // the key that started it
    float     pitch_; // semitones (MIDI), gliding to note_
    float     detune_;    // its own offset, semitones
    float     glide_own_; // below 0: the panel's
    bool      gate_; // key down or held
    bool      strike_; // a strike requested for the next Process
    float     level_; // output level, followed
    float     peak_; // peak since the strike
    float     follow_k_; // follower fall rate
    int       since_; // samples since the last strike
    float     damp_, damp_k_; // ring time (T60, s) easing to the damper's, and its rate
    int       mod_count_; // samples to the next UpdateMod
    uint32_t  age_; // start order, for stealing
    float     pan_l_, pan_r_; // stereo gains
};

} // namespace phymo
