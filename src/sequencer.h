/** @file sequencer.h
 *  @brief The step sequencer: up to 32 steps of up to 4 notes, at 16ths of
 *  the tempo or a MIDI clock (Step()).
 *
 *  Each step has its notes, on/off, a recorded timing offset (up to half a
 *  step either way), a length (48ths of a step, up to the whole pattern;
 *  long notes hold without retriggering) and strum.
 *
 *  Directions: forward, reverse, ping-pong, random; offsets play forward only.
 *  Recording quantizes to 1, 2 or 4 steps, or keeps timing as played; the
 *  first note into a step on a pass replaces it, later ones join. Step entry
 *  (stopped) fills the selected step and moves on when all keys are up.
 *  Tick emits events for the synth. No allocation: runs in the audio interrupt.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace phymo
{

struct SeqStep
{
    static constexpr int kNotes = 4; // four notes: a strum, and four sequencer voices
    int8_t  note[kNotes] = {-1, -1, -1, -1};
    uint8_t count        = 0; // notes in the step
    bool    on           = false;
    int8_t  offset       = 0; // 48ths of a step, -24 to 23
    uint16_t gate        = 24; // 48ths of a step, 1 to 48 x 32 (default half a step)
    bool    strum        = false; // strummed (STRUM track)
};

struct SeqPattern
{
    static constexpr int kSteps = 32;
    SeqStep step[kSteps];
    uint8_t length = 16;

    void Clear()
    {
        for(auto& s : step)
            s = SeqStep();
        length = 16;
    }
};

class Sequencer
{
  public:
    enum class Direction { Forward, Reverse, PingPong, Random, Count };

    /** A note to start or end. */
    struct Event
    {
        int8_t note;
        bool   on;
        bool   strum = false; // note on: its step is strummed
        uint16_t gate = 0; // its length, 48ths of a step
        float    lead = 0.f; // lateness after its grid slot, steps (offset and swing)
        bool     odd  = false; // an off-beat slot (swing)
    };
    static constexpr int kMaxEvents = 16; // per block, at most

    void Init(float sr)
    {
        sr_ = sr;
        pattern.Clear();
        dir_       = Direction::Forward;
        running_   = false;
        external_  = false;
        step_due_  = false;
        rate_      = 8.f;
        selected_  = 0;
        entry_fresh_ = true;
        transpose_ = 0;
        quantize_  = 1;
        swing_     = 0.5f;
        def_gate_  = 24;
        rng_       = 0x2468ace1u;
        for(int i = 0; i < SeqPattern::kSteps; i++)
            written_[i] = false;
        Rewind();
    }

    SeqPattern pattern; // the pattern playing

    // --- transport ---

    void Start()
    {
        Rewind();
        running_ = true;
        if(external_)
        {
            // on a clock, the first tick plays the first step
            primed_    = true;
            phase_     = 0.9999;
            fired_cur_ = true;
        }
    }
    /** Stops; its notes end at the next Tick. */
    void Stop()
    {
        running_ = false;
        stop_due_ = true;
    }
    bool Running() const { return running_; }

    /** Steps a second (the tempo x 4 / 60). */
    void SetRate(float steps_per_s) { rate_ = steps_per_s; }
    /** On an external clock: a step only when Step() says. */
    void SetExternal(bool e)
    {
        external_ = e;
        step_due_ = false;
    }
    void Step() { step_due_ = true; }

    void      SetDirection(Direction d) { dir_ = d; }
    Direction GetDirection() const { return dir_; }
    /** Swing, 0.5 (none) to 0.75: every other step plays late. */
    void  SetSwing(float s) { swing_ = s < 0.5f ? 0.5f : s > 0.75f ? 0.75f : s; }
    float GetSwing() const { return swing_; }

    /** Semitones the whole pattern plays up (or down). */
    void SetTranspose(int t) { transpose_ = t; }
    int  GetTranspose() const { return transpose_; }

    /** Where it is: the step playing (-1 stopped), and how far through it. */
    int   Playhead() const { return running_ ? cur_ : -1; }
    float Phase() const { return phase_; }

    // --- editing ---

    void SetLength(int len) { pattern.length = static_cast<uint8_t>(len < 1 ? 1 : len > SeqPattern::kSteps ? SeqPattern::kSteps : len); }
    int  Length() const { return pattern.length; }
    void Select(int step) { selected_ = Wrap(step); }
    int  Selected() const { return selected_; }
    void ToggleStep(int step)
    {
        SeqStep& s = pattern.step[Wrap(step)];
        s.on       = !s.on && s.count > 0; // empty steps stay off
    }

    /** Step entry: a note into the selected step (the first replaces it). */
    void EntryNote(int note)
    {
        if(selected_ >= pattern.length) // past the end: the pattern grows
            pattern.length = static_cast<uint8_t>(selected_ + 1);
        SeqStep& s = pattern.step[selected_];
        if(entry_fresh_)
        {
            s           = Fresh(s);
            entry_fresh_ = false;
        }
        Add(s, note);
        s.on = true;
    }

    /** A new note's length, 48ths of a step (step entry and recording). */
    void SetDefaultGate(int gate) { def_gate_ = static_cast<uint16_t>(gate < 1 ? 1 : gate > kMaxGate ? kMaxGate : gate); }
    int  DefaultGate() const { return def_gate_; }
    /** A step's strum on or off. */
    void SetStrum(int step, bool on) { pattern.step[Wrap(step)].strum = on; }
    /** Step entry: all keys up, move to the next step (growing the pattern,
     *  or wrapping after 32). */
    void EntryDone()
    {
        if(entry_fresh_)
            return;
        entry_fresh_ = true;
        selected_    = (selected_ + 1) % SeqPattern::kSteps;
    }
    /** Step entry starts afresh at `step` (arming record, stopped). */
    void EntryFrom(int step)
    {
        selected_    = Wrap(step);
        entry_fresh_ = true;
    }
    /** Step entry: a rest, then the next step. */
    void EntryRest()
    {
        if(selected_ >= pattern.length)
            pattern.length = static_cast<uint8_t>(selected_ + 1);
        pattern.step[selected_] = SeqStep();
        entry_fresh_            = true;
        selected_               = (selected_ + 1) % SeqPattern::kSteps;
    }
    /** Position in steps since Start (the arp follows it). */
    double Position() const { return pos_; }
    /** A step emptied. */
    void ClearStep(int step) { pattern.step[Wrap(step)] = SeqStep(); }
    /** A step's length, 48ths of a step. */
    void SetGate(int step, int gate)
    {
        pattern.step[Wrap(step)].gate = static_cast<uint16_t>(gate < 1 ? 1 : gate > kMaxGate ? kMaxGate : gate);
    }
    static constexpr int kMaxGate = 48 * SeqPattern::kSteps;
    /** A pattern to switch to at this one's start (now if stopped). */
    void Queue(const SeqPattern& p)
    {
        if(!running_)
        {
            pattern = p;
            return;
        }
        pending_     = p;
        has_pending_ = true;
    }
    void CancelQueue() { has_pending_ = false; }
    /** After a switch: the previous pattern. */
    const SeqPattern& Previous() const { return pending_; }
    bool Queued() const { return has_pending_; }
    /** True once after Tick changed to the queued pattern. */
    bool TakeSwitched()
    {
        const bool s = switched_;
        switched_    = false;
        return s;
    }

    /** Recording: the grid, in steps (1, 2, 4), or 0 for as played. */
    void SetQuantize(int steps) { quantize_ = steps; }
    int  GetQuantize() const { return quantize_; }

    /** Recording, while running: a note starts now. */
    void RecordOn(int note)
    {
        if(!running_)
            return;
        int    k;
        int8_t offset = 0;
        Nearest(k, offset);
        SeqStep& s = pattern.step[k];
        if(!written_[k])
        {
            s           = Fresh(s);
            written_[k] = true;
        }
        Add(s, note);
        s.on     = true;
        s.offset = offset;
        rec_note_[RecSlot(note)] = {static_cast<int8_t>(note), static_cast<int8_t>(k), static_cast<float>(pos_)};
    }
    /** Recording: a note ends now. */
    void RecordOff(int note)
    {
        if(!running_)
            return;
        for(auto& r : rec_note_)
            if(r.note == note && r.step >= 0)
            {
                const double len = pos_ - r.at;
                // as played: its length; quantized: only notes held across
                // steps stay long (whole steps)
                const int g = quantize_ > 0 ? (len >= 1.0 ? static_cast<int>(len + 0.5) * 48 : -1)
                                            : static_cast<int>(len * 48.0 + 0.5);
                if(g > 0)
                    pattern.step[r.step].gate = static_cast<uint16_t>(g > kMaxGate ? kMaxGate : g);
                r.step = -1;
            }
    }

    // --- playing ---

    /** `n` samples on: notes to start and end into `ev`; returns how many. */
    int Tick(size_t n, Event* ev)
    {
        int count = 0;
        if(stop_due_ || !running_)
        {
            stop_due_ = false;
            for(auto& so : sounding_)
                if(so.note >= 0)
                    Emit(ev, count, so.note, false), so.note = -1;
            return count;
        }
        double target = phase_ + rate_ * static_cast<double>(n) / sr_;
        if(external_)
        {
            // external clock: wait at the step's end for Step()
            if(step_due_)
                target = target < 1.0 ? 1.0 : target, step_due_ = false;
            else if(target >= 1.0)
                target = 0.9999;
        }
        for(;;)
        {
            const double seg_end = target < 1.0 ? target : 1.0;
            const double abs_end = slots_ + seg_end;
            // notes ending
            for(auto& so : sounding_)
                if(so.note >= 0 && so.off_at <= abs_end)
                    Emit(ev, count, so.note, false), so.note = -1;
            // this step, late by its offset (forward only)
            if(!fired_cur_)
            {
                const SeqStep& st = pattern.step[cur_];
                const double   at = Late(slots_, Forward() ? st.offset / 48.0 : 0.0);
                if(at <= seg_end)
                {
                    Fire(ev, count, st, slots_ + at, slots_);
                    fired_cur_ = true;
                }
            }
            // the next step, if it plays early (negative offset)
            if(Forward() && !fired_next_)
            {
                const SeqStep& st    = pattern.step[Wrap(cur_ + 1)];
                const double   early = 1.0 + st.offset / 48.0 + Swung(slots_ + 1.0);
                if(st.offset < 0 && early < 1.0 && early <= seg_end)
                {
                    Fire(ev, count, st, slots_ + early, slots_ + 1.0);
                    fired_next_ = true;
                }
            }
            if(target < 1.0)
            {
                phase_ = target;
                break;
            }
            // next step
            if(!primed_)
                slots_ += 1.0;
            cur_           = primed_ ? StartStep() : NextOf(cur_);
            primed_        = false;
            if(has_pending_ && cur_ == StartStep())
            {
                const SeqPattern was = pattern; // for Previous()
                pattern      = pending_;
                pending_     = was;
                has_pending_ = false;
                switched_    = true;
                cur_         = StartStep();
                for(int i = 0; i < SeqPattern::kSteps; i++)
                    written_[i] = false;
            }
            written_[cur_] = false; // recording may replace it again
            fired_cur_     = fired_next_;
            fired_next_    = false;
            phase_         = 0.0;
            target -= 1.0;
            if(target >= 1.0)
                target = 0.9999; // never two steps in a block
        }
        pos_ = slots_ + phase_;
        return count;
    }

  private:
    struct Sounding
    {
        int8_t note = -1;
        double off_at = 0.0; // steps
    };
    struct Rec
    {
        int8_t note = -1, step = -1;
        float  at   = 0.f;
    };

    /** Swing lateness for slot `slot`, a share of a step. */
    double Swung(double slot) const
    {
        return (static_cast<long>(slot) & 1) ? 2.0 * swing_ - 1.0 : 0.0;
    }
    /** A step's start within its slot: offset `at` plus swing, kept in the step. */
    double Late(double slot, double at) const
    {
        const double t = at + Swung(slot);
        return t < 0.0 ? 0.0 : t > 0.98 ? 0.98 : t;
    }

    void Rewind()
    {
        cur_        = StartStep();
        cur_dir_    = 1;
        phase_      = 0.0;
        pos_        = 0.0;
        slots_      = 0.0;
        fired_cur_  = false;
        fired_next_ = false;
        stop_due_   = false;
        primed_     = false;
        has_pending_ = false;
        switched_    = false;
        for(auto& s : sounding_)
            s.note = -1;
        for(auto& r : rec_note_)
            r.step = -1;
    }

    int StartStep() const { return dir_ == Direction::Reverse ? pattern.length - 1 : 0; }

    int Wrap(int step) const
    {
        const int len = pattern.length;
        return ((step % len) + len) % len;
    }

    /** The step after `k` in the current direction. */
    int NextOf(int k)
    {
        const int len = pattern.length;
        switch(dir_)
        {
            case Direction::Reverse: return Wrap(k - 1);
            case Direction::PingPong:
            {
                if(len == 1)
                    return 0;
                int nx = k + cur_dir_;
                if(nx >= len)
                    cur_dir_ = -1, nx = len - 2;
                else if(nx < 0)
                    cur_dir_ = 1, nx = 1;
                return nx;
            }
            case Direction::Random:
                rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
                return static_cast<int>(rng_ % static_cast<uint32_t>(len));
            default: return Wrap(k + 1);
        }
    }

    bool Forward() const { return dir_ == Direction::Forward; }

    /** Starts step `st`'s notes at `at` (steps since Start), grid slot `grid`. */
    void Fire(Event* ev, int& count, const SeqStep& st, double at, double grid)
    {
        if(!st.on)
            return;
        for(int i = 0; i < st.count; i++)
        {
            const int note = st.note[i] + transpose_;
            if(note < 0 || note > 127)
                continue;
            // a repeated note ends first
            for(auto& so : sounding_)
                if(so.note == note)
                    Emit(ev, count, note, false), so.note = -1;
            Sounding* slot = &sounding_[0];
            for(auto& so : sounding_)
                if(so.note < 0)
                {
                    slot = &so;
                    break;
                }
            if(slot->note >= 0) // all four sounding: the oldest gives way
                Emit(ev, count, slot->note, false);
            slot->note   = static_cast<int8_t>(note);
            slot->off_at = at + st.gate / 48.0;
            Emit(ev, count, note, true, st.strum, st.gate);
            if(count > 0 && ev[count - 1].on && st.strum)
                ev[count - 1].lead = static_cast<float>(at - grid), ev[count - 1].odd = static_cast<long>(grid) & 1;
        }
    }

    static void Emit(Event* ev, int& count, int note, bool on, bool strum = false, uint16_t gate = 0)
    {
        if(count < kMaxEvents)
            ev[count++] = {static_cast<int8_t>(note), on, strum, gate};
    }

    /** A step rewritten: emptied, default length, strum kept. */
    SeqStep Fresh(const SeqStep& was) const
    {
        SeqStep s;
        s.gate  = def_gate_;
        s.strum = was.strum;
        return s;
    }

    static void Add(SeqStep& s, int note)
    {
        for(int i = 0; i < s.count; i++)
            if(s.note[i] == note)
                return;
        if(s.count < SeqStep::kNotes)
            s.note[s.count++] = static_cast<int8_t>(note);
    }

    /** Recording: the step a note belongs to, and its offset. */
    void Nearest(int& k, int8_t& offset)
    {
        if(dir_ != Direction::Forward)
        {
            k      = phase_ < 0.5 ? cur_ : NextOf(cur_);
            offset = 0;
            return;
        }
        if(quantize_ > 0)
        {
            // nearest grid point, in steps from the pattern's start
            const double at = cur_ + phase_;
            const int    g  = quantize_;
            int          q  = static_cast<int>((at + 0.5 * g) / g) * g;
            k               = Wrap(q);
            offset          = 0;
            return;
        }
        const bool later = phase_ >= 0.5;
        k                = later ? Wrap(cur_ + 1) : cur_;
        const double off = later ? phase_ - 1.0 : phase_;
        int          o   = static_cast<int>(off * 48.0 + (off < 0 ? -0.5 : 0.5));
        offset           = static_cast<int8_t>(o < -24 ? -24 : o > 23 ? 23 : o);
    }

    int RecSlot(int note)
    {
        for(int i = 0; i < kRecSlots; i++)
            if(rec_note_[i].note == note || rec_note_[i].step < 0)
                return i;
        return 0;
    }

    static constexpr int kRecSlots = 8;

    float     sr_;
    Direction dir_;
    bool      running_, external_, step_due_, stop_due_;
    bool      primed_; // on a clock: the first tick plays the first step
    float     rate_;
    int       cur_, cur_dir_;
    double    phase_; // 0 to 1 through the step
    double    slots_; // whole steps since Start
    double    pos_; // position in steps since Start
    bool      fired_cur_, fired_next_;
    int       selected_;
    bool      entry_fresh_;
    int       transpose_;
    float     swing_;
    int       quantize_;
    uint16_t  def_gate_; // new note length
    uint32_t  rng_;
    bool      written_[SeqPattern::kSteps];
    Sounding  sounding_[SeqStep::kNotes];
    Rec       rec_note_[kRecSlots];
    SeqPattern pending_;
    bool      has_pending_ = false, switched_ = false;
};

} // namespace phymo
