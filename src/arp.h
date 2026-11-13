/** @file arp.h
 *  @brief The arpeggiator: steps through the held keys, one note a step.
 *
 *  Patterns: up, down, up & down, random, as played; over 1 to 4 octaves.
 *  Random: a chance per step of a rest, an octave up or an octave down.
 *  Runs at its own rate, or on an external clock (Step(), a 16th note).
 *  Each note lasts `gate` of a step.
 */
#pragma once
#include <cstddef>
#include <cstdint>

namespace phymo
{

class Arp
{
  public:
    enum class Pattern { Up, Down, UpDown, Random, Played, Count };

    static constexpr int kMaxNotes = 32 * 4; // held keys x octaves

    /** What to play this block: a note to end and / or one to start (-1: none). */
    struct Event
    {
        int off = -1, on = -1;
    };

    void Init(float sr)
    {
        sr_ = sr;
        Reset();
    }

    void Reset()
    {
        pattern_  = Pattern::Up;
        octaves_  = 1;
        gate_     = 0.5f;
        rng_      = 0x9e3779b9u;
        sounding_ = -1;
        random_   = 0.f;
        external_ = false;
        step_due_ = false;
        SetRate(8.f);
        Restart();
    }

    /** Steps a second. */
    void SetRate(float hz) { inc_ = hz / sr_; }
    void    SetPattern(Pattern p) { pattern_ = p; }
    Pattern GetPattern() const { return pattern_; }
    void    SetOctaves(int o) { octaves_ = o < 1 ? 1 : o > 4 ? 4 : o; }
    int     Octaves() const { return octaves_; }
    /** External clock: it waits for Step() instead of stepping itself. */
    void SetExternal(bool e)
    {
        external_ = e;
        step_due_ = false;
    }
    /** External clock: step now (at the next Tick). */
    void Step() { step_due_ = true; }

    /** A chance a step, 0 to 1, of a rest or an octave jump. */
    void SetRandom(float p) { random_ = p; }
    /** How much of a step each note lasts, 0 to 1. */
    void SetGate(float g) { gate_ = g; }

    /** From the top: the next step at once, from the pattern's start. */
    void Restart()
    {
        phase_ = 1.f;
        pos_   = -1;
        dir_   = 1;
    }

    /** The note it's playing now (-1: none), to end it when it's stopped. */
    int Sounding() const { return sounding_; }
    /** How far through its step it is, 0 to 1 (for a tempo light). */
    float Phase() const { return phase_ < 1.f ? phase_ : 0.f; }

    /** Forget the note playing (the caller has ended it). */
    void Silence() { sounding_ = -1; }

    /** `n` samples on, given `count` held keys, oldest first. */
    Event Tick(size_t n, const int* held, int count)
    {
        Event e;
        if(count == 0)
        {
            // nothing held: end the note and restart next time
            e.off     = sounding_;
            sounding_ = -1;
            Restart();
            return e;
        }
        const float before = phase_;
        phase_ += inc_ * static_cast<float>(n);
        // gate end
        if(sounding_ >= 0 && before < gate_ && phase_ >= gate_)
        {
            e.off     = sounding_;
            sounding_ = -1;
        }
        if(external_)
        {
            // external clock: wait for a step, holding just short of the next
            if(step_due_)
                phase_ = phase_ < 1.f ? 1.f : phase_, step_due_ = false;
            else if(phase_ >= 1.f)
                phase_ = 0.9999f;
        }
        if(phase_ < 1.f)
            return e;
        // new step
        phase_ -= phase_ >= 2.f ? phase_ : 1.f;
        if(sounding_ >= 0) // a gate of 1 runs into the next
            e.off = sounding_;
        const int len  = Build(held, count);
        int       note = notes_[Next(len)];
        if(random_ > 0.f && Rand() < random_)
        {
            const float r = Rand();
            if(r < 1.f / 3.f)
                return e; // rest
            note += r < 2.f / 3.f ? 12 : -12;
        }
        sounding_ = note;
        e.on      = sounding_;
        return e;
    }

  private:
    float Rand()
    {
        rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
        return static_cast<float>(rng_ >> 8) / 16777216.f; // 0 to 1
    }

    /** The step's notes in pattern order, into notes_; returns how many. */
    int Build(const int* held, int count)
    {
        int base[32], m = 0;
        for(int i = 0; i < count && m < 32; i++)
            base[m++] = held[i];
        if(pattern_ != Pattern::Played)
            for(int i = 1; i < m; i++) // low to high
                for(int j = i; j > 0 && base[j] < base[j - 1]; j--)
                {
                    const int t = base[j];
                    base[j]     = base[j - 1];
                    base[j - 1] = t;
                }
        int len = 0;
        for(int o = 0; o < octaves_; o++)
            for(int i = 0; i < m; i++)
                notes_[len++] = base[i] + 12 * o;
        return len;
    }

    /** The next index into the `len` notes. */
    int Next(int len)
    {
        if(pos_ >= len) // fewer notes than last step
            pos_ = len - 1;
        switch(pattern_)
        {
            case Pattern::Down:
                pos_ = pos_ <= 0 || pos_ >= len ? len - 1 : pos_ - 1;
                return pos_;
            case Pattern::UpDown:
            {
                if(len == 1)
                    return pos_ = 0;
                // ends not repeated
                int p = pos_ + dir_;
                if(p >= len)
                    dir_ = -1, p = len - 2;
                else if(p < 0)
                    dir_ = 1, p = 1;
                return pos_ = p;
            }
            case Pattern::Random:
            {
                rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
                return pos_ = static_cast<int>(rng_ % static_cast<uint32_t>(len));
            }
            default: // Up, Played
                pos_ = pos_ + 1 >= len ? 0 : pos_ + 1;
                return pos_;
        }
    }

    float    sr_;
    float    inc_; // step increment per sample
    float    phase_; // 0 to 1 through the step
    float    gate_;
    float    random_;
    bool     external_, step_due_;
    Pattern  pattern_;
    int      octaves_;
    int      pos_;      // the last index played
    int      dir_;      // up & down: 1 up, -1 down
    int      sounding_; // the note playing, -1 none
    uint32_t rng_;
    int      notes_[kMaxNotes];
};

} // namespace phymo
