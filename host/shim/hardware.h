// The desktop's stand-in for code/src/hardware.h, so panel.h builds off the
// CHOMPI (preset_test): nothing down or turned unless a test sets it (each
// Poll's edges and turns, then Next() for the next), the toggle up. Its
// switches' names are the real file's (gen/swid.h, copied from it by the
// Makefile).
#pragma once
#include <cstdint>

namespace daisy
{
struct System
{
    static uint32_t GetNow() { return 0; }
    static void     Delay(uint32_t) {}
};
} // namespace daisy

namespace chompi
{
class Hardware
{
  public:
#include "swid.h"

    struct Switches
    {
        bool down[40] = {}, was[40] = {};
        bool State(int sw) const { return down[sw]; }
        bool RisingEdge(int sw) const { return down[sw] && !was[sw]; }
        bool FallingEdge(int sw) const { return !down[sw] && was[sw]; }
    };
    struct Encoder
    {
        int32_t inc = 0;
        bool    down = false, was = false;
        int32_t Increment() const { return inc; }
        bool    RisingEdge() const { return down && !was; }
        bool    FallingEdge() const { return !down && was; }
        bool    Pressed() const { return down; }
    };

    bool     toggle = false; // the toggle: down, sequencer mode
    bool     GetToggleState() const { return toggle; }
    Switches button_sr;
    Encoder  enc[6];

    /** A switch down or up from the next Poll on. */
    void Set(SwId sw, bool down) { button_sr.down[static_cast<int>(sw)] = down; }
    /** After a Poll: its edges and turns gone. */
    void Next()
    {
        for(int i = 0; i < 40; i++)
            button_sr.was[i] = button_sr.down[i];
        for(auto& e : enc)
            e.inc = 0, e.was = e.down;
    }
};
} // namespace chompi
