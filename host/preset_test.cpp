// Loads the factory presets (presets/BANKn/P01.txt to P15.txt, as on the SD
// card) through the panel itself (code/src/panel.h, on the desktop over
// shim/'s stand-ins for the CHOMPI's hardware), so each knob means what it
// does on the CHOMPI, and plays each a phrase: notes up, a chord held,
// let go. Checks each one reads (every line a name the CHOMPI knows),
// sounds and stays under clipping; renders each to out/preset_B_NN.wav.
//   make -C host preset_test && (cd host && ./preset_test)
#include "panel.h"
#include "wav.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static constexpr float kSr     = 48000.f;
static int             g_fails = 0;

static void Check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
    g_fails += !ok;
}

static phymo::Synth  g_synth;
static chompi::Panel g_panel;
static chompi::Hardware g_hw;

/** `ms` of sound, the panel polled each 1 ms block (as the CHOMPI's audio
 *  interrupt does). */
static void Play(int ms, std::vector<float>& l, std::vector<float>& r, uint32_t& now)
{
    float bl[48], br[48];
    for(int b = 0; b < ms; b++)
    {
        g_panel.Poll(now++);
        g_synth.Process(bl, br, 48);
        l.insert(l.end(), bl, bl + 48), r.insert(r.end(), br, br + 48);
    }
}

int main()
{
    g_synth.Init(kSr);
    g_panel.Init(&g_hw, &g_synth);
    uint32_t now = 0;
    {
        // init first (CHOMPI + high A# held a second): a sound the card's
        // loads land on outside the view (in the view's browsing, they land
        // while it's open, or once kept)
        using Sw = chompi::Hardware::SwId;
        g_hw.Set(Sw::KEY_26, true), g_hw.Set(Sw::KEY_25, true);
        for(int i = 0; i < 1100; i++)
            g_panel.Poll(now++), g_hw.Next();
        g_hw.Set(Sw::KEY_25, false), g_panel.Poll(now++), g_hw.Next();
        g_hw.Set(Sw::KEY_26, false), g_panel.Poll(now++), g_hw.Next();
    }
    char     what[200];
    constexpr int kSlots = chompi::Panel::kSlots, kBank = chompi::Panel::kBankSlots;
    int      found[chompi::Panel::kPresetBanks] = {};
    double   level[kSlots] = {};
    for(int slot = 0; slot < kSlots; slot++) // (every bank's folder there is: presets/BANKn)
    {
        char file[64];
        std::snprintf(file, sizeof file, "../presets/BANK%d/P%02d.txt", slot / kBank + 1, slot % kBank + 1);
        FILE* f = std::fopen(file, "rb");
        if(!f)
            continue;
        found[slot / kBank]++;
        std::string text;
        char        buf[512];
        size_t      n;
        while((n = std::fread(buf, 1, sizeof buf, f)) > 0)
            text.append(buf, n);
        std::fclose(f);
        // its name, the line `name=` (the CHOMPI skips it, and `about=`)
        std::string name = "?";
        const size_t at = text.find("\nname=");
        if(at != std::string::npos)
            name = text.substr(at + 6, text.find('\n', at + 6) - at - 6);

        chompi::PresetState p  = chompi::Panel::Defaults();
        const bool          ok = chompi::ReadPreset(text.c_str(), p);
        g_panel.Loaded(slot, p, ok);
        std::vector<float> l, r;
        g_synth.AllNotesOff();
        Play(1500, l, r, now); // (the last one's tail, and the load)
        l.clear(), r.clear();

        // up a few notes, each held a while, then a chord held, let go
        const int notes[] = {48, 55, 60, 67};
        for(int note : notes)
        {
            g_synth.NoteOn(note, false, 0.75f);
            Play(550, l, r, now);
            g_synth.NoteOff(note, false);
            Play(150, l, r, now);
        }
        const int chord[] = {52, 59, 64};
        for(int note : chord)
            g_synth.NoteOn(note, false, 0.7f);
        Play(3000, l, r, now);
        for(int note : chord)
            g_synth.NoteOff(note, false);
        Play(2500, l, r, now);

        double peak = 0;
        bool   finite = true;
        for(size_t i = 0; i < l.size(); i++)
        {
            finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            peak   = fmax(peak, fmax(fabs(l[i]), fabs(r[i])));
        }
        // its loudest moment: the most of 300 ms windows over the single
        // notes (so a mono preset's alike: the chord's one note), at the
        // panel's own level (knob 6, -17 dB)
        double loud = 0;
        for(size_t w = 0; w + 14400 <= 4 * 700 * 48; w += 4800)
        {
            double we = 0;
            for(size_t i = w; i < w + 14400; i++)
                we += 0.5 * (l[i] * l[i] + r[i] * r[i]);
            loud = fmax(loud, we / 14400);
        }
        const double loud_db = 10 * log10(loud + 1e-20), peak_db = 20 * log10(peak + 1e-20);
        level[slot] = loud_db;

        // every line a name the CHOMPI knows (one it doesn't, it skips:
        // a typo would be silently the default): each among a preset's
        // own, as written
        char all[2048];
        chompi::WritePreset(chompi::Panel::Defaults(), all, sizeof all);
        std::string unknown;
        for(size_t q = text.find('\n'); q != std::string::npos && q + 1 < text.size(); q = text.find('\n', q + 1))
        {
            const size_t eq = text.find('=', q + 1), end = text.find('\n', q + 1);
            if(eq == std::string::npos || eq > end)
                continue;
            const std::string key = "\n" + text.substr(q + 1, eq - q - 1) + "=";
            if(key != "\nname=" && key != "\nabout=" && !std::strstr(all, key.c_str()))
                unknown += key.substr(1);
        }

        std::snprintf(what, sizeof what, "BANK%d/P%02d %-16s reads, at its loudest %.1f dB, peak %.1f dB%s%s", slot / kBank + 1, slot % kBank + 1,
                      name.c_str(), loud_db, peak_db, unknown.empty() ? "" : "; unknown: ", unknown.c_str());
        Check(ok && finite && loud_db > -60.0 && peak_db < -0.5 && unknown.empty(), what);
        std::snprintf(file, sizeof file, "out/preset_%d_%02d.wav", slot / kBank + 1, slot % kBank + 1);
        WriteWav(file, l, r, static_cast<uint32_t>(kSr));
    }
    // Browsing (the PRESET view): in (CHOMPI + high A#, tapped),
    // the wheel 15 detents on, bank 2's first slot, tried as it comes (it
    // asks for BANK2/P01); the sound it brings (FM, here) on while the view's
    // open; high A# tapped again, back to the sound it opened with; in
    // again and the same, but the wheel clicked: it stays.
    {
        using Sw = chompi::Hardware::SwId;
        auto poll = [&](int n = 1) {
            for(int i = 0; i < n; i++)
                g_panel.Poll(now++), g_hw.Next();
        };
        auto tap = [&](Sw sw) { g_hw.Set(sw, true), poll(), g_hw.Set(sw, false), poll(); };
        auto enter = [&]() { g_hw.Set(Sw::KEY_26, true), poll(), tap(Sw::KEY_25), g_hw.Set(Sw::KEY_26, false), poll(); };
        auto init  = [&]() { // (CHOMPI + high A# held a second: a String sound to start from)
            g_hw.Set(Sw::KEY_26, true), g_hw.Set(Sw::KEY_25, true), poll(1100);
            g_hw.Set(Sw::KEY_25, false), poll(), g_hw.Set(Sw::KEY_26, false), poll();
        };
        init();
        g_panel.SetSlotUsed(chompi::Panel::kBankSlots, true);
        chompi::PresetState fm = chompi::Panel::Defaults();
        fm.model               = static_cast<int>(phymo::Model::Fm);
        int asked[2];
        phymo::Model during[2], after[2];
        for(int click = 0; click < 2; click++)
        {
            enter();
            if(click) // (the wheel already there: away and back)
                g_hw.enc[4].inc = -1, poll(), g_hw.enc[4].inc = 1, poll();
            else
                for(int i = 0; i < chompi::Panel::kBankSlots; i++)
                    g_hw.enc[4].inc = 1, poll();
            asked[click] = g_panel.TakeLoad();
            g_panel.Loaded(asked[click], fm, true), poll(); // (the card's answer)
            during[click] = g_synth.GetModel();
            if(click)
                g_hw.enc[4].down = true, poll(), g_hw.enc[4].down = false, poll();
            else
                tap(Sw::KEY_25);
            after[click] = g_synth.GetModel();
            init();
        }
        const auto S = phymo::Model::String, F = phymo::Model::Fm;
        std::snprintf(what, sizeof what,
                      "browsing: 15 detents on, slot %d tried (BANK2/P01); left by high A#, the sound back (%s); by the "
                      "wheel's click, kept (%s)",
                      asked[0], after[0] == S ? "yes" : "no", after[1] == F ? "yes" : "no");
        Check(asked[0] == chompi::Panel::kBankSlots && asked[1] == chompi::Panel::kBankSlots && during[0] == F
                  && during[1] == F && after[0] == S && after[1] == F,
              what);
        // past a saved one onto an empty slot: the sound the view opened
        // with again (what a save there would keep), not the one tried;
        // and one still on its way when the wheel's moved on, dropped
        g_panel.SetSlotUsed(13, true);
        enter();
        for(int i = 0; i < 2 * chompi::Panel::kSlots; i++) // (to the first slot)
            g_hw.enc[4].inc = -1, poll();
        for(int i = 0; i < 13; i++)
            g_hw.enc[4].inc = 1, poll();
        const int tried = g_panel.TakeLoad();
        g_panel.Loaded(tried, fm, true), poll();
        const auto on_saved = g_synth.GetModel();
        g_hw.enc[4].inc = 1, poll(); // slot 15: empty
        const auto on_empty = g_synth.GetModel();
        g_hw.enc[4].inc = -1, poll(), g_hw.enc[4].inc = 1, poll(); // back to 14 and on to 15 before it's read
        g_panel.Loaded(g_panel.TakeLoad(), fm, true), poll();
        const auto late = g_synth.GetModel();
        tap(Sw::KEY_25);
        g_panel.SetSlotUsed(13, false);
        init(); // (as the tests after expect: a sound kept, loads landing)
        std::snprintf(what, sizeof what, "browsing: slot 14 tried (%s), slot 15 empty: the opening sound back (%s); a late one dropped (%s)",
                      on_saved == F ? "FM" : "not FM", on_empty == S ? "yes" : "no", late == S ? "yes" : "no");
        Check(tried == 13 && on_saved == F && on_empty == S && late == S, what);
    }

    // A version 4 file's bow strokes kept as long: its 3 s (0.6994, on 0.5 s
    // to 6 s) is 0.8358 on today's 0.04 s to 6 s; endless stays endless.
    {
        chompi::PresetState a = chompi::Panel::Defaults(), b = a;
        chompi::ReadPreset("phymo-preset 4\nbow_stroke=0.6994\n", a);
        chompi::ReadPreset("phymo-preset 4\nbow_stroke=1.0\n", b);
        std::snprintf(what, sizeof what, "an old file's bow strokes: 3 s read as %.4f (0.8358), endless as %.2f", a.alt[1][2],
                      b.alt[1][2]);
        Check(fabs(a.alt[1][2] - 0.8358f) < 0.002f && b.alt[1][2] >= 0.97f, what);
    }

    // Portamento off at its knob's least: init off; a file with it on and
    // a time, on (an old file's legato: always); a file from before (off,
    // a time), off.
    {
        using P = phymo::PortaMode;
        P got[3];
        chompi::PresetState a = chompi::Panel::Defaults(), b = a, c = a;
        b.porta = 2, b.knob[5][1] = 0.5f;
        c.porta = 0, c.knob[5][1] = 0.4f;
        const chompi::PresetState* in[3] = {&a, &b, &c};
        for(int i = 0; i < 3; i++)
        {
            g_panel.Loaded(0, *in[i], true);
            g_panel.Poll(now++), g_hw.Next();
            got[i] = g_synth.GetPortamento();
        }
        std::snprintf(what, sizeof what, "portamento: init %d, a file's legato %d, an old file's off %d (0 off, 1 always)",
                      static_cast<int>(got[0]), static_cast<int>(got[1]), static_cast<int>(got[2]));
        Check(got[0] == P::Off && got[1] == P::Always && got[2] == P::Off, what);
    }

    // The strum and cycle: through a file's text and back, and onto the
    // synth; an older file without them, none.
    {
        chompi::PresetState a = chompi::Panel::Defaults(), b = a, c = a;
        a.alt[5][1] = 0.4f, a.cycle = 1;
        char text[4096];
        chompi::WritePreset(a, text, sizeof text);
        const bool ok = chompi::ReadPreset(text, b);
        chompi::ReadPreset("phymo-preset 5\nring=0.5\n", c);
        g_panel.Loaded(0, b, true);
        g_panel.Poll(now++), g_hw.Next();
        const float strum = g_synth.GetStrum();
        const bool  cycle = g_synth.GetCycle();
        g_panel.Loaded(0, c, true);
        g_panel.Poll(now++), g_hw.Next();
        std::snprintf(what, sizeof what,
                      "strum, cycle: a file's %.2f, %d read back and played (%.2f, %d); an older file's %.2f, %d",
                      b.alt[5][1], b.cycle, strum, cycle, g_synth.GetStrum(), g_synth.GetCycle());
        Check(ok && fabs(b.alt[5][1] - 0.4f) < 1e-3f && b.cycle == 1 && fabs(strum - 0.4f) < 1e-3f && cycle
                  && g_synth.GetStrum() == 0.f && !g_synth.GetCycle(),
              what);
    }

    // The Vocal model, through the panel: CHOMPI + the wheel its ensemble
    // (no strike there to place), up from none.
    {
        using Sw = chompi::Hardware::SwId;
        chompi::PresetState v = chompi::Panel::Defaults();
        chompi::ReadPreset("phymo-preset 6\nmodel=4\nensemble=0.0\n", v);
        g_panel.Loaded(0, v, true);
        g_panel.Poll(now++), g_hw.Next();
        const float before = g_synth.GetEnsemble();
        g_hw.Set(Sw::KEY_26, true), g_panel.Poll(now++), g_hw.Next();
        for(int i = 0; i < 10; i++)
            g_hw.enc[4].inc = 1, g_panel.Poll(now++), g_hw.Next();
        g_hw.Set(Sw::KEY_26, false), g_panel.Poll(now++), g_hw.Next();
        std::snprintf(what, sizeof what, "Vocal: CHOMPI + the wheel, the ensemble %.2f to %.2f", before, g_synth.GetEnsemble());
        Check(before == 0.f && g_synth.GetEnsemble() > 0.05f, what);
    }

    // Version 5's Spectral tables, eight, to version 6's nine (the Reed in
    // after the Tine): each on its own table still; an FM's untouched.
    {
        chompi::PresetState a = chompi::Panel::Defaults(), b = a, c = a;
        chompi::ReadPreset("phymo-preset 5\nmodel=1\ntable=0.4286\n", a); // the Tine (3 of 0-7)
        chompi::ReadPreset("phymo-preset 5\nmodel=1\ntable=0.5714\n", b); // the Free bar (4)
        chompi::ReadPreset("phymo-preset 5\nmodel=2\ntable=0.5714\n", c); // FM
        std::snprintf(what, sizeof what, "v5 tables: Tine %.3f (0.375), Free bar %.3f (0.625), FM's kept %.3f (0.571)", a.knob[4][0],
                      b.knob[4][0], c.knob[4][0]);
        Check(fabs(a.knob[4][0] - 0.375f) < 1e-3f && fabs(b.knob[4][0] - 0.625f) < 1e-3f && fabs(c.knob[4][0] - 0.5714f) < 1e-3f,
              what);
    }

    // each bank there is full, and its presets as loud as each other, near
    // enough: each within 6 dB of the middle one
    for(int b = 0; b < chompi::Panel::kPresetBanks; b++)
    {
        if(!found[b])
            continue;
        std::vector<double> v(level + b * kBank, level + (b + 1) * kBank);
        std::sort(v.begin(), v.end());
        const double mid = v[kBank / 2];
        double       far = 0;
        for(int i = b * kBank; i < (b + 1) * kBank; i++)
            far = fmax(far, fabs(level[i] - mid));
        std::snprintf(what, sizeof what, "bank %d: %d presets, each within %.1f dB of the middle (%.1f dB)", b + 1, found[b], far,
                      mid);
        Check(found[b] == kBank && far < 6.0, what);
    }
    // Sequencer mode, through the panel: four steps entered; the LENGTH
    // page (CHOMPI + low F#): a key sets a new note's length, held 2 s
    // every step's (to the pattern's end), CHOMPI + a step key nothing; the
    // STRUM track (CHOMPI + low G#): a step key its strum; armed and
    // stopped, the wheel turned to a step plays it, and it ends.
    {
        using Sw = chompi::Hardware::SwId;
        auto  poll = [&](int n = 1) {
            for(int i = 0; i < n; i++)
                g_panel.Poll(now++), g_hw.Next();
        };
        auto tap    = [&](Sw sw) { g_hw.Set(sw, true), poll(), g_hw.Set(sw, false), poll(); };
        auto chompi = [&](Sw sw) { g_hw.Set(Sw::KEY_26, true), poll(), tap(sw), g_hw.Set(Sw::KEY_26, false), poll(); };
        g_hw.toggle = true;
        poll(5);
        auto& q = g_synth.Seq();
        q.pattern.Clear();
        q.SetLength(4);
        tap(Sw::KEY_28); // LOOP: armed, stopped: step entry from step 1
        tap(Sw::KEY_1), tap(Sw::KEY_2), tap(Sw::KEY_3), tap(Sw::KEY_4);
        const bool entered = q.pattern.step[3].count == 1 && q.pattern.step[0].gate == 24;
        chompi(Sw::KEY_18); // the LENGTH page
        tap(Sw::KEY_2);     // two steps
        const int def = q.DefaultGate();
        chompi(Sw::KEY_1);  // CHOMPI + step 1: not muted (the steps not in view)
        const bool kept = q.pattern.step[0].on;
        g_hw.Set(Sw::KEY_3, true), poll(1900);
        const bool early = q.pattern.step[0].gate == 24; // (not yet)
        poll(200), g_hw.Set(Sw::KEY_3, false), poll(); // three, held 2 s: every step
        const int g0 = q.pattern.step[0].gate, g2 = q.pattern.step[2].gate, g3 = q.pattern.step[3].gate;
        chompi(Sw::KEY_18); // out
        chompi(Sw::KEY_19); // the STRUM track
        tap(Sw::KEY_2);     // step 2 strummed
        const bool synced = g_synth.GetSeqSync();
        g_hw.enc[4].down = true, poll(), g_hw.enc[4].down = false, poll(); // the wheel's click: free
        const bool freed = !g_synth.GetSeqSync();
        g_hw.enc[4].down = true, poll(), g_hw.enc[4].down = false, poll(); // and synced again
        const bool sync_click = synced && freed && g_synth.GetSeqSync() && q.pattern.step[2].count > 0;
        tap(Sw::KEY_16);    // a black key: out
        const bool strum = q.pattern.step[1].strum && !q.pattern.step[0].strum;
        // (the STRUM track's an edit: its bank saved, two seconds on, the flag in it)
        static phymo::SeqPattern saved[chompi::kBankPatterns];
        g_panel.SetCardOk(true);
        {
            static phymo::SeqPattern drop[chompi::kBankPatterns];
            do // (what's waiting from before, saved and out of the way)
                poll(2600);
            while(g_panel.TakeBankSave(drop) >= 0);
            q.SetStrum(2, true);
            poll(2600);
        }
        const int  bank_saved = g_panel.TakeBankSave(saved);
        bool       in_file    = false;
        for(const auto& x : saved)
            in_file = in_file || x.step[2].strum;
        g_hw.enc[4].inc = -1; // the wheel: back to step 4 (armed, stopped: heard)
        poll();
        std::vector<float> l, r;
        auto seq_on = [&]() {
            int n = 0;
            for(int i = 0; i < phymo::Synth::kVoices; i++)
                n += g_synth.GetVoice(i).Gated() && g_synth.GetVoice(i).Root() >= phymo::Synth::kSeqRoot;
            return n;
        };
        const int heard = seq_on();
        Play(1200, l, r, now);
        const int after = seq_on();
        const int f = q.pattern.step[3].note[0];
        tap(Sw::KEY_5); // still armed: G into the step chosen (it had F), the next chosen after it
        const bool armed = q.pattern.step[3].note[0] == f + 2 && q.pattern.step[3].count == 1 && q.Selected() == 4;
        tap(Sw::KEY_28); // disarmed
        g_hw.toggle = false;
        poll(5);
        std::snprintf(what, sizeof what,
                      "sequencer: the LENGTH page's two steps %d (92); CHOMPI + step 1 kept%s; held 2 s%s, three: %d ... %d %d (140 92 24); "
                      "STRUM step 2%s, saved%s, sync click%s; the wheel's audition %d note, %d after; still armed%s",
                      def, kept ? "" : " NOT", early ? "" : " (too soon)", g0, g2, g3, strum ? "" : " NOT", bank_saved >= 0 && in_file ? "" : " NOT", sync_click ? "" : " NOT", heard, after, armed ? "" : " NOT");
        Check(entered && def == 92 && kept && early && g0 == 140 && g2 == 92 && g3 == 24 && strum && sync_click && bank_saved >= 0 && in_file
                  && heard == 1 && after == 0 && armed,
              what);
    }

    if(g_fails)
        std::printf("%d FAILED\n", g_fails);
    return g_fails ? 1 : 0;
}
