/** @file chompi_main.cpp
 *  @brief PHYMO firmware entry point: an 8-voice physical-modelling synth.
 *
 *  The synth is in ../../src (plain C++, also built on the desktop: host/).
 *   1. AudioCallback(): the audio interrupt, every block. Scans the
 *      controls, runs the panel, MIDI in, the synth, MIDI out (DIN).
 *   2. The main loop: LEDs, the SD card (storage.h), USB MIDI out, the
 *      battery; in DIAG builds, the CPU meter and PROFILE.txt on each save.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "util/CpuLoadMeter.h"
// the synth: built for speed (-O3)
#include "../../src/synth.h"
// the panel: built for size
#pragma GCC push_options
#pragma GCC optimize("Os")
#include "panel.h"
#include "storage.h"
#include "midi_io.h"
#include "crash_log.h"
#include <cstring>
#include <new>
#pragma GCC pop_options

using namespace daisy;
using namespace chompi;

Hardware     hw;
phymo::Synth  synth;
// the panel lives in D2 SRAM, leaving the main SRAM to the code; built at startup
alignas(32) uint8_t __attribute__((section(".d2_bss"))) panel_mem[sizeof(Panel)];
Panel&       panel = *reinterpret_cast<Panel*>(panel_mem);
CpuLoadMeter load_meter;
float        ticks_per_block_inv = 0.f; // inverse of a block's ticks (the load)
Storage      storage; // global: the card's DMA can't reach the stack (DTCM)
// the MIDI queues (76 KB) in D2 SRAM, cleared and built at startup
alignas(32) uint8_t __attribute__((section(".d2_bss"))) midi_mem[sizeof(MidiIo)];
MidiIo* midi = nullptr;
Options      options;

SdmmcHandler sdmmc;
// the SD driver invalidates the cache in 32-byte lines: give it its own lines
struct alignas(32) AlignedFs
{
    FatFSInterface fsi;
};
AlignedFs       fs_holder;
FatFSInterface& fsi = fs_holder.fsi;

// delay lines in SDRAM, the reverb tank in DTCM; InitSpace clears both
int16_t DSY_SDRAM_BSS                                   delay_mem[phymo::Space::kDelayWords];
uint16_t __attribute__((section(".dtcmram_bss")))       reverb_tank[phymo::Space::kTankWords];

// string delay lines (8 KB each, two a voice): the first few in DTCM, the
// rest in D2
constexpr int kFastStrings = 5;
float __attribute__((section(".dtcmram_bss"))) strings_fast[kFastStrings][phymo::StringRes::kLen];
float __attribute__((section(".d2_bss"))) strings_d2[2 * phymo::Synth::kVoices - kFastStrings][phymo::StringRes::kLen];

// --- profiling (DIAG builds): the cycle counter times each part of a
// block, and the stack depth is measured by painting it at startup ---
static uint32_t Cycles() { return DWT->CYCCNT; }
#ifdef PHYMO_DIAG
struct CallbackProfile
{
    uint64_t scan_sum = 0, panel_sum = 0, midi_sum = 0, total_sum = 0;
    uint32_t scan_worst = 0, panel_worst = 0, midi_worst = 0, total_worst = 0, blocks = 0;
} cb_prof;
static void Tally(uint64_t& sum, uint32_t& worst, uint32_t d) { sum += d, worst = d > worst ? d : worst; }
#endif
float cpu_mhz = 0.f; // measured at power-on (CpuMhz)
extern volatile uint32_t audio_blocks; // counted by each audio callback
extern uint32_t _edtcmram_bss; // end of DTCM statics: the stack's floor
#ifdef PHYMO_DIAG
static constexpr uint32_t kPaint = 0xa5a5a5a5u;
static void PaintStack()
{
    volatile uint32_t* p   = &_edtcmram_bss;
    volatile uint32_t* top = reinterpret_cast<uint32_t*>(__get_MSP() - 256);
    while(p < top)
        *p++ = kPaint;
}
static uint32_t StackUsed()
{
    const uint32_t* p = &_edtcmram_bss;
    while(*p == kPaint)
        p++;
    return 0x20020000u - reinterpret_cast<uint32_t>(p); // _estack
}
#endif
/** The CPU clock (MHz), timed against the audio over 200 blocks: a
 *  bootloader may leave SystemCoreClock wrong. Needs the audio running. */
static float CpuMhz()
{
    constexpr uint32_t kBlocks = 200;
    uint32_t           b       = audio_blocks;
    while(audio_blocks == b) {}
    b                 = audio_blocks;
    const uint32_t t0 = DWT->CYCCNT;
    while(audio_blocks - b < kBlocks) {}
    const uint32_t t       = DWT->CYCCNT - t0;
    const float    seconds = kBlocks * hw.seed.AudioBlockSize() / hw.seed.AudioSampleRate();
    return static_cast<float>(t) / seconds * 1e-6f;
}

#ifdef PHYMO_DIAG
/** The profile as text: each part's average and worst, in tenths of a
 *  percent of a block. */
static int Tenths(char* out, int size, float pct)
{
    const unsigned long t = static_cast<unsigned long>(pct * 10.f + 0.5f);
    return std::snprintf(out, size, "%3lu.%lu", t / 10, t % 10);
}
static int ProfileText(char* out, int size)
{
    const phymo::Profile  p   = synth.GetProfile();
    const CallbackProfile c   = cb_prof;
    const float           mhz = cpu_mhz > 1.f ? cpu_mhz : static_cast<float>(SystemCoreClock) * 1e-6f;
    const float           per = mhz * 1e6f * static_cast<float>(hw.seed.AudioBlockSize())
                      / hw.seed.AudioSampleRate(); // cycles per block
    const float           sb  = c.blocks ? 100.f / (per * static_cast<float>(c.blocks)) : 0.f;
    const float           pb  = p.blocks ? 100.f / (per * static_cast<float>(p.blocks)) : 0.f;
    static const char* const kName[phymo::Profile::kParts] = {"controls (seq, arp, LFO)", "voices", "body, vowel, tremolo",
                                                              "chorus", "delay, reverb", "output"};
    static const char* const kModel[5] = {"String", "Spectral", "FM", "Bowed", "Vocal"};
    int  n    = std::snprintf(out, size,
                          "PHYMO CPU profile: %% of a block's time (%lu cycles), average / worst\n"
                          "CPU clock: %lu MHz measured (SystemCoreClock says %lu)\n\n",
                          static_cast<unsigned long>(per), static_cast<unsigned long>(mhz + 0.5f),
                          static_cast<unsigned long>(SystemCoreClock / 1000000u));
    auto line = [&](const char* name, float avg, float worst) {
        n += std::snprintf(out + n, size - n, "%-26s", name);
        n += Tenths(out + n, size - n, avg);
        n += std::snprintf(out + n, size - n, " / ");
        n += Tenths(out + n, size - n, worst);
        n += std::snprintf(out + n, size - n, "\n");
    };
    line("whole block", sb * static_cast<float>(c.total_sum), 100.f * c.total_worst / per);
    line("keys, encoders (scan)", sb * static_cast<float>(c.scan_sum), 100.f * c.scan_worst / per);
    line("panel", sb * static_cast<float>(c.panel_sum), 100.f * c.panel_worst / per);
    line("MIDI in", sb * static_cast<float>(c.midi_sum), 100.f * c.midi_worst / per);
    for(int i = 0; i < phymo::Profile::kParts; i++)
    {
        line(kName[i], pb * static_cast<float>(p.sum[i]), 100.f * p.worst[i] / per);
        if(i == phymo::Profile::Space) // delay and reverb
        {
            line("  delay", pb * static_cast<float>(p.delay_sum), 100.f * p.delay_worst / per);
            line("  reverb", pb * static_cast<float>(p.reverb_sum), 100.f * p.reverb_worst / per);
        }
    }
    n += std::snprintf(out + n, size - n, "\nthe synth's worst block:");
    n += Tenths(out + n, size - n, 100.f * p.worst_block / per);
    n += std::snprintf(out + n, size - n, "%%, %d voices sounding, %s\none voice's worst:", p.worst_voices,
                       kModel[p.worst_model < 5 ? p.worst_model : 0]);
    n += Tenths(out + n, size - n, 100.f * p.worst_one / per);
    n += std::snprintf(out + n, size - n, "%% (note %d, %s)\nvoices sounding on average:", p.worst_one_note,
                       p.worst_one_struck ? "struck in that block" : "not struck: ringing");
    n += Tenths(out + n, size - n, p.blocks ? static_cast<float>(p.voices_sum) / p.blocks : 0.f);
    n += std::snprintf(out + n, size - n, "\nblocks: %lu\nstack used: %lu of %lu bytes\n", static_cast<unsigned long>(p.blocks),
                       static_cast<unsigned long>(StackUsed()),
                       static_cast<unsigned long>(0x20020000u - reinterpret_cast<uint32_t>(&_edtcmram_bss)));
    return n;
}

#endif
volatile bool running = false; // controls are left to main() until startup is done
volatile uint32_t audio_blocks = 0; // counted for the crash log and watchdog

/** Channels: out[0..1] headphones, out[2..3] main out, each left / right. */
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    Crash().blocks = ++audio_blocks;
    if(!running)
    {
        for(size_t i = 0; i < size; i++)
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        return;
    }
#ifdef PHYMO_DIAG
    load_meter.OnBlockStart();
#endif
    const uint32_t block_start = System::GetTick();
    const uint32_t c0          = Cycles();

    hw.ProcessAllControls();
    const uint32_t ca  = Cycles();
    const uint32_t now = System::GetNow();
    panel.Poll(now);
    const uint32_t cb = Cycles();
    midi->Poll(panel, synth, now);
    const uint32_t c1 = Cycles();

    synth.Process(out[0], out[1], size);
    static uint32_t played = 0;
    if(played < 1000 && ++played == 1)
        CrashStage(kStageFirstBlock);
    else if(played == 1000)
        CrashStage(kStageSettled);
    midi->Collect(panel, synth);
    // report this block's load so far to the synth's voice budget
    synth.ReportLoad(static_cast<float>(System::GetTick() - block_start) * ticks_per_block_inv);
    for(size_t i = 0; i < size; i++)
    {
        out[2][i] = out[0][i];
        out[3][i] = out[1][i];
    }
#ifdef PHYMO_DIAG
    {
        const uint32_t c2 = Cycles();
        Tally(cb_prof.scan_sum, cb_prof.scan_worst, ca - c0);
        Tally(cb_prof.panel_sum, cb_prof.panel_worst, cb - ca);
        Tally(cb_prof.midi_sum, cb_prof.midi_worst, c1 - cb);
        Tally(cb_prof.total_sum, cb_prof.total_worst, c2 - c0);
        cb_prof.blocks++;
    }
    load_meter.OnBlockEnd();
#else
    (void)c0, (void)ca, (void)cb, (void)c1;
#endif
}

// The hot loops' code, copied into the ITCM before anything runs it, a
// whole 64-bit word per volatile store (the ITCM's ECC needs full words).
extern uint64_t _siitcmdata, _sitcmram, _eitcmram;

#ifdef PHYMO_ITCM
static constexpr bool kItcm = true;
#else
static constexpr bool kItcm = false;
#endif

int main(void)
{
    CrashBegin(kItcm); // keep the last run's record, start this one's
    CrashHandlersOn();
    CrashWatchNull(); // watch for null-pointer writes
    if(kItcm)
        WatchdogStart(); // a hang resets and is logged
    {
        const volatile uint64_t* from = &_siitcmdata;
        volatile uint64_t*       to   = &_sitcmram;
        const volatile uint64_t* end  = &_eitcmram;
        while(to < end)
            *to++ = *from++;
    }
    __DSB();
    __ISB();
    CrashStage(kStageItcm);
    hw.Init();
    CrashStage(kStageHw);
    ItcmCheck(kStageHw);
    if(kItcm)
        ItcmProtect(); // writes to the code now fault
    memset(panel_mem, 0, sizeof(panel_mem));
    new(panel_mem) Panel;

    // clock the codecs at once, playing silence (unclocked, they make a loud
    // noise); the callback outputs silence until `running`
    hw.StartAudio(AudioCallback);
    CrashStage(kStageAudio);
    ItcmCheck(kStageAudio);

    hw.MpWrite(0x0c, 0B01010001); // BATT_LOW to 3 V
    hw.MpReadAll();
    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    {
 // each voice's own string first in fast memory
        float* strings[2 * phymo::Synth::kVoices];
        int    fast = 0, d2 = 0;
        for(int i = 0; i < 2 * phymo::Synth::kVoices; i++)
        {
            const bool own = i % 2 == 0;
            strings[i]     = own && fast < kFastStrings ? strings_fast[fast++] : strings_d2[d2++];
        }
        synth.SetStringMemory(strings);
    }
    synth.Init(hw.seed.AudioSampleRate());
    synth.InitSpace(reverb_tank, delay_mem);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // enable the cycle counter
    DWT->LAR    = 0xC5ACCE55;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    cpu_mhz = CpuMhz();
    synth.SetProfileClock(Cycles);
    synth.SetBlockTicks(cpu_mhz * 1e6f * static_cast<float>(hw.seed.AudioBlockSize()) / hw.seed.AudioSampleRate()); // the synth's room model works in these
#ifdef PHYMO_DIAG
    PaintStack();
#endif
    CrashStage(kStageSynth);
    ItcmCheck(kStageSynth);
    WatchdogFeed();

    // SD card: presets and MIDI options
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::VERY_FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);
    System::Delay(100);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    System::Delay(100);
    storage.Mount(fsi);
    CrashStage(kStageCard);
    ItcmCheck(kStageCard);
#ifdef PHYMO_DIAG
    if(LastRunToReport())
    {
        static char log[1024];
        const int n = CrashText(log, sizeof log);
        if(n > 0 && storage.WriteText("CRASH.txt", log, static_cast<size_t>(n)))
            LastRunToReport() = false;
    }
#endif
    WatchdogFeed();

    LedSetup();
    panel.Init(&hw, &synth);
    storage.Load(options, panel);
    memset(midi_mem, 0, sizeof(midi_mem));
    midi = new(midi_mem) MidiIo;
    midi->Init(options);
    panel.BootAnimation();
    CrashStage(kStagePanel);
    ItcmCheck(kStagePanel);
    WatchdogFeed();

    // clear the shift registers (0.5 s); CHOMPI + PLAY + LOOP at power-on:
    // shipping mode
    uint32_t sleep_state = 0;
    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        WatchdogFeed();
        sleep_state += hw.button_sr.State(int(Sw::KEY_26)) && hw.button_sr.State(int(Sw::KEY_27))
                       && hw.button_sr.State(int(Sw::KEY_28));
        System::DelayUs(100);
    }
    if(sleep_state > 4000)
        hw.MpWrite(0x08, 0B10111111); // SHIPPING MODE
    // clear edges collected during the wait
    for(int sw = 0; sw < 40; sw++)
    {
        hw.button_sr.RisingEdge(sw);
        hw.button_sr.FallingEdge(sw);
    }

    hw.usb_sw.Write(false);       // give USB control
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    System::Delay(1);
    hw.usb_sw.Write(true);        // take USB control

#ifdef PHYMO_DIAG
    load_meter.Init(hw.seed.AudioSampleRate(), static_cast<int>(hw.seed.AudioBlockSize()));
#endif
    ticks_per_block_inv = 1.f / (static_cast<float>(System::GetTickFreq()) * static_cast<float>(hw.seed.AudioBlockSize())
                                 / hw.seed.AudioSampleRate());
    ItcmCheck(kStageRunning);
    CrashStage(kStageRunning);
    running = true;

    uint32_t last_draw = 0, last_batt = 0, last_load = 0, fed_at = 0;
    while(1)
    {
        const uint32_t now = System::GetNow();
        if(audio_blocks != fed_at) // only while the audio runs
            fed_at = audio_blocks, WatchdogFeed();

        if(storage.Service(panel)) // presets saved, deleted, loaded
        {
#ifdef PHYMO_DIAG
            static char text[1024];
            const int   n = ProfileText(text, sizeof text);
            if(n > 0)
                storage.WriteText("PROFILE.txt", text, static_cast<size_t>(n));
            synth.ResetProfile();
            cb_prof = CallbackProfile();
#endif
        }
        midi->PumpUsb();

        if(now - last_draw >= 16)
        {
            last_draw = now;
            panel.Draw(now);
        }

        if(now - last_load >= 100)
        {
            last_load = now;
#ifdef PHYMO_DIAG
            panel.SetLoad(load_meter.GetAvgCpuLoad(), load_meter.GetMaxCpuLoad());
            load_meter.Reset();
#endif
            ItcmCheck(static_cast<Stage>(Crash().stage));
        }

        if(now - last_batt > 20)
        {
            last_batt = now;
            hw.LowBatteryLockoutCheck();
        }
    }
}
