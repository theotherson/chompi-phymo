// A quick listen: PHYMO's engine plays a few bars into out/render.wav.
//   make -C host render && (cd host && ./render)
#include "../src/synth.h"
#include "wav.h"
#include <vector>

int main()
{
    phymo::Synth s;
    s.Init(48000.f);
    s.SetVolume(0.8f);
    s.SetDecay(4.f), s.SetDamper(0.4f), s.SetKeyTrack(0.5f);
    s.SetType(0.6f), s.SetHardness(0.6f), s.SetNoise(0.15f);

    // an arpeggio up and a chord, each note an eighth at 100 BPM
    const int notes[] = {45, 52, 57, 60, 64, 69, 72, 76};
    std::vector<float> l, r;
    float              lb[48], rb[48];
    for(int bar = 0; bar < 2; bar++)
        for(int n : notes)
        {
            s.SetBrightness(bar == 0 ? 0.35f : 0.75f);
            s.NoteOn(n);
            for(int b = 0; b < 300; b++) // 300 ms
            {
                if(b == 250)
                    s.NoteOff(n);
                s.Process(lb, rb, 48);
                l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
            }
        }
    for(int b = 0; b < 2000; b++)
    {
        s.Process(lb, rb, 48);
        l.insert(l.end(), lb, lb + 48), r.insert(r.end(), rb, rb + 48);
    }
    WriteWav("out/render.wav", l, r, 48000);
    return 0;
}
