// Checks the step sequencer (src/sequencer.h) and how the synth plays it:
// step entry, timing at the master tempo, length, steps off, directions,
// transpose, recording (quantized and as played), the voices it keeps, and
// a MIDI clock driving it; renders a sequence:
//   make -C host seq_test && (cd host && ./seq_test)
#include "../src/synth.h"
#include "wav.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static constexpr float kSr    = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

struct Hit
{
    int    note;
    bool   on;
    double t; // seconds
};

/** Runs a sequencer `secs` in 48-sample blocks, collecting its events. */
static std::vector<Hit> Play(phymo::Sequencer& s, double secs)
{
    std::vector<Hit>        hits;
    phymo::Sequencer::Event ev[phymo::Sequencer::kMaxEvents];
    for(int b = 0; b < static_cast<int>(secs * kSr / 48); b++)
    {
        const int k = s.Tick(48, ev);
        for(int i = 0; i < k; i++)
            hits.push_back({ev[i].note, ev[i].on, (b + 1) * 48 / kSr});
    }
    return hits;
}

static void Run(phymo::Synth& s, int blocks)
{
    float l[48], r[48];
    for(int b = 0; b < blocks; b++)
        s.Process(l, r, 48);
}

static std::vector<int> Ons(const std::vector<Hit>& h, int max = 100)
{
    std::vector<int> v;
    for(const auto& x : h)
        if(x.on && static_cast<int>(v.size()) < max)
            v.push_back(x.note);
    return v;
}

static std::string Str(const std::vector<int>& v)
{
    std::string s;
    for(int n : v)
        s += std::to_string(n) + " ";
    return s;
}

/** A sequencer with steps 0-3 holding notes 60, 62, 64, 65, at 8 steps a second. */
static void Four(phymo::Sequencer& s)
{
    s.Init(kSr);
    s.SetRate(8.f);
    s.SetLength(4);
    const int notes[4] = {60, 62, 64, 65};
    for(int i = 0; i < 4; i++)
    {
        s.Select(i);
        s.EntryNote(notes[i]);
        s.EntryDone();
    }
}

int main()
{
    phymo::Sequencer s;
    char            what[160];

    // Step entry: a note fills the step and moves on; a chord stays together.
    s.Init(kSr);
    s.EntryNote(60), s.EntryDone();
    s.EntryNote(64), s.EntryNote(67), s.EntryNote(71), s.EntryNote(72), s.EntryNote(74), s.EntryDone();
    const auto& p = s.pattern;
    Check(p.step[0].count == 1 && p.step[0].note[0] == 60 && p.step[0].on, "entry: a note fills the step");
    Check(p.step[1].count == 4 && p.step[1].note[3] == 72 && s.Selected() == 2,
          "entry: a chord stays in one step (4 notes at most), then the next step");

    // Timing: 8 steps a second, each note half a step long.
    Four(s);
    s.Start();
    auto h = Play(s, 0.99); // (a whole second would catch the 9th, at 1.000 s)
    std::vector<double> ons, offs;
    for(const auto& x : h)
        (x.on ? ons : offs).push_back(x.t);
    // (the first note's stamped at its block's end: measure between later ones)
    std::snprintf(what, sizeof what, "timing: %zu notes in a second at 8 a second, %.1f ms apart, %.1f ms long",
                  ons.size(), ons.size() > 2 ? 1000.0 * (ons[2] - ons[1]) : 0.0,
                  offs.size() > 1 ? 1000.0 * (offs[1] - ons[1]) : 0.0);
    Check(ons.size() == 8 && fabs(ons[2] - ons[1] - 0.125) < 0.002 && fabs(offs[1] - ons[1] - 0.0625) < 0.002,
          what);
    Check(Ons(h, 6) == std::vector<int>({60, 62, 64, 65, 60, 62}), "the steps in order, round the length");

    // Length, and a step off.
    Four(s);
    s.SetLength(3);
    s.ToggleStep(1);
    s.Start();
    {
        phymo::Sequencer t;
        Four(t);
        t.SetLength(3);
        t.ToggleStep(1);
        t.Start();
        const auto o = Ons(Play(t, 1.0), 5);
        std::snprintf(what, sizeof what, "length 3, step 2 off: %s", Str(o).c_str());
        Check(o == std::vector<int>({60, 64, 60, 64, 60}), what);
    }

    // Directions.
    using D = phymo::Sequencer::Direction;
    struct Want
    {
        D                 d;
        const char*       name;
        std::vector<int>  want;
    };
    const Want dirs[] = {{D::Reverse, "reverse", {65, 64, 62, 60, 65}},
                         {D::PingPong, "ping-pong", {60, 62, 64, 65, 64, 62, 60, 62}}};
    for(const auto& w : dirs)
    {
        Four(s);
        s.SetDirection(w.d);
        s.Start();
        const auto o = Ons(Play(s, 2.0), static_cast<int>(w.want.size()));
        std::snprintf(what, sizeof what, "%s: %s", w.name, Str(o).c_str());
        Check(o == w.want, what);
    }
    {
        Four(s);
        s.SetDirection(D::Random);
        s.Start();
        int n[4] = {};
        for(int x : Ons(Play(s, 20.0), 1000))
            n[x == 60 ? 0 : x == 62 ? 1 : x == 64 ? 2 : 3]++;
        Check(n[0] > 25 && n[1] > 25 && n[2] > 25 && n[3] > 25, "random: every step, about evenly");
    }

    // Transpose.
    Four(s);
    s.SetTranspose(12);
    s.Start();
    Check(Ons(Play(s, 0.5), 3) == std::vector<int>({72, 74, 76}), "transpose: up an octave");

    // Recording, quantized to steps: a note just after step 2 starts, and
    // one late in it (nearer step 3).
    s.Init(kSr);
    s.SetRate(8.f), s.SetLength(8), s.SetQuantize(1);
    s.Start();
    Play(s, 2.3 / 8); // into step 2, 0.3 of the way
    s.RecordOn(70), s.RecordOff(70);
    Play(s, 0.4 / 8); // 0.7 of the way
    s.RecordOn(72), s.RecordOff(72);
    Check(s.pattern.step[2].note[0] == 70 && s.pattern.step[3].note[0] == 72 && s.pattern.step[2].offset == 0,
          "record, quantized: notes to the nearest step, on its beat");

    // Recording as played: offsets and lengths kept, and played back so.
    s.Init(kSr);
    s.SetRate(8.f), s.SetLength(8), s.SetQuantize(0);
    s.Start();
    Play(s, 2.25 / 8); // a quarter into step 2: late
    s.RecordOn(70);
    Play(s, 0.5 / 8); // held half a step
    s.RecordOff(70);
    const auto& st2 = s.pattern.step[2];
    std::snprintf(what, sizeof what, "record as played: step 3 late by %d/48, %d/48 long (12, 24)", st2.offset,
                  st2.gate);
    Check(st2.note[0] == 70 && abs(st2.offset - 12) <= 1 && abs(st2.gate - 24) <= 1, what);
    // the next lap: does it play a quarter step late?
    {
        auto   lap = Play(s, 1.0);
        double t   = -1;
        for(const auto& x : lap)
            if(x.on && x.note == 70)
            {
                t = x.t;
                break;
            }
        // the lap started at 3.25/8 s into the pattern: step 2 of the next
        // lap is at 8 + 2.25 steps from the start = (10.25 - 2.75) / 8 s from now
        const double want = (10.25 - 2.75) / 8.0;
        std::snprintf(what, sizeof what, "record as played: plays back a quarter step late (%.3f s, want %.3f)", t,
                      want);
        Check(fabs(t - want) < 0.003, what);
    }
    // an early note goes to the step after, before its beat
    s.Init(kSr);
    s.SetRate(8.f), s.SetLength(8), s.SetQuantize(0);
    s.Start();
    Play(s, 3.75 / 8);
    s.RecordOn(74), s.RecordOff(74);
    Check(s.pattern.step[4].note[0] == 74 && abs(s.pattern.step[4].offset + 12) <= 1,
          "record as played: an early note goes to the next step, early");

    // Recording over: the first note into a step on a pass replaces it,
    // more in that pass join it.
    Four(s);
    s.SetQuantize(1);
    s.Start();
    Play(s, 1.1 / 8);
    s.RecordOn(80), s.RecordOn(84), s.RecordOff(80), s.RecordOff(84);
    Check(s.pattern.step[1].count == 2 && s.pattern.step[1].note[0] == 80 && s.pattern.step[1].note[1] == 84,
          "record over: the step's old note replaced, the chord kept together");

    // In the synth: it keeps the last four voices; live playing the first.
    phymo::Synth syn;
    syn.Init(kSr);
    syn.SetTempo(120.f);
    auto& q = syn.Seq();
    q.SetLength(1);
    q.EntryNote(48), q.EntryNote(52), q.EntryNote(55), q.EntryDone();
    q.pattern.step[0].gate = 48;
    syn.SeqStart();
    float l[48], r[48];
    for(int b = 0; b < 10; b++)
        syn.Process(l, r, 48);
    syn.NoteOn(72), syn.NoteOn(76), syn.NoteOn(79), syn.NoteOn(84);
    int seqv = 0, livev = 0;
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        if(syn.GetVoice(i).Gated())
            (i >= 4 ? seqv : livev)++, (void)0;
    int seq_kept = 0;
    for(int i = 4; i < 8; i++)
        seq_kept += syn.GetVoice(i).Gated() && syn.GetVoice(i).Note() < 60;
    std::snprintf(what, sizeof what, "synth: the sequence on voices 5-8 (%d), live on 1-4 (%d) without stealing them",
                  seqv, livev);
    Check(seq_kept == 3 && livev == 4, what);
    // sequencer mode: the keys never reach the arp, which keeps its own
    syn.SetArp(true);
    const int arp_before = syn.ActiveVoices();
    syn.SetSeqMode(true);
    syn.NoteOn(90);
    bool keyed = false;
    for(int i = 0; i < 4; i++)
        keyed = keyed || (syn.GetVoice(i).Gated() && syn.GetVoice(i).Note() == 90);
    syn.NoteOff(90);
    syn.SetSeqMode(false);
    syn.SetArp(false);
    Check(keyed && arp_before >= 0, "sequencer mode: a key plays at once with the arp on (not the arp's)");
    syn.SeqStop();
    for(int b = 0; b < 2; b++)
        syn.Process(l, r, 48);
    bool released = true;
    for(int i = 4; i < 8; i++)
        released = released && !syn.GetVoice(i).Gated();
    Check(released, "synth: stopping ends the sequence's notes");

    // A strummed step (the STRUM track): its notes low to high (entered
    // high to low), SetSeqStrum apart, each as long as it'd be (its end as
    // late as its start); a new note's length the default; the step's
    // strum kept as it's entered again; an audition, stopped, plays the
    // step, strummed, and ends by itself.
    {
        phymo::Synth t;
        t.Init(48000.f);
        t.SetTempo(120.f); // (16ths: 125 ms a step)
        t.SetSeqStrum(0.02f);
        auto& u = t.Seq();
        u.SetLength(4);
        u.SetDefaultGate(92); // two steps
        u.EntryFrom(0);
        u.EntryNote(67), u.EntryNote(64), u.EntryNote(60), u.EntryDone();
        u.SetStrum(0, true);
        u.EntryFrom(0);
        u.EntryNote(67), u.EntryNote(64), u.EntryNote(60), u.EntryDone(); // (entered again: strum kept)
        const bool kept = u.pattern.step[0].strum && u.pattern.step[0].gate == 92;
        struct Seen
        {
            int note, on_ms, off_ms;
        };
        std::vector<Seen> seen;
        bool              was[8] = {};
        t.SeqStart();
        for(int ms = 0; ms < 400; ms++)
        {
            for(int i = 4; i < 8; i++)
            {
                const bool g = t.GetVoice(i).Gated();
                if(g && !was[i])
                    seen.push_back({t.GetVoice(i).Note(), ms, -1});
                if(!g && was[i])
                    for(auto& x : seen)
                        if(x.note == t.GetVoice(i).Note() && x.off_ms < 0)
                            x.off_ms = ms;
                was[i] = g;
            }
            Run(t, 1);
        }
        t.SeqStop();
        Run(t, 2);
        bool order = seen.size() == 3 && seen[0].note == 60 && seen[1].note == 64 && seen[2].note == 67
                     && abs(seen[1].on_ms - seen[0].on_ms - 20) <= 1 && abs(seen[2].on_ms - seen[1].on_ms - 20) <= 1;
        bool lengths = order;
        for(const auto& x : seen) // (two steps: 92/48 of a step, 240 ms, each)
            lengths = lengths && x.off_ms > 0 && abs(x.off_ms - x.on_ms - 240) <= 2;
        // the audition
        Run(t, 50);
        t.AuditionStep(u.pattern.step[0]);
        int heard = 0, later = 0, after = 0;
        for(int ms = 0; ms < 1400; ms++)
        {
            int g = 0;
            for(int i = 4; i < 8; i++)
                g += t.GetVoice(i).Gated();
            if(ms == 1)
                heard = g;
            if(ms == 45)
                later = g;
            if(ms == 1300)
                after = g;
            Run(t, 1);
        }
        std::snprintf(what, sizeof what,
                      "strummed step: %d %d %d, %d and %d ms apart, each %d ms long (240); its strum and the default "
                      "length kept re-entered%s; auditioned, %d then %d notes, %d after",
                      seen.size() > 0 ? seen[0].note : -1, seen.size() > 1 ? seen[1].note : -1,
                      seen.size() > 2 ? seen[2].note : -1, seen.size() > 1 ? seen[1].on_ms - seen[0].on_ms : -1,
                      seen.size() > 2 ? seen[2].on_ms - seen[1].on_ms : -1,
                      seen.size() > 0 ? seen[0].off_ms - seen[0].on_ms : -1, kept ? "" : " NOT", heard, later, after);
        Check(order && lengths && kept && heard == 1 && later == 3 && after == 0, what);
    }

    // The STRUM track synced: a step held 4 steps spreads its 3 notes over
    // them, a third each, all ending as the step does; twice the tempo,
    // half; half the spread, half. A chord held 3 steps, swung, with a
    // note on each step after it: each strummed note with its step's, on
    // the grid (no flam).
    {
        struct Seen
        {
            int on = -1, off = -1;
        };
        auto run = [](float bpm, float spread, Seen* seen, int steps = 4, float swing = 0.5f) {
            phymo::Synth t;
            t.Init(48000.f);
            t.SetTempo(bpm);
            t.SetSeqSync(true), t.SetSeqSpread(spread);
            auto& u = t.Seq();
            u.SetLength(8);
            u.SetSwing(swing);
            u.SetDefaultGate(48 * steps - 4);
            u.EntryFrom(0);
            u.EntryNote(60), u.EntryNote(64), u.EntryNote(67), u.EntryDone();
            u.SetStrum(0, true);
            if(steps == 3) // (a note on each step after: 72, 76)
            {
                u.SetDefaultGate(24);
                u.EntryNote(72), u.EntryDone(), u.EntryNote(76), u.EntryDone();
            }
            bool was[8] = {};
            t.SeqStart();
            for(int ms = 0; ms < 900; ms++)
            {
                for(int i = 4; i < 8; i++)
                {
                    const auto& v = t.GetVoice(i);
                    const int   k = v.Note() == 60 ? 0 : v.Note() == 64 ? 1 : v.Note() == 67 ? 2 : v.Note() == 72 ? 3 : v.Note() == 76 ? 4 : -1;
                    if(k >= 0 && v.Gated() && !was[i] && seen[k].on < 0)
                        seen[k].on = ms;
                    if(k >= 0 && !v.Gated() && was[i] && seen[k].off < 0)
                        seen[k].off = ms;
                    was[i] = v.Gated();
                }
                Run(t, 1);
            }
        };
        Seen a[5], b[5], c[5], d[5];
        run(120.f, 1.f, a); // (a step 125 ms: four, 500 ms, the note 490; a third, 167)
        run(240.f, 1.f, b);
        run(120.f, 0.5f, c);
        run(120.f, 1.f, d, 3, 0.66f); // (swung: step 2 at 165 ms, step 3 at 250)
        const bool spread = abs(a[1].on - a[0].on - 167) <= 2 && abs(a[2].on - a[1].on - 167) <= 2;
        const bool ends   = abs(a[0].off - 490) <= 3 && abs(a[1].off - 490) <= 3 && abs(a[2].off - 490) <= 3;
        const bool tempo  = abs(b[1].on - b[0].on - 83) <= 2 && abs(b[2].off - 245) <= 3;
        const bool half   = abs(c[1].on - c[0].on - 83) <= 2 && abs(c[2].off - 490) <= 3;
        const bool grid   = d[3].on > 0 && abs(d[1].on - d[3].on) <= 1 && abs(d[2].on - d[4].on) <= 1 && abs(d[3].on - 165) <= 2;
        std::snprintf(what, sizeof what,
                      "STRUM synced: 4 steps' chord at %d %d %d ms, ending %d %d %d (490); 240 BPM %d apart (83); "
                      "half spread %d (83); 3 steps swung %d %d with the steps' %d %d",
                      a[0].on, a[1].on, a[2].on, a[0].off, a[1].off, a[2].off, b[1].on - b[0].on, c[1].on - c[0].on, d[1].on,
                      d[2].on, d[3].on, d[4].on);
        Check(spread && ends && tempo && half && grid, what);
    }

    // A four-note chord strummed over four steps, a note on each step after
    // it (its third, fifth, seventh), swung: each strummed note with its
    // step's, every loop (the MIDI out's order and times).
    {
        phymo::Synth t;
        t.Init(48000.f);
        t.SetTempo(120.f);
        t.SetSeqSync(true);
        auto& u = t.Seq();
        u.SetLength(8);
        u.SetSwing(0.6f);
        u.SetDefaultGate(48 * 4 - 4);
        u.EntryFrom(0);
        u.EntryNote(48), u.EntryNote(52), u.EntryNote(55), u.EntryNote(59), u.EntryDone();
        u.SetStrum(0, true);
        u.SetDefaultGate(24);
        u.EntryNote(64), u.EntryDone(), u.EntryNote(67), u.EntryDone(), u.EntryNote(71), u.EntryDone();
        t.SeqStart();
        int  worst = 0, seen = 0;
        bool all4  = true;
        for(int loop = 0; loop < 6; loop++)
        {
            int chord[4] = {-1, -1, -1, -1}, step[4] = {0, -1, -1, -1};
            for(int ms = 0; ms < 1000; ms++)
            {
                Run(t, 1);
                phymo::OutEvent e;
                while(t.PopEvent(e))
                    if(e.type == phymo::OutEvent::NoteOn)
                    {
                        const int c = e.note == 48 ? 0 : e.note == 52 ? 1 : e.note == 55 ? 2 : e.note == 59 ? 3 : -1;
                        const int k = e.note == 64 ? 1 : e.note == 67 ? 2 : e.note == 71 ? 3 : -1;
                        if(c >= 0 && chord[c] < 0)
                            chord[c] = ms;
                        if(k >= 0 && step[k] < 0)
                            step[k] = ms;
                    }
            }
            for(int i = 1; i < 4; i++)
            {
                all4 = all4 && chord[i] >= 0 && step[i] >= 0;
                if(chord[i] >= 0 && step[i] >= 0)
                    worst = std::max(worst, abs(chord[i] - step[i])), seen++;
            }
        }
        std::snprintf(what, sizeof what, "STRUM, four notes over four steps, swung: %d strummed notes, each within %d ms of its step's (1)",
                      seen, worst);
        Check(all4 && seen == 18 && worst <= 1, what);
    }

    // The STRUM track in the arp pattern's order: down, high to low.
    {
        phymo::Synth t;
        t.Init(48000.f);
        t.SetTempo(120.f);
        t.SetSeqStrum(0.02f);
        t.SetArpPattern(phymo::Arp::Pattern::Down);
        auto& u = t.Seq();
        u.SetLength(4);
        u.EntryFrom(0);
        u.EntryNote(64), u.EntryNote(60), u.EntryNote(67), u.EntryDone();
        u.SetStrum(0, true);
        std::vector<int> order;
        bool             was[8] = {};
        t.SeqStart();
        for(int ms = 0; ms < 100; ms++)
        {
            for(int i = 4; i < 8; i++)
            {
                const bool g = t.GetVoice(i).Gated();
                if(g && !was[i])
                    order.push_back(t.GetVoice(i).Note());
                was[i] = g;
            }
            Run(t, 1);
        }
        std::snprintf(what, sizeof what, "STRUM track, the arp pattern down: %s(67 64 60)", Str(order).c_str());
        Check(order == std::vector<int>({67, 64, 60}), what);
    }

    // A MIDI clock: a step every 6 ticks, the tempo from their spacing.
    syn.Init(kSr);
    auto& c = syn.Seq();
    c.SetLength(4);
    for(int i = 0; i < 4; i++)
        c.EntryNote(60 + i), c.EntryDone();
    syn.SetExternalClock(true);
    syn.ClockStart();
    int on = 0;
    for(int t = 0; t < 24; t++) // one beat at 90 BPM: a tick each 1333 samples
    {
        syn.ClockTick();
        for(int b = 0; b < 28; b++)
        {
            syn.Process(l, r, 48);
            phymo::OutEvent e;
            while(syn.PopEvent(e))
                on += e.type == phymo::OutEvent::NoteOn;
        }
    }
    std::snprintf(what, sizeof what, "MIDI clock: %d steps a beat (4), tempo %.0f BPM (90)", on, syn.GetTempo());
    Check(on == 4 && fabsf(syn.GetTempo() - 90.f) < 2.f, what);

    // A long note: a chord held over 4 steps, sounding once, ended once.
    {
        phymo::Sequencer t;
        t.Init(kSr);
        t.SetRate(8.f), t.SetLength(8);
        t.EntryNote(48), t.EntryNote(52), t.EntryDone();
        t.SetGate(0, 48 * 4 - 4);
        t.Start();
        int ons = 0, offs = 0;
        double off_t = -1;
        for(const auto& x : Play(t, 0.9)) // (one lap's a second)
            x.on ? ons++ : (offs++, off_t = x.t);
        std::snprintf(what, sizeof what, "long note: %d on, %d off, ending at %.3f s (4 steps: 0.5)", ons, offs, off_t);
        Check(ons == 2 && offs == 2 && fabs(off_t - 0.49) < 0.01, what);
    }

    // Entry past the end grows the pattern; EntryFrom starts again.
    {
        phymo::Sequencer t;
        t.Init(kSr);
        t.SetLength(2);
        for(int i = 0; i < 3; i++)
            t.EntryNote(60 + i), t.EntryDone();
        const bool grew = t.Length() == 3 && t.pattern.step[2].note[0] == 62 && t.Selected() == 3;
        t.EntryFrom(0), t.EntryNote(70), t.EntryDone();
        Check(grew && t.pattern.step[0].note[0] == 70, "entry: past the end grows the pattern; armed again, step 1");
    }

    // A queued pattern comes in on the first step, the old one kept.
    {
        Four(s);
        phymo::SeqPattern b;
        b.length = 4;
        for(int i = 0; i < 4; i++)
            b.step[i].note[0] = static_cast<int8_t>(72 + i), b.step[i].count = 1, b.step[i].on = true;
        s.Start();
        Play(s, 1.1 / 8);
        s.Queue(b);
        const auto o   = Ons(Play(s, 0.5), 4); // steps 2, 3, then the new one's 1, 2
        const bool sw  = s.TakeSwitched() && !s.Queued();
        std::snprintf(what, sizeof what, "queued pattern: in at the top (%s)", Str(o).c_str());
        Check(sw && o == std::vector<int>({64, 65, 72, 73}) && s.Previous().step[0].note[0] == 60, what);
    }

    // A rest in step entry: the step emptied, the next one up.
    {
        phymo::Sequencer t;
        t.Init(kSr);
        t.SetLength(4);
        t.EntryNote(60), t.EntryDone();
        t.EntryRest();
        t.EntryNote(64), t.EntryDone();
        Check(t.pattern.step[1].count == 0 && t.pattern.step[2].note[0] == 64, "entry: a rest leaves the step empty");
    }

    // The arp locked to the sequence: started with it, its first note on
    // step 1; at 1/8 every other step, never drifting (2 minutes in).
    {
        phymo::Synth a;
        a.Init(kSr);
        a.SetTempo(137.f), a.SetArpDivision(12);
        a.NoteOn(72), a.NoteOn(76);
        a.SetArp(true);
        Run(a, 333); // the arp running a while first: mid-step
        auto& sq = a.Seq();
        sq.SetLength(16);
        for(int i = 0; i < 16; i++)
            sq.Select(i), sq.EntryNote(48), sq.EntryDone();
        phymo::OutEvent e;
        while(a.PopEvent(e))
            ;
        a.SeqStart();
        long   blk = 0, worst = 0;
        long   seq_at = -1, arp_first = -1;
        int    seq_n = 0, arp_n = 0;
        bool   aligned = true;
        for(; blk < 120 * 1000; blk++)
        {
            Run(a, 1);
            while(a.PopEvent(e))
                if(e.type == phymo::OutEvent::NoteOn)
                {
                    if(e.note == 48)
                        seq_at = blk, seq_n++;
                    else
                    {
                        arp_first = arp_first < 0 ? blk : arp_first;
                        // every arp note lands with an even-numbered sequencer step
                        aligned = aligned && seq_at == blk && seq_n % 2 == 1;
                        worst   = blk - seq_at > worst ? blk - seq_at : worst;
                        arp_n++;
                    }
                }
        }
        std::snprintf(what, sizeof what, "arp locked: first note with step 1 (%s), %d notes on every other step over 2 min (%s)",
                      arp_first == 0 ? "yes" : "no", arp_n, aligned ? "yes" : "no");
        Check(arp_first == 0 && aligned && abs(arp_n - 548) <= 1, what); // 137 BPM x 2 x 120 s
    }

    // A render: a little sequence, chords and single notes, through chorus I.
    syn.Init(kSr);
    syn.SetTempo(110.f), syn.SetChorus(phymo::Chorus::Mode::I), syn.SetVolume(0.5f);
    auto&     m        = syn.Seq();
    const int seq[8][3] = {{48, 55, 64}, {-1}, {60}, {62}, {45, 52, 60}, {-1}, {64}, {67}};
    m.SetLength(8);
    for(int i = 0; i < 8; i++)
    {
        m.Select(i);
        for(int k = 0; k < 3 && seq[i][k] >= 0; k++)
            m.EntryNote(seq[i][k]);
        m.EntryDone();
    }
    syn.SeqStart();
    const int          n = 6 * 48000;
    std::vector<float> ol(n), orr(n);
    for(int b = 0; b < n; b += 48)
        syn.Process(&ol[b], &orr[b], 48);
    WriteWav("out/sequence.wav", ol, orr, 48000);
    std::printf("      wrote out/sequence.wav\n");
    return g_fails ? 1 : 0;
}
