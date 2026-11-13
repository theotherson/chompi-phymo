// Checks the arpeggiator's patterns, octaves, rate and gate, how it hands
// keys over to the voices and back, pause, and portamento; renders an arp:
//   make -C host arp_test && (cd host && ./arp_test)
#include "../src/synth.h"
#include "wav.h"
#include <algorithm>
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

/** The first `count` notes the arp plays over `held`, in `p`. */
static std::vector<int> Sequence(phymo::Arp::Pattern p, std::vector<int> held, int octaves, int count)
{
    phymo::Arp a;
    a.Init(kSr);
    a.SetPattern(p);
    a.SetOctaves(octaves);
    a.SetRate(10.f);
    std::vector<int> out;
    for(int b = 0; b < 100000 && static_cast<int>(out.size()) < count; b++)
    {
        const auto e = a.Tick(48, held.data(), static_cast<int>(held.size()));
        if(e.on >= 0)
            out.push_back(e.on);
    }
    return out;
}

static std::string Str(const std::vector<int>& v)
{
    std::string s;
    for(int n : v)
        s += std::to_string(n) + " ";
    return s;
}

static void Same(const std::vector<int>& got, const std::vector<int>& want, const char* what)
{
    char line[160];
    std::snprintf(line, sizeof line, "%s: %s", what, Str(got).c_str());
    Check(got == want, line);
}

/** Gated notes across the voices, sorted. */
static std::vector<int> Gated(const phymo::Synth& s)
{
    std::vector<int> g;
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        if(s.GetVoice(i).Gated())
            g.push_back(s.GetVoice(i).Note());
    std::sort(g.begin(), g.end());
    return g;
}

static void Run(phymo::Synth& s, int ms)
{
    float l[48], r[48];
    for(int b = 0; b < ms; b++)
        s.Process(l, r, 48);
}

int main()
{
    using P = phymo::Arp::Pattern;
    // Patterns, over C E G pressed as G C E.
    const std::vector<int> held = {67, 60, 64};
    Same(Sequence(P::Up, held, 1, 7), {60, 64, 67, 60, 64, 67, 60}, "up");
    Same(Sequence(P::Down, held, 1, 7), {67, 64, 60, 67, 64, 60, 67}, "down");
    Same(Sequence(P::UpDown, held, 1, 9), {60, 64, 67, 64, 60, 64, 67, 64, 60}, "up & down (ends not repeated)");
    Same(Sequence(P::Played, held, 1, 6), {67, 60, 64, 67, 60, 64}, "as played");
    Same(Sequence(P::Up, held, 2, 7), {60, 64, 67, 72, 76, 79, 60}, "up, 2 octaves");
    {
        const auto r     = Sequence(P::Random, held, 1, 300);
        int        n[3] = {};
        for(int x : r)
            n[x == 60 ? 0 : x == 64 ? 1 : 2]++;
        Check(n[0] > 70 && n[1] > 70 && n[2] > 70, "random: all three notes, about evenly");
    }

    // Rate and gate: 8 steps a second, each note held half a step.
    {
        phymo::Arp a;
        a.Init(kSr);
        a.SetRate(8.f);
        int   ons = 0, on_at = -1, len = -1;
        int   one = 60;
        for(int b = 0; b < 2000; b++) // 2 s of 1 ms blocks
        {
            const auto e = a.Tick(48, &one, 1);
            if(e.on >= 0)
                ons++, on_at = b;
            if(e.off >= 0 && on_at >= 0 && len < 0)
                len = b - on_at;
        }
        char what[96];
        std::snprintf(what, sizeof what, "rate: %d notes in 2 s at 8 a second; each held %d ms (half of 125)", ons,
                      len);
        Check(ons >= 16 && ons <= 17 && len >= 60 && len <= 64, what);
    }

    // In the synth: the arp takes held keys, and gives them back.
    phymo::Synth s;
    s.Init(kSr);
    s.NoteOn(60), s.NoteOn(64);
    Check(Gated(s) == std::vector<int>({60, 64}), "arp off: keys play as chords");
    s.SetArp(true);
    Check(Gated(s).empty(), "arp on: the held chord goes quiet");
    Run(s, 2);
    Check(Gated(s).size() == 1, "arp on: one note at a time, at once");
    s.SetArpRun(false);
    Check(Gated(s).empty(), "paused: silent");
    Run(s, 500);
    Check(Gated(s).empty(), "paused: stays silent");
    s.SetArpRun(true);
    Run(s, 300);
    Check(Gated(s).size() <= 1, "running again: notes again, one at a time");
    s.SetArp(false);
    Check(Gated(s) == std::vector<int>({60, 64}), "arp off: the keys still held sound again");
    s.NoteOff(60), s.NoteOff(64);
    s.SetArp(true);
    Run(s, 300);
    Check(Gated(s).empty(), "arp on, no keys: nothing plays");

    // Portamento (mono): 60 to 72 over ~100 ms.
    using PM = phymo::PortaMode;
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Mono);
    s.SetPortamento(PM::Always, 0.1f);
    s.NoteOn(60), Run(s, 50);
    const float start = s.GetVoice(0).Pitch();
    s.NoteOn(72);
    Run(s, 25);
    const float mid = s.GetVoice(0).Pitch();
    Run(s, 100);
    const float end = s.GetVoice(0).Pitch();
    char        what[120];
    std::snprintf(what, sizeof what, "portamento: 60 → 72, at %.1f after 25 ms, %.2f after 125 ms", mid, end);
    Check(start == 60.f && mid > 63.f && mid < 71.f && fabsf(end - 72.f) < 0.1f, what);
    s.NoteOff(72), s.NoteOff(60), Run(s, 20);
    s.NoteOn(65), Run(s, 6); // (a detached note: mono's legato, no glide; after its 5 ms fade)
    Check(s.GetVoice(0).Pitch() == 65.f, "portamento, mono: a detached note doesn't glide (always legato)");
    s.SetPortamento(PM::Off, 0.1f);
    s.NoteOn(48);
    Run(s, 6); // (the ringing string's old note faded out first: kHushMove, 5 ms)
    Check(s.GetVoice(0).Pitch() == 48.f, "portamento off: the pitch jumps (after its 5 ms fade)");

    // Poly: voices left on old notes before portamento came on don't glide
    // from them: switching on starts afresh, then each note glides from the
    // last note played, whichever voice.
    s.Init(kSr);
    for(int n : {36, 84, 40, 90}) // four voices left on far-off notes
        s.NoteOn(n), Run(s, 5), s.NoteOff(n);
    Run(s, 200);
    s.SetPortamento(PM::Always, 0.1f);
    s.NoteOn(60);
    auto pitch_of = [&](int note) {
        for(int i = 0; i < phymo::Synth::kVoices; i++)
            if(s.GetVoice(i).Gated() && s.GetVoice(i).Note() == note)
                return s.GetVoice(i).Pitch();
        return -1.f;
    };
    Check(pitch_of(60) == 60.f, "portamento switched on: the first note doesn't glide (afresh)");
    Run(s, 20), s.NoteOff(60), Run(s, 20);
    s.NoteOn(67);
    Run(s, 1);
    const float p67 = pitch_of(67);
    std::snprintf(what, sizeof what, "poly: the next note glides from the last played (60), not its voice's own: at %.1f",
                  p67);
    Check(p67 >= 60.f && p67 < 64.f, what);

    // Legato: only with another key held.
    s.Init(kSr);
    s.SetPortamento(PM::Legato, 0.1f);
    s.NoteOn(60), Run(s, 20), s.NoteOff(60);
    s.NoteOn(67);
    Check(pitch_of(67) == 67.f, "legato: a detached note doesn't glide");
    s.NoteOn(72), Run(s, 1);
    Check(pitch_of(72) < 71.f, "legato: a note while another's held glides");

    // Chord mode: the chord glides as a block.
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Chord);
    s.SetPortamento(PM::Always, 0.1f);
    s.NoteOn(60), Run(s, 20), s.NoteOff(60);
    s.NoteOn(65), Run(s, 1);
    Check(pitch_of(65) < 61.f && pitch_of(69) < 65.f && pitch_of(72) < 68.f,
          "chord: the chord glides as a block from the last one");

    // A render: an arp over a minor chord, 2 octaves, up & down, chorus I.
    s.Init(kSr);
    s.SetChorus(phymo::Chorus::Mode::I);
    s.SetVolume(0.5f);
    s.SetDamper(0.15f);
    s.SetArp(true);
    s.SetArpPattern(P::UpDown), s.SetArpOctaves(2), s.SetArpRate(9.f);
    for(int n : {57, 60, 64})
        s.NoteOn(n);
    const int          n = 6 * 48000;
    std::vector<float> l(n), r(n);
    for(int b = 0; b < n; b += 48)
        s.Process(&l[b], &r[b], 48);
    WriteWav("out/arp.wav", l, r, 48000);
    std::printf("      wrote out/arp.wav (A minor, up & down, 2 octaves)\n");
    return g_fails ? 1 : 0;
}
