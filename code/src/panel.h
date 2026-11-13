/** @file panel.h
 *  @brief The CHOMPI's keys, knobs and LEDs, wired to the synth.
 *
 *  The controls are described in MANUAL.md. In short: the keys play; CHOMPI
 *  + a black key picks a view (MODEL, EXCITER, RESONATOR, BODY, LFO / ENV,
 *  FX, KBD, PRESET) or the octave; knobs 1-4 edit the view, with a second
 *  layer under CHOMPI + turn; the wheel is brightness (CHOMPI: strike
 *  position; pressed: bend; CHOMPI + pressed: mod); knob 6 is the level;
 *  LOOP is hold; PLAY starts the arp; the toggle switches to sequencer mode.
 *
 *  A knob's click resets it to its default unless it has a click action of
 *  its own (then CHOMPI + click resets). Knobs accelerate when turned fast.
 *
 *  MIDI CCs move the panel's values and turned knobs send theirs (kCcMap);
 *  CC 64 is hold. Poll() runs in the audio interrupt once a block, after
 *  the controls are scanned; Draw() runs from the main loop and only reads.
 *  Preset files are the main loop's (storage.h): here, only requests.
 */
#pragma once
#include "hardware.h"
#include "temp_led_stuff.h"
#include "../../src/synth.h"
#include "preset.h"
#include <atomic>
#include <cmath>

namespace chompi
{

using Sw = Hardware::SwId;

/** Keybed key (0-24, pitch order) for each switch id; -1 = not a key. */
static const int8_t kSwKey[40] = {
    -1, -1, -1, -1, -1, -1, -1, 1,  // ENC1-4 SW, NC6, KEY_26, SW_TOG, KEY_16
    2,  4,  5,  7,  3,  6,  8,  0,  // KEY_2..5, 17, 18, 19, KEY_1
    9,  11, 12, 14, 16, 10, 13, 15, // KEY_6..10, 20, 21, 22
    17, 19, 21, 23, 24, 18, 20, 22, // KEY_11..15, 23, 24, 25
    -1, -1, -1, -1, -1, -1, -1, -1, // ENC6 SW, KEY_27, KEY_28, NC
};
/** Keybed LED (SMT chain index) for each switch id. */
static const int8_t kSwLed[40] = {
    -1, -1, -1, -1, -1, -1, -1, 0,
    23, 22, 21, 20, 1,  2,  3,  24,
    19, 18, 17, 16, 15, 4,  5,  6,
    14, 13, 12, 11, 10, 7,  8,  9,
    -1, -1, -1, -1, -1, -1, -1, -1,
};
/** Physical encoder -> knob (0-3 knobs 1-4, 4 the wheel, 5 knob 6), each
 *  knob's LED(s), and knobs 1-4's click switches. */
static const uint8_t kEncoderKnob[6] = {1, 2, 3, 0, 4, 5};
static const uint8_t kKnobLed[6]     = {1, 2, 3, 4, 5, 9};
static const uint8_t kKnobLed2[6]    = {1, 2, 3, 4, 6, 9}; // the wheel has two
static const Sw      kKnobClick[4]   = {Sw::ENC_4_SW, Sw::ENC_1_SW, Sw::ENC_2_SW, Sw::ENC_3_SW};

constexpr Sw  kSwChompi  = Sw::KEY_26;
constexpr Sw  kSwPlay    = Sw::KEY_27;
constexpr Sw  kSwLoop    = Sw::KEY_28;
constexpr int kLedChompi = 0;
constexpr int kLedPlay   = 7;
constexpr int kLedLoop   = 8;

constexpr int   kLowestNote = 48;         // C3 on the lowest key
constexpr float kKnobStep   = 1.f / 48.f; // one detent of knobs 1-4 and 6
constexpr float kWheelStep  = 1.f / 128.f; // one detent of the wheel (brightness, position): the synth glides between them
constexpr float kBendStep   = 1.f / 24.f; // one detent of bend / mod

/** The views knobs 1-4 show. */
enum View
{
    kLfo,
    kExc,   // EXCITER: the strike
    kRes,   // RESONATOR: decay, damper, key tracking (every model)
    kBody, // BODY: the pickup and body
    kModel, // MODEL: the model and its spectrum
    kKbd,
    kFx,
    kEnv,   // ENV: the envelope (mid D# again: it and the LFO's take turns)
    kViews
};

/** CHOMPI + a key: what each of the 25 keys does (a View, or a command). */
enum KeyCmd
{
    kCmdNone    = -1,
    kCmdOctDown = 100,
    kCmdOctUp,
    kCmdPresets,
};
static const int kKeyCmd[25] = {
    kCmdNone, kCmdOctDown, kCmdNone, kCmdOctUp,  kCmdNone, // C  C# D  D# E
    kCmdNone, kModel,      kCmdNone, kExc,       kCmdNone, // F  F# G  G# A
    kRes,     kCmdNone,    kCmdNone, kBody,      kCmdNone, // A# B  C  C# D
    kLfo,     kCmdNone,    kCmdNone, kFx,        kCmdNone, // D# E  F  F# G
    kKbd,     kCmdNone,    kCmdPresets, kCmdNone, kCmdNone, // G# A  A# B  C
};

// Colours
static const float kRed[3]       = {1.f, 0.f, 0.f};
static const float kPink[3]      = {1.f, .3f, .6f};
static const float kOrange[3]    = {1.f, .45f, 0.f};
static const float kPurple[3]    = {.6f, 0.f, 1.f};
static const float kCream[3]     = {1.f, .85f, .6f};
static const float kBlue[3]      = {0.f, .3f, 1.f};
static const float kLightBlue[3] = {.4f, .75f, 1.f};
static const float kGreen[3]     = {0.f, 1.f, .2f};
static const float kTeal[3]      = {0.f, .8f, .6f};
static const float kMagenta[3]   = {1.f, 0.f, .8f};
static const float kYellow[3]    = {1.f, .85f, 0.f};
static const float kWhite[3]     = {1.f, 1.f, 1.f};
static const float kCoral[3]     = {1.f, .42f, .3f};
static const float kDarkBlue[3]  = {0.f, .08f, .65f};
static const float kDark[3]      = {0.f, 0.f, 0.f};

/** Each view's four knobs' colours. */
static const float* const kKnobCol[kViews][4] = {
    {kMagenta, kMagenta, kWhite, kWhite},       // LFO: rate, shape (its own colours), the envelope's and LFO's depths (the destination's)
    {kGreen, kOrange, kWhite, kPurple},         // EXCITER: type (the view's green), hardness, noise, strength
    {kOrange, kGreen, kBlue, kYellow},          // RESONATOR: attack, ring, damper, sustain
    {kRed, kRed, kTeal, kLightBlue},            // BODY: amount (the pickup's colour), tone, tremolo rate, depth
    {kLightBlue, kPurple, kMagenta, kPink},     // MODEL: table (the model's colour), stretch, morph (drawn both ways), its time
    {kPurple, kCream, kGreen, kYellow},         // KBD: key tracking, portamento, pattern (its own colours), rate
    {kWhite, kLightBlue, kWhite, kYellow},      // FX: chorus (its mode's colour), send, space (drawn green / white / blue), time
    {kRed, kYellow, kGreen, kBlue},             // ENV: attack, decay, sustain, release
};

/** The CHOMPI + turn values' colours (only the knobs that have one). */
static const float* AltCol(int v, int k)
{
    return v == kBody && k == 0 ? kPurple // BODY knob 1: the pickup's place
           : v == kBody && k == 2 ? kOrange // BODY knob 3: the vibrato's onset
           : v == kBody && k == 3 ? kGreen  // BODY knob 4: tremolo spread
           : v == kBody ? kOrange       // BODY knob 2: the vibrato's decay
           : v == kExc && k == 0 ? kPink // EXCITER knob 1: Life
           : v == kExc ? kCream         // EXCITER knob 4: velocity
           : v == kModel ? kYellow      // MODEL knob 2: snap
           : v == kFx && k == 0 ? kWhite // FX knob 1: stereo spread
           : v == kKbd && k == 0 ? kPurple // KBD knob 1: detune
           : v == kKbd && k == 1 ? kLightBlue // KBD knob 2: the strum
           : v == kKbd && k == 2 ? kWhite // KBD knob 3: arp octaves
           : v == kKbd ? kPink          // KBD knob 4: arp randomness
           : v == kLfo && k == 1 ? kRed  // LFO knob 2: the delay
           : v == kLfo ? kYellow        // LFO knob 1: synced rate
           : k == 2    ? kPink          // FX knob 3: ping-pong
                       : kRed;          // FX knob 4: tails
}

/** Knobs whose colour, not brightness, shows the value: from this colour at
 *  0 to kKnobCol's at 1, at full brightness (nullptr: brightness shows it).
 */
static const float* const kKnobFrom[kViews][4] = {
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
    {nullptr, nullptr, nullptr, nullptr},
};

/** Each view's colour on its black key (CHOMPI held). */
/** FM's lists' colours: harmonic, odd, halves, metal. */
static const float* const kFmListCol[4] = {kLightBlue, kGreen, kYellow, kWhite};

/** The models' colours: string orange, spectral red, FM teal, bowed purple, vocal pink. */
static const float* const kModelCol[5] = {kOrange, kRed, kTeal, kPurple, kPink};

/** The String model's materials' colours on MODEL knob 1: steel, bronze,
 *  nylon, nylgut, gut (blended between). */
/** Preset bank colours (slots loaded bright, saved dim; the wheel's too). */
static const float  kPresetWarm[3]       = {1.f, .85f, .5f};
static const float* const kPresetBankCol[8] = {kPresetWarm, kLightBlue, kMagenta, kGreen, kOrange, kTeal, kPink, kPurple};
static const float kSteel[3] = {.55f, .7f, 1.f}, kBronze[3] = {1.f, .5f, .15f}, kNylon[3] = {1.f, .95f, .85f},
                   kNylgut[3] = {1.f, .8f, .45f}, kGut[3] = {.75f, .45f, .2f};
static const float* const kMaterialCol[5] = {kSteel, kBronze, kNylon, kNylgut, kGut};

/** The Bowed model's bows' colours: rosin, horsehair, synthetic, e-bow. */
static const float* const kBowCol[4] = {kOrange, kCream, kLightBlue, kMagenta};

/** The Spectral model's tables' colours on MODEL knob 1 (tables.h's order):
 *  harmonic, odd, tuned bar, tine, reed, free bar, bell, plate, membrane. */
static const float* const kTableCol[phymo::kTables] = {kWhite, kLightBlue, kYellow, kOrange, kGreen,
                                                       kTeal,  kCream,     kPurple, kRed};

/** The Vocal model's vowels' colours on MODEL knob 1 (vocal.h's order): u,
 *  U, aw, ah, ae, eh, ih, ee. */
static const float* const kVowelCol[phymo::kVowels] = {kBlue, kLightBlue, kTeal, kGreen, kYellow, kOrange, kPink, kWhite};

/** The pickups' colours: clean (its key the BODY view's own), tine, reed, wood. */
static const float* const kPickupCol[6] = {kWhite, kOrange, kPink, kCream, kTeal, kYellow};

/** The LFO's shapes' colours: triangle, sine, square, saw up, saw down, S&H. */
static const float* const kShapeCol[6] = {kMagenta, kPink, kRed, kOrange, kYellow, kWhite};

/** The modulation's destinations' colours (voice.h's Dest), each the
 *  colour of what it moves (Panel::DestCol: some by the model): pitch
 *  white (the wheel's bend), brightness green (the wheel's), the morph the
 *  table's (MODEL knob 1: the model's colour), the exciter's (FM's depth
 *  and the bow's pressure, EXCITER knob 2, orange; String's and
 *  Spectral's Stretch, MODEL knob 2, purple), tone red (BODY knob 2), the
 *  pickup's place purple (BODY's CHOMPI + knob 1). */
static const float* const kDestCol[phymo::kDests] = {kWhite, kGreen, kOrange, kOrange, kRed, kPurple};

/** The arp's patterns' colours: up, down, up & down, random, as played. */
static const float* const kPatternCol[5] = {kGreen, kBlue, kTeal, kPink, kRed};
/** The arp's octaves' colours (KBD CHOMPI + knob 3): 1 (none more), 2, 3, 4. */
static const float* const kOctaveCol[4] = {kWhite, kBlue, kTeal, kPink};

/** The KBD view's key modes on the lowest white keys, in KeyMode's order:
 *  C mono, D poly 1, E poly 2, F unison, G chord. */
static const int kModeKey[5] = {0, 2, 4, 5, 7};

static const float kViewCol[kViews][3] = {
    {1.f, 0.f, .8f},  // LFO magenta
    {0.f, 1.f, .25f}, // EXCITER green
    {1.f, .45f, 0.f}, // RESONATOR orange
    {0.f, .4f, 1.f},  // BODY blue
    {.5f, .2f, 1.f},  // MODEL violet
    {1.f, .85f, 0.f}, // KBD yellow
    {0.f, .8f, .6f},  // FX teal
    {1.f, .7f, .35f}, // ENV cream
};

/** The knobs MIDI CCs reach (in) and send (out, when turned), the General
 *  MIDI sound controllers where there's one: the resonator's attack 73, ring
 *  75 and damper 72 (GM's attack, decay and release); the LFO's rate 76;
 *  tremolo depth 92; chorus level 93, send 91; portamento time 5.
 *  Besides: brightness 74 and the strike's position 71 (the wheel, where
 *  Also: brightness 74 and strike position 71 (the wheel), volume 7 (knob
 *  6), the mod wheel 1 (LFO to pitch), 64 hold, 65 portamento on / off. */
struct CcMapEntry
{
    int     view, knob;
    uint8_t cc;
};
static const CcMapEntry kCcMap[] = {{kRes, 0, 73}, {kRes, 1, 75}, {kRes, 2, 72}, {kLfo, 0, 76}, {kFx, 0, 93},
                                    {kFx, 1, 91},  {kKbd, 1, 5},  {kBody, 3, 92}};

class Panel
{
  public:
    void Init(Hardware* hw, phymo::Synth* synth)
    {
        hw_    = hw;
        synth_ = synth;
        for(int i = 0; i < 25; i++)
        {
            key_led_[i]  = -1;
            down_[i]     = false;
            key_note_[i] = -1;
        }
        for(int sw = 0; sw < 40; sw++)
            if(kSwKey[sw] >= 0)
                key_led_[kSwKey[sw]] = kSwLed[sw];

        vca_    = 0.65f; // -17 dB
        view_   = kRes;
        octave_ = 0;
        hold_   = false;
        for(int i = 0; i < kSlots; i++)
            slot_used_[i] = false;
        Restore(Defaults());
        for(int k = 0; k < 4; k++)
            ApplySeq(k);
        synth_->SetSeqSync(true); // (the STRUM track: synced, over each step's length)
        synth_->SetSeqSpread(seq_spread_);
        synth_->SetVolume(VcaGain(vca_));
    }

    void Poll(uint32_t now)
    {
        chompi_held_ = hw_->button_sr.State(static_cast<int>(kSwChompi));
        if(!chompi_held_)
            len_edit_ = false; // (CHOMPI let go: the steps all again)
        now_         = now;

        // a preset the main loop has read: on now
        // (only while the view's open, the wheel still on its slot; or
        // kept: left, going back to the sound it opened with, one still on
        // its way is dropped)
        if(in_ready_)
        {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            if(presets_ ? in_slot_ == wheel_slot_ : keep_)
            {
                Restore(in_state_);
                loaded_ = in_slot_;
                tried_  = presets_;
            }
            in_ready_ = false;
        }
        // high A# held a second: init
        if(a_down_ && !a_fired_ && now - a_down_at_ >= 1000)
        {
            a_fired_ = true;
            Restore(Defaults());
            loaded_ = -1;
            keep_   = true; // (in the view: it stays when it's left)
            undo_ = Defaults(), undo_loaded_ = -1, tried_ = false; // (and the empty slots play it)
            Flash(-1, 3);
        }

        // the toggle: down, sequencer mode; up, performance mode
        const bool seq = hw_->GetToggleState();
        if(seq != seq_mode_)
        {
            if(entry_down_ > 0) // a step entry under way: done, the next step selected
                synth_->Seq().EntryDone();
            seq_mode_       = seq;
            synth_->SetSeqMode(seq);   // the keys apart from the arp's
            armed_          = false;
            synth_->SetRecord(false);
            pat_key_        = -1;
            quant_page_     = mix_page_ = len_page_ = strum_page_ = false;
            len_key_        = -1;
            copy_down_      = clear_down_ = pdown_ = false;
            if(presets_)
                LeavePresets(false);
            transpose_mode_ = false;
            entry_down_     = 0;
            pattern_view_   = false;
        }
        SeqTimers();

        PollKeys();

        const bool play = Pressed(kSwPlay), loop = Pressed(kSwLoop);
        if(seq_mode_)
        {
            if(loop)
                Arm(!armed_); // record armed (no hold here)
            if(play)
                SeqPlay();
        }
        else if(presets_ && chompi_held_ && (play || loop))
        {
            // CHOMPI + PLAY saves the sound in the wheel's slot, CHOMPI +
            // LOOP deletes it
            if(play)
            {
                Snapshot(out_state_);
                std::atomic_signal_fence(std::memory_order_seq_cst);
                req_save_ = wheel_slot_;
            }
            else
                req_delete_ = wheel_slot_;
        }
        else
        {
            if(loop)
            {
                if(chompi_held_)
                    ChordEdit(!chord_edit_);
                else
                    SetHold(!hold_);
            }
            if(play && chompi_held_)
                SeqPlay(); // the sequence, from performance mode
            else if(play)
                synth_->SetArp(!synth_->GetArp()); // on: running at once
        }

        // knobs 1-4's clicks; knob 6's, in sequencer mode, the MIX page in / out
        for(int k = 0; k < 4; k++)
            if(Pressed(kKnobClick[k]))
                Click(k);
        if(Pressed(Sw::ENC_6_SW) && seq_mode_)
            mix_page_ = !mix_page_;

        // the wheel's press: picks bend or mod, centred, until let go
        if(hw_->enc[4].RisingEdge())
        {
            wheel_mod_    = chompi_held_;
            wheel_val_    = 0.f;
            wheel_turned_ = false;
        }
        else if(hw_->enc[4].FallingEdge())
        {
            wheel_val_ = 0.f;
            synth_->SetBend(0.f);
            synth_->SetMod(0.f);
            if(!wheel_turned_ && presets_ && !wheel_mod_) // the preset view: a click keeps the sound, and leaves
                LeavePresets(true);
            else if(!wheel_turned_ && wheel_mod_) // CHOMPI + a click: the metronome on / off
            {
                synth_->SetMetronome(!synth_->GetMetronome());
                metro_at_ = now_ ? now_ : 1;
            }
            else if(seq_mode_ && strum_page_ && !wheel_turned_) // the STRUM track: synced (the step's length) or free (its own time)
                synth_->SetSeqSync(!synth_->GetSeqSync());
            else if(seq_mode_ && armed_ && !wheel_turned_) // a click (not a bend), recording: a rest
                Rest();
        }

        for(int e = 0; e < 6; e++)
        {
            const int inc = hw_->enc[e].Increment();
            if(!inc)
                continue;
            const int      knob = kEncoderKnob[e];
            const uint32_t dt   = now - last_turn_[knob];
            last_turn_[knob]    = now;
            Turn(knob, inc, Accel(dt));
        }
    }

    /** The CPU meter's readings, 0 to 1. Only called in DIAG builds. */
    void SetLoad(float avg, float max)
    {
        load_avg_ = avg;
        load_max_ = max;
        if(max > 0.9f)
            overload_until_ = daisy::System::GetNow() + 300;
    }

    void Draw(uint32_t now)
    {
        DrawKeys();

        if(seq_mode_)
            DrawSeqKnobs(now);
        else
        // knobs 1-4: each its own colour (kKnobCol), brightness the value
        // A knob with a CHOMPI + turn value shows it, in its own colour
        // (AltCol), while CHOMPI is held.
        for(int k = 0; k < 4; k++)
        {
            if(!Built(view_, k))
            {
                SetPthLedFloat(kKnobLed[k], 0.f, 0.f, 0.f);
                continue;
            }
            const bool   alt  = chompi_held_ && HasAlt(view_, k);
            float        mode[3];
            bool         flat = false; // (a stepped choice: its colour shows it, at full)
            const bool   bowed = synth_->GetModel() == phymo::Model::Bow;
            const float* c    = alt ? (view_ == kModel && k == 0 ? ModelAltCol()
                                       : view_ == kExc && k == 1 ? kBlue    // Bowed: the bow's weight
                                       : view_ == kModel && k == 1 && bowed ? kLightBlue // Bowed: the flageolet
                                       : view_ == kExc && k == 2 ? kWhite   // ... its strokes
                                                                 : AltCol(view_, k))
                                    : kKnobCol[view_][k];
            // CHOMPI + a destination's key (the LFO and ENV views): only the
            // knob it moves lit, in its colour (the wheel's: none)
            if(dest_key_ >= 0 && chompi_held_)
            {
                int dv, dk;
                DestTarget(dest_, dv, dk);
                const float* dc = DestCol(dest_);
                const float  on = dv >= 0 && dk == k ? 1.f : 0.f;
                SetPthLedFloat(kKnobLed[k], on * dc[0], on * dc[1], on * dc[2]);
                continue;
            }
            const float* from = alt ? nullptr : kKnobFrom[view_][k];
            const float  x    = Value(view_, k);
            if(view_ == kFx && k == 0 && !alt)
            {
                ChorusColour(mode[0], mode[1], mode[2]);
                c = mode;
            }
            else if(view_ == kFx && k == 2 && !alt)
            {
                // the space: delay green, both white, reverb blue, at full brightness
                const float* a = x < .5f ? kGreen : kWhite;
                const float* b = x < .5f ? kWhite : kBlue;
                const float  f = x < .5f ? 2.f * x : 2.f * x - 1.f;
                SetPthLedFloat(kKnobLed[k], a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f,
                               a[2] + (b[2] - a[2]) * f);
                continue;
            }
            else if(view_ == kKbd && k == 2) // the pattern, or (CHOMPI) the octaves: a colour each, at full
            {
                c    = alt ? kOctaveCol[static_cast<int>(x * 3.999f)] : kPatternCol[PatternOf(x)];
                flat = true;
            }
            else if(view_ == kLfo && k == 1 && !alt)
                c = kShapeCol[ShapeOf(x)];
            else if(view_ == kLfo && k >= 2)
            {
                // a depth, both ways from none (dark, its flat middle), in
                // its destination's colour; inverted (below) paler
                const float  d  = Centred(x);
                const float* dc = DestCol(dest_);
                const float  m  = fabsf(d), w = d < 0.f ? 0.45f : 0.f;
                SetPthLedFloat(kKnobLed[k], m * (dc[0] + (1.f - dc[0]) * w), m * (dc[1] + (1.f - dc[1]) * w),
                               m * (dc[2] + (1.f - dc[2]) * w));
                continue;
            }
            else if(view_ == kModel && k == 0 && !alt && synth_->GetModel() != phymo::Model::Fm)
            {
                // where along the tables (Spectral), materials (String) or
                // bows (Bowed): each its colour, at full on it, dimmer
                // between two (their colours mixed)
                const auto          m    = synth_->GetModel();
                const float* const* cols = m == phymo::Model::Spectral ? kTableCol
                                           : m == phymo::Model::String ? kMaterialCol
                                           : m == phymo::Model::Vocal  ? kVowelCol
                                                                       : kBowCol;
                const int n = m == phymo::Model::Spectral ? phymo::kTables
                              : m == phymo::Model::String ? phymo::kMaterials
                              : m == phymo::Model::Vocal  ? phymo::kVowels
                                                          : phymo::kBows;
                const float         at   = x * static_cast<float>(n - 1);
                int                 a    = static_cast<int>(at);
                a                        = a > n - 2 ? n - 2 : a;
                const float f = at - static_cast<float>(a), v = 1.f - 1.3f * (f < .5f ? f : 1.f - f); // (halfway: 0.35)
                const float *ca = cols[a], *cb = cols[a + 1];
                SetPthLedFloat(kKnobLed[k], v * (ca[0] + (cb[0] - ca[0]) * f), v * (ca[1] + (cb[1] - ca[1]) * f),
                               v * (ca[2] + (cb[2] - ca[2]) * f));
                continue;
            }
            else if(view_ == kModel && k == 0 && !alt)
            {
                // FM: the model's colour (its list's, a moment after its click)
                c = list_at_ && now - list_at_ < kShowFor ? kFmListCol[synth_->GetFmList()] : kModelCol[static_cast<int>(phymo::Model::Fm)];
            }
            else if(view_ == kBody && k == 0 && !alt)
                c = synth_->GetPickup() == phymo::PickupMode::Clean ? kViewCol[kBody] // the pickup's colour
                                                                     : kPickupCol[static_cast<int>(synth_->GetPickup())];
            else if(view_ == kModel && !alt && (k == 2 || k == 1))
            {
                // the morph, and the stretch, both ways from none (dark, its
                // flat middle): the morph below blue, above magenta; the
                // stretch squeezed teal, spread purple
                const float  d  = Centred(x);
                const float* mc = k == 2 ? (d < 0.f ? kBlue : kMagenta) : (d < 0.f ? kTeal : kPurple);
                const float  mb = fabsf(d);
                SetPthLedFloat(kKnobLed[k], mc[0] * mb, mc[1] * mb, mc[2] * mb);
                continue;
            }
            else if(view_ == kLfo && k == 0 && !alt && synth_->GetLfoSync() > 0.f)
                c = kYellow; // synced
            else if(view_ == kFx && k == 3 && !alt && synth_->GetDelayGlide())
                c = kOrange;
            if(view_ == kKbd && k == 1 && !alt && synth_->GetPortamento() == phymo::PortaMode::Off)
            {
                SetPthLedFloat(kKnobLed[k], .05f * c[0], .05f * c[1], .05f * c[2]); // off
                continue;
            }
            if(view_ == kEnv && k == 1 && synth_->GetCycle())
                c = kPink; // cycle
            if(from)
                SetPthLedFloat(kKnobLed[k], from[0] + (c[0] - from[0]) * x, from[1] + (c[1] - from[1]) * x,
                               from[2] + (c[2] - from[2]) * x);
            else
            {
                const float v = flat ? 1.f : 0.1f + 0.9f * x;
                SetPthLedFloat(kKnobLed[k], c[0] * v, c[1] * v, c[2] * v);
            }
        }
        // knob 6: the level, white; the MIX page, orange (the sequence alone)
        // through pink to purple (the keys alone)
        if(seq_mode_ && mix_page_)
        {
            const float  t  = mix_ < 0.5f ? 2.f * mix_ : 2.f * mix_ - 1.f;
            const float* a  = mix_ < 0.5f ? kOrange : kPink;
            const float* z  = mix_ < 0.5f ? kPink : kPurple;
            SetPthLedFloat(kKnobLed[5], a[0] + (z[0] - a[0]) * t, a[1] + (z[1] - a[1]) * t, a[2] + (z[2] - a[2]) * t);
        }
        else
        {
            const float vv = 0.1f + 0.9f * vca_;
            SetPthLedFloat(kKnobLed[5], vv, vv, vv);
        }
        // the wheel's two LEDs
        int dv = -1, dk = -1;
        if(dest_key_ >= 0 && chompi_held_)
            DestTarget(dest_, dv, dk);
        if(dest_key_ >= 0 && chompi_held_) // a destination's key: the wheel lit if it's the wheel's (pitch, brightness)
            WheelLeds(dv < 0 ? 1.f : 0.f, dv < 0 ? 1.f : 0.f, DestCol(dest_));
        else if(metro_at_ != 0 && now_ - metro_at_ < 400) // the metronome just switched: green on, red off
            WheelLeds(1.f, 1.f, synth_->GetMetronome() ? kGreen : kRed);
        else if(seq_mode_ && strum_page_ && chompi_held_) // the STRUM track's spread on the wheel: yellow (free: light blue)
            WheelLeds(1.f, 1.f, synth_->GetSeqSync() ? kYellow : kLightBlue);
        else if(LengthWheel())
        {
            // the pattern's length: its last step's page colour, bright
            static const float* const kPageCol[4] = {kRed, kOrange, kYellow, kGreen};
            WheelLeds(1.f, 1.f, kPageCol[(synth_->Seq().Length() - 1) / 8]);
        }
        else if(hw_->enc[4].Pressed() && wheel_mod_)
        {
            // mod: dark at centre; right, LFO to brightness (light blue on the
            // right LED); left, LFO to pitch (purple on the left)
            const float m = fabsf(wheel_val_);
            const float b = m > 0.f ? 0.2f + 0.8f * m : 0.f;
            if(wheel_val_ > 0.f)
                WheelLeds(0.f, b, kLightBlue);
            else
                WheelLeds(b, 0.f, kPurple);
        }
        else if(hw_->enc[4].Pressed())
        {
            const float b = 0.2f + 0.8f * fabsf(wheel_val_); // bend: white, both
            WheelLeds(b, b, kWhite);
        }
        else if(presets_ && !seq_mode_)
            WheelLeds(0.5f, 0.5f, kPresetBankCol[wheel_slot_ / kBankSlots]); // the wheel's slot's bank's colour
        else if(seq_mode_)
        {
            // the selected step's page colour (the wheel selects steps)
            static const float* const kPageCol[4] = {kRed, kOrange, kYellow, kGreen};
            const float* c = kPageCol[synth_->Seq().Selected() / 8];
            WheelLeds(0.5f, 0.5f, c);
        }
        else
        {
            // brightness green (the strike's position yellow while CHOMPI is
            // held), sweeping left to right: at the bottom only the left LED,
            // dim; both by the middle; at the top only the right, bright
            const bool  res   = chompi_held_;
            const bool  vocal = synth_->GetModel() == phymo::Model::Vocal; // (CHOMPI: its ensemble, pink)
            const float v     = res ? (vocal ? alt_[kModel][2] : pos_) : bright_;
            const float b   = 0.2f + 0.8f * v;
            const float l   = b * (v < 0.5f ? 1.f : 2.f * (1.f - v));
            const float r   = b * (v > 0.5f ? 1.f : 2.f * v);
            WheelLeds(l, r, res ? (vocal ? kPink : kYellow) : kGreen);
        }

        if(seq_mode_)
            SetPthLedFloat(kLedPlay, 0.f, synth_->Seq().Running() ? 1.f : 0.f, 0.f);
        else if(!synth_->GetArp())
            SetPthLedFloat(kLedPlay, 0.f, 0.f, 0.f);
        else if(synth_->GetArpRun())
            SetPthLedFloat(kLedPlay, 0.f, 1.f, 0.f);
        else
            SetPthLedFloat(kLedPlay, 1.f, .6f, 0.f);
        if(seq_mode_) // record: red, flashing armed, solid recording (running, or steps entered)
        {
            const bool  solid = armed_ && (synth_->Seq().Running() || entered_);
            const float b     = !armed_ ? 0.f : solid || (now / 250) % 2 ? 1.f : 0.1f;
            SetPthLedFloat(kLedLoop, b, 0.f, 0.f);
        }
        else if(chord_edit_) // choosing a chord: flashing teal, 4 times a second
        {
            const float b = (now / 125) % 2 ? 1.f : 0.f;
            SetPthLedFloat(kLedLoop, kTeal[0] * b, kTeal[1] * b, kTeal[2] * b);
        }
        else
            SetPthLedFloat(kLedLoop, hold_ ? 1.f : 0.f, hold_ ? .6f : 0.f, 0.f);
        // CHOMPI: red after an overload; else yellow at the start of each arp
        // step while it runs; else dim white
        const bool flash = static_cast<int32_t>(overload_until_ - now) > 0;
        const bool beat  = synth_->GetArpNote() >= 0 && synth_->GetArpPhase() < 0.25f;
        if(flash)
            SetPthLedFloat(kLedChompi, 1.f, 0.f, 0.f);
        else if(beat)
            SetPthLedFloat(kLedChompi, 1.f, .8f, 0.f);
        else
            SetPthLedFloat(kLedChompi, .15f, .15f, .15f);
        fill_led_data();
    }

    /** A sweep across the keys at power-on, so you know which firmware booted. */
    void BootAnimation()
    {
        for(int step = 0; step < 40; step++)
        {
            for(int k = 0; k < 25; k++)
            {
                const float d = fabsf(k - step * 0.7f);
                const float b = d < 4.f ? 1.f - d / 4.f : 0.f;
                SetSmtLedFloat(key_led_[k], b, 0.7f * b, 0.4f * b);
            }
            fill_led_data();
            daisy::System::Delay(12);
        }
        for(int k = 0; k < 25; k++)
            SetSmtLed(key_led_[k], 0, 0, 0);
        fill_led_data();
    }

    // --- presets: the main loop's side (storage.h) ---

    static constexpr int kBankSlots   = 15;                         // a bank's slots: the white keys
    static constexpr int kPresetBanks = 8;                          // the banks, the wheel's in the preset view
    static constexpr int kSlots       = kBankSlots * kPresetBanks; // every slot, bank by bank (P01.txt to P120.txt)

    /** The sound at power-on, and at init. */
    static PresetState Defaults()
    {
        PresetState d;
        d.knob[kLfo][0] = 0.5f; // rate, about 1.7 Hz (the shape triangle, no delay, the depths none)
        d.knob[kEnv][0] = 0.19f; // attack, ~5 ms
        d.knob[kEnv][1] = 0.6f;  // decay, ~0.5 s
        d.knob[kEnv][3] = 0.55f; // release, ~0.33 s (sustain 0)
        d.knob[kExc][0] = 0.3f; // type: nearer a hammer
        d.knob[kExc][1] = 0.5f; // hardness
        d.knob[kExc][2] = 0.1f; // noise
        d.knob[kExc][3] = 0.8f; // strength
        d.alt[kExc][0]  = 0.2f; // Life: a little
        d.alt[kExc][3]  = 0.8f; // velocity: most of the way
        d.knob[kRes][1] = 0.6f; // ring, ~2.4 s (attack none, sustain off)
        d.knob[kRes][2] = 0.4f; // damper, ~0.22 s
        d.knob[kKbd][0] = 0.5f; // key tracking
        d.alt[kKbd][0]  = 0.5f; // detune: unison ± 12 cents, a 3-cent drift
        d.knob[kBody][0]  = 0.4f; // the pickup's amount (the pickup clean)
        d.knob[kBody][1]  = 0.5f; // tone: as designed
        d.knob[kBody][2]  = 0.5f; // tremolo ~4.2 Hz (its depth 0); Bowed, the vibrato ~5.2 Hz
        d.alt[kBody][2]   = 0.2f;  // Bowed: the vibrato's onset, 0.5 s
        d.alt[kBody][1]   = 1.f;   // ... and its decay: none (held as it is)
        d.alt[kExc][2]    = 1.f;   // Bowed: one endless stroke
        d.knob[kModel][1] = 0.5f; // stretch: none (the model the string)
        d.knob[kModel][2] = 0.5f; // morph: none
        d.knob[kModel][3] = 0.4f; // the morph's time, ~0.22 s
        d.alt[kModel][0]  = 1.f;  // all 24 partials
        d.knob[kKbd][1] = 0.f; // portamento off (up from here, on: ~5 ms to 2 s)
        d.knob[kKbd][3] = 0.6f; // arp ~4.6 steps a second
        d.knob[kFx][0]  = 1.f;  // chorus level (the chorus itself off)
        d.knob[kFx][2]  = 0.5f; // space: both
        d.knob[kFx][3]  = 0.5f; // time, ~260 ms
        d.alt[kFx][0]   = 0.3f; // stereo spread
        d.alt[kFx][2]   = 1.f;  // ping-pong
        d.alt[kFx][3]   = 0.4f; // tails
        d.bright        = 0.5f;
        d.pos           = 0.3f;
        d.key_mode      = static_cast<int>(phymo::KeyMode::Poly1);
        return d;
    }

    /** Which slots have a file (at startup). */
    void SetSlotUsed(int slot, bool used) { slot_used_[slot] = used; }
    void SetCardOk(bool ok) { card_ok_ = ok; }

    /** A save asked for: its slot (-1 none), and the sound into `s`. */
    int TakeSave(PresetState& s)
    {
        const int slot = req_save_;
        if(slot < 0)
            return -1;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        s         = out_state_;
        req_save_ = -1;
        return slot;
    }
    void SaveDone(int slot, bool ok)
    {
        if(ok)
        {
            slot_used_[slot] = true, loaded_ = slot, keep_ = true;
            undo_ = out_state_, undo_loaded_ = slot, tried_ = false; // (the empty slots play what was saved)
        }
        Flash(slot, ok ? 1 : 0);
    }
    int TakeDelete()
    {
        const int slot = req_delete_;
        req_delete_    = -1;
        return slot;
    }
    void DeleteDone(int slot, bool ok)
    {
        if(ok)
        {
            slot_used_[slot] = false;
            if(loaded_ == slot)
                loaded_ = -1;
        }
        Flash(slot, ok ? 2 : 0);
    }
    int TakeLoad()
    {
        const int slot = req_load_;
        req_load_      = -1;
        return slot;
    }
    /** The preset read: on at the next Poll (or a failure flashed). */
    void Loaded(int slot, const PresetState& s, bool ok)
    {
        if(!ok)
        {
            Flash(slot, 0);
            return;
        }
        in_state_ = s;
        in_slot_  = slot;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        in_ready_ = true;
    }

    // --- MIDI: midi_io.h's side ---

    /** A CC in: the panel's value it maps to, as if turned (no CC back out). */
    void MidiCc(int cc, int value)
    {
        const float x = value / 127.f;
        switch(cc)
        {
            case 1: synth_->SetMod(-x); return; // the mod wheel: LFO to the pitch (vibrato)
            case 7: vca_ = x, synth_->SetVolume(VcaGain(vca_)); return;
            case 64: SetHold(value >= 64); return; // the sustain pedal
            case 65:
                synth_->SetPortamento(value >= 64 ? phymo::PortaMode::Always : phymo::PortaMode::Off,
                                      PortaTime(knob_[kKbd][1]));
                return;
            case 71: pos_ = x, synth_->SetPosition(pos_); return;
            case 74: bright_ = x, synth_->SetBrightness(bright_); return;
            default: break;
        }
        for(const auto& m : kCcMap)
            if(m.cc == cc)
            {
                knob_[m.view][m.knob] = x;
                Apply(m.view, m.knob);
            }
    }
    /** A CC to send (a mapped knob turned); false when there's none. */
    bool PopCc(uint8_t& cc, uint8_t& value)
    {
        if(cc_tail_ == cc_head_)
            return false;
        cc       = cc_q_[cc_tail_][0];
        value    = cc_q_[cc_tail_][1];
        cc_tail_ = (cc_tail_ + 1) % kCcQueue;
        return true;
    }
    /** May the arp follow MIDI clock (KBD knob 4's click)? */
    bool SyncOk() const { return sync_ok_; }

  private:
    // --- keys ---

    void PollKeys()
    {
        auto& sr = hw_->button_sr;
        for(int sw = 0; sw < 40; sw++)
        {
            const int k = kSwKey[sw];
            if(k < 0)
                continue;
            if(sr.RisingEdge(sw))
            {
                down_[k]     = true;
                key_note_[k] = -1; // (a command key: its release ends nothing)
                // high A#, with CHOMPI or in the preset view: the presets'
                // key (tap: the view in / out; held a second: init)
                if(seq_mode_)
                {
                    SeqKeyDown(k);
                    continue;
                }
                if(kKeyCmd[k] == kCmdPresets && (chompi_held_ || presets_))
                {
                    a_down_    = true;
                    a_fired_   = false;
                    a_down_at_ = now_;
                    continue;
                }
                if(chompi_held_ && !presets_ && (view_ == kLfo || view_ == kEnv) && DestOfKey(k) >= 0)
                {
                    // the LFO and ENV views: CHOMPI + a destination's key (C to
                    // A) picks it, and while held shows where it is (its view's
                    // key, its knob)
                    dest_     = DestOfKey(k);
                    dest_at_  = now_ ? now_ : 1;
                    dest_key_ = k;
                    continue;
                }
                if(chompi_held_)
                {
                    // (in the preset view a view leaves it, the sound as it opened)
                    const int cmd = kKeyCmd[k];
                    if(presets_ && cmd >= 0 && cmd < kViews)
                        LeavePresets(false);
                    Command(k);
                    continue;
                }
                const int note = kLowestNote + 12 * octave_ + k;
                if(latched_[note])
                {
                    // that very note, held by hold: pressing it lets it go
                    // (its release then does nothing)
                    synth_->NoteOff(note);
                    latched_[note] = false;
                    continue;
                }
                key_note_[k] = note;
                if(chord_edit_)
                    ChordToggle(note);
                synth_->NoteOn(note);
            }
            else if(sr.FallingEdge(sw))
            {
                down_[k] = false;
                if(seq_mode_ && SeqKeyUp(k))
                    continue;
                if(k == dest_key_)
                {
                    dest_key_ = -1;
                    continue;
                }
                if(kKeyCmd[k] == kCmdPresets && a_down_)
                {
                    a_down_ = false;
                    if(!a_fired_) // a tap: the preset view in, or out (the sound as it opened)
                    {
                        if(presets_)
                            LeavePresets(false);
                        else
                            EnterPresets();
                    }
                    continue;
                }
                const int note = key_note_[k];
                key_note_[k]   = -1;
                if(note < 0)
                    continue; // a command key, or one that let a held note go
                // step entry: every key that went in let go, the next step
                if(seq_mode_ && entry_down_ > 0 && --entry_down_ == 0)
                    synth_->Seq().EntryDone();
                if(hold_ && !seq_mode_)
                    latched_[note] = true; // keeps sounding until hold is off
                else
                    synth_->NoteOff(note); // the note it started, even if the octave changed since
            }
        }
    }

    void Command(int k)
    {
        // the KBD view: the lowest white keys pick the key mode
        if(view_ == kKbd)
            for(int m = 0; m < 5; m++)
                if(k == kModeKey[m])
                {
                    ChangeMode(static_cast<phymo::KeyMode>(m));
                    return;
                }
        const int cmd = kKeyCmd[k];
        if(cmd >= 0 && cmd < kViews)
        {
            // tapped again (not just leaving the presets): MODEL's the next
            // model, BODY's the next pickup
            if(cmd == view_ && !presets_ && cmd == kModel)
                synth_->SetModel(static_cast<phymo::Model>((static_cast<int>(synth_->GetModel()) + 1)
                                                           % static_cast<int>(phymo::Model::Count)));
            else if(cmd == view_ && !presets_ && cmd == kBody)
                synth_->SetPickup(static_cast<phymo::PickupMode>((static_cast<int>(synth_->GetPickup()) + 1)
                                                                 % static_cast<int>(phymo::PickupMode::Count)));
            // the LFO's key: tapped again, the ENV view, and back
            if(cmd == kLfo && !presets_ && (view_ == kLfo || view_ == kEnv))
                view_ = view_ == kLfo ? kEnv : kLfo;
            else
                view_ = static_cast<View>(cmd);
        }
        else if(cmd == kCmdOctDown && octave_ > -2)
            octave_--;
        else if(cmd == kCmdOctUp && octave_ < 2)
            octave_++;
    }

    /** Changing mode lets every voice go, so forget what hold was holding
     *  too (keys still down end on release). */
    void ChangeMode(phymo::KeyMode m)
    {
        if(synth_->GetKeyMode() != m)
            for(int n = 0; n < 128; n++)
                latched_[n] = false;
        synth_->SetKeyMode(m);
    }

    /** Starts choosing a chord (from none), or saves the one chosen. */
    void ChordEdit(bool on)
    {
        chord_edit_ = on;
        synth_->SetChordEdit(on);
        if(on)
        {
            chord_n_ = 0;
            return;
        }
        if(chord_n_ == 0)
            return; // nothing chosen: the chord in memory stays
        synth_->SetChord(chord_notes_, chord_n_);
        ChangeMode(phymo::KeyMode::Chord);
    }

    /** While choosing: a key joins the chord, or leaves it if it's in. */
    void ChordToggle(int note)
    {
        for(int i = 0; i < chord_n_; i++)
            if(chord_notes_[i] == note)
            {
                chord_notes_[i] = chord_notes_[--chord_n_];
                return;
            }
        if(chord_n_ < phymo::Synth::kVoices)
            chord_notes_[chord_n_++] = note;
    }

    /** Is `note` sounding from a key: 2 a key's down playing it, 1 it's
     *  held by hold, 0 neither. By pitch, so wherever the octave keys put
     *  the keyboard, a note shows where it sounds (or not, off it). */
    int NoteLed(int note) const
    {
        for(int j = 0; j < 25; j++)
            if(down_[j] && key_note_[j] == note)
                return 2;
        return note >= 0 && note < 128 && latched_[note] ? 1 : 0;
    }

    /** What key `k` shows of the arp's note: 1 it's that key's pitch on
     *  the keyboard as the octave keys have set it; 2 it's below the
     *  keyboard and `k` is the low C, or above it and `k` the high C; 0
     *  neither (or the arp's not playing). */
    int ArpLed(int k) const
    {
        const int a = synth_->GetArpNote();
        if(a < 0)
            return 0;
        const int i = a - (kLowestNote + 12 * octave_);
        if(i == k)
            return 1;
        if((i < 0 && k == 0) || (i > 24 && k == 24))
            return 2;
        return 0;
    }

    bool InChord(int note) const
    {
        for(int i = 0; i < chord_n_; i++)
            if(chord_notes_[i] == note)
                return true;
        return false;
    }

    void SetHold(bool on)
    {
        hold_ = on;
        synth_->SetLatchLoop(on); // held notes, faded, strike again
        if(on)
            return;
        // Off: let go of every note that's only sounding because of hold.
        for(int n = 0; n < 128; n++)
            if(latched_[n])
            {
                synth_->NoteOff(n);
                latched_[n] = false;
            }
    }

    // --- knobs ---

    /** How many steps a detent is worth, from the time since the knob's
     *  last detent (ms). */
    static int Accel(uint32_t dt) { return dt < 12 ? 6 : dt < 25 ? 3 : dt < 50 ? 2 : 1; }

    void Turn(int knob, int inc, int accel)
    {
        const int fast = inc * accel; // bend and mod use plain `inc`
        if(knob == 5 && seq_mode_ && mix_page_)
        {
            mix_ = Clamp01(mix_ + fast * kKnobStep);
            ApplyMix();
        }
        else if(knob == 5)
        {
            vca_ = Clamp01(vca_ + fast * kKnobStep);
            synth_->SetVolume(VcaGain(vca_));
            SendCc(7, vca_);
        }
        else if(knob == 4)
            TurnWheel(inc, fast);
        else if(seq_mode_ && knob == 3 && chompi_held_)
        {
            swing_ = Clamp01(swing_ + fast * kKnobStep); // CHOMPI + tempo: the swing
            synth_->Seq().SetSwing(0.5f + 0.25f * swing_);
        }
        else if(seq_mode_ && knob == 0)
        {
            // the step's size: a step every two clicks (a nudge doesn't change it)
            div_acc_ += inc;
            if(div_acc_ >= 2 || div_acc_ <= -2)
            {
                const int last = phymo::Synth::kSeqDivs - 1;
                int       d    = synth_->GetSeqDivision() + (div_acc_ > 0 ? 1 : -1);
                d              = d < 0 ? 0 : d > last ? last : d;
                div_acc_       = 0;
                seq_knob_[0]   = (d + 0.5f) / phymo::Synth::kSeqDivs;
                ApplySeq(0);
            }
        }
        else if(seq_mode_)
        {
            seq_knob_[knob] = Clamp01(seq_knob_[knob] + fast * kKnobStep);
            ApplySeq(knob);
        }
        else if(Built(view_, knob))
        {
            if(chompi_held_ && HasAlt(view_, knob))
            {
                float& a = Alt(view_, knob);
                a        = Clamp01(a + fast * kKnobStep);
                if(&a == &pos_)
                    SendCc(71, pos_);
            }
            else
            {
                // LFO knob 1 turned on its own: the free rate, so sync off
                if(view_ == kLfo && knob == 0)
                    alt_[kLfo][0] = 0.f;
                float& x = Knob(view_, knob);
                x        = Clamp01(x + fast * kKnobStep);
                for(const auto& m : kCcMap)
                    if(m.view == view_ && m.knob == knob)
                        SendCc(m.cc, x);
                if(view_ == kLfo && knob >= 2)
                    dest_at_ = now_ ? now_ : 1; // (the routing shown on the keys)
            }
            Apply(view_, knob);
        }
    }

    void TurnWheel(int inc, int fast)
    {
        if(hw_->enc[4].Pressed() && seq_mode_ && !pattern_view_)
        {
            // pressed and turned: the pattern's length, the view on its end
            auto& q       = synth_->Seq();
            wheel_turned_ = true;
            q.SetLength(q.Length() + inc);
            ShowStep(q.Length() - 1);
        }
        else if(hw_->enc[4].Pressed())
        {
            wheel_turned_ = true;
            wheel_val_ = wheel_val_ + inc * kBendStep;
            wheel_val_ = wheel_val_ < -1.f ? -1.f : wheel_val_ > 1.f ? 1.f : wheel_val_;
            if(wheel_mod_)
                synth_->SetMod(wheel_val_);
            else
                synth_->SetBend(wheel_val_);
        }
        else if(presets_ && !seq_mode_)
            WheelSlot(inc);
        else if(seq_mode_)
        {
            if(pattern_view_) // the pattern page: the bank
                view_bank_ = (view_bank_ + (inc > 0 ? 1 : kBanks - 1)) % kBanks;
            else if(chompi_held_ && strum_page_ && synth_->GetSeqSync()) // the STRUM track synced: CHOMPI + wheel, its share of the step
            {
                seq_spread_ = Clamp01(seq_spread_ + fast * kKnobStep);
                synth_->SetSeqSpread(seq_spread_);
            }
            else if(chompi_held_ && strum_page_) // ... free: its spacing
            {
                seq_strum_ = Clamp01(seq_strum_ + fast * kKnobStep);
                synth_->SetSeqStrum(phymo::Synth::kStrumTime * seq_strum_ * seq_strum_);
            }
            else if(chompi_held_)
                NoteLength(inc);
            else
            {
                // the step the keys enter into, round the pattern's length
                auto&     q   = synth_->Seq();
                const int len = q.Length() < 1 ? 1 : q.Length();
                q.Select(((q.Selected() + inc) % len + len) % len);
                ShowStep(q.Selected());
                if(q.Running())
                    scroll_at_ = now_ ? now_ : 1; // (the pages shown a second, then the playhead again)
                else if(armed_) // armed, stopped: the step heard as it's chosen
                    synth_->AuditionStep(q.pattern.step[q.Selected()]);
            }
        }
        else if(chompi_held_ && synth_->GetModel() == phymo::Model::Vocal)
        {
            // the Vocal model: its ensemble (no strike to place; kept in
            // MODEL knob 3's slot, `ensemble`, as it was)
            alt_[kModel][2] = Clamp01(alt_[kModel][2] + fast * kWheelStep);
            synth_->SetEnsemble(alt_[kModel][2]);
        }
        else if(chompi_held_)
        {
            pos_ = Clamp01(pos_ + fast * kWheelStep);
            synth_->SetPosition(pos_);
            SendCc(71, pos_);
        }
        else
        {
            bright_ = Clamp01(bright_ + fast * kWheelStep);
            synth_->SetBrightness(bright_);
            SendCc(74, bright_);
        }
    }

    /** A knob's click: its own switch where it has one (the LFO's trigger,
     *  FM's list, the arp's sync, the chorus, the delay's glide,
     *  portamento's mode); otherwise its value back to its default. CHOMPI +
     *  a click: its CHOMPI + turn value back to the default (or, a knob
     *  with none, its value: so a knob with a switch can still be reset). */
    void Click(int k)
    {
        if(seq_mode_)
            return; // (the sequencer's knobs: none of these)
        if(chompi_held_)
        {
            Reset(view_, k, HasAlt(view_, k));
            return;
        }
        const bool fm = synth_->GetModel() == phymo::Model::Fm;
        if(view_ == kLfo && k == 1)
        {
            using T = phymo::LfoTrigger; // NOTE, FREE, VOICE, round
            const T t = synth_->GetLfoTrigger();
            synth_->SetLfoTrigger(t == T::Note ? T::Free : t == T::Free ? T::Voice : T::Note);
            trig_at_ = now_ ? now_ : 1;
        }
        else if(view_ == kLfo && k >= 2)
        {
            dest_    = (dest_ + 1) % phymo::kDests; // the next destination (knobs 3 and 4 together)
            dest_at_ = now_ ? now_ : 1;
        }
        else if(view_ == kModel && k == 0 && fm)
        {
            synth_->SetFmList((synth_->GetFmList() + 1) % phymo::kFmLists);
            list_at_ = now_ ? now_ : 1; // (knob 1 in the list's colour a moment)
        }
        else if(view_ == kKbd && k == 3)
            sync_ok_ = !sync_ok_; // the arp follows MIDI clock when it comes (or not)
        else if(view_ == kFx && k == 0)
            synth_->SetChorus(NextChorus(synth_->GetChorus()));
        else if(view_ == kFx && k == 3)
            synth_->SetDelayGlide(!synth_->GetDelayGlide());
        else if(view_ == kEnv && k == 1)
            synth_->SetCycle(!synth_->GetCycle()); // cycle: held notes strike again each attack and decay
        else
            Reset(view_, k, false);
    }

    /** Knob `k` of view `v` (its CHOMPI + turn value, `alt`) back to its default. */
    void Reset(int v, int k, bool alt)
    {
        const PresetState d = Defaults();
        if(alt)
        {
            float& a = Alt(v, k);
            a        = &a == &pos_ ? d.pos : (&d.alt[0][0])[&a - &alt_[0][0]]; // (the slot's own default)
            if(&a == &pos_)
                synth_->SetPosition(pos_), SendCc(71, pos_);
        }
        else
        {
            Knob(v, k) = v == kLfo && k >= 2 ? 0.5f : d.knob[v][k]; // (a depth: none)
            for(const auto& m : kCcMap)
                if(m.view == v && m.knob == k)
                    SendCc(m.cc, knob_[v][k]);
        }
        Apply(v, k);
    }

    /** Is knob `k` of view `v` built yet? */
    bool Built(int v, int k) const
    {
        // (FM's EXCITER knob 1: free, but for its CHOMPI + turn, Life)
        return !(v == kExc && k == 0 && !chompi_held_ && synth_->GetModel() == phymo::Model::Fm);
    }

    /** Does knob `k` of view `v` have a CHOMPI + turn value (built so far)? */
    bool HasAlt(int v, int k) const
    {
        if(v == kModel && k <= 2 && synth_->GetModel() == phymo::Model::Vocal)
            return false; // Vocal: none here (its tape, magnetism, CHOMPI + EXCITER knob 1; its ensemble, CHOMPI + the wheel)
        if(v == kModel && k == 3)
            return synth_->GetModel() == phymo::Model::Spectral; // Spectral: its tonebar
        if(v == kModel && k == 0)
            return synth_->GetModel() != phymo::Model::Fm;
        if(v == kBody && k == 0)
            return synth_->GetModel() != phymo::Model::Fm; // the pickup's place (FM: no string)
        if(v == kExc && (k == 1 || k == 2) && synth_->GetModel() == phymo::Model::Bow)
            return true; // the bow's weight, its strokes
        return (v == kFx && k != 1) || v == kKbd || (v == kLfo && k <= 1) || (v == kBody && k == 3)
               || (v == kBody && k == 2 && Vibrato())
               || (v == kModel && k == 1) || (v == kExc && (k == 0 || k == 3));
    }

    /** Where knob `k` of view `v` keeps its value: the LFO view's knobs 3
     *  and 4 the envelope's and the LFO's depth to the destination they
     *  show (dest_), as their knobs (0.5 none); otherwise its own. */
    const float& Knob(int v, int k) const
    {
        if(v == kLfo && k >= 2)
            return k == 2 ? env_to_[dest_] : lfo_to_[dest_];
        return knob_[v][k];
    }
    float& Knob(int v, int k) { return const_cast<float&>(static_cast<const Panel*>(this)->Knob(v, k)); }

    /** Every destination's depths to the synth. */
    void ApplyDepths()
    {
        for(int d = 0; d < phymo::kDests; d++)
        {
            synth_->SetEnvDepth(static_cast<phymo::Dest>(d), Centred(env_to_[d]));
            synth_->SetLfoDepth(static_cast<phymo::Dest>(d), Centred(lfo_to_[d]));
        }
    }

    /** Where knob `k` of view `v` keeps its CHOMPI + turn value. MODEL knob
     *  1's is the model's own: Spectral's partials, String's second string
     *  (kept in RESONATOR knob 1's slot, where it was, so presets keep it),
     *  Bowed's place (the wheel's, CHOMPI + turn: the same value). MODEL
     *  knob 2's, String's: its second string's interval (RESONATOR knob
     *  2's slot), the others' snap. */
    const float& Alt(int v, int k) const
    {
        if(v == kBody && k == 3 && Vibrato())
            return alt_[kBody][1]; // Bowed: the vibrato's release (kept in BODY knob 2's slot, which has none; its onset, knob 3's own)
        if(v == kModel && k == 0)
        {
            const auto m = synth_->GetModel();
            return m == phymo::Model::String ? alt_[kRes][0] : m == phymo::Model::Bow ? pos_ : alt_[kModel][0];
        }
        if(v == kModel && k == 1 && synth_->GetModel() == phymo::Model::String)
            return alt_[kRes][1]; // String: the second string's interval (kept in RESONATOR knob 2's slot, which has none)
        if(v == kModel && k == 1 && synth_->GetModel() == phymo::Model::Bow)
            return alt_[kRes][2]; // Bowed: the flageolet (kept in RESONATOR knob 3's slot, which has none)
        if(v == kExc && k == 0 && synth_->GetModel() == phymo::Model::Vocal)
            return tape_; // Vocal: its tape, "magnetism", in Life's place (its own: `tape` in a file)
        return alt_[v][k];
    }
    float& Alt(int v, int k) { return const_cast<float&>(static_cast<const Panel*>(this)->Alt(v, k)); }

    /** The BODY view's knobs 3 and 4 a vibrato, not the tremolo (Bowed, Vocal). */
    bool Vibrato() const
    {
        const auto m = synth_->GetModel();
        return m == phymo::Model::Bow || m == phymo::Model::Vocal;
    }

    /** CHOMPI + MODEL knob 1's colour, by model: partials green, the second
     *  string purple, the bow's place white. */
    const float* ModelAltCol() const
    {
        const auto m = synth_->GetModel();
        return m == phymo::Model::String ? kPurple : m == phymo::Model::Bow ? kWhite : kGreen;
    }

    /** A knob that's none at its middle (Stretch, Morph): -1 to 1, with a
     *  flat stretch either side of the middle (kFlat) that's exactly 0, so
     *  none is easy to turn to. */
    static float Centred(float x)
    {
        const float d = 2.f * x - 1.f, a = fabsf(d);
        if(a <= kFlat)
            return 0.f;
        return (d < 0.f ? -1.f : 1.f) * (a - kFlat) / (1.f - kFlat);
    }

    /** LFO knob 3's value to its shape, in six equal parts. */
    static int ShapeOf(float x)
    {
        const int p = static_cast<int>(x * 6.f);
        return p > 5 ? 5 : p;
    }

    /** CHOMPI + LFO knob 1: 0 free, then 1/8, 1/4, 1/2, 1 and 2 LFO
     *  cycles an arp step, in six equal parts. */
    static float SyncOf(float x)
    {
        static const float kSync[6] = {0.f, .125f, .25f, .5f, 1.f, 2.f};
        const int          p        = static_cast<int>(x * 6.f);
        return kSync[p > 5 ? 5 : p];
    }

    /** KBD knob 3's value to its pattern, in five equal parts. */
    static int PatternOf(float x)
    {
        const int p = static_cast<int>(x * 5.f);
        return p > 4 ? 4 : p;
    }

    /** KBD knob 2: portamento time, 5 ms to 2 s. */
    static float PortaTime(float x) { return 0.005f * powf(400.f, x); }

    /** What knob `k`'s LED shows in view `v`: the CHOMPI + turn value while
     *  CHOMPI is held, if it has one. */
    float Value(int v, int k) const { return chompi_held_ && HasAlt(v, k) ? Alt(v, k) : Knob(v, k); }

    /** Sends knob `k`'s values (turn, and CHOMPI + turn) in view `v` to the synth. */
    void Apply(int v, int k)
    {
        const float x = knob_[v][k];
        switch(v)
        {
            case kLfo:
                if(k == 0)
                {
                    synth_->SetLfoRate(0.1f * powf(300.f, x)); // 0.1 .. 30 Hz (to measure)
                    synth_->SetLfoSync(SyncOf(alt_[kLfo][0]));
                }
                else if(k == 1)
                {
                    synth_->SetLfoShape(static_cast<phymo::Lfo::Shape>(ShapeOf(x)));
                    SetLfoDelay(alt_[kLfo][1]);
                }
                else if(k == 2) // (the pitch's squared in the synth: the low end a fine vibrato)
                    synth_->SetEnvDepth(static_cast<phymo::Dest>(dest_), Centred(env_to_[dest_]));
                else
                    synth_->SetLfoDepth(static_cast<phymo::Dest>(dest_), Centred(lfo_to_[dest_]));
                break;
            case kEnv: // attack 1 ms .. 5 s, decay and release 5 ms .. 10 s, sustain
                synth_->SetEnv(0.001f * powf(5000.f, knob_[kEnv][0]), 0.005f * powf(2000.f, knob_[kEnv][1]),
                               knob_[kEnv][2], 0.005f * powf(2000.f, knob_[kEnv][3]));
                break;
            case kKbd:
                if(k == 0)
                {
                    synth_->SetKeyTrack(x);
                    synth_->SetDetune(alt_[kKbd][0]);
                }
                else if(k == 1) // at the least, off; CHOMPI + turn, the strum
                {
                    synth_->SetPortamento(x <= 0.f ? phymo::PortaMode::Off : phymo::PortaMode::Always, PortaTime(x));
                    synth_->SetStrum(alt_[kKbd][1]);
                }
                else if(k == 2)
                {
                    synth_->SetArpPattern(static_cast<phymo::Arp::Pattern>(PatternOf(x)));
                    synth_->SetArpOctaves(1 + static_cast<int>(alt_[kKbd][2] * 3.999f));
                }
                else if(k == 3)
                {
                    // a division of the master tempo, in seven equal parts: its
                    // step in clock ticks (24 a beat)
                    static const int kDiv[7] = {24, 16, 12, 8, 6, 4, 3}; // 1/4, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32
                    const int        d       = static_cast<int>(x * 7.f);
                    synth_->SetArpDivision(kDiv[d > 6 ? 6 : d]);
                    synth_->SetArpRandom(alt_[kKbd][3]);
                }
                break;
            case kFx:
                if(k == 0)
                {
                    synth_->SetChorusLevel(x);
                    synth_->SetSpread(alt_[kFx][0]);
                }
                else if(k == 1)
                    synth_->SetSend(x);
                else if(k == 2)
                {
                    synth_->SetSpace(x);
                    synth_->SetPingPong(alt_[kFx][2]);
                }
                else
                {
                    synth_->SetTime(x);
                    synth_->SetTails(alt_[kFx][3]);
                }
                break;
            case kModel:
                if(k == 0)
                {
                    synth_->SetTable(x);
                    synth_->SetPartials(alt_[kModel][0]);
                    synth_->SetCourse(alt_[kRes][0]);
                    synth_->SetPosition(pos_);
                }
                else if(k == 1)
                {
                    synth_->SetStretch(0.5f + 0.5f * Centred(x));
                    synth_->SetTape(tape_); // (Vocal's: CHOMPI + this knob)
                    synth_->SetSnap(alt_[kModel][1]);
                    synth_->SetInterval(alt_[kRes][1]);
                    synth_->SetFlageolet(alt_[kRes][2]);
                }
                else if(k == 2)
                {
                    synth_->SetMorph(0.5f + 0.5f * Centred(x));
                    synth_->SetEnsemble(alt_[kModel][2]); // (Vocal's: CHOMPI + this knob)
                }
                else
                {
                    synth_->SetMorphTime(0.02f * powf(400.f, x)); // 20 ms .. 8 s
                    synth_->SetTonebar(alt_[kModel][3]); // (Spectral's: CHOMPI + this knob)
                }
                break;
            case kBody:
                if(k == 0)
                {
                    synth_->SetPickupAmount(x);
                    synth_->SetPickupPlace(alt_[kBody][0]);
                }
                else if(k == 1)
                    synth_->SetPickupTone(x);
                else if(k == 2)
                    synth_->SetTremoloRate(1.5f * powf(8.f, x)); // 1.5 .. 12 Hz
                else
                {
                    synth_->SetTremoloDepth(x);
                    synth_->SetTremoloSpread(alt_[kBody][3]);
                }
                // (and, the same knobs, the Bowed model's vibrato: 3 .. 7 Hz,
                // its depth, its onset 0 .. 2 s; the synth hears whichever
                // the model has)
                synth_->SetVibrato(3.f * powf(7.f / 3.f, knob_[kBody][2]), knob_[kBody][3], 2.5f * alt_[kBody][2],
                                   alt_[kBody][1] >= kEndless ? -1.f : 3.f * powf(4.f, alt_[kBody][1] / kEndless));
                break;
            case kExc:
                if(k == 0)
                {
                    synth_->SetType(x);
                    synth_->SetLife(alt_[kExc][0]);
                    synth_->SetTape(tape_); // (Vocal's: CHOMPI + this knob, magnetism)
                }
                else if(k == 1 || k == 2)
                {
                    if(k == 1)
                        synth_->SetHardness(x);
                    else
                        synth_->SetNoise(x);
                    // (Bowed: CHOMPI + knob 3, its strokes' length, 0.04 (a tremolo) .. 6 s each
                    // way, endless at the top; CHOMPI + knob 2, its weight)
                    const float st = alt_[kExc][2];
                    synth_->SetBowStroke(st >= kEndless ? 0.f : kStrokeMin * powf(kStrokeRange, st / kEndless), alt_[kExc][1]);
                }
                else
                {
                    synth_->SetStrength(x);
                    synth_->SetVelocityAmount(alt_[kExc][3]);
                }
                break;
            case kRes:
                if(k == 0) // none, then 2 ms .. 2 s
                    synth_->SetAttack(x > 0.f ? 0.002f * powf(1000.f, x) : 0.f);
                else if(k == 1)
                    synth_->SetDecay(0.1f * powf(200.f, x)); // the ring, 0.1 .. 20 s
                else if(k == 2)
                    synth_->SetDamper(0.03f * powf(100.f, x)); // 30 ms .. 3 s
                else
                    synth_->SetSustain(x);
                break;
            default: break;
        }
    }

    /** LFO delay, after the Juno-60's slider: at 0, 2.5, 5, 7.5 and 10, the
     *  silence and then the fade-in time, interpolated between. */
    void SetLfoDelay(float v)
    {
        static const float kDelay[5] = {0.f, 0.0639f, 0.85f, 1.2f, 2.786f};
        static const float kFade[5]  = {0.f, 0.053f, 0.188f, 0.348f, 1.f};
        const float        x = v * 4.f;
        const int          i = x >= 4.f ? 3 : static_cast<int>(x);
        const float        f = x - static_cast<float>(i);
        synth_->SetLfoDelay(kDelay[i] + (kDelay[i + 1] - kDelay[i]) * f, kFade[i] + (kFade[i + 1] - kFade[i]) * f);
    }

    // --- LEDs ---

    void DrawKeys()
    {
        if(seq_mode_)
        {
            for(int k = 0; k < 25; k++)
            {
                float r, g, b;
                SeqKeyColour(k, r, g, b);
                SetSmtLedFloat(key_led_[k], r, g, b);
            }
            return;
        }
        static const int kWhiteKeys[15] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
        // the keys play notes without CHOMPI, commands with it (in the
        // preset view the slots and banks shown either way: CHOMPI + PLAY
        // saves, the slot in view)
        const bool notes = !chompi_held_;
        for(int k = 0; k < 25; k++)
        {
            float r = 0.f, g = 0.f, b = 0.f;
            if(presets_)
                PresetKeyColour(k, r, g, b);
            else if(notes)
            {
                // notes: bright while down, dimmer while held by hold;
                // choosing a chord, its notes teal; the arp's note red on
                // its key, or the edge C pink when it's off the keyboard
                const int arp  = ArpLed(k);
                const int held = NoteLed(kLowestNote + 12 * octave_ + k);
                if(arp == 1)
                    r = 1.f, g = 0.f, b = 0.f;
                else if(arp == 2)
                    r = kPink[0], g = kPink[1], b = kPink[2];
                else if(held == 2)
                    r = 1.f, g = .7f, b = .4f;
                else if(chord_edit_ && InChord(kLowestNote + 12 * octave_ + k))
                    r = kTeal[0], g = kTeal[1], b = kTeal[2];
                else if(held == 1)
                {
                    // held by hold: fading with the note, brightening as it strikes again
                    const float f = 0.15f + 0.85f * synth_->NoteFade(kLowestNote + 12 * octave_ + k);
                    r = .55f * f, g = .4f * f, b = .22f * f;
                }
            }
            else
                KeyCommandColour(k, r, g, b);
            SetSmtLedFloat(key_led_[k], r, g, b);
        }
        // the LFO view: a destination just clicked or its depth turned, C
        // to A show the routing a while; the trigger just clicked, C to E
        // show it (over the notes)
        if(notes && !presets_ && view_ == kLfo)
        {
            if(trig_at_ && now_ - trig_at_ < kShowFor)
                for(int w = 0; w < 3; w++)
                    TriggerKey(w, key_led_[kWhiteKeys[w]]);
            else if(dest_at_ && now_ - dest_at_ < kShowFor)
                for(int w = 0; w < phymo::kDests; w++)
                    RoutingKey(w, key_led_[kWhiteKeys[w]]);
        }
        if(notes || presets_)
            return;
        // the white keys: the CPU load, lit up to the average, the peak's dim.
        // In the KBD view, C to G show the key mode instead; in the LFO view,
        // the routing.
        const bool modes = view_ == kKbd, routing = view_ == kLfo;
        for(int w = 0; w < 15; w++)
        {
            if(modes && w < 5)
            {
                const float s = static_cast<int>(synth_->GetKeyMode()) == w ? 1.f : 0.12f;
                SetSmtLedFloat(key_led_[kWhiteKeys[w]], kYellow[0] * s, kYellow[1] * s, kYellow[2] * s);
                continue;
            }
            if(routing && w < phymo::kDests)
            {
                RoutingKey(w, key_led_[kWhiteKeys[w]]);
                continue;
            }
            const float pct = (w + 1) / 15.f;
            const float lit = pct <= load_avg_ + 1e-4f ? 1.f : pct <= load_max_ + 1e-4f ? 0.25f : 0.f;
            float       r = 0.f, g = 0.f;
            if(pct > 0.9f)
                r = lit;
            else if(pct > 0.6f)
                r = lit, g = 0.5f * lit;
            else
                g = lit;
            SetSmtLedFloat(key_led_[kWhiteKeys[w]], r, g, 0.f);
        }
    }

    static constexpr float    kStrokeMin = 0.04f, kStrokeRange = 150.f; // the strokes: 0.04 s each way (a tremolo, ~12 changes a second) to 6 s
    static constexpr float    kEndless = 0.97f; // a knob from here up: never (the vibrato's decay, the bow's strokes)
    static constexpr uint32_t kShowFor = 1200; // ms the routing, or the trigger, shows on the keys after a click

    /** The destination on key `k` (C to A: kDests of the white keys), or -1. */
    static int DestOfKey(int k)
    {
        static const int kWhite[phymo::kDests] = {0, 2, 4, 5, 7, 9};
        for(int w = 0; w < phymo::kDests; w++)
            if(kWhite[w] == k)
                return w;
        return -1;
    }

    /** Where destination `d`'s control is: its view and knob (-1 both: the
     *  wheel's, pitch and brightness). The exciter's by the model: FM's
     *  depth and the bow's pressure EXCITER knob 2, else Stretch. */
    void DestTarget(int d, int& view, int& knob) const
    {
        const auto m  = synth_->GetModel();
        const bool ex = m == phymo::Model::Fm || m == phymo::Model::Bow;
        switch(static_cast<phymo::Dest>(d))
        {
            case phymo::Dest::Morph: view = kModel, knob = 0; break;
            case phymo::Dest::Exciter: view = ex ? kExc : kModel, knob = 1; break;
            case phymo::Dest::Tone: view = kBody, knob = 1; break;
            case phymo::Dest::Place: view = kBody, knob = 0; break;
            default: view = knob = -1; break;
        }
    }

    /** Destination `d`'s colour: its control's (kDestCol; the morph the
     *  model's, the exciter's by the model). */
    const float* DestCol(int d) const
    {
        const auto m = synth_->GetModel();
        if(d == static_cast<int>(phymo::Dest::Morph))
            return kModelCol[static_cast<int>(m)];
        if(d == static_cast<int>(phymo::Dest::Exciter))
            return m == phymo::Model::Fm || m == phymo::Model::Bow ? kOrange : kPurple;
        return kDestCol[d];
    }

    /** Destination `w`'s key in the routing: the one knobs 3 and 4 show
     *  bright, others with a depth (either) dim, the rest all but dark; in
     *  the destination's colour. */
    void RoutingKey(int w, int8_t led)
    {
        const bool  on = Centred(lfo_to_[w]) != 0.f || Centred(env_to_[w]) != 0.f;
        const float s  = w == dest_ ? 1.f : on ? 0.3f : 0.04f;
        const float* dc = DestCol(w);
        SetSmtLedFloat(led, dc[0] * s, dc[1] * s, dc[2] * s);
    }

    /** The LFO's trigger on a key: NOTE, FREE, VOICE (magenta, the one it is bright). */
    void TriggerKey(int w, int8_t led)
    {
        const float s = static_cast<int>(synth_->GetLfoTrigger()) == w ? 1.f : 0.08f;
        SetSmtLedFloat(led, kMagenta[0] * s, kMagenta[1] * s, kMagenta[2] * s);
    }

    /** With CHOMPI held: what black key `k` shows. */
    void KeyCommandColour(int k, float& r, float& g, float& b) const
    {
        const int cmd = kKeyCmd[k];
        if(cmd >= 0 && cmd < kViews)
        {
            int dv = -1, dk;
            if(dest_key_ >= 0 && chompi_held_)
                DestTarget(dest_, dv, dk);
            const bool on = dest_key_ >= 0 && chompi_held_ ? cmd == dv : cmd == view_ || (cmd == kLfo && view_ == kEnv);
            const float  s  = on ? 1.f : 0.15f;
            const float* c  = kViewCol[cmd == kLfo && view_ == kEnv ? kEnv : cmd];
            if(cmd == kModel) // in the model's colour
                c = kModelCol[static_cast<int>(synth_->GetModel())];
            else if(cmd == kBody && synth_->GetPickup() != phymo::PickupMode::Clean) // in the pickup's
                c = kPickupCol[static_cast<int>(synth_->GetPickup())];
            r = c[0] * s, g = c[1] * s, b = c[2] * s;
        }
        else if(cmd == kCmdOctDown || cmd == kCmdOctUp)
        {
            // each step of octave lights its key brighter
            const int   steps = cmd == kCmdOctDown ? (octave_ < 0 ? -octave_ : 0) : (octave_ > 0 ? octave_ : 0);
            const float s     = steps == 0 ? 0.1f : steps == 1 ? 0.5f : 1.f;
            r = g = b = s;
        }
        else if(cmd == kCmdPresets)
        {
            const float s = presets_ ? 1.f : 0.4f; // dark blue: the presets' key
            r = kDarkBlue[0] * s, g = kDarkBlue[1] * s, b = kDarkBlue[2] * s;
        }
    }

    /** The chorus's mode as a colour: off dim white, I blue, II violet,
     *  I + II white, I | II teal. */
    void ChorusColour(float& r, float& g, float& b) const
    {
        {
            using Mode   = phymo::Chorus::Mode;
            const Mode m = synth_->GetChorus();
            if(m == Mode::I)
                r = 0.f, g = .3f, b = 1.f;
            else if(m == Mode::II)
                r = .6f, g = 0.f, b = 1.f;
            else if(m == Mode::Both)
                r = g = b = .8f;
            else if(m == Mode::Parallel)
                r = 0.f, g = .8f, b = .6f;
            else
                r = g = b = 0.1f;
        }
    }

    /** The wheel's left and right LEDs, at brightness `l` and `r`, in `c`.
     *  (kKnobLed[4] is taken as the left one: swap if it's the right.) */
    void WheelLeds(float l, float r, const float* c)
    {
        SetPthLedFloat(kKnobLed[4], c[0] * l, c[1] * l, c[2] * l);
        SetPthLedFloat(kKnobLed2[4], c[0] * r, c[1] * r, c[2] * r);
    }

    // --- sequencer mode ---

    /** Start / stop the sequence. */
    void SeqPlay()
    {
        if(synth_->Seq().Running())
        {
            synth_->SeqStop();
            if(q_bank_ >= 0) // a pattern queued: in now
                SelectPattern(q_bank_, q_pat_);
        }
        else
            synth_->SeqStart();
    }

    /** Record armed (LOOP), or not. Armed while stopped, step entry starts
     *  at step 1; running, notes played go in. */
    void Arm(bool on)
    {
        armed_   = on;
        entered_ = false;
        synth_->SetRecord(on);
        if(on && !synth_->Seq().Running())
        {
            synth_->Seq().EntryFrom(0);
            ShowStep(0);
        }
    }

    /** The wheel clicked while recording: a rest. Stopped, the step being
     *  entered is emptied and the next comes up; running, the step playing
     *  is emptied. */
    void Rest()
    {
        auto& q = synth_->Seq();
        if(q.Running())
        {
            if(q.Playhead() >= 0)
                q.ClearStep(q.Playhead());
            return;
        }
        if(entry_down_ > 0)
            return; // (notes going in: not now)
        q.EntryRest();
        entered_ = true;
        ShowStep(q.Selected());
    }

    /** A key down in sequencer mode. */
    void SeqKeyDown(int k)
    {
        const int w = SlotOf(k);
        if(pattern_view_ || copy_down_)
        {
            if(w >= 0) // a pattern: COPY held, the playing one copied there; else it plays when let go
            {
                if(copy_down_)
                    CopyTo(w);
                else
                    pat_key_ = w;
                return;
            }
            if(!chompi_held_) // a black key: out
            {
                pattern_view_ = false;
                return;
            }
        }
        if(transpose_mode_)
        {
 // each key sets the transpose (its distance from middle C)
            // and it stays; the same key twice quickly sets it and leaves.
            // CHOMPI held too (CHOMPI + G#: out)
            if(chompi_held_ && k == kKeyTranspose)
            {
                transpose_mode_ = false;
                return;
            }
            synth_->Seq().SetTranspose(k - 12);
            const bool twice = k == tap_key_ && now_ - tap_at_ <= kDoubleTapMs;
            tap_key_         = twice ? -1 : k;
            tap_at_          = now_;
            if(twice)
                transpose_mode_ = false;
            return;
        }
        if(quant_page_)
        {
            // the QUANTIZE page: white keys 1-4 as played, 1/16, 1/8, 1/4;
            // CHOMPI + D# again leaves (so does a black key)
            if(w >= 0 && w < 4)
            {
                static const int kGrid[4] = {0, 1, 2, 4};
                synth_->Seq().SetQuantize(kGrid[w]);
                return;
            }
            if(w >= 0)
                return;
            quant_page_ = false;
            if(!chompi_held_ || k == kKeyQuant)
                return;
        }
        if(len_page_ && !chompi_held_)
        {
            // the LENGTH page: white keys 1-15, a new note's length in steps
            // (held 2 s: every step's, SeqTimers); a black key leaves
            if(w >= 0)
            {
                len_key_ = w, len_at_ = now_, len_fired_ = false;
                synth_->Seq().SetDefaultGate(GateOf(w + 1));
            }
            else
                len_page_ = false;
            return;
        }
        if(len_page_ && w >= 0) // (CHOMPI + a step, the steps not in view: nothing)
            return;
        if(strum_page_ && !chompi_held_)
        {
            // the STRUM track: a step key, its strum on / off; a black key leaves
            auto&     q    = synth_->Seq();
            const int step = StepOfKey(k);
            if(step >= 0 && step < q.Length())
                q.SetStrum(step, !q.pattern.step[step].strum);
            else if(step < 0)
                strum_page_ = false;
            return;
        }
        if(chompi_held_)
        {
            SeqCommand(k);
            return;
        }
        const int note = kLowestNote + 12 * octave_ + k;
        key_note_[k]   = note;
        synth_->NoteOn(note); // heard (never the arp's); recorded too, if armed and running
        if(armed_ && !synth_->Seq().Running())
        {
            synth_->Seq().EntryNote(note); // into the selected step
            entry_down_++;
            entered_ = true;
            ShowStep(synth_->Seq().Selected());
        }
    }

    /** A key let go in sequencer mode: true if it was one of the pattern,
     *  COPY, CLEAR, PATTERN or long-note keys (nothing more to do). */
    bool SeqKeyUp(int k)
    {
        auto& q = synth_->Seq();
        if(len_key_ >= 0 && SlotOf(k) == len_key_)
        {
            len_key_ = -1;
            return true;
        }
        if(pat_key_ >= 0 && SlotOf(k) == pat_key_)
        {
            SelectPattern(view_bank_, pat_key_);
            pat_key_ = -1;
            return true;
        }
        if(k == kKeyCopy && copy_down_)
        {
            copy_down_ = false;
            if(copy_opened_)
                pattern_view_ = false;
            return true;
        }
        if(k == kKeyClear && clear_down_)
        {
            clear_down_ = false;
            if(!clear_fired_) // a tap: the selected step
            {
                SaveUndo();
                q.ClearStep(q.Selected());
            }
            return true;
        }
        if(k == kKeyPattern && pdown_)
        {
            pdown_ = false;
            if(!pfired_) // a tap: the pattern page in / out
            {
                pattern_view_ = !pattern_view_;
                view_bank_    = bank_;
            }
            return true;
        }
        return false;
    }

    /** The holds (PATTERN 2 s: write protect; CLEAR 1 s: the pattern; a
     *  LENGTH key 2 s: every step's length), a
     *  queued pattern come in, and saving the patterns 2 s after an edit. */
    void SeqTimers()
    {
        auto& q = synth_->Seq();
        if(pdown_ && !pfired_ && now_ - pdown_at_ >= 2000)
        {
            pfired_           = true;
            protect_          = !protect_;
            protect_flash_at_ = now_;
            req_protect_      = protect_ ? 1 : 0;
        }
        if(len_key_ >= 0 && !len_fired_ && now_ - len_at_ >= 2000)
        {
            len_fired_ = true;
            LengthAll(len_key_ + 1);
        }
        if(clear_down_ && !clear_fired_ && now_ - clear_at_ >= 1000)
        {
            clear_fired_ = true;
            SaveUndo();
            q.pattern.Clear();
        }
        if(q.TakeSwitched()) // the queued pattern in, on its first step
        {
            banks_[bank_][pat_] = q.Previous();
            bank_ = q_bank_, pat_ = q_pat_;
            q_bank_ = q_pat_ = -1;
            sum_    = Sum(q.pattern);
        }
        if(now_ - last_sum_at_ >= 250)
        {
            last_sum_at_     = now_;
            const uint32_t x = Sum(q.pattern);
            if(x != sum_)
                sum_ = x, dirty_[bank_] = true, last_edit_ = now_;
        }
        if(protect_ || !card_ok_ || req_bank_save_ >= 0 || now_ - last_edit_ < 2000)
            return;
        for(int b = 0; b < kBanks; b++)
            if(dirty_[b])
            {
                StorePattern();
                for(int i = 0; i < chompi::kBankPatterns; i++)
                    bank_out_[i] = banks_[b][i];
                dirty_[b] = false;
                std::atomic_signal_fence(std::memory_order_seq_cst);
                req_bank_save_ = b;
                return;
            }
    }

    /** A pattern's contents, as a number: changed, it was edited. */
    static uint32_t Sum(const phymo::SeqPattern& p)
    {
        uint32_t h   = 2166136261u;
        auto     mix = [&](int x) { h = (h ^ static_cast<uint32_t>(x)) * 16777619u; };
        mix(p.length);
        for(const auto& st : p.step)
        {
            for(int i = 0; i < st.count; i++)
                mix(st.note[i]);
            mix(st.count), mix(st.on), mix(st.offset), mix(st.gate), mix(st.strum); // (the STRUM track's too: an edit, saved)
        }
        return h;
    }

    void SaveUndo()
    {
        clr_undo_    = synth_->Seq().pattern;
        clr_undo_ok_ = true;
    }
    /** COPY + CLEAR: the last clear undone; again, redone. */
    void UndoClear()
    {
        if(!clr_undo_ok_)
            return;
        auto&                  q = synth_->Seq();
        const phymo::SeqPattern t = q.pattern;
        q.pattern                = clr_undo_;
        clr_undo_                = t;
    }

    // --- patterns ---

    static constexpr int kBanks = 3;
    static constexpr int kKeyPattern = 18, kKeyCopy = 20, kKeyClear = 22, kKeyTranspose = 13, kKeyQuant = 15;
    static constexpr int kKeyLength = 6, kKeyStrumTrack = 8; // low F#: the LENGTH page; low G#: the STRUM track
    static constexpr uint32_t kDoubleTapMs = 350;

    /** The playing pattern's edits back into its slot. */
    void StorePattern() { banks_[bank_][pat_] = synth_->Seq().pattern; }

    /** COPY + a pattern key: the playing pattern copied there. */
    void CopyTo(int w)
    {
        StorePattern();
        banks_[view_bank_][w] = synth_->Seq().pattern;
        dirty_[view_bank_]    = true;
        last_edit_            = now_;
    }

    /** A pattern key let go: stopped, it plays now; running, it
     *  waits for the end of this one, and again (queued), it's in now. The
     *  playing one again cancels a queue. */
    void SelectPattern(int b, int w)
    {
        auto& q = synth_->Seq();
        if(b == bank_ && w == pat_)
        {
            q.CancelQueue();
            q_bank_ = q_pat_ = -1;
            return;
        }
        StorePattern();
        if(q.Running() && !(b == q_bank_ && w == q_pat_))
        {
            q.Queue(banks_[b][w]);
            q_bank_ = b, q_pat_ = w;
            return;
        }
        q.CancelQueue();
        q_bank_ = q_pat_ = -1;
        q.pattern = banks_[b][w];
        bank_ = b, pat_ = w;
        sum_      = Sum(q.pattern);
        if(!q.Running())
            q.Select(0);
    }

    static bool Used(const phymo::SeqPattern& p)
    {
        for(const auto& st : p.step)
            if(st.count > 0)
                return true;
        return false;
    }

    /** The pattern page's keys: the playing pattern flashing slowly,
     *  the queued one quickly, the others with notes dim, empty ones dark;
     *  in the bank's colour. */
    void PatternKeyColour(int k, float& r, float& g, float& b) const
    {
        static const float* const kBankCol[kBanks] = {kLightBlue, kMagenta, kWhite};
        const int                 w                = SlotOf(k);
        if(w < 0)
        {
            if(k == kKeyPattern) // PATTERN: write protect on magenta, off light blue
                ProtectColour(r, g, b);
            return;
        }
        const bool  flash = static_cast<int32_t>(flash_until_ - now_) > 0 && flash_slot_ == -2;
        if(flash) // a bank not saved: red
        {
            r = 1.f, g = b = 0.f;
            return;
        }
        const float* c       = kBankCol[view_bank_];
        const bool   playing = view_bank_ == bank_ && w == pat_;
        const bool   queued  = view_bank_ == q_bank_ && w == q_pat_;
        const bool   used    = playing ? Used(synth_->Seq().pattern) : Used(banks_[view_bank_][w]);
        float        s       = used ? 0.25f : 0.f;
        if(queued)
            s = (now_ / 100) % 2 ? 1.f : 0.1f;
        else if(playing)
            s = (now_ / 400) % 2 ? 1.f : 0.3f;
        if(copy_down_ && !playing && !queued) // COPY held: every key a place for it
            s = s > 0.f ? s : 0.06f;
        r = c[0] * s, g = c[1] * s, b = c[2] * s;
    }

    void ProtectColour(float& r, float& g, float& b) const
    {
        const float* c = protect_ ? kMagenta : kLightBlue;
        r = c[0], g = c[1], b = c[2];
    }

    // --- the card's side (storage.h) ---
  public:
    phymo::SeqPattern* Bank(int b) { return banks_[b]; }
    /** The banks read at power-on: bank 1's first pattern into the sequencer. */
    void BanksLoaded()
    {
        synth_->Seq().pattern = banks_[0][0];
        sum_                  = Sum(banks_[0][0]);
    }
    /** Write protect, from options.txt; and a change of it to save there. */
    void SetProtect(bool on) { protect_ = on; }
    bool TakeProtect(int& on)
    {
        if(req_protect_ < 0)
            return false;
        on            = req_protect_;
        req_protect_  = -1;
        return true;
    }
    /** A bank save asked for: its number (-1 none), the patterns into `out`. */
    int TakeBankSave(phymo::SeqPattern* out)
    {
        const int b = req_bank_save_;
        if(b < 0)
            return -1;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        for(int i = 0; i < chompi::kBankPatterns; i++)
            out[i] = bank_out_[i];
        req_bank_save_ = -1;
        return b;
    }
    void BankSaved(bool ok)
    {
        if(!ok)
            Flash(-2, 0); // (saved: nothing to see, it's automatic)
    }

  private:
    /** CHOMPI + a key in sequencer mode. */
    void SeqCommand(int k)
    {
        auto&     q    = synth_->Seq();
        const int step = StepOfKey(k);
        if(step >= 0) // a step: selected (CHOMPI + wheel: its note's length); selected already, on / off
        {
            len_edit_ = false;
            if(step < q.Length())
            {
                if(step == q.Selected())
                    q.ToggleStep(step);
                q.Select(step);
                ShowStep(step);
            }
            return;
        }
        switch(k)
        {
            case 1:
            case 3: Command(k); break; // the octave, as in performance mode
            case kKeyQuant: quant_page_ = !quant_page_; break; // the QUANTIZE page in / out
            case kKeyTranspose: transpose_mode_ = !transpose_mode_, tap_key_ = -1; break;
            case kKeyPattern:               // PATTERN: a tap, the page; held 2 s, write protect
                pdown_    = true;
                pfired_   = false;
                pdown_at_ = now_;
                break;
            case kKeyCopy: // COPY: held, a pattern key gets the playing pattern
                copy_down_   = true;
                copy_opened_ = !pattern_view_;
                if(copy_opened_)
                    pattern_view_ = true, view_bank_ = bank_;
                break;
            case kKeyClear: // CLEAR: a tap, the selected step; held 1 s, the pattern; with COPY, undo
                if(copy_down_)
                    UndoClear();
                else
                    clear_down_ = true, clear_fired_ = false, clear_at_ = now_;
                break;
            case kKeyLength: // the LENGTH page in / out
                len_page_ = !len_page_, strum_page_ = quant_page_ = false, len_key_ = -1;
                break;
            case kKeyStrumTrack: // the STRUM track in / out
                strum_page_ = !strum_page_, len_page_ = quant_page_ = false;
                break;
            default: break; // low A#: free
        }
    }

    /** `n` steps as a gate (48ths of a step): one, half a step (as a new
     *  note's always was); more, all but a sliver of them (NoteLength's). */
    static int GateOf(int n) { return n > 1 ? 48 * n - 4 : 24; }

    /** A LENGTH key held 2 s: every step with notes `n` steps long, as far
     *  as the pattern's end (undo: COPY + CLEAR). */
    void LengthAll(int n)
    {
        auto& q = synth_->Seq();
        SaveUndo();
        for(int j = 0; j < q.Length(); j++)
            if(q.pattern.step[j].count > 0)
            {
                const int most = q.Length() - j;
                q.SetGate(j, GateOf(n < most ? n : most));
            }
        len_flash_at_ = now_ ? now_ : 1;
    }

    /** The half in view (0-3: steps 1-8, 9-16, 17-24, 25-32): the one last
     *  pressed or flipped to; while running, the playhead's once the
     *  pattern has notes past step 8. */
    int ShownView() const
    {
        const auto& q = synth_->Seq();
        if(q.Running() && !LengthWheel() && !len_edit_ && !Scrolling())
        {
            bool past = false;
            for(int i = 8; i < q.Length() && !past; i++)
                past = q.pattern.step[i].count > 0;
            if(past && q.Playhead() >= 0)
                return q.Playhead() / 8;
        }
        return 2 * page_ + half_;
    }

    /** The step white key `k` shows (-1: a black key): keys
     *  1-7 steps 1-7, 9-15 steps 10-16, middle C step 8 or 9 by the half in
     *  view; plus 16 on the second page. */
    int StepOfKey(int k) const
    {
        const int w = SlotOf(k);
        if(w < 0)
            return -1;
        const int v = ShownView();
        const int s = w < 7 ? w : w > 7 ? w + 1 : (v & 1 ? 8 : 7);
        return s + 16 * (v >= 2);
    }

    /** The wheel turned while the sequence runs: its pages shown (the
     *  selected step blinking) for a second, then the playhead again. */
    bool Scrolling() const { return scroll_at_ != 0 && now_ - scroll_at_ < 1000; }

    /** The wheel pressed and turned in sequencer mode: setting the
     *  pattern's length (the keys showing it: the length view). */
    bool LengthWheel() const
    {
        return seq_mode_ && !pattern_view_ && wheel_turned_ && hw_->enc[4].Pressed();
    }

    /** Brings step `s` into view: its page and half. */
    void ShowStep(int s)
    {
        page_ = s >= 16;
        half_ = s % 16 >= 8;
    }

    /** The step a white key is in the length view: as StepOfKey, but middle
     *  C is whichever of its two steps is the end (if one is). */
    int LenStepOfKey(int k) const
    {
        const int step = StepOfKey(k);
        if(SlotOf(k) != 7)
            return step;
        const int base = step - step % 8 - (step % 16 >= 8 ? 8 : 0); // the 16's first step
        const int end  = synth_->Seq().Length() - 1;
        return end == base + 7 || end == base + 8 ? end : step;
    }

    /** CHOMPI + wheel: the selected step's note longer or shorter, in
     *  steps, as far as the pattern's end, ringing on over the steps after
     *  it (their notes still play: the voices overlap). The keys show it
     *  alone (len_edit_) till CHOMPI is let go; the first click only shows
     *  it, as it is. */
    void NoteLength(int inc)
    {
        auto&     q   = synth_->Seq();
        const int sel = q.Selected();
        if(sel >= q.Length() || q.pattern.step[sel].count == 0)
            return;
        const int was = Steps(q.pattern.step[sel].gate);
        int       n   = was;
        if(len_edit_)
        {
            const int most = q.Length() - sel;
            n              = was + inc < 1 ? 1 : was + inc > most ? most : was + inc;
            if(n != was)
                q.SetGate(sel, n > 1 ? 48 * n - 4 : 24);
        }
        len_edit_ = true;
        ShowStep(sel + n - 1); // the view on its end
    }

    /** A gate (48ths of a step) in whole steps, at least 1. */
    static int Steps(int gate) { return gate > 48 ? (gate + 47) / 48 : 1; }

    /** The step whose note holds over step `s` (a long note), or -1. */
    int TieOwner(int s) const
    {
        const auto& p = synth_->Seq().pattern;
        for(int o = s - 1; o >= 0; o--)
            if(p.step[o].on && p.step[o].count > 0 && p.step[o].gate > 48 * (s - o))
                return o;
        return -1;
    }

    void ApplySeq(int k)
    {
        const float x = seq_knob_[k];
        switch(k)
        {
            case 0: synth_->SetSeqDivision(static_cast<int>(x * phymo::Synth::kSeqDivs)); break;
            case 1: synth_->SetSeqGlide(x > 0.f ? 0.005f * powf(400.f, x) : 0.f); break; // off, 5 ms .. 2 s
            case 2:
            {
                const int d = static_cast<int>(x * 4.f);
                synth_->Seq().SetDirection(static_cast<phymo::Sequencer::Direction>(d > 3 ? 3 : d));
                break;
            }
            case 3: synth_->SetTempo(30.f + 210.f * x); break; // 30 .. 240 BPM
            default: break;
        }
    }

    /** Sequencer mode's knob LEDs: the step's size (1/4 blue, 1/8 teal,
     *  1/8T orange, 1/16 green, 1/16T magenta, 1/32 red), glide cream (dim
     *  off), direction (forward green, reverse blue, ping-pong teal, random
     *  pink), tempo yellow, pulsing on the beat (the sequence's own while
     *  it runs, MIDI clock's too; stopped, the tempo's) (CHOMPI held: the swing,
     *  pink, dim at none). */
    void DrawSeqKnobs(uint32_t now)
    {
        static const float* const kDivCol[phymo::Synth::kSeqDivs] = {kBlue, kTeal, kOrange, kGreen, kMagenta, kRed};
        const float*               sc = kDivCol[synth_->GetSeqDivision()];
        SetPthLedFloat(kKnobLed[0], sc[0], sc[1], sc[2]);
        const float gl = seq_knob_[1] > 0.f ? 0.2f + 0.8f * seq_knob_[1] : 0.05f;
        SetPthLedFloat(kKnobLed[1], kCream[0] * gl, kCream[1] * gl, kCream[2] * gl);
        static const float* const kDirCol[4] = {kGreen, kBlue, kTeal, kPink};
        const float*               dc        = kDirCol[static_cast<int>(synth_->Seq().GetDirection())];
        SetPthLedFloat(kKnobLed[2], dc[0], dc[1], dc[2]);
        // (on the sequence's own beats while it runs, MIDI clock's too; stopped, the tempo's)
        const float run   = synth_->GetSeqBeatPhase();
        const float beat  = static_cast<float>(now) * synth_->GetTempo() / 60000.f;
        const float ph    = run >= 0.f ? run : beat - static_cast<int>(beat);
        const float pulse = ph < 0.15f ? 1.f : 0.3f;
        if(chompi_held_)
        {
            const float sw = 0.1f + 0.9f * swing_;
            SetPthLedFloat(kKnobLed[3], kPink[0] * sw, kPink[1] * sw, kPink[2] * sw);
        }
        else
            SetPthLedFloat(kKnobLed[3], kYellow[0] * pulse, kYellow[1] * pulse, kYellow[2] * pulse);
    }

    /** The MIX page (knob 6, sequencer mode): the middle both full; left
     *  fades the keys out (full left: the sequence alone), right the
     *  sequence (full right: the keys alone). */
    void ApplyMix()
    {
        synth_->SetSeqLevel(mix_ <= 0.5f ? 1.f : 2.f * (1.f - mix_));
        synth_->SetLiveLevel(mix_ >= 0.5f ? 1.f : 2.f * mix_);
    }

    /** A key's colour in sequencer mode. */
    void SeqKeyColour(int k, float& r, float& g, float& b) const
    {
        r = g = b = 0.f;
        const auto& q = synth_->Seq();
        if(now_ - protect_flash_at_ < 600) // write protect switched: all flashing magenta (on) / light blue (off)
        {
            if((now_ - protect_flash_at_) / 150 % 2 == 0)
                ProtectColour(r, g, b);
            return;
        }
        if(transpose_mode_) // the transpose's key yellow (middle C: none), the playhead white
        {
            if(k == 12 + q.GetTranspose())
                r = kYellow[0], g = kYellow[1], b = kYellow[2];
            else if(chompi_held_ && k == kKeyTranspose)
                r = kYellow[0], g = kYellow[1], b = 0.f;
            else if(StepOfKey(k) >= 0 && StepOfKey(k) == q.Playhead())
                r = g = b = 1.f;
            else if(k == 12)
                r = g = b = 0.1f;
            return;
        }
        if(LengthWheel()) // the length view, while the wheel sets it: the steps in it lit (with notes as usual, empty ones dim), the last bright in its page's colour, past it dim white
        {
            const int step = LenStepOfKey(k);
            if(step >= 0)
            {
                static const float* const kCol[4] = {kRed, kOrange, kYellow, kGreen};
                const float*              c       = kCol[step / 8];
                const auto&               st      = q.pattern.step[step];
                const float s = step >= q.Length() ? 0.f : step == q.Length() - 1 ? 1.f : st.count > 0 ? 0.5f : 0.12f;
                r = c[0] * s, g = c[1] * s, b = c[2] * s;
                if(step >= q.Length())
                    r = g = b = 0.04f;
            }
            return;
        }
        if(quant_page_) // the QUANTIZE page: keys 1-4, the grid chosen bright; D# its colour
        {
            const int    w  = SlotOf(k);
            const int    qn = q.GetQuantize();
            const int    on = qn == 0 ? 0 : qn == 1 ? 1 : qn == 2 ? 2 : 3;
            const float* c  = w == 0 ? kWhite : w == 1 ? kGreen : w == 2 ? kTeal : kBlue;
            if(w >= 0 && w < 4)
            {
                const float v = w == on ? 1.f : 0.12f;
                r = c[0] * v, g = c[1] * v, b = c[2] * v;
            }
            else if(k == kKeyQuant)
                SeqCommandColour(k, r, g, b);
            return;
        }
        if(len_page_) // the LENGTH page: keys 1-15 a bar to the length chosen (bright), orange; a 2 s hold's flash white
        {
            const int w = SlotOf(k);
            if(w >= 0)
            {
                const int   n = Steps(q.DefaultGate());
                const bool  fl = len_flash_at_ && now_ - len_flash_at_ < 300;
                const float v  = fl ? 1.f : w + 1 == n ? 1.f : w + 1 < n ? 0.25f : 0.04f;
                const float* c = fl ? kWhite : kOrange;
                r = c[0] * v, g = c[1] * v, b = c[2] * v;
            }
            else if(k == kKeyLength)
                SeqCommandColour(k, r, g, b);
            return;
        }
        if(strum_page_ && chompi_held_) // CHOMPI held on the STRUM track: its spread (synced) or spacing (free), a white bar over keys 1-15
        {
            const int w = SlotOf(k);
            if(w >= 0)
            {
                const float v = synth_->GetSeqSync() ? seq_spread_ : seq_strum_;
                const int   n = static_cast<int>(std::ceil(v * 15.f - 1e-3f)); // (lit keys: 0 none, 15 all)
                const float l = w + 1 == n ? 1.f : w + 1 < n ? 0.25f : 0.04f;
                r = g = b = l;
            }
            else if(k == kKeyStrumTrack)
                SeqCommandColour(k, r, g, b);
            return;
        }
        if(strum_page_ && !chompi_held_) // the STRUM track: each step strummed light blue, the rest's notes dim, the playhead white
        {
            static const float* const kCol[4] = {kRed, kOrange, kYellow, kGreen};
            const int                 step    = StepOfKey(k);
            if(step < 0)
            {
                if(k == kKeyStrumTrack)
                    SeqCommandColour(k, r, g, b);
                return;
            }
            if(step >= q.Length())
                return;
            const auto& st = q.pattern.step[step];
            if(step == q.Playhead())
                r = g = b = 1.f;
            else if(st.strum) // (synced yellow, free light blue)
            {
                const float* sc = synth_->GetSeqSync() ? kYellow : kLightBlue;
                r = sc[0], g = sc[1], b = sc[2];
            }
            else if(st.count > 0)
                r = kCol[step / 8][0] * .12f, g = kCol[step / 8][1] * .12f, b = kCol[step / 8][2] * .12f;
            if(step / 8 != ShownView())
                r *= 0.2f, g *= 0.2f, b *= 0.2f;
            return;
        }
        if(len_edit_) // CHOMPI + wheel: the selected note alone, its trail dim, the rest dark
        {
            static const float* const kCol[4] = {kRed, kOrange, kYellow, kGreen};
            const int                 step    = StepOfKey(k);
            const int                 sel     = q.Selected();
            if(step < 0 || step < sel || step >= q.Length())
                return;
            const int    n = Steps(q.pattern.step[sel].gate);
            const float* c = kCol[sel / 8];
            const float  v = step == sel ? 1.f : step < sel + n ? 0.12f : 0.f;
            r = c[0] * v, g = c[1] * v, b = c[2] * v;
            return;
        }
        if(pattern_view_ || copy_down_)
        {
            if(SlotOf(k) >= 0 || !chompi_held_)
            {
                PatternKeyColour(k, r, g, b);
                return;
            }
        }
        const int step = StepOfKey(k);
        if(step >= 0 && flash_slot_ == -2 && static_cast<int32_t>(flash_until_ - now_) > 0)
        {
            r = 1.f, g = b = 0.f; // a bank not saved
            return;
        }
        if(step < 0) // a black key
        {
            if(chompi_held_)
                SeqCommandColour(k, r, g, b);
            else if(down_[k] && key_note_[k] >= 0)
                r = 1.f, g = .7f, b = .4f;
            return;
        }
        if(down_[k] && key_note_[k] >= 0 && !chompi_held_)
        {
            r = 1.f, g = .7f, b = .4f; // playing it
            return;
        }
        static const float* const kPageCol[4] = {kRed, kOrange, kYellow, kGreen};
        if(q.Playhead() >= 0 && !chompi_held_ && !Scrolling())
        {
            // running: only the step playing, on its page's keys, in its
            // page's colour (dim, a rest); CHOMPI held, every step as below
            const int head = q.Playhead(), s = head % 16;
            if(SlotOf(k) != (s < 7 ? s : s <= 8 ? 7 : s - 1)) // (middle C: 8 or 9)
                return;
            const phymo::SeqStep& st = q.pattern.step[head];
            const float*         c  = kPageCol[head / 8];
            const float          v  = st.on && st.count > 0 ? 1.f : 0.2f;
            r = c[0] * v, g = c[1] * v, b = c[2] * v;
            return;
        }
        if(step >= q.Length())
        {
            if(!q.Running() && step == q.Selected() && armed_) // entry's next step, past the end
                r = g = b = (now_ / 250) % 2 ? 1.f : 0.15f;
            return; // past the end
        }
        // each 8 steps their own colour: red, orange, yellow, green
        const float*              c           = kPageCol[step / 8];
        const phymo::SeqStep&      st          = q.pattern.step[step];
        const int                 owner       = TieOwner(step);
        if(step == q.Playhead())
            r = g = b = 1.f;
        else if((!q.Running() || chompi_held_ || Scrolling()) && step == q.Selected())
        {
            const float w = (now_ / 250) % 2 ? 1.f : 0.15f; // selected: blinking white
            r = g = b = w;
        }
        else if(owner >= 0 && !st.on)
        {
            // held over by a long note (empty or muted): its first step's colour, dim
            const float* oc = kPageCol[owner / 8];
            const float  s  = 0.12f;
            r = oc[0] * s, g = oc[1] * s, b = oc[2] * s;
        }
        else
        {
            // on: its page's colour; muted (notes kept): faint white; a
            // rest (empty): dark (dim colour is a long note's)
            if(st.on)
                r = c[0] * 0.5f, g = c[1] * 0.5f, b = c[2] * 0.5f;
            else if(st.count > 0)
                r = g = b = 0.04f;
        }
        if(step / 8 != ShownView()) // the half not in view: dimmed
            r *= 0.2f, g *= 0.2f, b *= 0.2f;
    }

    /** CHOMPI held in sequencer mode: the black keys' jobs. */
    void SeqCommandColour(int k, float& r, float& g, float& b) const
    {
        const auto& q = synth_->Seq();
        switch(k)
        {
            case 1:
            case 3: KeyCommandColour(k, r, g, b); break; // octave
            case kKeyQuant: // quantize: off dim, 1/16 green, 1/8 teal, 1/4 blue
            {
                const int    qn = q.GetQuantize();
                const float* c  = qn == 1 ? kGreen : qn == 2 ? kTeal : kBlue;
                const float  s  = qn == 0 ? 0.1f : 1.f;
                r = c[0] * s, g = c[1] * s, b = c[2] * s;
                break;
            }
            case kKeyTranspose: r = kYellow[0] * .5f, g = kYellow[1] * .5f, b = 0.f; break; // transpose
            case kKeyPattern: ProtectColour(r, g, b); break;                    // PATTERN
            case kKeyCopy: r = kTeal[0], g = kTeal[1], b = kTeal[2]; break;     // COPY
            case kKeyClear: r = 1.f, g = b = 0.f; break;                        // CLEAR
            case kKeyLength: // LENGTH: orange (its page open, bright)
                r = kOrange[0] * (len_page_ ? 1.f : .4f), g = kOrange[1] * (len_page_ ? 1.f : .4f), b = 0.f;
                break;
            case kKeyStrumTrack: // STRUM: synced yellow, free light blue (its page open, bright)
            {
                const float  s  = strum_page_ ? 1.f : .4f;
                const float* sc = synth_->GetSeqSync() ? kYellow : kLightBlue;
                r = sc[0] * s, g = sc[1] * s, b = sc[2] * s;
                break;
            }
            default: break;
        }
    }

    // --- presets ---

    /** The slot a white key is (0-14, lowest first), or -1 for a black key. */
    static int SlotOf(int k)
    {
        static const int kWhite[25] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6, 7,
                                       -1, 8, -1, 9, 10, -1, 11, -1, 12, -1, 13, 14};
        return kWhite[k];
    }

    /** The view opened: the sound kept to go back to, the wheel on the
     *  loaded slot (or where it was). */
    void EnterPresets()
    {
        Snapshot(undo_);
        undo_loaded_ = loaded_;
        keep_        = false;
        tried_       = false;
        presets_     = true;
        if(loaded_ >= 0)
            wheel_slot_ = loaded_;
    }

    /** The view left: `keep`, the sound as it is (the wheel's click);
     *  else, unless one was saved or init since, back to the sound it
     *  opened with. */
    void LeavePresets(bool keep)
    {
        presets_ = false;
        if(keep || keep_)
        {
            keep_ = true; // (a try still on its way lands)
            return;
        }
        req_load_ = -1;
        Restore(undo_);
        loaded_ = undo_loaded_;
    }

    /** The wheel in the view: the slot, a detent each, from one bank's
     *  15th into the next's first, as far as the bank after the last with
     *  a sound (to save into); a saved one tried as it comes, an empty one
     *  the sound the view opened with (or last saved, or init: what a save
     *  there would keep). */
    void WheelSlot(int inc)
    {
        int last = 0;
        for(int i = 0; i < kSlots; i++)
            if(slot_used_[i])
                last = i;
        int end = (last / kBankSlots + 2) * kBankSlots - 1;
        end     = end > kSlots - 1 ? kSlots - 1 : end;
        int w   = wheel_slot_ + (inc > 0 ? 1 : -1);
        w       = w < 0 ? 0 : w > end ? end : w;
        if(w == wheel_slot_)
            return;
        wheel_slot_ = w;
        if(slot_used_[w])
        {
            if(!tried_) // (from the sound itself: what it is now, any turns in the view too, to come back to)
                Snapshot(undo_), undo_loaded_ = loaded_;
            req_load_ = w;
        }
        else
        {
            req_load_ = -1;
            if(tried_) // (a saved one tried on the way: back from it)
            {
                Restore(undo_);
                loaded_ = undo_loaded_, tried_ = false;
            }
        }
    }

    /** `kind`: 0 failed (red, blinking), 1 saved (green), 2 deleted (red), 3 init (white, all). */
    void Flash(int slot, int kind)
    {
        flash_slot_  = slot;
        flash_kind_  = kind;
        flash_until_ = now_ + 400;
    }

    void Snapshot(PresetState& s) const
    {
        for(int v = 0; v < kViews; v++)
            for(int k = 0; k < 4; k++)
                s.knob[v][k] = knob_[v][k], s.alt[v][k] = alt_[v][k];
        for(int d = 0; d < phymo::kDests; d++)
            s.lfo_to[d] = lfo_to_[d], s.env_to[d] = env_to_[d];
        s.bright      = bright_;
        s.pos         = pos_;
        s.tape        = tape_;
        s.chorus      = static_cast<int>(synth_->GetChorus());
        s.porta       = static_cast<int>(synth_->GetPortamento());
        s.cycle       = synth_->GetCycle();
        s.key_mode    = static_cast<int>(synth_->GetKeyMode());
        s.lfo_free    = static_cast<int>(synth_->GetLfoTrigger());
        s.delay_glide = synth_->GetDelayGlide();
        s.fm_list     = synth_->GetFmList();
        s.model       = static_cast<int>(synth_->GetModel());
        s.pickup      = static_cast<int>(synth_->GetPickup());
        s.chord_n     = synth_->GetChord(s.chord);
    }

    void Restore(const PresetState& s)
    {
        auto pick = [](int x, int n) { return x < 0 ? 0 : x >= n ? n - 1 : x; };
        for(int v = 0; v < kViews; v++)
            for(int k = 0; k < 4; k++)
                knob_[v][k] = s.knob[v][k], alt_[v][k] = s.alt[v][k];
        for(int d = 0; d < phymo::kDests; d++)
            lfo_to_[d] = s.lfo_to[d], env_to_[d] = s.env_to[d];
        bright_ = s.bright;
        pos_    = s.pos;
        tape_   = s.tape;
        for(int v = 0; v < kViews; v++)
            for(int k = 0; k < 4; k++)
                Apply(v, k);
        synth_->SetBrightness(bright_);
        synth_->SetPosition(pos_);
        synth_->SetChorus(static_cast<phymo::Chorus::Mode>(pick(s.chorus, 5)));
        // (a file's portamento off: the knob at its least, now that's off;
        // an old file's legato, always)
        if(pick(s.porta, 3) == static_cast<int>(phymo::PortaMode::Off))
            knob_[kKbd][1] = 0.f;
        Apply(kKbd, 1);
        synth_->SetCycle(s.cycle != 0);
        synth_->SetLfoTrigger(static_cast<phymo::LfoTrigger>(pick(s.lfo_free, 3)));
        ApplyDepths();
        synth_->SetDelayGlide(s.delay_glide != 0);
        synth_->SetFmList(s.fm_list);
        synth_->SetModel(static_cast<phymo::Model>(pick(s.model, static_cast<int>(phymo::Model::Count))));
        synth_->SetPickup(static_cast<phymo::PickupMode>(pick(s.pickup, static_cast<int>(phymo::PickupMode::Count))));
        synth_->SetChord(s.chord, s.chord_n);
        ChangeMode(static_cast<phymo::KeyMode>(pick(s.key_mode, 5)));
        synth_->SetVolume(VcaGain(vca_));
    }

    /** The preset view's keys: slots and the black keys' jobs. */
    void PresetKeyColour(int k, float& r, float& g, float& b) const
    {
        const int  key   = SlotOf(k);
        const int  bank  = wheel_slot_ / kBankSlots;
        const int  slot  = bank * kBankSlots + key;
        const bool flash = static_cast<int32_t>(flash_until_ - now_) > 0;
        if(key < 0)
        {
            // the black keys, low C# up, the banks: the wheel's bright, ones
            // with sounds dim; high A# the presets' key
            static const int kBlack[10] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
            int              n          = -1;
            for(int i = 0; i < kPresetBanks; i++)
                n = kBlack[i] == k ? i : n;
            if(kKeyCmd[k] == kCmdPresets || n < 0)
            {
                if(kKeyCmd[k] == kCmdPresets)
                    KeyCommandColour(k, r, g, b);
                return;
            }
            bool used = false;
            for(int i = n * kBankSlots; i < (n + 1) * kBankSlots; i++)
                used = used || slot_used_[i];
            const float  s  = n == bank ? 1.f : used ? 0.15f : 0.f;
            const float* bc = kPresetBankCol[n];
            r = s * bc[0], g = s * bc[1], b = s * bc[2];
            return;
        }
        const float* c = kPresetBankCol[bank];
        if(flash && (flash_kind_ == 3 || flash_slot_ == slot))
        {
            const float on = flash_kind_ == 0 ? ((now_ / 60) % 2 ? 1.f : 0.f) : 1.f;
            r = (flash_kind_ == 1 ? 0.f : 1.f) * on;
            g = (flash_kind_ == 0 || flash_kind_ == 2 ? 0.f : 1.f) * on;
            b = (flash_kind_ == 3 ? 1.f : 0.f) * on;
            return;
        }
        if(!card_ok_)
            r = .05f; // no card: the slots dim red
        else if(slot == wheel_slot_ && slot_used_[slot])
            r = c[0], g = c[1], b = c[2];
        else if(slot == wheel_slot_)
            r = g = b = .2f; // (the wheel's, empty: dim white, to save into)
        else if(slot_used_[slot])
            r = .25f * c[0], g = .25f * c[1], b = .25f * c[2];
    }

    void SendCc(uint8_t cc, float x)
    {
        const int next = (cc_head_ + 1) % kCcQueue;
        if(next == cc_tail_)
            return;
        cc_q_[cc_head_][0] = cc;
        cc_q_[cc_head_][1] = static_cast<uint8_t>(x * 127.f + 0.5f);
        cc_head_           = next;
    }

    // --- helpers ---

    static float Clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

    /** Knob 6's position (0 to 1) to the VCA's gain: evenly in decibels,
     *  -48 dB to 0 dB, so each detent is about 1 dB wherever it is. Fully
     *  down is silent. */
    static float VcaGain(float v) { return v <= 0.f ? 0.f : powf(10.f, -48.f * (1.f - v) / 20.f); }

    static phymo::Chorus::Mode NextChorus(phymo::Chorus::Mode m)
    {
        using Mode = phymo::Chorus::Mode;
        return m == Mode::Off    ? Mode::I
               : m == Mode::I    ? Mode::II
               : m == Mode::II   ? Mode::Both
               : m == Mode::Both ? Mode::Parallel
                                 : Mode::Off;
    }

    /** A fresh press of a button that isn't a key. libDaisy's 4021 driver
     *  only reports a new rising edge after the falling edge has been read,
     *  so the release is consumed too. */
    bool Pressed(Sw sw)
    {
        auto&     sr = hw_->button_sr;
        const int i  = static_cast<int>(sw);
        if(sr.RisingEdge(i))
            return true;
        sr.FallingEdge(i);
        return false;
    }

    Hardware*    hw_    = nullptr;
    phymo::Synth* synth_ = nullptr;
    int8_t       key_led_[25];
    bool         down_[25];     // physically down
    bool         latched_[128] = {}; // notes let go but held by hold, by pitch (MIDI note)
    int          key_note_[25]; // the note each key started (-1: none, or a command)
    float        knob_[kViews][4] = {};
    float        alt_[kViews][4]  = {}; // CHOMPI + turn values (HasAlt)
    float        lfo_to_[phymo::kDests] = {.5f, .5f, .5f, .5f, .5f, .5f}; // the depths, as their knobs (0.5 none)
    float        env_to_[phymo::kDests] = {.5f, .5f, .5f, .5f, .5f, .5f};
    int          dest_    = 0; // the destination the LFO view's knobs 3 and 4 show
    uint32_t     dest_at_ = 0; // when it was last clicked or turned (the routing shown on the keys a while)
    int          dest_key_ = -1; // CHOMPI + a destination's key held (the LFO and ENV views): that key
    uint32_t     list_at_  = 0;  // FM's list last clicked (knob 1 in its colour a moment)
    uint32_t     trig_at_ = 0; // when the LFO's trigger was last clicked (shown on the keys a while)
    float        bright_    = 0.f, pos_ = 0.f, vca_ = 0.f;
    float        tape_      = 0.f; // the Vocal model's tape (CHOMPI + MODEL knob 2: its own, not snap's slot)
    View         view_      = kRes;
    int          octave_    = 0;
    bool         hold_      = false;
    bool         wheel_mod_ = false; // the wheel's press is mod (CHOMPI held at the press), not bend
    float        wheel_val_ = 0.f;   // bend / mod, -1 to 1
    bool         chompi_held_ = false;
    uint32_t     now_         = 0;
    // presets
    bool          presets_ = false, card_ok_ = false;
    bool          slot_used_[kSlots];
    int           wheel_slot_  = 0;       // the preset view's slot, the wheel's (its bank's too: 0 to kSlots - 1)
    int           undo_loaded_ = -1;      // the slot loaded as the view opened
    bool          keep_        = false;   // the view's sound kept when it's left (a click, a save, init)
    bool          tried_       = false;   // a saved one tried in the view, on now (an empty slot: back to undo_)
    int           loaded_    = -1;
    PresetState   undo_, out_state_, in_state_;
    volatile int  req_save_ = -1, req_delete_ = -1, req_load_ = -1, in_slot_ = -1;
    volatile bool in_ready_ = false;
    bool          a_down_ = false, a_fired_ = false; // high A#: held, and init done
    uint32_t      a_down_at_ = 0;
    int           flash_slot_ = -1, flash_kind_ = 0;
    uint32_t      flash_until_ = 0;
    // MIDI
    static constexpr int kCcQueue = 32;
    uint8_t       cc_q_[kCcQueue][2];
    volatile int  cc_head_ = 0, cc_tail_ = 0;
    bool          sync_ok_ = true;
    // sequencer mode
    bool          seq_mode_       = false;
    bool          page_           = false; // steps 17-32 in view
    bool          half_           = false; // the half in view: steps 9-16 (middle C shows step 9)
    bool          armed_          = false; // LOOP: recording armed
    bool          wheel_turned_   = false; // turned while pressed: a bend (sequencer mode: the length), not a click (a rest)
    bool          entered_        = false; // armed, stopped: steps entered (LOOP solid)
    bool          len_edit_       = false; // CHOMPI + wheel: the selected note's length, shown alone
    uint32_t      scroll_at_      = 0;     // the wheel last turned while running (Scrolling)
    uint32_t      metro_at_       = 0;     // the metronome switched (CHOMPI + the wheel's click), for its flash
    bool          transpose_mode_ = false; // the keys set the transpose
    int           tap_key_        = -1;    // transpose mode: the last key, for a double tap (leaves)
    uint32_t      tap_at_         = 0;
    int           entry_down_     = 0;     // keys down that went into the selected step
    float         seq_knob_[4]    = {3.5f / 6.f, 0.f, 0.f, (120.f - 30.f) / 210.f}; // step size, glide, direction, tempo
    float         swing_          = 0.f;   // CHOMPI + tempo: 0 none (50%) to 1 (75%)
    float         mix_            = 0.5f;  // knob 6's MIX page: the sequence against the keys
    int           div_acc_        = 0;     // knob 1's clicks toward the next step size
    bool          mix_page_       = false; // knob 6 on the MIX page (sequencer mode)
    bool          quant_page_     = false; // CHOMPI + mid D#: the QUANTIZE page
    bool          len_page_       = false; // CHOMPI + low F#: the LENGTH page
    bool          strum_page_     = false; // CHOMPI + low G#: the STRUM track
    int           len_key_        = -1;    // the LENGTH page's key held (its white key, 0-14)
    uint32_t      len_at_         = 0;     // ... when it went down
    bool          len_fired_      = false; // ... held 2 s: every step given its length
    uint32_t      len_flash_at_   = 0;     // every step given its length: the keys flash
    float         seq_strum_      = 0.1732f; // the STRUM track's spacing, free, as a knob (its square: 30 ms)
    float         seq_spread_     = 1.f;     // ... synced, its share of the step's length
    // patterns
    phymo::SeqPattern banks_[kBanks][chompi::kBankPatterns];
    phymo::SeqPattern bank_out_[chompi::kBankPatterns]; // a bank being saved
    int              bank_ = 0, pat_ = 0;     // the playing pattern
    int              view_bank_ = 0;          // the bank the pattern view shows
    bool             pattern_view_ = false;
    int              pat_key_ = -1;           // a pattern key down: acts when let go
    int              q_bank_ = -1, q_pat_ = -1; // the pattern queued for the end of this one
    bool             copy_down_ = false, copy_opened_ = false; // CHOMPI + high F#: COPY
    bool             clear_down_ = false, clear_fired_ = false; // CHOMPI + high A#: CLEAR
    uint32_t         clear_at_ = 0;
    bool             pdown_ = false, pfired_ = false; // CHOMPI + mid C#: tap the pattern page, 2 s protect
    uint32_t         pdown_at_ = 0;
    bool             protect_ = false;        // write protect: nothing saved to the card
    uint32_t         protect_flash_at_ = 0x80000000u;
    volatile int     req_protect_ = -1;
    phymo::SeqPattern clr_undo_;               // the pattern before the last clear (COPY + CLEAR)
    bool             clr_undo_ok_ = false;
    uint32_t         sum_ = 0, last_edit_ = 0, last_sum_at_ = 0; // the playing pattern's edits, for saving
    bool             dirty_[kBanks] = {};
    volatile int     req_bank_save_ = -1;
    bool         chord_edit_  = false; // choosing a chord
    int          chord_notes_[phymo::Synth::kVoices] = {};
    int          chord_n_     = 0;
    float        load_avg_ = 0.f, load_max_ = 0.f;
    uint32_t     overload_until_ = 0;
    uint32_t     last_turn_[6]   = {}; // each knob's last detent, ms
};

} // namespace chompi
