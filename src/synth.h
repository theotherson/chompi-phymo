/** @file synth.h
 *  @brief The engine: eight voices (voice.h), an LFO, the body and tremolo
 *  (body.h), the chorus, the delay and reverb (space.h) and the output stage.
 *
 *  Key modes:
 *   - Mono:   one voice; last key wins, falling back legato to the one held.
 *   - Poly 1: each note takes the next free voice in turn.
 *   - Poly 2: a note returns to the voice that last played it, else the
 *             lowest free one.
 *   - Chord:  each key plays the stored chord from its root (SetChord), one
 *             chord at a time.
 *   - Unison: four detuned voices on one note, struck a block apart.
 *  With all voices busy, a new note takes the oldest released voice, else
 *  the oldest held one.
 *
 *  Strum (poly, chord): notes starting together are spaced SetStrum apart,
 *  in the arp's pattern order; the arp's octaves add a glissando through
 *  each inversion. Cycle: held notes restrike when their ENV attack and
 *  decay finish. Detune: a small drift per voice.
 *
 *  Spread places each voice in the stereo field (kPan). The arpeggiator,
 *  when on, plays the held keys. Portamento glides from the last note
 *  played, always or legato.
 *
 *  Modulation: the LFO and each voice's envelope reach six destinations.
 *  The LFO trigger is FREE, NOTE (every new note restarts it) or VOICE (one
 *  per voice). Depths glide. The wheel bends pitch; its mod adds LFO to the
 *  brightness (up) or pitch (down).
 *
 *  The sequencer plays on the last four voices while running. One tempo
 *  drives the sequencer, arp, LFO sync and MIDI clock out; an incoming MIDI
 *  clock replaces it. MIDI events are queued for the firmware (PopEvent).
 *
 *  Runs in the audio interrupt: nothing allocates or blocks.
 */
#pragma once
#include "voice.h"
#include "body.h"
#include "sympathetic.h"
#include "chorus.h"
#include "space.h"
#include "lfo.h"
#include "arp.h"
#include "output.h"
#include "sequencer.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace phymo
{

enum class KeyMode { Mono, Poly1, Poly2, Unison, Chord };
enum class PortaMode { Off, Always, Legato };

/** What restarts the LFO: every new note (NOTE), nothing (FREE), or each
 *  voice's own notes (VOICE). Stored in presets as 0, 1, 2. */
enum class LfoTrigger { Note, Free, Voice };

/** What the synth played, for MIDI out. */
struct OutEvent
{
    enum Type : uint8_t { NoteOn, NoteOff, Clock, Start, Stop } type;
    uint8_t note;
};

/** Per-part block costs in cycle-counter ticks (SetProfileClock): sums and
 *  worsts, and the worst block's voices and model. */
struct Profile
{
    enum Part { Control, Voices, Body, Chorus, Space, Out, kParts };
    uint64_t sum[kParts]   = {};
    uint32_t worst[kParts] = {};
    uint32_t blocks = 0, worst_block = 0, voices_sum = 0;
    uint8_t  worst_voices = 0, worst_model = 0;
    uint64_t delay_sum = 0, reverb_sum = 0; // delay and reverb
    uint32_t delay_worst = 0, reverb_worst = 0;
    uint32_t worst_one = 0;           // one voice's worst block
    int8_t   worst_one_note = -1; // its note
    bool     worst_one_struck = false; // struck in that block
};

class Synth
{
  public:
    static constexpr int   kVoices    = 8;
    static constexpr int   kSeqVoices = 4; // the last four, while running
    static constexpr int   kUnisonVoices = 4; // unison voices
    static constexpr int   kStrumMax  = 32; // strum queue size
    static constexpr float kStrumTime = 1.f; // strum spacing at full, s (squared)
    static constexpr float kStrumGather = 0.01f; // the first strummed note waits this long for the rest
    static constexpr int   kGlissMax  = 3; // glissando octaves, at most
    static constexpr float kGlissFade = 0.7f; // velocity per octave (-3 dB)
    static constexpr int   kGlissTop  = 108; // no pass above C8
    static constexpr int   kSeqRoot   = 1000; // sequencer voices' Root()
    static constexpr int   kMaxHeld   = 32; // keys remembered
    static constexpr int   kMaxBlock  = 48; // samples per Process call
    static constexpr float kVoiceGain = 0.25f; // each voice into the mix
    static constexpr float kKnee      = 0.7f; // output limiter knee
    static constexpr float kShapeSmooth = 0.02f; // MODEL knob glide, each of two stages, s
    static constexpr float kSmooth    = 0.01f; // wheel glide, each of two stages, s
    static constexpr int   kPartialBudget = 96; // Spectral partials, all voices together

    /** The strings' delay lines: 2 x kVoices of StringRes::kLen floats.
     *  Set before Init. */
    void SetStringMemory(float* const* m)
    {
        for(int i = 0; i < 2 * kVoices; i++)
            str_mem_[i] = m[i];
    }

    void Init(float sr)
    {
        sr_          = sr;
        space_ready_ = false;
        for(int i = 0; i < kVoices; i++)
        {
#ifndef PHYMO_STRINGS_OUTSIDE
            if(!str_mem_[2 * i])
                str_mem_[2 * i] = own_str_[2 * i], str_mem_[2 * i + 1] = own_str_[2 * i + 1];
#endif
            voices_[i].SetMemory(str_mem_[2 * i], str_mem_[2 * i + 1]);
        }
        for(auto& v : voices_)
            v.Init(sr);
        chorus_.Init(sr);
        lfo_.Init(sr);
        arp_.Init(sr);
        seq_.Init(sr);
        output_.Init();
        body_.Init(sr);
        sym_.Init(sr);
        vowel_.Init(sr);
        trem_.Init(sr);
        Reset();
    }

    void Reset()
    {
        for(auto& v : voices_)
            v.Reset();
        chorus_.Reset();
        lfo_.Reset();
        arp_.Reset();
        body_.Reset();
        sym_.Reset();
        vowel_.Reset();
        trem_.Reset();
        chord_[0] = 0, chord_[1] = 4, chord_[2] = 7; // major triad
        chord_n_    = 3;
        chord_edit_ = false;
        arp_on_     = false;
        arp_run_    = true;
        ev_head_ = ev_tail_ = 0;
        external_   = false;
        ticks_      = 0;
        clock_acc_  = 0.f;
        last_tick_  = 0;
        samples_    = 0;
        porta_      = PortaMode::Off;
        last_note_  = -1;
        glide_time_ = 0.1f;
        p_         = VoiceParams();
        mode_      = KeyMode::Poly1;
        held_      = 0;
        next_      = 0;
        count_     = 0;
        uni_next_ = uni_n_ = 0;
        strum_n_ = 0, strum_wait_ = 0, strum_ = 0.f, cycle_ = false, strumming_ = false;
        strum_count_ = 0, strum_head_ = strum_prev_ = -1;
        volume_    = 0.7f;
        level_     = 0.f;
        for(int d = 0; d < kDests; d++)
            lfo_d_[d].Jump(0.f), env_d_[d].Jump(0.f);
        lfo_trig_  = LfoTrigger::Note;
        trem_depth_ = 0.f;
        SetEnv(0.005f, 0.5f, 0.f, 0.3f);
        lfo_rate_  = 5.f;
        lfo_sync_  = 0.f;
        bpm_       = 120.f;
        ext_bpm_   = 120.f;
        arp_tps_   = 6; // 16ths
        seq_mode_  = false;
        arp_locked_ = false;
        lock_tick_  = -1;
        other_held_ = 0;
        live_      = kVoices;
        budget_    = kVoices, calm_ = hot_ = 0;
        vel_       = 1.f;
        loop_      = false;
        seq_level_ = 1.f;
        live_level_ = 1.f;
        seq_div_   = 3; // 1/16
        metro_     = false;
        beat_      = -1;
        click_left_ = 0;
        seq_glide_ = 0.f;
        rec_       = false;
        UpdateRates();
        detune_    = 0.f;
        mod_       = 0.f;
        SetSpread(0.3f);
        bright_s_.Jump(p_.bright), pos_s_.Jump(p_.strike.position);
        bend_s_.Jump(p_.bend), mod_s_.Jump(0.f);
        table_s_.Jump(p_.table), stretch_s_.Jump(p_.stretch), snap_s_.Jump(p_.snap), partials_s_.Jump(p_.partials);
    }

    // --- notes ---

    /** A key pressed; `echo` false for MIDI input (not sent back out). */
    void NoteOn(int note, bool echo = true, float vel = 1.f)
    {
        vel_ = vel; // CHOMPI keys: full velocity
        if(echo && !KeysToArp())
            Push(OutEvent::NoteOn, note);
        Forget(note); // in case of a MIDI repeat
        if(held_ == kMaxHeld)
            Forget(stack_[0]); // full: drop the oldest
        stack_[held_++] = note;
        if(rec_ && seq_mode_ && seq_.Running())
            seq_.RecordOn(note);
        if(KeysToArp()) // the arp plays the held keys
            return;
        if(StackMode())
            ReleaseAll(); // one chord (or unison note) at a time
        VoiceOn(note);
    }

    /** A key let go. */
    void NoteOff(int note, bool echo = true)
    {
        if(echo && !KeysToArp())
            Push(OutEvent::NoteOff, note);
        const bool was_top = held_ > 0 && stack_[held_ - 1] == note;
        Forget(note);
        if(rec_ && seq_mode_)
            seq_.RecordOff(note);
        if(KeysToArp())
            return;
        if(StackMode())
        {
            if(!was_top)
                return; // not the chord sounding
            VoiceOff(note);
            if(held_ > 0)
                VoiceOn(stack_[held_ - 1]); // back to the held key's chord
            return;
        }
        if(mode_ == KeyMode::Mono)
        {
            if(!was_top)
                return; // not the one sounding
            if(held_ > 0)
                voices_[0].SetNote(stack_[held_ - 1], Glide() ? voices_[0].Pitch() : -1.f); // legato back
            else
                voices_[0].NoteOff();
            return;
        }
        VoiceOff(note);
    }

    /** Releases every voice; held keys are forgotten. */
    void AllNotesOff()
    {
        for(auto& v : voices_)
            v.NoteOff();
        uni_next_ = uni_n_; // no unison strikes pending
        strum_n_  = 0, passes_ = 0, let_n_ = 0;
        arp_.Silence();
        held_ = 0;
    }

    // --- the arpeggiator ---

    /** On: the arp plays the held keys. Off: held keys sound as they are. */
    void SetArp(bool on)
    {
        if(on == arp_on_)
            return;
        arp_on_ = on;
        if(on)
        {
            for(int i = 0; i < held_; i++) // the arp's notes take over
                Push(OutEvent::NoteOff, stack_[i]);
            Push(OutEvent::Start, 0);
            arp_run_ = true;
            for(auto& v : voices_)
                v.NoteOff();
            arp_.Restart();
            return;
        }
        StopArpNote();
        Push(OutEvent::Stop, 0);
        if(mode_ == KeyMode::Mono)
        {
            if(held_ > 0)
                VoiceOn(stack_[held_ - 1]), Push(OutEvent::NoteOn, stack_[held_ - 1]);
        }
        else
            for(int i = 0; i < held_; i++)
                VoiceOn(stack_[i]), Push(OutEvent::NoteOn, stack_[i]);
    }
    bool GetArp() const { return arp_on_; }

    /** Play / pause: paused, the arp holds its place, silent. */
    void SetArpRun(bool run)
    {
        if(!run)
            StopArpNote();
        if(arp_on_ && run != arp_run_)
            Push(run ? OutEvent::Start : OutEvent::Stop, 0);
        arp_run_ = run;
    }

    // --- MIDI clock in ---

    /** External (MIDI) clock, or the tempo. */
    void SetExternalClock(bool on)
    {
        external_ = on;
        arp_.SetExternal(on);
        seq_.SetExternal(on);
        UpdateRates();
    }
    bool GetExternalClock() const { return external_; }

    /** A MIDI clock tick (24 a beat): the sequencer steps every sixth, the
     *  arp at its division; the tick spacing sets the tempo. */
    void ClockTick()
    {
        if(!external_)
            return;
        if(last_tick_ > 0)
        {
            const float dt = static_cast<float>(samples_ - last_tick_);
            if(dt > 0.f)
            {
                const float bpm = 60.f * sr_ / (24.f * dt);
                ext_bpm_ += (bpm - ext_bpm_) * 0.2f;
                UpdateRates();
            }
        }
        last_tick_ = samples_;
        if(ticks_ % (arp_tps_ > 0 ? arp_tps_ : 6) == 0) // free rate: 16ths
            arp_.Step();
        if(ticks_ % (24 / SeqSteps(seq_div_)) == 0)
            seq_.Step();
        ticks_++;
    }
    /** MIDI start: the arp and sequence from the top. */
    void ClockStart()
    {
        ticks_ = 0;
        arp_.Restart();
        SetArpRun(true);
        seq_.Start();
    }
    void ClockStop()
    {
        SetArpRun(false);
        seq_.Stop();
    }
    void ClockContinue()
    {
        SetArpRun(true);
        if(!seq_.Running())
            seq_.Start();
    }

    // --- the master tempo ---

    /** BPM, 30 to 300. */
    void SetTempo(float bpm)
    {
        bpm_ = bpm < 30.f ? 30.f : bpm > 300.f ? 300.f : bpm;
        UpdateRates();
    }
    /** The current tempo: the master's, or the MIDI clock's. */
    float GetTempo() const { return external_ ? ext_bpm_ : bpm_; }
    /** The arp step in MIDI clock ticks: 24 (1/4), 16 (1/4T), 12 (1/8),
     *  8 (1/8T), 6 (1/16), 4 (1/16T), 3 (1/32). */
    void SetArpDivision(int ticks_per_step)
    {
        arp_tps_ = ticks_per_step < 1 ? 1 : ticks_per_step > 96 ? 96 : ticks_per_step;
        UpdateRates();
    }

    /** Sequencer mode: the keys play directly (and record when armed). */
    void SetSeqMode(bool on)
    {
        if(on == seq_mode_)
            return;
        seq_mode_ = on;
        for(int i = 0; i < kMaxHeld; i++) // swap the keys' list and the arp's
        {
            const int t = stack_[i];
            stack_[i] = other_stack_[i], other_stack_[i] = t;
        }
        const int h = held_;
        held_ = other_held_, other_held_ = h;
    }
    bool GetSeqMode() const { return seq_mode_; }

    // --- the sequencer ---

    Sequencer&       Seq() { return seq_; }
    const Sequencer& Seq() const { return seq_; }
    void             SeqStart()
    {
        seq_.Start();
        if(arp_on_)
            arp_.Restart(); // the arp restarts on the first step
        if(!external_)
            Push(OutEvent::Start, 0);
    }
    void SeqStop()
    {
        seq_.Stop();
        if(!external_)
            Push(OutEvent::Stop, 0);
    }
    /** Sequencer voice level, 0 to 1. */
    void SetSeqLevel(float v) { seq_level_ = v; }
    /** Live voice level, 0 to 1. */
    void SetLiveLevel(float v) { live_level_ = v; }

    /** This block's CPU load (0 to 1): sustained over kLoadHigh, or over
     *  kLoadMax once, lowers the voice budget; under kLoadLow for
     *  kCalmBlocks raises it again. */
    void ReportLoad(float load)
    {
        load_ = load;
        // ignore blocks with a strike or a voice already choking; otherwise
        // set the budget one under what's sounding
        const bool struck = struck_;
        struck_           = false;
        int        sounding = 0;
        bool       going    = false;
        for(const auto& v : voices_)
            sounding += v.Active() && !v.Choking(), going = going || v.Choking();
        // the fixed cost: the load less the voices' (learnt)
        if(per_block_ > 0.f && cv_ > 0.f && !struck)
        {
            const float f = load - cv_ * static_cast<float>(sounding);
            cf_ += ((f > 0.f ? f : 0.f) - cf_) * 0.05f;
        }
        if(struck || going)
            hot_ = 0;
        else if(load > kLoadMax || (load > kLoadHigh && ++hot_ >= 3))
        {
            const int b = sounding - 1 < budget_ ? sounding - 1 : budget_ - 1;
            budget_     = b > kMinVoices ? b : kMinVoices;
            hot_ = calm_ = 0;
        }
        else if(load <= kLoadHigh)
            hot_ = 0;
        if(load < kLoadLow && ++calm_ >= kCalmBlocks)
        {
            budget_ = budget_ < kVoices ? budget_ + 1 : kVoices;
            calm_   = 0;
        }
        else if(load >= kLoadLow)
            calm_ = 0;
    }
    int  VoiceBudget() const { return budget_; }
    void SetVoiceBudget(int n) { budget_ = n < kMinVoices ? kMinVoices : n > kVoices ? kVoices : n; }
    static constexpr float kLoadMax = 0.92f, kLoadHigh = 0.85f, kLoadLow = 0.6f;
    static constexpr int   kCalmBlocks = 500; // half a second
    static constexpr int   kMinVoices  = 2;

    /** Sequencer step: 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 (0-5). */
    static constexpr int kSeqDivs = 6;
    /** Steps a beat at division `d`. */
    static int SeqSteps(int d)
    {
        static const int steps[kSeqDivs] = {1, 2, 3, 4, 6, 8};
        return steps[d];
    }
    void                 SetSeqDivision(int d)
    {
        seq_div_ = d < 0 ? 0 : d >= kSeqDivs ? kSeqDivs - 1 : d;
        UpdateRates();
    }
    int GetSeqDivision() const { return seq_div_; }

    /** The metronome: a click each beat while running, the bar's first higher. */
    void SetMetronome(bool on) { metro_ = on; }
    bool GetMetronome() const { return metro_; }
    /** A strummed step's spacing when free, s. */
    void  SetSeqStrum(float seconds) { seq_strum_ = seconds < 0.f ? 0.f : seconds; }
    float GetSeqStrum() const { return seq_strum_; }
    /** Synced: the strum spreads over the step's length. */
    void  SetSeqSync(bool on) { seq_sync_ = on; }
    bool  GetSeqSync() const { return seq_sync_; }
    void  SetSeqSpread(float share) { seq_spread_ = share < 0.f ? 0.f : share > 1.f ? 1.f : share; }
    float GetSeqSpread() const { return seq_spread_; }

    /** Plays step `s` on the sequencer voices for its length (a second at
     *  most), stopped. */
    void AuditionStep(const SeqStep& s)
    {
        Audition(-1); // end the last one's notes
        if(!s.on || s.count == 0)
            return;
        int notes[SeqStep::kNotes], n = 0;
        for(int i = 0; i < s.count; i++)
        {
            const int x = s.note[i] + seq_.GetTranspose();
            if(x >= 0 && x <= 127)
                notes[n++] = x;
        }
        if(s.strum)
            Order(notes, n);
        const float step = 60.f / (bpm_ * static_cast<float>(SeqSteps(seq_div_))); // s
        float       len  = step * static_cast<float>(s.gate) / 48.f;
        len              = len > kAuditionMax ? kAuditionMax : len;
        for(int i = 0; i < n; i++)
        {
            const int heard = static_cast<int>(48.f * len / step + 0.5f); // its length as heard, a second at most
            const int late  = s.strum ? static_cast<int>(static_cast<float>(i) * SeqSpacing(heard, n)) : 0;
            SeqStrummed(notes[i], late);
            aud_note_[i] = notes[i];
        }
        aud_n_    = n;
        aud_left_ = static_cast<int>(len * sr_) + (s.strum && !seq_sync_ ? static_cast<int>(static_cast<float>(n - 1) * SeqSpacing(s.gate, n)) : 0);
    }

    void SetSeqGlide(float seconds)
    {
        seq_glide_ = seconds > 0.f ? 1.f - expf(-static_cast<float>(Voice::kModEvery)
                                                 / (0.2f * (seconds < 0.001f ? 0.001f : seconds) * sr_))
                                   : 0.f;
    }
    /** Recording: played notes go into the running sequence. */
    void SetRecord(bool r) { rec_ = r; }
    bool GetRecord() const { return rec_; }

    /** What the synth played, for MIDI out; false when there's nothing. */
    bool PopEvent(OutEvent& e)
    {
        if(ev_tail_ == ev_head_)
            return false;
        e        = events_[ev_tail_];
        ev_tail_ = (ev_tail_ + 1) % kEvents;
        return true;
    }
    bool GetArpRun() const { return arp_run_; }
    /** For the LEDs: the arp's sounding note (-1: none). */
    int   GetArpNote() const { return arp_on_ && arp_run_ ? arp_.Sounding() : -1; }
    float GetArpPhase() const { return arp_.Phase(); }
    /** Position in the beat while running, 0 to 1 (-1 stopped). */
    float GetSeqBeatPhase() const
    {
        if(!seq_.Running())
            return -1.f;
        const double b = seq_.Position() / SeqSteps(seq_div_);
        return static_cast<float>(b - static_cast<double>(static_cast<long>(b)));
    }

    /** The arp rate in steps a second (tests; the panel uses SetArpDivision). */
    void SetArpRate(float steps_per_s)
    {
        arp_tps_ = 0;
        arp_free_rate_ = steps_per_s;
        UpdateRates();
    }
    /** Per-step chance of a rest or an octave jump, 0 to 1. */
    void SetArpRandom(float p) { arp_.SetRandom(p); }
    void SetArpPattern(Arp::Pattern p) { arp_.SetPattern(p); }
    void SetArpOctaves(int o) { arp_.SetOctaves(o); }

    // --- portamento ---

    /** Portamento: glide time (s) and mode (always, legato, off). Switching
     *  on starts afresh. */
    void SetPortamento(PortaMode m, float seconds)
    {
        if(porta_ == PortaMode::Off && m != PortaMode::Off)
            last_note_ = -1;
        porta_      = m;
        glide_time_ = seconds;
        // per-UpdateMod share: a time constant of a fifth of the time
        const float tau = 0.2f * (seconds < 0.001f ? 0.001f : seconds);
        p_.glide        = m != PortaMode::Off ? 1.f - expf(-static_cast<float>(Voice::kModEvery) / (tau * sr_)) : 0.f;
    }
    PortaMode GetPortamento() const { return porta_; }

    // --- strum, the glissando, cycle ---

    /** Strum, 0 (off) to 1 (kStrumTime between notes, squared). */
    void  SetStrum(float v) { strum_ = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    float GetStrum() const { return strum_; }
    /** Glissando octaves: the arp's less one, when strumming. */
    int Gliss() const { return strum_ > 0.f ? arp_.Octaves() - 1 : 0; }
    /** Cycle: held notes restrike when their ENV attack and decay finish. */
    void SetCycle(bool on) { cycle_ = on; }
    bool GetCycle() const { return cycle_; }

    void    SetKeyMode(KeyMode m)
    {
        if(m == mode_)
            return;
        AllNotesOff();
        mode_ = m;
        Pan();
        Detune();
    }

    /** Stereo spread of the voices, 0 (centred) to 1. */
    void SetSpread(float s)
    {
        spread_ = s;
        Pan();
    }
    KeyMode GetKeyMode() const { return mode_; }

    // --- chord memory ---

    /** The chord for chord mode, from `n` notes (any order, up to kVoices):
     *  kept as intervals above the lowest. */
    void SetChord(const int* notes, int n)
    {
        if(n <= 0)
            return;
        n = n > kVoices ? kVoices : n;
        int lowest = notes[0];
        for(int i = 1; i < n; i++)
            lowest = notes[i] < lowest ? notes[i] : lowest;
        for(int i = 0; i < n; i++)
            chord_[i] = notes[i] - lowest;
        for(int i = 1; i < n; i++) // low to high
            for(int j = i; j > 0 && chord_[j] < chord_[j - 1]; j--)
            {
                const int t   = chord_[j];
                chord_[j]     = chord_[j - 1];
                chord_[j - 1] = t;
            }
        chord_n_ = n;
    }

    /** The chord memory's intervals into `out` (kVoices of room); returns how many. */
    int GetChord(int* out) const
    {
        for(int i = 0; i < chord_n_; i++)
            out[i] = chord_[i];
        return chord_n_;
    }

    /** While a chord's being chosen, keys play single notes (chord mode
     *  acts as poly 1), so you hear the notes you pick. */
    void SetChordEdit(bool on) { chord_edit_ = on; }

    // --- sound ---

    /** Fills `l` and `r` with `n` samples (any `n`; in pieces of kMaxBlock). */
    /** A cycle counter for profiling and the room model; none, nothing timed. */
    void SetProfileClock(uint32_t (*clock)()) { clock_ = clock, space_.SetClock(clock); }
    const Profile& GetProfile() const { return prof_; }
    /** A block's cycles on that clock (0: none, as on the desktop). */
    void SetBlockTicks(float ticks) { per_block_ = ticks; }
    /** Tests: set the learnt fixed, voice and strike costs, as shares of a block. */
    void SetLoadModel(float fixed, float voice, float strike)
    {
        cf_ = fixed, cv_ = voice, cs_ = strike;
        per_block_ = per_block_ > 0.f ? per_block_ : 1.f;
    }

    void ResetProfile() { prof_ = Profile(); }

    void Process(float* l, float* r, size_t n)
    {
        struck_ = false; // for ReportLoad: a strike this block
        for(size_t pos = 0; pos < n; pos += kMaxBlock)
            ProcessBlock(l + pos, r + pos, n - pos < kMaxBlock ? n - pos : kMaxBlock);
    }

    /** Output level, as a gain (knob 6). */
    void SetVolume(float v) { volume_ = v; }

    // the string: brightness and strike position glide (the wheel moves them)

    /** Brightness, 0 to 1 (the wheel). */
    void SetBrightness(float v) { bright_s_.Set(v); }
    /** Strike position, 0 (near the bridge) to 1 (the middle). */
    void SetPosition(float v) { pos_s_.Set(v); }
    /** The strike, each 0 to 1: hammer to pick, soft to hard, noise, strength. */
    void SetType(float v) { p_.strike.type = v; }
    void SetHardness(float v)
    {
        p_.strike.hardness = v;
        p_.fm_depth        = 10.f * v; // FM index, up to 10 radians
    }
    void SetNoise(float v)
    {
        p_.strike.noise = v;
        p_.fm_fb        = 1.5f * v; // FM feedback, up to 1.5 radians
    }
    void SetStrength(float v) { p_.strike.strength = v; }
    /** The latch loop (hold on): each held note restrikes once it has faded
     *  36 dB below its peak. Not while the arp plays. */
    void SetLatchLoop(bool on) { loop_ = on; }
    /** `note`'s level against its strike's peak, 0 to 1 (key LEDs). */
    float NoteFade(int note) const
    {
        float f = 0.f;
        for(const auto& v : voices_)
            if(v.Gated() && v.Note() == note && v.Fade() > f)
                f = v.Fade();
        return f;
    }

    /** Velocity to strength, 0 to 1. */
    void SetVelocityAmount(float v) { p_.vel_amount = v; }
    /** Per-strike randomness, 0 to 1. */
    void SetLife(float v) { p_.life = v; }
    /** String second string, 0 (none) to 1 (15 cents). */
    void SetCourse(float v) { p_.course = v; }
    /** Vocal ensemble, 0 to 1: per-note pitch and formants. */
    void SetEnsemble(float v) { p_.ensemble = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    /** Spectral tonebar, 0 (none) to 1 (modal_res.h). */
    void SetTonebar(float v) { p_.tonebar = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    /** Vocal tape, 0 (none) to 1: shared wow and flutter, tape colour on the
     *  mix, and an 8 s reel per held note. */
    void SetTape(float v) { p_.tape = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    float GetTape() const { return p_.tape; }
    float GetTonebar() const { return p_.tonebar; }
    float GetEnsemble() const { return p_.ensemble; }
    /** 0 to 1: the second string's interval, up to two octaves, pulled
     *  towards unison, a fifth, the octave, the twelfth and two octaves. */
    void SetInterval(float v)
    {
        static const float kAt[5] = {0.f, 7.01955f, 12.f, 19.01955f, 24.f}; // 1, 3:2, 2, 3, 4
        const float x = (v < 0.f ? 0.f : v > 1.f ? 1.f : v) * 4.f; // 0 to 4 along kAt
        const float n = floorf(x + 0.5f), t = x - n; // nearest, and offset (±0.5)
        const float a = 2.f * (t < 0.f ? -t : t), a5 = a * a * a * a * a;
        const float y = n + 0.5f * (t < 0.f ? -a5 : a5); // pulled towards it
        int         i = static_cast<int>(y);
        i             = i > 3 ? 3 : i;
        p_.interval   = kAt[i] + (kAt[i + 1] - kAt[i]) * (y - static_cast<float>(i));
    }
    float GetInterval() const { return p_.interval; }
    /** 0 to 1: Bowed flageolet (CHOMPI + MODEL knob 2): off at the bottom,
     *  then a touch at 1/2 to 1/6 of the string, in equal steps. */
    void SetFlageolet(float x)
    {
        x = x < 0.f ? 0.f : x > 1.f ? 1.f : x;
        if(x < kFlagOff)
        {
            p_.flag_a = 0.f;
            return;
        }
        int h = 2 + static_cast<int>((x - kFlagOff) / (1.f - kFlagOff) * 5.f);
        p_.flag_n = h > 6 ? 6 : h, p_.flag_a = 1.f;
    }
    static constexpr float kFlagOff = 0.1f;
    /** RESONATOR knob 1, s: String/Spectral swell (0 none), FM/bow attack. */
    void SetAttack(float seconds) { p_.attack = seconds < 0.f ? 0.f : seconds; }
    /** RESONATOR knob 4, 0 (off) to 1: the level a held note holds at. */
    void SetSustain(float v) { p_.sustain = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    /** T60 while held and once released, s. */
    void SetDecay(float seconds) { p_.decay = seconds; }
    void SetDamper(float seconds) { p_.damper = seconds; }
    /** Decay key tracking, 0 to 1 (1: half as long each octave up). */
    void SetKeyTrack(float v) { p_.key_track = v; }

    // the model and shared spectrum; the EXCITER knobs also drive FM

    void  SetModel(Model m) { p_.model = m; }
    Model GetModel() const { return p_.model; }
    /** MODEL knob 1, 0 to 1: Spectral tables, FM ratio list, String
     *  materials or Bowed bows. */
    void SetTable(float x) { Shape(table_s_, p_.table, x * static_cast<float>(kTables - 1)); }
    /** 0 to 1, 0.5 none: partials squeezed or spread; String stiffness above
     *  the middle. */
    void SetStretch(float x) { Shape(stretch_s_, p_.stretch, x - 0.5f); }
    /** 0 to 1: towards whole-number ratios. */
    void SetSnap(float x) { Shape(snap_s_, p_.snap, x); }
    /** 0 to 1: 1 to 24 partials. */
    void SetPartials(float x) { Shape(partials_s_, p_.partials, 1.f + static_cast<float>(kModes - 1) * x); }
    /** 0 to 1, 0.5 none: each note's spectrum offset, gliding back over the
     *  morph time. */
    void SetMorph(float x) { p_.morph = (2.f * x - 1.f) * static_cast<float>(kTables - 1); }
    void SetMorphTime(float seconds) { p_.morph_time = seconds < 0.005f ? 0.005f : seconds; }
    /** FM ratio list: 0 harmonic, 1 odd, 2 halves, 3 metal. */
    void SetFmList(int l) { p_.fm_list = l < 0 ? 0 : l > kFmLists - 1 ? kFmLists - 1 : l; }
    int  GetFmList() const { return p_.fm_list; }

    // the pickup per voice; body, vowel and tremolo on the mix

    /** Clean, Tine, Reed, Wood, Vowel or Violin (the last three on the mix). */
    void SetPickup(PickupMode m)
    {
        p_.pickup = m;
        body_.SetKind(m == PickupMode::Violin);
        body_.SetAmount(m == PickupMode::Wood || m == PickupMode::Violin ? p_.drive : 0.f);
        vowel_.SetAmount(m == PickupMode::Vowel ? p_.drive : 0.f);
    }
    PickupMode GetPickup() const { return p_.pickup; }
    /** 0 to 1: pickup filters or body size (±1.5 octaves); with Vowel, the vowel. */
    void SetPickupTone(float t)
    {
        p_.tone = t;
        body_.SetTone(t);
    }
    /** 0 to 1: pickup drive, or the Wood / Vowel / Violin amount. */
    void SetPickupAmount(float v)
    {
        p_.drive = v;
        SetPickup(p_.pickup);
    }
    /** 0 to 1: pickup position: 0 the whole string, then bridge (thin) to
     *  middle (hollow). Not FM. */
    void  SetPickupPlace(float v) { p_.place = v < 0.f ? 0.f : v > 1.f ? 1.f : v; }
    float GetPickupPlace() const { return p_.place; }
    void SetTremoloRate(float hz) { trem_.SetRate(hz); }
    /** Tremolo depth, 0 to 1 (Bowed and Vocal use vibrato instead). */
    void SetTremoloDepth(float d) { trem_depth_ = d; }
    /** Bowed vibrato: rate (Hz), depth (0 to 1, ± kVibMax semitones),
     *  onset (s), and decay (s; below 0, held until release). */
    void SetVibrato(float hz, float depth, float onset, float decay)
    {
        p_.vib_hz    = hz;
        const float d = depth < 0.f ? 0.f : depth > 1.f ? 1.f : depth;
        p_.vib_depth = kVibMax * (Exp2Fast(kVibCurve * d) - 1.f) / (Exp2Fast(kVibCurve) - 1.f); // anti-log
        p_.vib_onset = onset < 0.f ? 0.f : onset;
        p_.vib_decay = decay;
    }
    static constexpr float kVibMax   = 1.f; // ± semitones at full
    static constexpr float kVibCurve = 5.f; // depth curve: 2^(5x)
    /** Bowed strokes: length each way (s; 0 one endless stroke) and weight
     *  along the bow (0 even, 1 heavier at the frog). */
    void SetBowStroke(float seconds, float weight)
    {
        p_.stroke = seconds < 0.f ? 0.f : seconds;
        p_.weight = weight < 0.f ? 0.f : weight > 1.f ? 1.f : weight;
    }
    /** 0: the same both sides; 1: opposite (autopan). */
    void SetTremoloSpread(float s) { trem_.SetSpread(s); }

    /** Detune, 0 to 1: unison spread (±25 cents) and per-voice drift. */
    void SetDetune(float d)
    {
        detune_ = d;
        Detune();
    }

    // modulation depths per destination, -1 to 1

    void  SetLfoDepth(Dest d, float v) { lfo_d_[static_cast<int>(d)].Set(Clamp(v)); }
    void  SetEnvDepth(Dest d, float v) { env_d_[static_cast<int>(d)].Set(Clamp(v)); }
    float GetLfoDepth(Dest d) const { return lfo_d_[static_cast<int>(d)].t; }
    float GetEnvDepth(Dest d) const { return env_d_[static_cast<int>(d)].t; }
    /** Legacy LFO depths, 0 to 1. */
    void SetLfoToPitch(float v) { SetLfoDepth(Dest::Pitch, v); }
    void SetLfoToBright(float v) { SetLfoDepth(Dest::Bright, v); }
    void SetLfoToMorph(float v) { SetLfoDepth(Dest::Morph, v); }

    /** The envelope: attack, decay, release (s), sustain 0 to 1. */
    void SetEnv(float attack, float decay, float sustain, float release)
    {
        p_.env = Env::Times(attack, decay, sustain, release, sr_, Voice::kModEvery);
    }

    /** LFO rate (Hz), delay and fade (s). */
    void SetLfoRate(float hz)
    {
        lfo_rate_ = hz;
        LfoRate();
    }
    /** Sync to the arp: 0 free, else LFO cycles per arp step (1/8 to 2). */
    void SetLfoSync(float cycles_per_step)
    {
        lfo_sync_ = cycles_per_step;
        LfoRate();
    }
    float GetLfoSync() const { return lfo_sync_; }
    /** The LFO's current rate, Hz. */
    float LfoHz() const { return lfo_sync_ > 0.f ? arp_rate_ * lfo_sync_ : lfo_rate_; }
    void  SetLfoShape(Lfo::Shape sh)
    {
        lfo_.SetShape(sh);
        p_.lfo_shape = sh;
    }
    void SetLfoDelay(float delay_s, float fade_s)
    {
        lfo_.SetDelay(delay_s);
        lfo_.SetFade(fade_s);
        p_.lfo_delay = delay_s, p_.lfo_fade = fade_s;
    }
    /** What restarts the LFO. */
    void SetLfoTrigger(LfoTrigger t)
    {
        lfo_trig_     = t;
        p_.lfo_voice  = t == LfoTrigger::Voice;
    }
    LfoTrigger GetLfoTrigger() const { return lfo_trig_; }
    /** The synth LFO's value, -1 to 1 (tests). */
    float LfoValue() const { return p_.lfo; }
    /** Free-running (or NOTE). */
    void SetLfoFree(bool f) { SetLfoTrigger(f ? LfoTrigger::Free : LfoTrigger::Note); }
    bool GetLfoFree() const { return lfo_trig_ == LfoTrigger::Free; }

    /** The wheel: bend -1 to 1 (± Voice::kBendRange semitones), mod -1 to 1. */
    void SetBend(float b) { bend_s_.Set(b); }
    void SetMod(float m) { mod_s_.Set(m); }

    void         SetChorus(Chorus::Mode m) { chorus_.SetMode(m); }
    Chorus::Mode GetChorus() const { return chorus_.GetMode(); }
    void         SetChorusLevel(float v) { chorus_.SetLevel(v); }

    /** Output stage on / off (on at boot). */
    void SetOutputStage(bool on) { output_.SetOn(on); }
    bool GetOutputStage() const { return output_.On(); }

    /** Memory for the reverb tank (Space::kTankWords) and delay lines
     *  (Space::kDelayWords); without it they're skipped. Clears both: call
     *  from the main loop. */
    void InitSpace(uint16_t* tank, int16_t* delay_mem)
    {
        space_.Init(sr_, tank, delay_mem);
        space_ready_ = true;
    }
    // the send bus, each 0 to 1: send, space, time, tails, ping-pong
    void SetSend(float v) { space_.SetSend(v); }
    void SetSpace(float v) { space_.SetSpace(v); }
    void SetTime(float v) { space_.SetTime(v); }
    void SetTails(float v) { space_.SetTails(v); }
    void SetPingPong(float v) { space_.SetPingPong(v); }
    void SetDelayGlide(bool g) { space_.SetGlide(g); }
    bool GetDelayGlide() const { return space_.Glide(); }

    /** Voices sounding. */
    int ActiveVoices() const
    {
        int a = 0;
        for(const auto& v : voices_)
            a += v.Active();
        return a;
    }
    const Voice& GetVoice(int i) const { return voices_[i]; }

  private:
    void ProcessBlock(float* l, float* r, size_t n)
    {
        uint32_t t[Profile::kParts + 1];
        t[0] = Clock();
        samples_ += static_cast<uint32_t>(n);
        if(uni_next_ < uni_n_) // unison: the next voice, a block after the last
            voices_[uni_next_++].NoteOn(uni_note_, ++count_, uni_from_, uni_note_, uni_vel_);
        Strum(static_cast<int>(n));
        GlideControls(n);
        Detune(n); // drift
        // sequencer notes on its voices
        {
            Sequencer::Event ev[Sequencer::kMaxEvents];
            const int        k = seq_.Tick(n, ev);
            int              strummed[Sequencer::kMaxEvents], ns = 0; // a strummed step's notes
            const Sequencer::Event* first = nullptr; // its first note
            for(int i = 0; i < k; i++)
            {
                if(ev[i].on && ev[i].strum && seq_.Running())
                    strummed[ns++] = ev[i].note, first = first ? first : &ev[i]; // as entered
                else if(ev[i].on)
                    SeqVoiceOn(ev[i].note), Push(OutEvent::NoteOn, ev[i].note);
                else
                    SeqLater(ev[i].note, false, seq_.Running() ? seq_late_[ev[i].note] : 0);
            }
            Order(strummed, ns);
            for(int i = 0; i < ns; i++)
                SeqStrummed(strummed[i], seq_sync_ ? SeqOnGrid(*first, i, ns) : static_cast<int>(static_cast<float>(i) * SeqSpacing(0, ns)));
            if(seq_was_ && !seq_.Running())
                for(int i = 0; i < seq_n_; i++) // just stopped: drop pending starts
                    if(seq_q_[i].on)
                        seq_q_[i--] = seq_q_[--seq_n_];
            seq_was_ = seq_.Running();
            SeqDue();
            Audition(static_cast<int>(n));
            live_ = seq_.Running() ? kVoices - kSeqVoices : kVoices;
        // metronome
            const long beat = seq_.Running() ? static_cast<long>(seq_.Position() / SeqSteps(seq_div_)) : -1;
            if(beat != beat_ && beat >= 0 && metro_)
                StartClick(beat % 4 == 0);
            beat_ = beat;
        }
        LockArp();
        // arp step
        if(arp_on_ && arp_run_)
        {
            const Arp::Event e = seq_mode_ ? arp_.Tick(n, other_stack_, other_held_) : arp_.Tick(n, stack_, held_);
            if(e.off >= 0)
                VoiceOff(e.off), Push(OutEvent::NoteOff, e.off);
            if(e.on >= 0)
                VoiceOn(e.on, false), Push(OutEvent::NoteOn, e.on);
        }
        // MIDI clock out, 24 a beat
        if(!external_ && ((arp_on_ && arp_run_) || seq_.Running()))
        {
            clock_acc_ += 24.f * bpm_ / 60.f * static_cast<float>(n) / sr_;
            while(clock_acc_ >= 1.f)
                Push(OutEvent::Clock, 0), clock_acc_ -= 1.f;
        }

        // the LFO and depths, plus the wheel's mod (brightness up, pitch down)
        p_.lfo = lfo_.Advance(static_cast<uint32_t>(n));
        TapeSpeed(n);
        for(int d = 0; d < kDests; d++)
            p_.lfo_to[d] = lfo_d_[d].b, p_.env_to[d] = env_d_[d].b;
        const int pd = static_cast<int>(Dest::Pitch), bd = static_cast<int>(Dest::Bright);
        p_.lfo_to[pd] = p_.lfo_to[pd] * fabsf(p_.lfo_to[pd]) + (mod_ < 0.f ? mod_ * mod_ : 0.f);
        p_.env_to[pd] = p_.env_to[pd] * fabsf(p_.env_to[pd]);
        p_.lfo_to[bd] += mod_ > 0.f ? mod_ : 0.f;

        // Spectral partial budget, shared among sounding voices
        {
            const int active = ActiveVoices();
            p_.partial_cap   = static_cast<float>(kPartialBudget) / static_cast<float>(active > 1 ? active : 1);
        }

        // CPU budget: choke the oldest released voice (else held) until it fits
        Budget();

        // sum and pan the voices (silent ones skipped)
        t[1] = Clock();
        for(size_t i = 0; i < n; i++)
            l[i] = r[i] = 0.f;
        // strikes from quiet: choke the surplus first (MakeRoom), then strike
        // as many as fit this block (StrikeRoom), the rest later
        bool any_waiting = false;
        for(const auto& v : voices_)
            any_waiting = any_waiting || v.Waiting();
        if(any_waiting)
            MakeRoom();
        int allow = StrikeRoom();
        if(!any_waiting)
            waited_ = 0;
        else if(allow > 0)
            waited_ = 0;
        else if(++waited_ > kStrikeWait)
            allow = 1, waited_ = 0;
        int      strikes = 0, steady_n = 0;
        uint32_t steady = 0, strike = 0;
        for(auto& v : voices_)
            if(v.Active())
            {
                if(v.Waiting() && strikes++ >= allow)
                    continue;
                const bool     hit = v.Striking();
                struck_            = struck_ || hit; // a strike this block
                const uint32_t a   = Clock();
                v.Process(p_, l, r, n, kVoiceGain * (v.Root() >= kSeqRoot ? seq_level_ : live_level_));
                const uint32_t d = Clock() - a;
                if(hit)
                    strike = d > strike ? d : strike;
                else if(!v.Choking())
                    steady += d, steady_n++;
                if(d > prof_.worst_one)
                    prof_.worst_one = d, prof_.worst_one_note = static_cast<int8_t>(v.Note()), prof_.worst_one_struck = hit;
            }
        Learn(steady, steady_n, strike);
        t[2] = Clock();
        // latch loop: faded held notes restrike
        if(loop_ && !(arp_on_ && arp_run_))
            for(int i = 0; i < live_; i++)
                if(voices_[i].Faded())
                    voices_[i].Restrike(), Retrigger();
        // cycle: restrike after the ENV's attack and decay
        if(cycle_)
            for(int i = 0; i < live_; i++)
                if(voices_[i].Root() < kSeqRoot && voices_[i].Cycled(p_))
                    voices_[i].Restrike(), Retrigger();
        // body and vowel tone: the newest note's modulation
        {
            const Voice* newest = nullptr;
            for(const auto& v : voices_)
                if(v.Active() && (!newest || v.Age() > newest->Age()))
                    newest = &v;
            newest_sym_ = newest ? static_cast<float>(newest->Note()) : newest_sym_;
            float t = p_.tone + (newest ? newest->ToneMod() : 0.f);
            t       = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
            if(p_.pickup == PickupMode::Wood || p_.pickup == PickupMode::Violin)
                body_.SetTone(t);
            vowel_.SetVowel(t);
        }
        // Bowed sympathetic strings, before the body
        {
            const float st = p_.model == Model::Bow ? 2.f * p_.stretch : 0.f;
            sym_.SetAmount(st < 0.f ? -st : st);
            if(st > 0.f)
                sym_.TuneOpen();
            else if(st < 0.f && newest_sym_ >= 0.f)
                sym_.TuneTo(440.f * Exp2Fast((newest_sym_ - 69.f) / 12.f));
            sym_.Process(l, r, n);
        }
        body_.Process(l, r, n);
        vowel_.Process(l, r, n);
        trem_.SetDepth(p_.model == Model::Bow || p_.model == Model::Vocal ? 0.f : trem_depth_); // Bowed and Vocal: vibrato instead
        trem_.Process(l, r, n);
        if(p_.model == Model::Vocal && p_.tape > 0.f)
            TapeColour(l, r, n);
        t[3] = Clock();

        // chorus at full level, volume after it; the level glides (~5 ms)
        chorus_.Process(l, r, l, r, n);
        t[4] = Clock();
        if(space_ready_)
            space_.Process(l, r, n);
        t[5] = Clock();
        output_.Process(l, r, n);
        Click(l, r, n);
        for(size_t i = 0; i < n; i++)
        {
            level_ += (volume_ - level_) * 0.004f;
            l[i] = Limit(l[i] * level_);
            r[i] = Limit(r[i] * level_);
        }
        if(clock_)
        {
            t[6] = Clock();
            Account(t);
        }
    }

    uint32_t Clock() const { return clock_ ? clock_() : 0; }

    // Vocal tape
    static constexpr float kWow     = 0.0028f; // wow depth at full, share of speed (~5 cents)
    static constexpr float kFlutter = 0.0009f; // flutter depth (~1.5 cents)
    static constexpr float kWowHz = 0.55f, kFlutterHz = 7.3f;

    /** Tape speed this block: wow with a slow random wander, plus flutter. */
    void TapeSpeed(size_t n)
    {
        if(p_.tape <= 0.f || p_.model != Model::Vocal)
        {
            p_.tape_speed = 1.f;
            return;
        }
        const float dt = static_cast<float>(n) / sr_;
        wow_ph_ += kWowHz * dt, wow_ph_ -= wow_ph_ >= 1.f ? 1.f : 0.f;
        flut_ph_ += kFlutterHz * dt, flut_ph_ -= flut_ph_ >= 1.f ? 1.f : 0.f;
        if((tape_left_ -= static_cast<int>(n)) <= 0) // a new aim every ~0.4 s
        {
            tape_rng_ ^= tape_rng_ << 13, tape_rng_ ^= tape_rng_ >> 17, tape_rng_ ^= tape_rng_ << 5;
            tape_aim_  = static_cast<float>(static_cast<int32_t>(tape_rng_)) * 4.6566129e-10f;
            tape_left_ = static_cast<int>(0.4f * sr_);
        }
        tape_wan_ += (tape_aim_ - tape_wan_) * 0.01f;
        p_.tape_speed = 1.f + p_.tape * (kWow * (0.7f * SinCycles(wow_ph_) + 0.5f * tape_wan_) + kFlutter * SinCycles(flut_ph_));
    }

    /** Tape colour on the mix: a ~70 Hz high-pass and a 16 kHz to 6.5 kHz
     *  low-pass. */
    void TapeColour(float* l, float* r, size_t n)
    {
        const float hp = 1.f - Exp2Fast(-1.4426950f * 6.2831853f * 70.f / sr_);
        const float lp = 1.f - Exp2Fast(-1.4426950f * 6.2831853f * 16000.f * Exp2Fast(-1.3f * p_.tape) / sr_);
        for(size_t i = 0; i < n; i++)
        {
            tape_hl_ += (l[i] - tape_hl_) * hp, tape_hr_ += (r[i] - tape_hr_) * hp;
            tape_ll_ += (l[i] - tape_hl_ - tape_ll_) * lp, tape_lr_ += (r[i] - tape_hr_ - tape_lr_) * lp;
            l[i] = tape_ll_, r[i] = tape_lr_;
        }
    }
    /** A block's part times into the profile. */
    PHYMO_ONCE void Account(const uint32_t* t)
    {
        for(int p = 0; p < Profile::kParts; p++)
        {
            const uint32_t d = t[p + 1] - t[p];
            prof_.sum[p] += d;
            prof_.worst[p] = d > prof_.worst[p] ? d : prof_.worst[p];
        }
        const uint32_t dt = space_.DelayTicks(), rt = space_.ReverbTicks();
        prof_.delay_sum += dt, prof_.reverb_sum += rt;
        prof_.delay_worst  = dt > prof_.delay_worst ? dt : prof_.delay_worst;
        prof_.reverb_worst = rt > prof_.reverb_worst ? rt : prof_.reverb_worst;
        const int v = ActiveVoices();
        prof_.voices_sum += static_cast<uint32_t>(v);
        prof_.blocks++;
        if(t[Profile::kParts] - t[0] > prof_.worst_block)
        {
            prof_.worst_block  = t[Profile::kParts] - t[0];
            prof_.worst_voices = static_cast<uint8_t>(v);
            prof_.worst_model  = static_cast<uint8_t>(p_.model);
        }
    }

    /** Metronome click: 1.5 kHz on the bar's first beat, 1 kHz on the rest, 30 ms. */
    void StartClick(bool downbeat)
    {
        click_left_  = static_cast<int>(.03f * sr_);
        click_phase_ = 0.f;
        click_inc_   = (downbeat ? 1500.f : 1000.f) / sr_;
        click_env_   = kClickLevel * (downbeat ? 1.4f : 1.f);
        click_decay_ = expf(-1.f / (.006f * sr_)); // 6 ms
    }
    void Click(float* l, float* r, size_t n)
    {
        for(size_t i = 0; i < n && click_left_ > 0; i++)
        {
            const float c = click_env_ * sinf(click_phase_ * 6.2831853f);
            l[i] += c, r[i] += c;
            click_phase_ += click_inc_;
            click_phase_ -= click_phase_ >= 1.f ? 1.f : 0.f;
            click_env_ *= click_decay_;
            click_left_--;
        }
    }
    static constexpr float kClickLevel = .09f;

    /** Soft limiter: untouched below kKnee, then bending smoothly towards full scale. */
    static float Limit(float x)
    {
        const float a = fabsf(x);
        if(a <= kKnee)
            return x;
        const float u = (a - kKnee) / (1.f - kKnee);
        const float y = kKnee + (1.f - kKnee) * u / (1.f + u);
        return x < 0.f ? -y : y;
    }

    bool Glide() const { return p_.glide > 0.f; }

    /** A panel value gliding to its target through two one-poles. */
    struct Smooth
    {
        float t = 0.f, a = 0.f, b = 0.f; // target, and the two stages (b: the value)
        void  Set(float x) { t = x; }
        void  Jump(float x) { t = a = b = x; }
    /** A block on (`k` the coefficient); false once arrived. */
        bool Step(float k)
        {
            if(a == t && b == t)
                return false;
            a += (t - a) * k;
            b += (a - b) * k;
            if(fabsf(t - a) < 1e-5f && fabsf(t - b) < 1e-5f)
                a = b = t;
            return true;
        }
    };

    /** The wheel's values, a block nearer their targets. */
    void GlideControls(size_t n)
    {
        const float k = 1.f - expf(-static_cast<float>(n) / (kSmooth * sr_));
        if(bright_s_.Step(k))
            p_.bright = bright_s_.b;
        if(pos_s_.Step(k))
            p_.strike.position = pos_s_.b;
        for(int d = 0; d < kDests; d++)
            lfo_d_[d].Step(k), env_d_[d].Step(k);
        if(bend_s_.Step(k))
            p_.bend = bend_s_.b;
        if(mod_s_.Step(k))
            mod_ = mod_s_.b;
        // MODEL knobs (kShapeSmooth)
        const float ks = 1.f - expf(-static_cast<float>(n) / (kShapeSmooth * sr_));
        if(table_s_.Step(ks))
            p_.table = table_s_.b;
        if(stretch_s_.Step(ks))
            p_.stretch = stretch_s_.b;
        if(snap_s_.Step(ks))
            p_.snap = snap_s_.b;
        if(partials_s_.Step(ks))
            p_.partials = partials_s_.b;
    }

    /** A MODEL knob's value: gliding while anything sounds, immediate otherwise. */
    void Shape(Smooth& s, float& value, float x)
    {
        if(ActiveVoices() > 0)
            s.Set(x);
        else
            s.Jump(x), value = x;
    }

    /** While the sequence runs on the tempo, the arp steps on its grid
     *  (every arp_tps_ clock ticks), never drifting. */
    void LockArp()
    {
        const bool lock = seq_.Running() && !external_ && arp_tps_ > 0; // free rate: its own
        if(lock != arp_locked_)
        {
            arp_locked_ = lock;
            lock_tick_  = -1;
            arp_.SetExternal(lock || external_);
        }
        if(!lock)
            return;
        const long now = static_cast<long>(seq_.Position() * (24 / SeqSteps(seq_div_)));
        if(now < lock_tick_ || now - lock_tick_ > 24)
            lock_tick_ = now - 1; // the step size changed: no catching up
        for(long t = lock_tick_ + 1; t <= now; t++)
            if(t % arp_tps_ == 0)
                arp_.Step();
        lock_tick_ = now;
    }

    /** Playing chords (not choosing one). */
    bool ChordMode() const { return mode_ == KeyMode::Chord && !chord_edit_; }
    /** Modes that play one key at a time across several voices. */
    bool StackMode() const { return ChordMode() || mode_ == KeyMode::Unison; }
    /** The keys go to the arp. */
    bool KeysToArp() const { return arp_on_ && !seq_mode_; }

    void LfoRate()
    {
        lfo_.SetRate(LfoHz());
        p_.lfo_hz = LfoHz();
    }

    /** A new note struck (not a legato glide): NOTE restarts the LFO. */
    void Retrigger()
    {
        if(lfo_trig_ == LfoTrigger::Note)
            lfo_.Restart();
    }

    static float Clamp(float v) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; }

    /** The tempo to the arp, sequencer and LFO sync. */
    void UpdateRates()
    {
        const float bpm = external_ ? ext_bpm_ : bpm_;
        arp_rate_       = arp_tps_ > 0 ? bpm / 60.f * 24.f / static_cast<float>(arp_tps_) : arp_free_rate_;
        arp_.SetRate(arp_rate_);
        seq_.SetRate(bpm / 60.f * static_cast<float>(SeqSteps(seq_div_)));
        LfoRate();
    }

    /** Per-voice pitch offsets each block: unison spread (± kUnisonCents)
     *  and a slow random drift (± kDriftCents). */
    void Detune(size_t n = 0)
    {
        static const float kShare[kVoices] = {-1.f, 1.f, -0.33f, 0.33f, 0.f, 0.f, 0.f, 0.f}; // unison's four, evenly fanned
        const float        d2              = detune_ * detune_;
        const float        k = 1.f - Exp2Fast(-1.4426950f * static_cast<float>(n) / (0.3f * kDriftTime * sr_));
        for(int i = 0; i < kVoices; i++)
        {
            if(n > 0 && (drift_left_[i] -= static_cast<int>(n)) <= 0)
            {
                drift_aim_[i]  = DriftRand();
                drift_left_[i] = static_cast<int>((0.5f + 0.5f * (DriftRand() + 1.f)) * kDriftTime * sr_);
            }
            drift_[i] += (drift_aim_[i] - drift_[i]) * k;
            const float spread = mode_ == KeyMode::Unison ? kUnisonCents * kShare[i] : 0.f;
            voices_[i].SetDetune(d2 * (spread + kDriftCents * drift_[i]) / 100.f);
        }
    }
    static constexpr float kUnisonCents = 50.f; // unison spread at full detune, ± cents
    static constexpr float kDriftCents  = 12.f; // drift at full, ± cents
    static constexpr float kDriftTime   = 2.f; // drift turn time, s

    /** -1 to 1, uniform. */
    float DriftRand()
    {
        drift_rng_ ^= drift_rng_ << 13;
        drift_rng_ ^= drift_rng_ >> 17;
        drift_rng_ ^= drift_rng_ << 5;
        return static_cast<float>(static_cast<int32_t>(drift_rng_)) * 4.6566129e-10f;
    }

    /** Releases every held key's voice (not the sequencer's). */
    void ReleaseAll()
    {
        for(auto& v : voices_)
            if(v.Gated() && v.Root() < kSeqRoot)
                v.NoteOff();
        uni_next_ = uni_n_;
        strum_n_  = 0, passes_ = 0, let_n_ = 0;
    }

    /** A sequencer note on its voices: a free one, else its oldest. */
    PHYMO_ONCE void SeqVoiceOn(int note)
    {
        int best = -1;
        for(int i = kVoices - kSeqVoices; i < kVoices; i++)
            if(!voices_[i].Gated() && (best < 0 || voices_[i].Age() < voices_[best].Age()))
                best = i;
        if(best < 0)
            for(int i = kVoices - kSeqVoices; i < kVoices; i++)
                if(best < 0 || voices_[i].Age() < voices_[best].Age())
                    best = i;
        Voice& v = voices_[best];
        Retrigger();
        v.NoteOn(note, ++count_, seq_glide_ > 0.f && v.Note() >= 0 ? v.Pitch() : -1.f, kSeqRoot + note);
        v.SetGlideOwn(seq_glide_);
    }
    PHYMO_ONCE void SeqVoiceOff(int note)
    {
        for(auto& v : voices_)
            if(v.Gated() && v.Root() == kSeqRoot + note)
                v.NoteOff();
    }

    /** A sequencer note starting (or ending) `late` samples from now; a late
     *  start ends as late, keeping its length. */
    PHYMO_ONCE void SeqLater(int note, bool on, int late)
    {
        if(on)
        {
            seq_late_[note] = late;
            for(int i = 0; i < seq_n_; i++) // its pending end: now
                if(seq_q_[i].note == note && !seq_q_[i].on)
                {
                    seq_q_[i--] = seq_q_[--seq_n_];
                    SeqVoiceOff(note), Push(OutEvent::NoteOff, note);
                }
        }
        else
            seq_late_[note] = 0;
        if(late <= 0 || seq_n_ >= kSeqLater)
        {
            if(on)
                SeqVoiceOn(note), Push(OutEvent::NoteOn, note);
            else
            {
                for(int i = 0; i < seq_n_; i++) // not yet started: drop
                    if(seq_q_[i].note == note && seq_q_[i].on)
                        seq_q_[i--] = seq_q_[--seq_n_];
                SeqVoiceOff(note), Push(OutEvent::NoteOff, note);
            }
            return;
        }
        seq_q_[seq_n_++] = {static_cast<int8_t>(note), on, samples_ + static_cast<uint32_t>(late)};
    }

    /** A strummed step's spacing, samples: free, or synced to the step. */
    float SeqSpacing(int gate, int n) const
    {
        if(!seq_sync_)
            return seq_strum_ * sr_;
        const float step = 60.f / (bpm_ * static_cast<float>(SeqSteps(seq_div_))) * sr_; // samples
        return seq_spread_ * step * static_cast<float>(gate) / 48.f / static_cast<float>(n > 0 ? n : 1);
    }
    /** A synced strum's note `i` of `n`: samples after the first, on the
     *  grid and swung. */
    PHYMO_ONCE int SeqOnGrid(const Sequencer::Event& e, int i, int n) const
    {
        if(i <= 0)
            return 0;
        const float span = e.gate >= 48 ? std::floor(e.gate / 48.f + 0.5f) : e.gate / 48.f; // steps
        const float p    = seq_spread_ * span * static_cast<float>(i) / static_cast<float>(n);
        const float whole = std::floor(p + 0.5f);
        float       at    = p;
        if(std::fabs(p - whole) < 1e-3f && ((static_cast<int>(whole) & 1) != e.odd)) // an off step: swung
            at += 2.f * seq_.GetSwing() - 1.f;
        const float step = 60.f / (bpm_ * static_cast<float>(SeqSteps(seq_div_))) * sr_; // samples
        const float late = (at - e.lead) * step;
        return late > 0.f ? static_cast<int>(late + 0.5f) : 0;
    }
    /** A strummed note `late` samples on: free, ending as late; synced, at
     *  the step's end. */
    void SeqStrummed(int note, int late)
    {
        SeqLater(note, true, late);
        if(seq_sync_)
            seq_late_[note] = 0;
    }

    /** Sequencer notes due now. */
    PHYMO_ONCE void SeqDue()
    {
        for(int i = 0; i < seq_n_; i++)
            if(static_cast<int32_t>(samples_ - seq_q_[i].at) >= 0)
            {
                const auto q = seq_q_[i];
                seq_q_[i--]  = seq_q_[--seq_n_];
                if(q.on)
                    SeqVoiceOn(q.note), Push(OutEvent::NoteOn, q.note);
                else
                    SeqVoiceOff(q.note), Push(OutEvent::NoteOff, q.note);
            }
    }

    /** An audition's time, `n` samples on; -1 ends it now. */
    PHYMO_ONCE void Audition(int n)
    {
        if(aud_n_ == 0)
            return;
        aud_left_ = n < 0 ? 0 : aud_left_ - n;
        if(aud_left_ > 0)
            return;
        for(int i = 0; i < aud_n_; i++)
        {
            for(int j = 0; j < seq_n_; j++) // not started yet: drop
                if(seq_q_[j].note == aud_note_[i])
                    seq_q_[j--] = seq_q_[--seq_n_];
            seq_late_[aud_note_[i]] = 0;
            SeqVoiceOff(aud_note_[i]);
        }
        aud_n_ = 0;
    }

    /** Each voice's pan: kPan times the spread (centre in mono). */
    void Pan()
    {
        // alternating sides, outermost first
        static const float kPan[kVoices] = {-1.f, 1.f, -0.71f, 0.71f, -0.43f, 0.43f, -0.14f, 0.14f};
        for(int i = 0; i < kVoices; i++)
            voices_[i].SetPan(mode_ == KeyMode::Mono ? 0.f : spread_ * kPan[i]);
    }

    /** Whether a new note glides: portamento on, a note before, and in
     *  legato mode another key held or the arp playing. */
    bool GlidesNow() const
    {
        if(porta_ == PortaMode::Off || last_note_ < 0)
            return false;
        return (porta_ == PortaMode::Always && mode_ != KeyMode::Mono) || held_ > 1 || (arp_on_ && arp_run_);
    }

    /** Starts key `note` on voices per the key mode, gliding from the last
     *  note played. */
    PHYMO_ONCE void VoiceOn(int note, bool strum = true)
    {
        for(int i = 0; i < let_n_; i++) // pressed again during its strum: held again
            if(let_[i] == note)
                let_[i--] = let_[--let_n_];
        const bool glide = GlidesNow();
        Retrigger();
        if(mode_ == KeyMode::Mono)
            voices_[0].NoteOn(note, ++count_, glide ? voices_[0].Pitch() : -1.f, note, vel_); // every note retriggers
        else if(mode_ == KeyMode::Chord && !chord_edit_)
            for(int i = 0; i < chord_n_; i++)
                Play(note + chord_[i], note, glide ? static_cast<float>(last_note_ + chord_[i]) : -1.f, strum);
        else if(mode_ == KeyMode::Unison) // unison: the first now, the rest a block apart
        {
            uni_note_ = note, uni_from_ = glide ? static_cast<float>(last_note_) : -1.f, uni_vel_ = vel_;
            uni_n_ = Unison(), uni_next_ = 1;
            voices_[0].NoteOn(note, ++count_, uni_from_, note, vel_);
        }
        else
            Play(note, note, glide ? static_cast<float>(last_note_) : -1.f, strum);
        if(strum_n_ > 0 && strum_wait_ <= 0 && !strumming_) // a strum begun: its first waits for the rest
        {
            const float sp = kStrumTime * strum_ * strum_;
            strum_wait_    = static_cast<int>((sp < kStrumGather ? sp : kStrumGather) * sr_);
            strumming_     = true;
        }
        Strum(0); // at once, if it needn't wait
        last_note_ = note;
    }

    /** A note to start: now, or into the strum queue. */
    PHYMO_ONCE void Play(int note, int root, float from, bool strum)
    {
        if(!strum || strum_ <= 0.f)
        {
            StartVoice(note, root, from);
            return;
        }
        if(strum_n_ == 0 && !strumming_) // a new strum: its direction from the arp pattern
        {
            const Arp::Pattern p = arp_.GetPattern();
            strum_flip_          = !strum_flip_;
            strum_dir_ = p == Arp::Pattern::UpDown ? (strum_flip_ ? Arp::Pattern::Up : Arp::Pattern::Down) : p;
        }
        Queue(note, root, vel_, 0, Key(note, true));
        if(strumming_ && strum_count_ > 0 && pass_ == 1 && gl_n_ < kStrumMax) // late, before the passes: add to its chord
        {
            int j = gl_n_++;
            for(; j > 0 && gl_[j - 1].note > note; j--)
                gl_[j] = gl_[j - 1];
            gl_[j]  = {note, root, vel_, 0, 0};
            passes_ = gl_n_ * Gliss();
        }
    }

    /** A note's position in the strum order. */
    int Key(int note, bool played)
    {
        switch(strum_dir_)
        {
            case Arp::Pattern::Down: return -note;
            case Arp::Pattern::Random: return static_cast<int>(StrumRand() >> 8);
            case Arp::Pattern::Played: return played ? ++strum_seq_ : note;
            default: return note;
        }
    }
    uint32_t StrumRand()
    {
        strum_rng_ ^= strum_rng_ << 13;
        strum_rng_ ^= strum_rng_ >> 17;
        strum_rng_ ^= strum_rng_ << 5;
        return strum_rng_;
    }

    /** Into the strum queue, by pass then key; dropped if full or off the keyboard. */
    PHYMO_ONCE void Queue(int note, int root, float vel, int pass, int key)
    {
        if(strum_n_ >= kStrumMax || note > kGlissTop || note < 0)
            return;
        int i = strum_n_++;
        for(; i > 0 && (strum_q_[i - 1].pass > pass || (strum_q_[i - 1].pass == pass && strum_q_[i - 1].key > key)); i--)
            strum_q_[i] = strum_q_[i - 1];
        strum_q_[i] = {note, root, vel, pass, key};
    }

    /** The glissando's chord from the waiting notes, and its passes (each
     *  inversion for each extra octave). */
    PHYMO_ONCE void Chord()
    {
        gl_n_ = 0;
        for(int i = 0; i < strum_n_; i++)
            if(strum_q_[i].pass == 0)
            {
                int j = gl_n_++;
                for(; j > 0 && gl_[j - 1].note > strum_q_[i].note; j--)
                    gl_[j] = gl_[j - 1];
                gl_[j] = strum_q_[i];
            }
        passes_ = gl_n_ * Gliss(), pass_ = 1;
    }

    /** Glissando pass `p`: the chord's p-th inversion, kGlissFade quieter
     *  per octave. */
    PHYMO_ONCE void Pass(int p)
    {
        const int   n    = gl_n_, sign = strum_dir_ == Arp::Pattern::Down ? -1 : 1;
        const float fade = Exp2Fast(-0.5145732f * static_cast<float>(p) / static_cast<float>(n)); // kGlissFade ^ octaves
        int         in   = 0;
        for(int i = 0; i < n; i++)
        {
            const int m   = i + sign * p;
            const int idx = (m % n + n) % n;
            const int x   = gl_[idx].note + 12 * ((m - idx) / n);
            if(x < 0 || x > kGlissTop)
                continue;
            Queue(x, gl_[idx].root, gl_[idx].vel * fade, p, Key(x, false));
            in++;
        }
        if(in == 0)
            passes_ = 0;
    }

    /** A strummed step's notes, ordered by the arp pattern. */
    void Order(int* notes, int n)
    {
        Arp::Pattern p = arp_.GetPattern();
        if(p == Arp::Pattern::UpDown)
            seq_flip_ = !seq_flip_, p = seq_flip_ ? Arp::Pattern::Up : Arp::Pattern::Down;
        if(p == Arp::Pattern::Played)
            return;
        if(p == Arp::Pattern::Random)
        {
            for(int i = n - 1; i > 0; i--)
            {
                const int j = static_cast<int>(StrumRand() % static_cast<uint32_t>(i + 1)), t = notes[i];
                notes[i] = notes[j], notes[j] = t;
            }
            return;
        }
        for(int i = 1; i < n; i++)
            for(int j = i; j > 0 && (p == Arp::Pattern::Down ? notes[j - 1] < notes[j] : notes[j - 1] > notes[j]); j--)
            {
                const int t = notes[j];
                notes[j] = notes[j - 1], notes[j - 1] = t;
            }
    }

    /** The strum queue, `n` samples on: the next note once its spacing is up. */
    PHYMO_ONCE void Strum(int n)
    {
        strum_wait_ = strum_wait_ > n ? strum_wait_ - n : 0;
        if(strum_n_ == 0 && strum_wait_ == 0 && pass_ <= passes_ && gl_n_ > 0) // the glissando's next pass
            Pass(pass_++);
        if(strum_n_ == 0 && strum_wait_ == 0)
        {
            strumming_ = false, strum_count_ = 0, passes_ = 0; // done
            LetGo();
        }
        if(strum_n_ == 0 || strum_wait_ > 0)
            return;
        if(strum_count_ == 0) // its chord complete: start the glissando
            Chord();
        const Pending q = strum_q_[0];
        for(int i = 1; i < strum_n_; i++)
            strum_q_[i - 1] = strum_q_[i];
        strum_n_--;
        // with portamento, glide from the note before
        const int   prev = strum_count_ == 0 ? strum_head_ : strum_prev_;
        const float from = porta_ != PortaMode::Off && prev >= 0 ? static_cast<float>(prev) : -1.f;
        StartVoice(q.note, q.root, from, q.vel);
        Retrigger();
        if(strum_count_++ == 0)
            strum_head_ = q.note;
        strum_prev_ = q.note;
        strum_wait_ = static_cast<int>(kStrumTime * strum_ * strum_ * sr_);
    }

    /** Unison voices: kUnisonVoices, fewer under the CPU budget. */
    int Unison() const
    {
        int u = kUnisonVoices < live_ ? kUnisonVoices : live_;
        return u < budget_ ? u : budget_;
    }

    /** One note on a voice for key `root`, gliding from `from` (below 0: none). */
    void StartVoice(int note, int root, float from) { StartVoice(note, root, from, vel_); }
    PHYMO_ONCE void StartVoice(int note, int root, float from, float vel)
    {
        // already sounding for this key: retrigger that voice
        for(auto& v : voices_)
            if(v.Gated() && v.Note() == note && v.Root() == root)
            {
                v.NoteOn(note, ++count_, from, root, vel);
                return;
            }
        voices_[Allocate(note)].NoteOn(note, ++count_, from, root, vel);
    }

    /** Ends everything key `note` started. */
    PHYMO_ONCE void VoiceOff(int note)
    {
        if(Sweeping(note)) // a strum plays out; its damper once done
        {
            if(let_n_ < kMaxHeld)
                let_[let_n_++] = note;
            return;
        }
        for(auto& v : voices_)
            if(v.Gated() && v.Root() == note)
                v.NoteOff();
        if(note == uni_note_)
            uni_next_ = uni_n_;
        int k = 0; // drop its notes still to be strummed
        for(int i = 0; i < strum_n_; i++)
            if(strum_q_[i].root != note)
                strum_q_[k++] = strum_q_[i];
        strum_n_ = k, k = 0;
        for(int i = 0; i < gl_n_; i++)
            if(gl_[i].root != note)
                gl_[k++] = gl_[i];
        if(k != gl_n_)
            gl_n_ = k, passes_ = k > 0 ? passes_ : 0; // fewer notes: inversions from here
    }

    /** Whether key `root`'s strum is still to play. */
    PHYMO_ONCE bool Sweeping(int root) const
    {
        if(!strumming_)
            return false;
        for(int i = 0; i < strum_n_; i++)
            if(strum_q_[i].root == root)
                return true;
        if(pass_ <= passes_)
            for(int i = 0; i < gl_n_; i++)
                if(gl_[i].root == root)
                    return true;
        return false;
    }

    /** Strum done: release the keys let go during it. */
    PHYMO_ONCE void LetGo()
    {
        for(int i = 0; i < let_n_; i++)
            for(auto& v : voices_)
                if(v.Gated() && v.Root() == let_[i])
                    v.NoteOff();
        let_n_ = 0;
    }

    /** Ends the arp's note, if any. */
    void StopArpNote()
    {
        if(arp_.Sounding() >= 0)
            VoiceOff(arp_.Sounding()), Push(OutEvent::NoteOff, arp_.Sounding());
        arp_.Silence();
    }

    /** Queues a MIDI out event (dropped if full). */
    void Push(OutEvent::Type t, int note)
    {
        const int next = (ev_head_ + 1) % kEvents;
        if(next == ev_tail_)
            return;
        events_[ev_head_] = {t, static_cast<uint8_t>(note < 0 ? 0 : note > 127 ? 127 : note)};
        ev_head_          = next;
    }

    static constexpr int kEvents = 64;
    // --- CPU room ---
    // Learnt from the cycle counter: a sounding voice's cost per block (cv_),
    // a strike's (cs_, its worst, slowly forgotten) and the fixed cost (cf_).
    // From them: how many voices fit under kLoadHigh (Fit), choking a
    // chord's surplus at once (MakeRoom), and how many strikes fit per block
    // (StrikeRoom).
    static constexpr float kStrikeRoom = 0.85f, kStrikeHard = 0.92f; // strike headroom: normally, and after waiting
    static constexpr int   kStrikeWait = 60; // max blocks a strike waits (30 ms)



    /** Voices that fit (all, until learnt). */
    int Fit() const
    {
        if(cv_ <= 0.f)
            return kVoices;
        const int n = static_cast<int>((kLoadHigh - cf_) / cv_);
        return n < kMinVoices ? kMinVoices : n > kVoices ? kVoices : n;
    }

    /** Strikes from quiet that fit this block: under kStrikeRoom, or after a
     *  short wait, under kStrikeHard. */
    int StrikeRoom() const
    {
        if(per_block_ <= 0.f || cv_ <= 0.f)
            return kVoices;
        int sounding = 0;
        for(const auto& v : voices_)
            sounding += v.Active() && !v.Waiting();
        const float now = cf_ + cv_ * static_cast<float>(sounding);
        const float cs  = cs_ > 2.f * cv_ ? cs_ : 2.f * cv_; // a strike costs at least two sounding voices
        const int   n   = static_cast<int>((kStrikeRoom - now) / cs);
        if(n <= 0)
            return waited_ >= 2 && now + cs <= kStrikeHard ? 1 : 0;
        return n;
    }

    /** Chokes the oldest voices (released first) to make room for waiting strikes. */
    PHYMO_ONCE void MakeRoom()
    {
        int waiting = 0, sounding = 0, going = 0;
        for(const auto& v : voices_)
        {
            waiting += v.Waiting();
            going += v.Choking();
            sounding += v.Active() && !v.Choking() && !v.Waiting();
        }
        int surplus = sounding + waiting - Fit();
        while(surplus-- > 0)
        {
            Voice* out = nullptr;
            for(int pass = 0; pass < 2 && !out; pass++)
                for(auto& v : voices_)
                    if(v.Active() && !v.Choking() && !v.Waiting() && (pass == 1 || !v.Gated()) && (!out || v.Age() < out->Age()))
                        out = &v;
            if(!out)
                break;
            out->Choke();
        }
    }

    /** A block's voice cycles into the learnt costs. */
    void Learn(uint32_t steady, int n, uint32_t strike)
    {
        if(per_block_ <= 0.f || !clock_)
            return;
        if(n > 0)
            cv_ += (static_cast<float>(steady) / (per_block_ * static_cast<float>(n)) - cv_) * 0.02f;
        const float s = static_cast<float>(strike) / per_block_;
        cs_           = s > cs_ ? s : cs_ * 0.99998f; // its worst, half forgotten in ~17 s
                                                       //  in under one, a quiet moment and the next chord let in too much: 103%)
    }

    /** Voices sounding past the budget choked, the oldest let-go first. */
    void Budget()
    {
        int sounding = 0;
        for(const auto& v : voices_)
            sounding += v.Active() && !v.Choking();
        while(sounding > budget_)
        {
            Voice* out = nullptr;
            for(int pass = 0; pass < 2 && !out; pass++)
                for(auto& v : voices_)
                    if(v.Active() && !v.Choking() && (pass == 1 || !v.Gated()) && (!out || v.Age() < out->Age()))
                        out = &v;
            if(!out)
                break;
            out->Choke(), sounding--;
        }
    }

    /** Which voice a new note gets (Poly modes). */
    int Allocate(int note)
    {
        if(mode_ == KeyMode::Poly2)
        {
            // the voice that last played this note, if it's not held
            for(int i = 0; i < live_; i++)
                if(!voices_[i].Gated() && voices_[i].Note() == note)
                    return i;
            // else the lowest silent one
            for(int i = 0; i < live_; i++)
                if(!voices_[i].Active())
                    return i;
        }
        else
        {
            // Poly 1: the next silent voice, going round from the last used
            for(int k = 0; k < live_; k++)
            {
                const int i = (next_ + k) % live_;
                if(!voices_[i].Active())
                {
                    next_ = (i + 1) % live_;
                    return i;
                }
            }
        }
        // None silent: the oldest released voice, else the oldest held one.
        int best = -1;
        for(int pass = 0; pass < 2 && best < 0; pass++)
            for(int i = 0; i < live_; i++)
            {
                const bool candidate = pass == 0 ? !voices_[i].Gated() : true;
                if(candidate && (best < 0 || voices_[i].Age() < voices_[best].Age()))
                    best = i;
            }
        next_ = (best + 1) % live_;
        return best;
    }

    void Forget(int note)
    {
        int j = 0;
        for(int i = 0; i < held_; i++)
            if(stack_[i] != note)
                stack_[j++] = stack_[i];
        held_ = j;
    }

    uint32_t (*clock_)() = nullptr; // the profile's cycle counter (SetProfileClock)
    Profile     prof_;
    Voice       voices_[kVoices];
    float*      str_mem_[2 * kVoices] = {}; // the strings' delay lines (SetStringMemory)
#ifndef PHYMO_STRINGS_OUTSIDE
    float own_str_[2 * kVoices][StringRes::kLen]; // (on the desktop: the synth's own)
#endif
    Chorus      chorus_;
    Body        body_;
    Sympathetic sym_;             // Bowed's sympathetic strings (Stretch)
    float       newest_sym_ = -1.f; // the newest note (MIDI), for the strings tuned to it
    Vowel       vowel_;
    Tremolo     trem_;
    Space       space_;
    OutputStage output_;
    bool        space_ready_;     // InitSpace has given it memory
    float       sr_;
    Lfo         lfo_;
    Arp         arp_;
    bool        arp_on_, arp_run_;
    int         chord_[kVoices];  // chord mode's intervals above its lowest (0 first)
    int         chord_n_;
    bool        chord_edit_;      // a chord being chosen: chord mode plays single notes
    PortaMode   porta_;
    int         last_note_;       // the last key started (chord mode: its root), for portamento
    float       glide_time_;
    VoiceParams p_;
    KeyMode     mode_;
    int         stack_[kMaxHeld]; // held keys, oldest first
    int         held_;
    int         next_;            // Poly 1: where to start looking for a voice
    uint32_t    count_;           // notes started, for voices' ages
    int         uni_note_ = -1, uni_next_ = 0, uni_n_ = 0; // unison's note, its next voice to strike, and how many
    struct Pending
    {
        int   note, root;
        float vel;
        int   pass; // the glissando's: 0 the notes played
        int   key;  // its place in its pass's order (Key)
    };
    Pending     strum_q_[kStrumMax]; // notes waiting to be strummed, lowest first
    int         strum_n_ = 0, strum_wait_ = 0; // how many, and samples till the next may start
    bool        strumming_ = false; // a strum under way (its first's gathered the rest)
    int         strum_count_ = 0;   // notes it's started so far
    Pending     gl_[kStrumMax];     // the glissando's chord, by pitch (Chord)
    int         gl_n_ = 0, pass_ = 0, passes_ = 0; // its notes, its next pass, and how many
    int         let_[kMaxHeld], let_n_ = 0; // keys let go while their strum plays out (LetGo)
    int         strum_head_ = -1, strum_prev_ = -1; // the last strum's first note, and the note just started (portamento's from)
    float       strum_ = 0.f;     // the strum, 0 to 1 (SetStrum)
    Arp::Pattern strum_dir_ = Arp::Pattern::Up; // this strum's way (Play)
    bool        strum_flip_ = false, seq_flip_ = false; // up & down: the last strum's (and step's) way
    int         strum_seq_ = 0;   // as played: the keys' count
    uint32_t    strum_rng_ = 0x13579bdfu;
    bool        cycle_ = false;   // held notes strike again (SetCycle)
    float       uni_from_ = -1.f, uni_vel_ = 1.f;           // ... their glide's start, and velocity
    float       volume_;
    float       level_;           // volume_, gliding
    Smooth      lfo_d_[kDests], env_d_[kDests]; // the LFO's and envelope's depths, -1 to 1, gliding
    LfoTrigger  lfo_trig_;
    float       trem_depth_;      // the tremolo's, from the panel (none with the Bowed model)
    float       mod_;             // the wheel's mod, -1 to 1
    Smooth      bright_s_, pos_s_, bend_s_, mod_s_; // gliding to where they're set
    Smooth      table_s_, stretch_s_, snap_s_, partials_s_;        // the MODEL knobs', the same
    float       spread_;          // 0 to 1
    float       detune_;          // 0 to 1
    float       drift_[kVoices] = {}, drift_aim_[kVoices] = {}; // each voice's drift, -1 to 1, and where it's going
    int         drift_left_[kVoices] = {};                      // samples until its next aim
    uint32_t    drift_rng_ = 0x9e3779b9u;
    float       lfo_rate_, lfo_sync_, arp_rate_; // the LFO's free rate, its sync (0 free), the arp's
    float       bpm_, ext_bpm_;   // the master tempo, and a MIDI clock's
    int         arp_tps_;         // the arp's step in clock ticks (0: arp_free_rate_)
    int         other_stack_[kMaxHeld]; // the other mode's keys (sequencer mode: the arp's)
    int         other_held_;
    bool        seq_mode_;
    bool        arp_locked_ = false; // the arp stepping on the sequence's grid (LockArp)
    long        lock_tick_  = -1;    // the last clock tick (24 a beat) it was given
    float       arp_free_rate_ = 8.f;
    Sequencer   seq_;
    float       vel_;             // the last note's velocity (the arp's notes take it too)
    bool        loop_;            // the latch loop (SetLatchLoop)
    int         live_;            // voices for live playing: the first 4 while the sequencer runs
    int         budget_;          // voices allowed to sound at once (the CPU's: ReportLoad)
    float       load_ = 0.f;      // the last block's load, 0 to 1 (ReportLoad; 0: none told)
    int         waited_ = 0;      // blocks a strike's waited for room (MakeRoom)
    bool        struck_ = false;  // a strike in the last Process (ReportLoad's)
    float       wow_ph_ = 0.f, flut_ph_ = 0.f, tape_wan_ = 0.f, tape_aim_ = 0.f; // the tape's motor
    int         tape_left_ = 0;
    uint32_t    tape_rng_  = 0x9e3779b9u;
    float       tape_hl_ = 0.f, tape_hr_ = 0.f, tape_ll_ = 0.f, tape_lr_ = 0.f; // its colour's filters
    float       per_block_ = 0.f; // a block's cycles (SetBlockTicks; 0: none, the desktop)
    float       cv_ = 0.f, cs_ = 0.f, cf_ = 0.f; // learnt: a voice's share of a block, a strike's (dearest), what's fixed
    int         calm_, hot_;      // blocks the load's been low, and high, in a row
    float       seq_level_, seq_glide_;
    static constexpr int   kSeqLater    = 16;   // sequencer notes waiting (a strummed step's), at most
    static constexpr float kAuditionMax = 1.f;  // an auditioned step heard this long at most, s
    struct SeqQueued
    {
        int8_t   note;
        bool     on;
        uint32_t at; // samples_ it's due
    };
    SeqQueued   seq_q_[kSeqLater];
    int         seq_n_ = 0;
    bool        seq_was_ = false;    // running at the last block
    int         seq_late_[128] = {}; // each note's delay as it started (its end as late)
    float       seq_strum_ = 0.03f;  // the STRUM track's spacing, free, s
    bool        seq_sync_  = false;  // ... or synced: spread over the step's length (the panel's default)
    float       seq_spread_ = 1.f;   // its share of it
    int         aud_note_[SeqStep::kNotes] = {}, aud_n_ = 0, aud_left_ = 0; // an audition's notes, and samples left
    float       live_level_;      // the live voices' (the mix with the sequence's)
    int         seq_div_;         // the sequencer's step (SeqSteps)
    bool        metro_;           // the metronome on
    long        beat_;            // the beat it last clicked on (-1 stopped)
    int         click_left_ = 0;
    float       click_phase_ = 0.f, click_inc_ = 0.f, click_env_ = 0.f, click_decay_ = 0.f;
    bool        rec_;
    OutEvent    events_[kEvents];
    int         ev_head_, ev_tail_;
    bool        external_;        // the arp on MIDI clock
    uint32_t    ticks_, samples_, last_tick_;
    float       clock_acc_;       // clock out: ticks owed
};

} // namespace phymo
