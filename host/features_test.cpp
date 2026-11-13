// Checks unison and detune, the LFO's shapes and synced rate, the arp's
// randomness, and the latch loop:
//   make -C host features_test && (cd host && ./features_test)
#include "../src/synth.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr float kSr    = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static void Run(phymo::Synth& s, int ms)
{
    float l[48], r[48];
    for(int b = 0; b < ms; b++)
        s.Process(l, r, 48);
}

/** The voice holding `note`, or -1. */
static int VoiceOf(const phymo::Synth& s, int note)
{
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        if(s.GetVoice(i).Note() == note && s.GetVoice(i).Active())
            return i;
    return -1;
}

int main()
{
    phymo::Synth s;

    // Unison: four on one note (kUnisonVoices), detuned against each other,
    // struck a block apart (all four by the fourth); one at a time.
    s.Init(kSr);
    s.SetKeyMode(phymo::KeyMode::Unison);
    s.SetDetune(1.f);
    s.NoteOn(60);
    Run(s, 4);
    int   gated = 0;
    float lo = 1e9f, hi = -1e9f;
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        if(s.GetVoice(i).Gated() && s.GetVoice(i).Note() == 60)
            gated++;
    Check(gated == phymo::Synth::kUnisonVoices, "unison: its four voices on the note");
    s.NoteOn(64);
    Run(s, 4);
    gated = 0;
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        gated += s.GetVoice(i).Gated() && s.GetVoice(i).Note() == 64;
    Check(gated == phymo::Synth::kUnisonVoices, "unison: a new key takes all four (one note at a time)");
    s.NoteOff(64);
    Run(s, 4);
    gated = 0;
    for(int i = 0; i < phymo::Synth::kVoices; i++)
        gated += s.GetVoice(i).Gated() && s.GetVoice(i).Note() == 60;
    Check(gated == phymo::Synth::kUnisonVoices, "unison: letting go goes back to the key still held");
    {
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Unison);
        t.NoteOn(60);
        int n[5];
        for(int b = 0; b < 5; b++)
        {
            n[b] = 0;
            for(int i = 0; i < phymo::Synth::kVoices; i++)
                n[b] += t.GetVoice(i).Gated();
            Run(t, 1);
        }
        t.NoteOff(60), t.NoteOn(62), t.NoteOff(62); // (let go before its last strikes: they don't come)
        Run(t, 4);
        int late = 0;
        for(int i = 0; i < phymo::Synth::kVoices; i++)
            late += t.GetVoice(i).Gated();
        char what[160];
        std::snprintf(what, sizeof what, "unison: struck a block apart (%d, %d, %d, %d, %d voices); let go early, %d struck after",
                      n[0], n[1], n[2], n[3], n[4], late);
        Check(n[0] == 1 && n[1] == 2 && n[2] == 3 && n[3] == 4 && n[4] == 4 && late == 0, what);
    }
    (void)lo, (void)hi;

    // Detune: unison fanned out ± 50 cents at full (± the drift); in every
    // mode each voice drifting on its own, ± 12 cents at full, a quarter of
    // that at half (its square); none at 0.
    {
        auto spread = [](phymo::KeyMode m, float d, double& span, double& wander, double& most) {
            phymo::Synth t;
            t.Init(kSr);
            t.SetKeyMode(m), t.SetDetune(d);
            double vlo[8], vhi[8];
            for(int i = 0; i < 8; i++)
                vlo[i] = 1e9, vhi[i] = -1e9;
            span = 0, most = 0;
            for(int ms = 0; ms < 20000; ms++)
            {
                Run(t, 1);
                double lo = 1e9, hi = -1e9;
                for(int i = 0; i < 8; i++)
                {
                    const double c = 100.0 * t.GetVoice(i).Detune();
                    lo = fmin(lo, c), hi = fmax(hi, c), vlo[i] = fmin(vlo[i], c), vhi[i] = fmax(vhi[i], c);
                    most = fmax(most, fabs(c));
                }
                span = fmax(span, hi - lo);
            }
            wander = 1e9;
            for(int i = 0; i < 8; i++)
                wander = fmin(wander, vhi[i] - vlo[i]); // the least any voice moved
        };
        double us, uw, um, ps, pw, pm, hs, hw, hm, zs, zw, zm;
        spread(phymo::KeyMode::Unison, 1.f, us, uw, um);
        spread(phymo::KeyMode::Poly1, 1.f, ps, pw, pm);
        spread(phymo::KeyMode::Poly1, 0.5f, hs, hw, hm);
        spread(phymo::KeyMode::Mono, 0.f, zs, zw, zm);
        char dt[200];
        std::snprintf(dt, sizeof dt,
                      "detune: unison %.0f cents across; poly's drift each voice moving %.1f+ cents over 20 s, at most %.1f "
                      "(half: %.1f); 0 none (%.2f)", us, pw, pm, hm, zm);
        Check(us > 90.0 && um <= 62.0 && pw > 8.0 && pm <= 12.01 && hm <= 3.01 && hm > 1.0 && zm == 0.0, dt);
    }

    // Strum: a chord's notes spaced out low to high (pressed high to low);
    // the glissando (the arp's octaves: 2, an octave up), every inversion on
    // the way, stacking up (3: two octaves, to G''); with portamento, each
    // from the one before; let go, it plays out, then the damper; a key a
    // little late (15 ms) joins the chord. Its order the arp pattern's:
    // down, sweeping down; up & down, each strum the other way; random,
    // every note once; as played.
    {
        struct Start
        {
            int note, ms;
            float from;
        };
        using Pat = phymo::Arp::Pattern;
        int  held_end = 0; // (voices still held at the end, let go early)
        auto starts = [&held_end](float strum, int octaves, bool porta, std::vector<Start>& got, bool let_go,
                                  Pat pat = Pat::Up, bool twice = false, bool late = false) {
            phymo::Synth t;
            t.Init(kSr);
            t.SetKeyMode(phymo::KeyMode::Poly1);
            t.SetStrum(strum), t.SetArpOctaves(octaves), t.SetArpPattern(pat);
            if(porta)
                t.SetPortamento(phymo::PortaMode::Always, 0.05f);
            t.NoteOn(67), t.NoteOn(64);
            if(!late)
                t.NoteOn(60);
            uint32_t age[phymo::Synth::kVoices] = {};
            for(int ms = 0; ms < 600; ms++)
            {
                if(late && ms == 15)
                    t.NoteOn(60);
                if(let_go && ms == 30)
                    t.NoteOff(67), t.NoteOff(64), t.NoteOff(60);
                if(twice && ms == 300) // (a second strum)
                    t.NoteOff(67), t.NoteOff(64), t.NoteOff(60), t.NoteOn(67), t.NoteOn(64), t.NoteOn(60);
                for(int i = 0; i < phymo::Synth::kVoices; i++)
                {
                    const auto& v = t.GetVoice(i);
                    if(v.Gated() && v.Age() != age[i]) // (a strike: its age is the note count's)
                        age[i] = v.Age(), got.push_back({v.Note(), ms, v.Pitch()});
                }
                Run(t, 1);
            }
            held_end = 0;
            for(int i = 0; i < phymo::Synth::kVoices; i++)
                held_end += t.GetVoice(i).Gated();
        };
        const float sv = sqrtf(0.025f / phymo::Synth::kStrumTime); // (its square: 25 ms)
        std::vector<Start> a, b, c, d, e, f, g, h, k, two;
        starts(0.f, 3, false, a, false); // (no strum: no glissando, whatever the octaves)
        starts(sv, 1, false, b, false);
        starts(sv, 2, false, c, false);
        starts(sv, 2, false, d, true);
        const int held_after = held_end;
        std::vector<Start> lt;
        starts(sv, 2, false, lt, false, Pat::Up, false, true);
        starts(sv, 2, true, e, false);
        starts(sv, 2, false, f, false, Pat::Down);
        starts(sv, 3, false, two, false);
        starts(sv, 1, false, g, false, Pat::UpDown, true);
        starts(sv, 1, false, h, false, Pat::Random);
        starts(sv, 1, false, k, false, Pat::Played);
        const int gap = 25;
        auto notes = [](const std::vector<Start>& v) {
            std::vector<int> n;
            for(const auto& x : v)
                n.push_back(x.note);
            return n;
        };
        const bool down = notes(f) == std::vector<int>({67, 64, 60, 64, 60, 55, 60, 55, 52, 55, 52, 48}); // G E C ... G, E, C,
        const bool climb = two.size() == 21 && two.back().note == 91; // (two octaves: to G'')
        const bool updown = notes(g) == std::vector<int>({60, 64, 67, 67, 64, 60});
        std::vector<int> rnd = notes(h);
        std::sort(rnd.begin(), rnd.end());
        const bool random = rnd == std::vector<int>({60, 64, 67});
        const bool played = notes(k) == std::vector<int>({67, 64, 60});
        bool spaced = b.size() == 3 && b[0].note == 60 && b[1].note == 64 && b[2].note == 67
                      && abs(b[1].ms - b[0].ms - gap) <= 1 && abs(b[2].ms - b[1].ms - gap) <= 1;
        static const int kSweep[12] = {60, 64, 67, 64, 67, 72, 67, 72, 76, 72, 76, 79}; // C E G ... C' E' G'
        bool sweep = c.size() == 12;
        for(size_t i = 0; sweep && i < 12; i++)
            sweep = c[i].note == kSweep[i] && (i == 0 || abs(c[i].ms - c[i - 1].ms - gap) <= 1);
        bool glides = e.size() == 12 && fabsf(e[0].from - 60.f) < 0.01f; // (the first: none before it)
        for(size_t i = 1; glides && i < 12; i++)
            glides = fabsf(e[i].from - static_cast<float>(e[i - 1].note)) // (each from the one before: nearer it
                     < 0.25f * fabsf(static_cast<float>(e[i].note - e[i - 1].note)); //  than its own a block in)
        const bool early = notes(d) == notes(c) && held_after == 0; // (let go at 30 ms: all of it, then let go)
        const bool joined = lt.size() == 12 && lt.back().note == 79; // (the late key's C in every inversion)
        char what[320];
        std::snprintf(what, sizeof what,
                      "strum: off, %zu at once (by %d ms); on, %d %d %d %d ms apart; the glissando an octave up, %zu "
                      "strikes, every inversion%s, two octaves %zu; portamento, each from the one before%s; let go at 30 ms, %zu played, %d held after; late, %zu; "
                      "down%s, up & down%s, random%s, as played%s",
                      a.size(), a.empty() ? -1 : a.back().ms, b.size() > 0 ? b[0].note : -1, b.size() > 1 ? b[1].note : -1,
                      b.size() > 2 ? b[2].note : -1, b.size() > 1 ? b[1].ms - b[0].ms : -1, c.size(), sweep ? "" : " NOT",
                      two.size(), glides ? "" : " NOT", d.size(), held_after, lt.size(), down ? "" : " NOT", updown ? "" : " NOT", random ? "" : " NOT",
                      played ? "" : " NOT");
        Check(a.size() == 3 && a.back().ms == 0 && spaced && sweep && climb && glides && early && joined && down
                  && updown && random && played,
              what);
    }

    // Cycle: a held note strikes again each time its ENV's attack and decay
    // are done (5 ms and 50 ms: about every 55), never closer than 40 ms;
    // let go, it stops. Off, it doesn't.
    {
        auto cycles = [](bool on, float decay, int& held, int& after) {
            phymo::Synth t;
            t.Init(kSr);
            t.SetEnv(0.005f, decay, 0.f, 0.05f);
            t.SetCycle(on);
            t.NoteOn(60);
            held = after = 0;
            float last = 0.f;
            for(int ms = 0; ms < 1500; ms++)
            {
                if(ms == 1000)
                    t.NoteOff(60);
                Run(t, 1);
                const float e = t.GetVoice(0).EnvValue();
                if(e > last + 0.05f && last < 0.2f && ms > 2) // (its attack again)
                    (ms < 1000 ? held : after)++;
                last = e;
            }
        };
        int on_held, on_after, fast_held, fast_after, off_held, off_after;
        cycles(true, 0.05f, on_held, on_after);
        cycles(true, 0.005f, fast_held, fast_after);
        cycles(false, 0.05f, off_held, off_after);
        char what[200];
        std::snprintf(what, sizeof what,
                      "cycle: held 1 s, struck again %d times (5 + 50 ms ENV), let go %d; a 10 ms ENV %d times (40 ms "
                      "apart at least); off, %d",
                      on_held, on_after, fast_held, off_held);
        Check(on_held >= 15 && on_held <= 19 && on_after == 0 && fast_held >= 22 && fast_held <= 25 && off_held == 0,
              what);
    }

    // LFO shapes, at 10 Hz for a second each
    using Sh = phymo::Lfo::Shape;
    for(int sh = 0; sh < static_cast<int>(Sh::Count); sh++)
    {
        phymo::Lfo f;
        f.Init(kSr);
        f.SetRate(10.f);
        f.SetShape(static_cast<Sh>(sh));
        float mn = 1e9f, mx = -1e9f, prev = 0.f;
        int   jumps = 0, distinct = 0;
        for(int i = 0; i < 48000; i++)
        {
            const float x = f.Process();
            mn = fminf(mn, x), mx = fmaxf(mx, x);
            if(i && fabsf(x - prev) > 0.5f)
                jumps++;
            if(i && x != prev)
                distinct++;
            prev = x;
        }
        static const char* kNames[] = {"triangle", "sine", "square", "saw up", "saw down", "sample & hold"};
        bool ok = mn >= -1.001f && mx <= 1.001f;
        if(sh == 0 || sh == 1)
            ok = ok && jumps == 0 && mx > 0.99f && mn < -0.99f;
        if(sh == 2)
            ok = ok && jumps >= 19 && jumps <= 21; // two edges a cycle
        if(sh == 3 || sh == 4)
            ok = ok && jumps >= 9 && jumps <= 11; // one reset a cycle
        if(sh == 5)
            ok = ok && distinct >= 9 && distinct <= 11; // a new value a cycle, held
        char what[64];
        std::snprintf(what, sizeof what, "LFO %s: its shape, in -1 to 1", kNames[sh]);
        Check(ok, what);
    }

    // LFO synced: one cycle an arp step, at 4 steps a second, is 4 Hz.
    s.Init(kSr);
    s.SetArpRate(4.f), s.SetLfoSync(1.f), s.SetLfoRate(0.3f);
    const float synced = s.LfoHz();
    s.SetArpRate(8.f);
    const float follows = s.LfoHz();
    s.SetLfoSync(0.f);
    const float freed = s.LfoHz();
    char        sw[110];
    std::snprintf(sw, sizeof sw, "LFO sync: %.1f Hz at a cycle a step of 4 a second, %.1f at 8, %.1f free (0.3)", synced,
                  follows, freed);
    Check(synced == 4.f && follows == 8.f && freed == 0.3f, sw);

    // The arp's randomness: about a third of the chosen steps each a rest,
    // an octave up, an octave down.
    {
        phymo::Arp a;
        a.Init(kSr);
        a.SetRate(20.f), a.SetRandom(1.f);
        int one = 60, ons = 0, up = 0, down = 0, steps = 0;
        float ph_prev = 1.f;
        for(int b = 0; b < 60000; b++)
        {
            const auto e = a.Tick(48, &one, 1);
            if(a.Phase() < ph_prev)
                steps++;
            ph_prev = a.Phase();
            if(e.on >= 0)
                ons++, up += e.on == 72, down += e.on == 48;
        }
        const float rest = 1.f - float(ons) / steps;
        char        what[96];
        std::snprintf(what, sizeof what, "arp randomness: %.0f%% rests, %.0f%% up, %.0f%% down (a third each)",
                      100.f * rest, 100.f * up / steps, 100.f * down / steps);
        Check(fabsf(rest - 1.f / 3) < 0.05f && fabsf(float(up) / steps - 1.f / 3) < 0.05f
                  && fabsf(float(down) / steps - 1.f / 3) < 0.05f,
              what);
    }

    // The latch loop: held (hold on) and faded, a note strikes again; off,
    // or with the arp playing, it doesn't. Two notes on their own decays
    // drift apart.
    {
        // the strikes: a note's level (10 ms windows) jumping up 20 dB
        auto strikes = [](bool loop, bool arp, int note2, std::vector<int>* when) {
            phymo::Synth t;
            t.Init(kSr);
            t.SetKeyMode(phymo::KeyMode::Poly1), t.SetVolume(1.f), t.SetDecay(0.6f), t.SetKeyTrack(1.f), t.SetLife(0.f);
            t.SetLatchLoop(loop);
            if(arp)
                t.SetArp(true);
            t.NoteOn(60);
            if(note2 >= 0)
                t.NoteOn(note2);
            float  l[48], r[48];
            double last = 0;
            int    n    = 0;
            for(int w = 0; w < 400; w++) // 4 s
            {
                double e = 0;
                for(int b = 0; b < 10; b++)
                {
                    t.Process(l, r, 48);
                    for(int i = 0; i < 48; i++)
                        e += l[i] * l[i];
                }
                const double db = 10.0 * log10(e + 1e-20);
                if(w > 0 && db > last + 20.0)
                {
                    n++;
                    if(when)
                        when->push_back(w);
                }
                last = db;
            }
            return n;
        };
        const int on = strikes(true, false, -1, nullptr), off = strikes(false, false, -1, nullptr);
        char      what[160];
        std::snprintf(what, sizeof what, "latch loop: a held note strikes again %d times in 4 s (decay 0.6 s); without it %d", on, off);
        Check(on >= 4 && off == 0, what);
        const int arp_loop = strikes(true, true, -1, nullptr), arp_alone = strikes(false, true, -1, nullptr);
        std::snprintf(what, sizeof what, "latch loop: not with the arp playing (%d strikes, as many as without the loop: %d)", arp_loop,
                      arp_alone);
        Check(arp_loop == arp_alone, what);
        // two notes an octave apart, key tracking at 1, a 2 s decay: the
        // higher fades in half the time, so strikes again twice as often
        // (each voice's own; a decay much shorter, and the level follower's
        // own fall, 50 ms, sets the pace for both)
        phymo::Synth t;
        t.Init(kSr);
        t.SetKeyMode(phymo::KeyMode::Poly1), t.SetVolume(1.f), t.SetDecay(2.f), t.SetKeyTrack(1.f), t.SetLife(0.f);
        t.SetLatchLoop(true);
        t.NoteOn(60), t.NoteOn(72);
        int   count[2] = {0, 0};
        float was[2]   = {1.f, 1.f};
        for(int w = 0; w < 800; w++) // 8 s
        {
            Run(t, 10);
            for(int i = 0; i < phymo::Synth::kVoices; i++)
            {
                const auto& v = t.GetVoice(i);
                if(!v.Gated())
                    continue;
                const int   which = v.Note() == 72;
                const float f     = v.Fade();
                if(f > 0.9f && was[which] < 0.1f)
                    count[which]++;
                was[which] = f;
            }
        }
        std::snprintf(what, sizeof what, "latch loop: C4 and C5 held, each on its own decay: C4 struck again %d times, C5 %d", count[0],
                      count[1]);
        Check(count[0] >= 4 && count[1] >= 2 * count[0] - 2 && count[1] <= 2 * count[0] + 2, what);
    }
    // A choked voice's strings aren't wiped as it ends (that was the CHOMPI's
    // worst block); the next note clears what it reads: a low C2 rung loud,
    // choked, then C5 on the same voice: C2 not heard in it (as quiet as a
    // fresh voice's C5 is there), and C5 as loud as a fresh one's.
    {
        auto c5 = [](bool after_c2, double& at_c2, double& at_c5) {
            static float      a[phymo::StringRes::kLen], b[phymo::StringRes::kLen];
            phymo::Voice      v;
            phymo::VoiceParams p;
            p.model = phymo::Model::String, p.decay = 8.f;
            v.SetMemory(a, b);
            v.Init(48000.f);
            float l[48], r[48];
            auto  run = [&](int blocks, std::vector<float>* out) {
                for(int k = 0; k < blocks; k++)
                {
                    for(int i = 0; i < 48; i++)
                        l[i] = r[i] = 0.f;
                    v.Process(p, l, r, 48, 1.f);
                    if(out)
                        out->insert(out->end(), l, l + 48);
                }
            };
            if(after_c2)
            {
                v.NoteOn(36, 1, -1.f, 36);
                run(400, nullptr); // (ringing loud)
                v.Choke();
                run(200, nullptr); // (the choke's done: Reset, its lines left)
            }
            v.NoteOn(72, 2, -1.f, 72);
            std::vector<float> x;
            run(100, &x);
            auto mag = [&](double hz) {
                double c = 0, q = 0;
                for(size_t i = 0; i < x.size(); i++)
                    c += x[i] * cos(2 * M_PI * hz * i / 48000.0), q += x[i] * sin(2 * M_PI * hz * i / 48000.0);
                return sqrt(c * c + q * q) / x.size();
            };
            at_c2 = mag(65.406), at_c5 = mag(523.25);
        };
        double c2a, c5a, c2b, c5b;
        c5(true, c2a, c5a);
        c5(false, c2b, c5b);
        char what[160];
        std::snprintf(what, sizeof what, "a choked voice reused: C2 in its C5 %.1f dB (a fresh voice's %.1f), C5 %.1f dB (fresh %.1f)",
                      20 * log10(c2a + 1e-12), 20 * log10(c2b + 1e-12), 20 * log10(c5a + 1e-12), 20 * log10(c5b + 1e-12));
        Check(20 * log10((c2a + 1e-12) / (c2b + 1e-12)) < 3.0 && fabs(20 * log10(c5a / c5b)) < 1.0, what);
    }

    // The CPU room model, set to a measured heavy patch (24% fixed, a voice
    // 9.4%, a strike 16.6%: six fit). A chord from quiet with room to spare
    // (none learnt): struck at once. Three held and a four-note chord over
    // them: one too many, the oldest held one choked, none other; the chord
    // struck as it fits, the last of it once the choked one's gone (its
    // 25 ms fade: no room before, with a margin). Six held and four
    // more: four choked at once, the four struck. The budget: a strike's
    // blocks over not counted; over unstruck, one under what's sounding.
    {
        auto count = [](phymo::Synth& t, int& going, int& waiting, int& gated) {
            going = waiting = gated = 0;
            for(int i = 0; i < phymo::Synth::kVoices; i++)
                going += t.GetVoice(i).Choking(), waiting += t.GetVoice(i).Waiting(),
                    gated += t.GetVoice(i).Gated() && !t.GetVoice(i).Choking();
        };
        float l[48], r[48];
        int   g, w, n;
        // room to spare
        phymo::Synth a;
        a.Init(48000.f);
        a.SetKeyMode(phymo::KeyMode::Poly1);
        for(int k : {48, 52, 55, 59, 62, 65, 69})
            a.NoteOn(k);
        a.Process(l, r, 48);
        count(a, g, w, n);
        const bool at_once = w == 0 && n == 7;
        // three held, a chord of four over them
        phymo::Synth b;
        b.Init(48000.f);
        b.SetKeyMode(phymo::KeyMode::Poly1), b.SetSustain(1.f), b.SetDecay(8.f);
        b.SetLoadModel(0.24f, 0.094f, 0.166f);
        for(int k : {36, 40, 43})
            b.NoteOn(k), b.Process(l, r, 48);
        for(int k : {60, 64, 67, 71})
            b.NoteOn(k);
        b.Process(l, r, 48);
        count(b, g, w, n);
        const bool one     = g == 1 && VoiceOf(b, 36) >= 0 && b.GetVoice(VoiceOf(b, 36)).Choking();
        int        blocks  = 1;
        for(; blocks < 100 && w > 0; blocks++)
            b.Process(l, r, 48), count(b, g, w, n);
        const bool chord = w == 0 && VoiceOf(b, 40) >= 0 && VoiceOf(b, 43) >= 0 && VoiceOf(b, 71) >= 0;
        // six held, four more
        phymo::Synth c;
        c.Init(48000.f);
        c.SetKeyMode(phymo::KeyMode::Poly1), c.SetSustain(1.f), c.SetDecay(8.f);
        c.SetLoadModel(0.24f, 0.094f, 0.166f);
        for(int k : {36, 40, 43, 47, 50, 53})
            c.NoteOn(k), c.Process(l, r, 48);
        for(int k : {60, 64})
            c.NoteOn(k);
        c.Process(l, r, 48);
        count(c, g, w, n);
        const int four = g; // (two new now: eight asked of eight, two over the six that fit... and the stealing)
        // the budget, struck blocks over
        phymo::Synth d;
        d.Init(48000.f);
        d.SetKeyMode(phymo::KeyMode::Poly1), d.SetSustain(1.f), d.SetDecay(8.f);
        for(int k : {36, 40, 43})
            d.NoteOn(k), d.Process(l, r, 48), d.ReportLoad(0.3f);
        for(int k : {60, 64, 67, 71})
            d.NoteOn(k), d.Process(l, r, 48), d.ReportLoad(0.95f);
        const int kept = d.VoiceBudget();
        for(int i = 0; i < 3; i++)
            d.Process(l, r, 48);
        d.ReportLoad(0.95f);
        const int after = d.VoiceBudget();
        char what[240];
        std::snprintf(what, sizeof what,
                      "the CPU's room: room to spare, a chord at once%s; three held and four over them, the oldest choked "
                      "alone%s, the chord struck in %d blocks%s; six held and two more, %d choked (2); the budget, struck "
                      "blocks over, %d (8); unstruck, %d (6)",
                      at_once ? "" : " NOT", one ? "" : " NOT", blocks, chord ? "" : " NOT ALL", four, kept, after);
        Check(at_once && one && chord && blocks <= 30 && four == 2 && kept == 8 && after == 6, what);
    }

    // The profile (the CHOMPI's PROFILE.txt): a counter ticking once a read,
    // each block's six parts timed (the voices' with two reads more a voice:
    // each voice's own, the worst one's 1); two voices, the worst block's;
    // reset, none.
    {
        static uint32_t ticks = 0;
        phymo::Synth    t;
        t.Init(48000.f);
        t.SetProfileClock([]() { return ticks++; });
        t.NoteOn(60), t.NoteOn(64);
        float l[48], r[48];
        for(int b = 0; b < 100; b++)
            t.Process(l, r, 48);
        const phymo::Profile p = t.GetProfile();
        bool                 each = true;
        for(int i = 0; i < phymo::Profile::kParts; i++)
            each = each && p.sum[i] == (i == phymo::Profile::Voices ? 500u : 100u) && p.worst[i] == (i == phymo::Profile::Voices ? 5u : 1u);
        t.ResetProfile();
        char what[120];
        std::snprintf(what, sizeof what, "profile: %u blocks, each part timed%s, worst block %u (10), %d voices, one's %u (1); reset %u",
                      p.blocks, each ? "" : " NOT", p.worst_block, p.worst_voices, p.worst_one, t.GetProfile().blocks);
        Check(p.blocks == 100 && each && p.worst_block == 10 && p.worst_voices == 2 && p.worst_one == 1 && t.GetProfile().blocks == 0,
              what);
    }

    return g_fails ? 1 : 0;
}
