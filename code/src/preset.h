/** @file preset.h
 *  @brief A preset and its text file: one `name=value` per line, readable
 *  and editable by hand.
 *
 *    phymo-preset 6
 *    ring=0.600      each control by name, its knob's value (0 to 1); kParam
 *    detune=0.500    maps names to knobs, so moving a control changes no files
 *    lfo_pitch=0.5   LFO depth per destination (0.5 none); `env_` the envelope's
 *    bright=0.500    the wheel: brightness
 *    pos=0.300       CHOMPI + wheel: strike position
 *    tape=0.000      Vocal tape
 *    chorus=1        Chorus::Mode
 *    porta=0         PortaMode
 *    key_mode=1      KeyMode
 *    cycle=0         restrike held notes after each ENV attack and decay
 *    lfo_free=0      LFO trigger: NOTE 0, FREE 1, VOICE 2
 *    delay_glide=0
 *    fm_list=0       FM ratio list
 *    model=0         Model
 *    pickup=0        PickupMode
 *    chord=0,4,7     chord memory intervals
 *
 *  Not stored: volume, octave, hold, arp on / off. Reading starts from
 *  defaults, so missing lines keep them and unknown ones are skipped. Older
 *  versions are converted as they're read (FromV1 to FromV5).
 */
#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace chompi
{

/** The share of a bipolar knob's travel around the middle that reads as none. */
static constexpr float kFlat = 0.06f;

struct PresetState
{
    static constexpr int kViews = 8, kKnobs = 4, kChord = 8, kDests = 6;

    float knob[kViews][kKnobs] = {};
    float alt[kViews][kKnobs]  = {};
    float lfo_to[kDests] = {.5f, .5f, .5f, .5f, .5f, .5f}; // depths as knob values (0.5 none)
    float env_to[kDests] = {.5f, .5f, .5f, .5f, .5f, .5f};
    float bright = 0.f, pos = 0.f, tape = 0.f; // tape: CHOMPI + MODEL knob 2
    int   chorus = 0, porta = 0, key_mode = 1, cycle = 0;
    int   lfo_free = 0, delay_glide = 0, fm_list = 0, model = 0, pickup = 0;
    int   chord[kChord] = {0, 4, 7};
    int   chord_n       = 3;
};

/** Each control's file name by panel position: [view][knob] for the turn and
 *  the CHOMPI + turn (nullptr: none). Views as panel.h's View. To move a
 *  control, move its name here; never rename it. */
struct ParamName
{
    const char* turn;
    const char* alt;
};
static const ParamName kParam[PresetState::kViews][PresetState::kKnobs] = {
    {{"lfo_rate", "lfo_sync"}, {"lfo_shape", "lfo_delay"}, {nullptr, nullptr}, {nullptr, nullptr}},
    {{"type", "life"}, {"hardness", "bow_weight"}, {"noise", "bow_stroke"}, {"strength", "velocity"}},
    {{"attack", "course"}, {"ring", "interval"}, {"damper", "flageolet"}, {"sustain", nullptr}},
    {{"pickup_amount", "pickup_place"}, {"tone", "vib_decay"}, {"trem_rate", "vib_onset"}, {"trem_depth", "trem_spread"}},
    {{"table", "partials"}, {"stretch", "snap"}, {"morph", "ensemble"}, {"morph_time", "tonebar"}},
    {{"key_track", "detune"}, {"porta_time", "strum"}, {"arp_pattern", "arp_octaves"}, {"arp_rate", "arp_random"}},
    {{"chorus_level", "spread"}, {"send", nullptr}, {"space", "ping_pong"}, {"time", "tails"}},
    {{"env_attack", nullptr}, {"env_decay", nullptr}, {"env_sustain", nullptr}, {"env_release", nullptr}},
};
/** Destination names, after `lfo_` or `env_`. */
static const char* const kDepthName[PresetState::kDests] = {"pitch", "bright", "morph", "exciter", "tone", "place"};

/** printf onto the end of `buf` (`size` bytes, `len` used so far). */
__attribute__((format(printf, 4, 5))) inline void Put(char* buf, size_t size, size_t& len, const char* fmt, ...)
{
    if(len >= size)
        return;
    va_list args;
    va_start(args, fmt);
    const int n = std::vsnprintf(buf + len, size - len, fmt, args);
    va_end(args);
    len += n > 0 ? static_cast<size_t>(n) : 0;
}

/** A value as fixed-point "1.2345" (newlib-nano printf has no %f). */
struct Fixed
{
    int whole, frac;
};
inline Fixed Fix(float x)
{
    const int t = static_cast<int>((x > 0.f ? x : 0.f) * 10000.f + 0.5f);
    return {t / 10000, t % 10000};
}

/** Writes `s` as text into `buf`; returns the length, 0 if it didn't fit. */
inline size_t WritePreset(const PresetState& s, char* buf, size_t size)
{
    size_t len = 0;
#define put(...) Put(buf, size, len, __VA_ARGS__)
    put("phymo-preset 6\n");
    for(int v = 0; v < PresetState::kViews; v++)
        for(int k = 0; k < PresetState::kKnobs; k++)
        {
            const Fixed x = Fix(s.knob[v][k]), a = Fix(s.alt[v][k]);
            if(kParam[v][k].turn)
                put("%s=%d.%04d\n", kParam[v][k].turn, x.whole, x.frac);
            if(kParam[v][k].alt)
                put("%s=%d.%04d\n", kParam[v][k].alt, a.whole, a.frac);
        }
    for(int d = 0; d < PresetState::kDests; d++)
    {
        const Fixed l = Fix(s.lfo_to[d]), e = Fix(s.env_to[d]);
        put("lfo_%s=%d.%04d\nenv_%s=%d.%04d\n", kDepthName[d], l.whole, l.frac, kDepthName[d], e.whole, e.frac);
    }
    const Fixed bright = Fix(s.bright), pos = Fix(s.pos);
    put("bright=%d.%04d\npos=%d.%04d\n", bright.whole, bright.frac, pos.whole, pos.frac);
    const Fixed tape = Fix(s.tape);
    put("tape=%d.%04d\n", tape.whole, tape.frac);
    put("chorus=%d\nporta=%d\nkey_mode=%d\ncycle=%d\n", s.chorus, s.porta, s.key_mode, s.cycle);
    put("lfo_free=%d\ndelay_glide=%d\nfm_list=%d\nmodel=%d\npickup=%d\n", s.lfo_free, s.delay_glide, s.fm_list,
        s.model, s.pickup);
    put("chord=");
    for(int i = 0; i < s.chord_n; i++)
        put(i ? ",%d" : "%d", s.chord[i]);
    put("\n");
#undef put
    return len < size ? len : 0;
}

/** An old one-way depth (0 to 1) as a bipolar knob. */
inline float BothWays(float x) { return x <= 0.f ? 0.5f : 0.5f + 0.5f * (kFlat + (1.f - kFlat) * x); }

/** Version 1 to 2: the LFO view's and RESONATOR's knobs moved. */
inline void FromV1(PresetState& s)
{
    float(&lfo)[PresetState::kKnobs] = s.knob[0];
    const float delay = lfo[1], shape = lfo[2], pitch = lfo[3];
    lfo[1] = shape, s.alt[0][1] = delay, lfo[2] = lfo[3] = 0.f;
    s.lfo_to[0] = BothWays(pitch);
    s.lfo_to[1] = BothWays(s.knob[2][3]);
    s.lfo_to[2] = BothWays(s.alt[2][3]);
    s.knob[2][3] = s.alt[2][3] = 0.f;
}

/** Version 2 to 3: RESONATOR became attack, ring, damper, sustain; key
 *  tracking and detune moved to KBD; FM and bow attack moved from EXCITER
 *  knob 1; detune rescaled. */
inline void FromV2(PresetState& s)
{
    float(&res)[PresetState::kKnobs] = s.knob[2];
    float(&kbd)[PresetState::kKnobs] = s.knob[5];
    const float decay = res[0], damper = res[1], track = res[2], detune = kbd[0];
    const bool  own_attack = s.model == 2 || s.model == 3; // FM, Bowed
    res[0] = own_attack ? s.knob[1][0] : 0.f, res[1] = decay, res[2] = damper, res[3] = 0.f;
    kbd[0]       = track;
    s.alt[5][0]  = s.key_mode == 0 ? 0.f // KeyMode: Mono 0, Unison 3
                   : s.key_mode == 3 ? std::sqrt(25.f * detune / 50.f)
                                     : std::sqrt(5.f * detune / 12.f);
}

/** Version 4 to 5: bow strokes from 0.5-6 s to 0.04-6 s, length kept. */
inline void FromV4(PresetState& s)
{
    float& x = s.alt[1][2];
    if(x < 0.97f)
    {
        const float x5 = 0.97f * std::log(0.5f * std::pow(12.f, x / 0.97f) / 0.04f) / std::log(150.f);
        x              = x5 < 0.f ? 0.f : x5 > 0.969f ? 0.969f : x5;
    }
}

/** Version 5 to 6: the Reed table inserted after the Tine; Table and morph
 *  positions kept on their tables. */
inline void FromV5(PresetState& s)
{
    if(s.model != 1)
        return;
    const float t  = s.knob[4][0] * 7.f;
    const float t6 = t <= 3.f ? t : t >= 4.f ? t + 1.f : 3.f + 2.f * (t - 3.f);
    s.knob[4][0]   = t6 / 8.f;
    // the morph knob's flat middle
    const float d = 2.f * s.knob[4][2] - 1.f, ad = std::fabs(d);
    if(ad > kFlat)
    {
        const float m = (ad - kFlat) / (1.f - kFlat) * 7.f / 8.f; // tables, as a share of eight
        s.knob[4][2]  = 0.5f + (d < 0.f ? -0.5f : 0.5f) * (kFlat + (1.f - kFlat) * m);
    }
}

/** Versions 1 to 3 stored CHOMPI + turns by position; the positions that
 *  were in use then (bits: knobs 1-4). */
static const unsigned kOldAlt[PresetState::kViews] = {
    0x3, // LFO
    0x9, // EXCITER
    0x9, // RESONATOR
    0x8, // BODY
    0x3, // MODEL
    0xd, // KBD
    0xd, // FX
    0x0, // ENV
};

/** Where control `name` lives in `s` (nullptr: unknown). */
inline float* Named(PresetState& s, const char* name)
{
    for(int v = 0; v < PresetState::kViews; v++)
        for(int k = 0; k < PresetState::kKnobs; k++)
        {
            if(kParam[v][k].turn && !std::strcmp(name, kParam[v][k].turn))
                return &s.knob[v][k];
            if(kParam[v][k].alt && !std::strcmp(name, kParam[v][k].alt))
                return &s.alt[v][k];
        }
    const bool lfo = !std::strncmp(name, "lfo_", 4), env = !std::strncmp(name, "env_", 4);
    for(int d = 0; (lfo || env) && d < PresetState::kDests; d++)
        if(!std::strcmp(name + 4, kDepthName[d]))
            return &(lfo ? s.lfo_to : s.env_to)[d];
    return nullptr;
}

/** Reads text over `s` (holding defaults); false if it isn't a preset. */
inline bool ReadPreset(const char* buf, PresetState& s)
{
    if(std::strncmp(buf, "phymo-preset", 11) != 0)
        return false;
    const int version = std::atoi(buf + 12);
    auto clamp01 = [](float x) { return x < 0.f ? 0.f : x > 1.f ? 1.f : x; };
    const char* p = buf;
    while(*p)
    {
        const char* end = std::strchr(p, '\n');
        if(!end)
            end = p + std::strlen(p);
        const char* eq = static_cast<const char*>(std::memchr(p, '=', static_cast<size_t>(end - p)));
        if(eq)
        {
            char name[32] = {};
            const size_t nl = static_cast<size_t>(eq - p) < sizeof name - 1 ? static_cast<size_t>(eq - p)
                                                                            : sizeof name - 1;
            std::memcpy(name, p, nl);
            const char* val = eq + 1;
            int         v, k;
            float*      named = version >= 4 ? Named(s, name) : nullptr;
            if(named)
                *named = clamp01(std::strtof(val, nullptr));
 // versions 1 to 3: by position
            else if(version < 4 && (name[0] == 'k' || name[0] == 'a') && std::sscanf(name + 1, "%d.%d", &v, &k) == 2
                    && v >= 0 && v < PresetState::kViews && k >= 0 && k < PresetState::kKnobs)
            {
                if(name[0] == 'k' || (kOldAlt[v] >> k & 1))
                    (name[0] == 'k' ? s.knob : s.alt)[v][k] = clamp01(std::strtof(val, nullptr));
            }
            else if(version < 4 && (name[0] == 'l' || name[0] == 'e') && name[1] >= '0'
                    && name[1] < '0' + PresetState::kDests && name[2] == 0)
                (name[0] == 'l' ? s.lfo_to : s.env_to)[name[1] - '0'] = clamp01(std::strtof(val, nullptr));
            else if(!std::strcmp(name, "bright"))
                s.bright = clamp01(std::strtof(val, nullptr));
            else if(!std::strcmp(name, "pos"))
                s.pos = clamp01(std::strtof(val, nullptr));
            else if(!std::strcmp(name, "tape"))
                s.tape = clamp01(std::strtof(val, nullptr));
            else if(!std::strcmp(name, "chorus"))
                s.chorus = std::atoi(val);
            else if(!std::strcmp(name, "porta"))
                s.porta = std::atoi(val);
            else if(!std::strcmp(name, "key_mode"))
                s.key_mode = std::atoi(val);
            else if(!std::strcmp(name, "cycle"))
                s.cycle = std::atoi(val);
            else if(!std::strcmp(name, "lfo_free"))
                s.lfo_free = std::atoi(val);
            else if(!std::strcmp(name, "delay_glide"))
                s.delay_glide = std::atoi(val);
            else if(!std::strcmp(name, "fm_list"))
                s.fm_list = std::atoi(val);
            else if(!std::strcmp(name, "model"))
                s.model = std::atoi(val);
            else if(!std::strcmp(name, "pickup"))
                s.pickup = std::atoi(val);
            else if(!std::strcmp(name, "chord"))
            {
                int         n = 0;
                const char* q = val;
                while(q < end && n < PresetState::kChord)
                {
                    char* next;
                    const long x = std::strtol(q, &next, 10);
                    if(next == q)
                        break;
                    s.chord[n++] = static_cast<int>(x);
                    q            = *next == ',' ? next + 1 : next;
                }
                if(n > 0)
                    s.chord_n = n;
            }
        }
        p = *end ? end + 1 : end;
    }
    if(version < 2)
        FromV1(s);
    if(version < 3)
        FromV2(s);
    if(version < 5)
        FromV4(s);
    if(version < 6)
        FromV5(s);
    return true;
}

} // namespace chompi

// --- sequencer banks ---

#include "../../src/sequencer.h"

namespace chompi
{

static constexpr int kBankPatterns = 15;

/** A bank of patterns as text: `phymo-seq 1`, then each pattern with notes
 *  (`p=3 len=16`) and its steps (`s=0 on=1 o=0 g=24 n=60,64 t=1`, t the strum). */
inline size_t WriteBank(const phymo::SeqPattern* bank, char* buf, size_t size)
{
    size_t len = 0;
    Put(buf, size, len, "phymo-seq 1\n");
    for(int p = 0; p < kBankPatterns; p++)
    {
        bool any = false;
        for(const auto& st : bank[p].step)
            any = any || st.count > 0;
        if(!any)
            continue;
        Put(buf, size, len, "p=%d len=%d\n", p, bank[p].length);
        for(int j = 0; j < phymo::SeqPattern::kSteps; j++)
        {
            const auto& st = bank[p].step[j];
            if(st.count == 0 && !st.strum)
                continue;
            Put(buf, size, len, "s=%d on=%d o=%d g=%d n=", j, st.on ? 1 : 0, st.offset, st.gate);
            for(int i = 0; i < st.count; i++)
                Put(buf, size, len, i ? ",%d" : "%d", st.note[i]);
            Put(buf, size, len, st.strum ? " t=1\n" : "\n");
        }
    }
    return len < size ? len : 0;
}

/** Reads a bank's text over `bank` (cleared first); false if it isn't one. */
inline bool ReadBank(const char* buf, phymo::SeqPattern* bank)
{
    if(std::strncmp(buf, "phymo-seq", 8) != 0)
        return false;
    for(int p = 0; p < kBankPatterns; p++)
        bank[p].Clear();
    int         pat = -1;
    const char* q   = buf;
    while(*q)
    {
        int p, l, j, on, o, g;
        if(std::sscanf(q, "p=%d len=%d", &p, &l) == 2 && p >= 0 && p < kBankPatterns)
        {
            pat                = p;
            bank[pat].length   = static_cast<uint8_t>(l < 1 ? 1 : l > phymo::SeqPattern::kSteps ? phymo::SeqPattern::kSteps : l);
        }
        else if(pat >= 0 && std::sscanf(q, "s=%d on=%d o=%d g=%d", &j, &on, &o, &g) == 4 && j >= 0
                && j < phymo::SeqPattern::kSteps)
        {
            auto& st  = bank[pat].step[j];
            st.on     = on != 0;
            st.offset = static_cast<int8_t>(o < -24 ? -24 : o > 23 ? 23 : o);
            st.gate   = static_cast<uint16_t>(g < 1 ? 1 : g > phymo::Sequencer::kMaxGate ? phymo::Sequencer::kMaxGate : g);
            st.count  = 0;
            const char* n = std::strstr(q, " n="); // (with its space: "on=" has an "n=" too)
            const char* e = std::strchr(q, '\n');
            const char* t = std::strstr(q, " t=");
            st.strum      = t && (!e || t < e) && t[3] == '1';
            if(n && (!e || n < e))
            {
                n += 3;
                while(st.count < phymo::SeqStep::kNotes)
                {
                    char*      next;
                    const long x = std::strtol(n, &next, 10);
                    if(next == n)
                        break;
                    st.note[st.count++] = static_cast<int8_t>(x);
                    n                   = *next == ',' ? next + 1 : next;
                }
            }
        }
        const char* e = std::strchr(q, '\n');
        q             = e ? e + 1 : q + std::strlen(q);
    }
    return true;
}

} // namespace chompi
