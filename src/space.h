/** @file space.h
 *  @brief The delay and reverb send bus, after the chorus. Both run fully
 *  wet in parallel from one send, their returns added to the dry.
 *
 *    send       dry / wet crossfade
 *    space      0 delay only, 0.5 both, 1 reverb only
 *    time       delay time ~10 ms to ~2 s; also the reverb's pre-delay and
 *               early reflections
 *    tails      delay feedback and reverb decay
 *    ping-pong  0 stereo, 1 bouncing side to side
 *    glide      time changes crossfade (no pitch bend) or glide (tape-like)
 *
 *  Each runs only while sent to or ringing (~10 s after the last send).
 *  Init clears the memory it's given (the reverb tank and the delay's two
 *  2 s lines); call it before the audio starts.
 */
#pragma once
#include "dsp_util.h" // PHYMO_HOT
#include "digital_delay.h"
#include "fx/models/quadraverb.h"
#include <cstddef>
#include <cstdint>

namespace phymo
{

class Space
{
  public:
    static constexpr size_t kTankWords   = fx::kTankWords;
    static constexpr size_t kDelayWords  = 2 * DigitalDelay::kLen;
    static constexpr size_t kMaxBlock    = fx::kMaxBlock;
    static constexpr float  kDelayReturn = 0.7f; // the delay's return level

    void Init(float sr, uint16_t* tank, int16_t* delay_mem)
    {
        delay_.Init(sr, delay_mem);
        verb_.Init(sr);
        verb_.SetMemory(tank);
        verb_.SetAmount(1.f); // all wet: the send sets the level
        send_ = 0.f, space_ = 0.5f;
        SetTime(0.5f);
        SetTails(0.5f);
        verb_.Reset();
        delay_tail_ = reverb_tail_ = 0;
    }

    void SetSend(float s) { send_ = s; }
    void SetSpace(float s) { space_ = s; }
    /** 0 to 1: ~10 ms to ~2 s, with the reverb's pre-delay and early reflections. */
    void SetTime(float t)
    {
        const float s = .99f * t * t * t * 2.f + 0.01f; // seconds
        delay_.SetTime(s);
        // reverb pre-delay: 15% of the delay time, within 23-175 ms
        const float pre = (0.15f * s * 1000.f - 23.f) / 152.f;
        verb_.SetParam(0, pre < 0.f ? 0.f : pre > 1.f ? 1.f : pre);
        verb_.SetParam(1, 0.3f + 0.7f * t); // more early reflections with longer times
    }
    /** 0 to 1: the delay's feedback (0 to 0.95) and the reverb's decay. */
    void SetTails(float t)
    {
        delay_.SetFeedback(0.95f * t);
        verb_.SetParam(2, t);
    }
    void SetPingPong(float p) { delay_.SetPingPong(p); }
    /** false: time changes crossfade; true: they glide. */
    void SetGlide(bool g) { delay_.SetGlide(g); }
    bool Glide() const { return delay_.Glide(); }

    /** In place, stereo, n samples (up to kMaxBlock). */
    PHYMO_HOT void Process(float* l, float* r, size_t n)
    {
        // send levels and remaining dry
        const float in        = fx::SendToLevel(send_);
        const float dry       = fx::SendToDry(send_);
        const float to_delay  = in * (space_ < .5f ? 1.f : 2.f - 2.f * space_);
        const float to_reverb = in * (space_ > .5f ? 1.f : 2.f * space_);

        float dl[kMaxBlock], dr[kMaxBlock], rl[kMaxBlock], rr[kMaxBlock];
        for(size_t i = 0; i < n; i++)
        {
            dl[i] = l[i] * to_delay, dr[i] = r[i] * to_delay;
            rl[i] = l[i] * to_reverb, rr[i] = r[i] * to_reverb;
            l[i] *= dry, r[i] *= dry;
        }
        const uint32_t t0 = clock_ ? clock_() : 0;
        if(Awake(to_delay > 0.f, delay_tail_, n))
            delay_.Process(dl, dr, l, r, n, kDelayReturn);
        const uint32_t t1 = clock_ ? clock_() : 0;
        if(Awake(to_reverb > 0.f, reverb_tail_, n))
        {
            verb_.Process(rl, rr, n);
            for(size_t i = 0; i < n; i++)
                l[i] += rl[i], r[i] += rr[i];
        }
        if(clock_)
            delay_ticks_ = t1 - t0, reverb_ticks_ = clock_() - t1;
    }

    /** Cycle counter for profiling; the last block's delay and reverb cycles. */
    void SetClock(uint32_t (*clock)()) { clock_ = clock; }
    uint32_t DelayTicks() const { return delay_ticks_; }
    uint32_t ReverbTicks() const { return reverb_ticks_; }

  private:
    /** Running while sent to, then ~10 s for the tail. */
    static bool Awake(bool sent, uint32_t& tail, size_t n)
    {
        if(sent)
            tail = kTailSamples;
        else if(tail == 0)
            return false;
        else
            tail = tail > n ? tail - static_cast<uint32_t>(n) : 0;
        return true;
    }

    static constexpr uint32_t kTailSamples = 10 * 48000;

    DigitalDelay   delay_;
    fx::Quadraverb verb_;
    float          send_, space_;
    uint32_t (*clock_)() = nullptr;
    uint32_t       delay_ticks_ = 0, reverb_ticks_ = 0;
    uint32_t       delay_tail_, reverb_tail_;
};

} // namespace phymo
