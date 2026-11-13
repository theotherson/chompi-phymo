// Checks what MIDI and presets rely on, without the hardware: a preset
// round-trips through its text (code/src/preset.h); the synth queues the
// keys' notes (not MIDI-in's), the arp's notes and its clock (6 a step);
// the arp follows an external clock (a step every 6 ticks, its rate from
// their spacing):
//   make -C host midi_test && (cd host && ./midi_test)
#include "../code/src/preset.h"
#include "../src/synth.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr float kSr    = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static void Run(phymo::Synth& s, int blocks)
{
    float l[48], r[48];
    for(int b = 0; b < blocks; b++)
        s.Process(l, r, 48);
}

int main()
{
    // A preset round-trips.
    {
        chompi::PresetState a;
        for(int v = 0; v < chompi::PresetState::kViews; v++)
            for(int k = 0; k < 4; k++)
                a.knob[v][k] = (v * 4 + k) / 32.f, a.alt[v][k] = 1.f - (v * 4 + k) / 32.f;
        for(int d = 0; d < chompi::PresetState::kDests; d++)
            a.lfo_to[d] = d / 7.f, a.env_to[d] = 1.f - d / 9.f;
        a.bright = 0.42f, a.pos = 0.77f, a.chorus = 3, a.porta = 1, a.key_mode = 4;
        a.lfo_free = 1, a.delay_glide = 1, a.fm_list = 3, a.model = 2, a.pickup = 1;
        a.chord_n = 4, a.chord[0] = 0, a.chord[1] = 3, a.chord[2] = 7, a.chord[3] = 10;
        char         buf[4096];
        const size_t n = chompi::WritePreset(a, buf, sizeof buf);
        chompi::PresetState b; // its defaults are other values
        const bool          read = n > 0 && chompi::ReadPreset(buf, b);
        bool                same = read && fabsf(a.bright - b.bright) < 1e-4f && fabsf(a.pos - b.pos) < 1e-4f
                    && a.chorus == b.chorus && a.porta == b.porta && a.key_mode == b.key_mode
                    && a.lfo_free == b.lfo_free && a.delay_glide == b.delay_glide && a.fm_list == b.fm_list
                    && a.model == b.model && a.pickup == b.pickup
                    && a.chord_n == b.chord_n;
        for(int v = 0; v < chompi::PresetState::kViews; v++)
            for(int k = 0; k < 4; k++) // (every control with a name: the rest aren't a preset's)
                same = same && (!chompi::kParam[v][k].turn || fabsf(a.knob[v][k] - b.knob[v][k]) < 1e-4f)
                       && (!chompi::kParam[v][k].alt || fabsf(a.alt[v][k] - b.alt[v][k]) < 1e-4f);
        for(int i = 0; i < a.chord_n; i++)
            same = same && a.chord[i] == b.chord[i];
        for(int d = 0; d < chompi::PresetState::kDests; d++)
            same = same && fabsf(a.lfo_to[d] - b.lfo_to[d]) < 1e-4f && fabsf(a.env_to[d] - b.env_to[d]) < 1e-4f;
        char what[64];
        std::snprintf(what, sizeof what, "preset: round-trips through its text (%zu bytes)", n);
        Check(same, what);

        chompi::PresetState d;
        d.bright = 0.5f;
        Check(!chompi::ReadPreset("not a preset\n", d) && d.bright == 0.5f, "preset: another file is refused");
        Check(chompi::ReadPreset("phymo-preset 1\npos=0.25\nwhat=7\n", d) && d.pos == 0.25f && d.bright == 0.5f,
              "preset: missing lines keep the defaults, unknown ones are skipped");

        // A version 1 file (before the ENV page): the LFO's delay to CHOMPI +
        // shape, the shape to knob 2, the old depths (pitch, brightness,
        // morph) to theirs, both ways (none in the middle).
        chompi::PresetState o;
        const bool ok = chompi::ReadPreset("phymo-preset 1\nk0.1=0.4\nk0.2=0.7\nk0.3=0.5\nk2.3=1\na2.3=0\n", o);
        const bool moved = ok && fabsf(o.alt[0][1] - 0.4f) < 1e-4f && fabsf(o.knob[0][1] - 0.7f) < 1e-4f
                           && o.knob[0][2] == 0.f && o.knob[0][3] == 0.f && o.knob[2][3] == 0.f
                           && fabsf(o.lfo_to[0] - (0.5f + 0.5f * (0.06f + 0.94f * 0.5f))) < 1e-4f
                           && fabsf(o.lfo_to[1] - 1.f) < 1e-4f && o.lfo_to[2] == 0.5f && o.env_to[0] == 0.5f;
        Check(moved, "preset: a version 1 file's LFO and its depths moved to version 2's places");

        // A version 2 file (before RESONATOR's attack and sustain): its
        // decay, damper and key tracking to ring, damper and KBD knob 1, the
        // detune to its CHOMPI + turn (poly's cents kept); FM's attack (Type) to Attack.
        chompi::PresetState w;
        const bool ok2 = chompi::ReadPreset(
            "phymo-preset 2\nmodel=2\nk1.0=0.3\nk2.0=0.6\nk2.1=0.4\nk2.2=0.7\nk2.3=0.9\nk5.0=0.2\n", w);
        const bool moved2 = ok2 && fabsf(w.knob[2][0] - 0.3f) < 1e-4f && fabsf(w.knob[2][1] - 0.6f) < 1e-4f
                            && fabsf(w.knob[2][2] - 0.4f) < 1e-4f && w.knob[2][3] == 0.f
                            && fabsf(w.knob[5][0] - 0.7f) < 1e-4f && fabsf(w.alt[5][0] - sqrtf(5.f * 0.2f / 12.f)) < 1e-4f;
        Check(moved2, "preset: a version 2 file's RESONATOR and KBD knobs moved to version 3's places");

        // An old file's CHOMPI + turns that held nothing then (written as 0)
        // keep today's defaults (the bow's strokes, the vibrato's onset and
        // decay); its detune (poly: x 5 cents, fixed) kept as many cents.
        chompi::PresetState u, def;
        u.alt[1][2] = def.alt[1][2] = 1.f, u.alt[3][1] = def.alt[3][1] = 1.f, u.alt[3][2] = def.alt[3][2] = 0.2f;
        const bool ok3 = chompi::ReadPreset(
            "phymo-preset 2\nkey_mode=1\nk5.0=1.0\na1.2=0.0\na3.1=0.0\na3.2=0.0\na1.0=0.4\n", u);
        const bool kept = ok3 && u.alt[1][2] == def.alt[1][2] && u.alt[3][1] == def.alt[3][1]
                          && u.alt[3][2] == def.alt[3][2] && fabsf(u.alt[1][0] - 0.4f) < 1e-4f
                          && fabsf(u.alt[5][0] - sqrtf(5.f / 12.f)) < 1e-4f;
        Check(kept, "preset: an old file's empty CHOMPI + turns keep their defaults; its detune kept in cents");

        // By name (version 4): every name its own; a file's lines in any
        // order land where kParam says (ring: RESONATOR knob 2; detune: KBD
        // knob 1's CHOMPI + turn; a depth), and places mean nothing in it.
        bool unique = true;
        for(int v = 0; v < chompi::PresetState::kViews; v++)
            for(int k = 0; k < 4; k++)
                for(const char* n : {chompi::kParam[v][k].turn, chompi::kParam[v][k].alt})
                    for(int u = 0; n && u < chompi::PresetState::kViews; u++)
                        for(int j = 0; j < 4; j++)
                            for(const char* m : {chompi::kParam[u][j].turn, chompi::kParam[u][j].alt})
                                unique = unique && (!m || m == n || std::strcmp(m, n) != 0);
        chompi::PresetState q;
        const bool ok4 = chompi::ReadPreset("phymo-preset 4\nenv_tone=0.9\ndetune=0.2\nring=0.25\nk2.1=0.7\n", q);
        const bool named = ok4 && unique && fabsf(q.knob[2][1] - 0.25f) < 1e-4f && fabsf(q.alt[5][0] - 0.2f) < 1e-4f
                           && fabsf(q.env_to[4] - 0.9f) < 1e-4f;
        Check(named, "preset: by name, in any order, each name its own (a place's line ignored)");
    }

    // A bank of patterns round-trips.
    {
        static phymo::SeqPattern a[chompi::kBankPatterns], b[chompi::kBankPatterns];
        for(auto& x : a)
            x.Clear();
        a[2].length = 12;
        a[2].step[0].note[0] = 60, a[2].step[0].count = 1, a[2].step[0].on = true;
        a[2].step[5].note[0] = 64, a[2].step[5].note[1] = 67, a[2].step[5].count = 2, a[2].step[5].offset = -7,
        a[2].step[5].gate = 40;
        a[14].length = 32, a[14].step[31].note[0] = 72, a[14].step[31].count = 1;
        a[2].step[5].strum = true, a[2].step[7].strum = true; // (strummed: one with notes, one without)
        static char buf[8192];
        const size_t n  = chompi::WriteBank(a, buf, sizeof buf);
        const bool   ok = n > 0 && chompi::ReadBank(buf, b);
        bool same = ok && b[2].length == 12 && b[2].step[0].note[0] == 60 && b[2].step[0].on
                    && b[2].step[5].count == 2 && b[2].step[5].note[1] == 67 && b[2].step[5].offset == -7
                    && b[2].step[5].gate == 40 && !b[2].step[5].on && b[14].length == 32
                    && b[14].step[31].note[0] == 72 && b[0].step[0].count == 0 && b[2].step[5].strum
                    && b[2].step[7].strum && b[2].step[7].count == 0 && !b[2].step[0].strum;
        phymo::SeqPattern old[chompi::kBankPatterns]; // (an older file: no strum)
        const bool        was = chompi::ReadBank("phymo-seq 1\np=0 len=16\ns=0 on=1 o=0 g=24 n=60,64\n", old);
        same = same && was && old[0].step[0].count == 2 && !old[0].step[0].strum;
        Check(same, "patterns: a bank round-trips through its text (its strums too; an older file's, none)");
    }

    phymo::Synth     s;
    phymo::OutEvent  e;
    auto            drain = [&](int& on, int& off, int& clk, int& start, int& stop) {
        on = off = clk = start = stop = 0;
        while(s.PopEvent(e))
            on += e.type == phymo::OutEvent::NoteOn, off += e.type == phymo::OutEvent::NoteOff,
                clk += e.type == phymo::OutEvent::Clock, start += e.type == phymo::OutEvent::Start,
                stop += e.type == phymo::OutEvent::Stop;
    };
    int on, off, clk, start, stop;

    // Keys' notes go out; MIDI-in's don't (no echo).
    s.Init(kSr);
    s.NoteOn(60), s.NoteOff(60);
    s.NoteOn(62, false), s.NoteOff(62, false);
    drain(on, off, clk, start, stop);
    Check(on == 1 && off == 1, "out: the keys' notes, not MIDI-in's");

    // The arp on its own: start, its notes, and clock, 6 a step.
    s.Init(kSr);
    s.SetArpRate(8.f);
    s.NoteOn(60), s.NoteOn(64);
    drain(on, off, clk, start, stop);
    s.SetArp(true);
    {
        // 1 s: 8 steps. Drained each block, as the firmware does (the queue
        // holds 64)
        int a = 0, b = 0, c = 0, d = 0, f = 0;
        on = off = clk = start = stop = 0;
        for(int blk = 0; blk < 1000; blk++)
        {
            Run(s, 1);
            drain(a, b, c, d, f);
            on += a, off += b, clk += c, start += d, stop += f;
        }
    }
    char what[110];
    std::snprintf(what, sizeof what, "out, arp on: start %d, %d notes, %d clock ticks in 1 s at 8 steps (48)", start,
                  on, clk);
    Check(start == 1 && on >= 8 && on <= 9 && clk >= 47 && clk <= 49 && off >= 2, what);
    s.SetArpRun(false);
    drain(on, off, clk, start, stop);
    Check(stop == 1, "out: pausing the arp sends stop");

    // An external clock: a step every 6 ticks, its rate from their spacing.
    s.Init(kSr);
    s.SetArpRate(2.f);
    s.NoteOn(60);
    s.SetArp(true);
    s.SetExternalClock(true);
    s.ClockStart();
    drain(on, off, clk, start, stop);
    // 120 BPM: 48 ticks a second, one each 1000 samples
    for(int t = 0; t < 48; t++)
    {
        s.ClockTick();
        Run(s, 1000 / 48 + 1);
    }
    drain(on, off, clk, start, stop);
    std::snprintf(what, sizeof what, "external clock: %d steps for 48 ticks (8), no clock echoed out (%d)", on, clk);
    Check(on == 8 && clk == 0, what);
    s.SetExternalClock(false);
    Run(s, 1000);
    drain(on, off, clk, start, stop);
    Check(on >= 1 && on <= 3, "back on its own rate (2 steps a second) once the clock's gone");

    // The arp's divisions, in clock ticks: 1/4 triplets, 16 ticks a step.
    s.Init(kSr);
    s.SetArpDivision(16);
    s.NoteOn(60);
    s.SetArp(true);
    s.SetExternalClock(true);
    s.ClockStart();
    drain(on, off, clk, start, stop);
    for(int t = 0; t < 48; t++)
    {
        s.ClockTick();
        Run(s, 1000 / 48 + 1);
    }
    drain(on, off, clk, start, stop);
    std::snprintf(what, sizeof what, "arp 1/4 triplets: %d steps in two beats (3)", on);
    Check(on == 3, what);
    return g_fails ? 1 : 0;
}
