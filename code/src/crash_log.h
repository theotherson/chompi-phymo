/** @file crash_log.h
 *  @brief A crash record in the backup SRAM, surviving a reset: startup
 *  stage, audio blocks run, and on a fault the fault registers and stacked
 *  pc / lr. A fault records and resets; with the watchdog on, so does a
 *  hang. DIAG builds write the last record to /PHYMO/CRASH.txt at startup.
 *
 *  Look up the pc with `arm-none-eabi-addr2line -fe CHOMPI.elf <pc>`.
 */
#pragma once
#include "daisy_seed.h"
#include <cstdio>

/** The ITCM's code and its image (chompi_sram.lds). */
extern uint64_t _siitcmdata, _sitcmram, _eitcmram;

namespace chompi
{

/** Startup stages. */
enum Stage : uint32_t
{
    kStageMain = 1,   // main() entered
    kStageItcm,       // the hot loops copied to the ITCM
    kStageHw,         // the hardware set up
    kStageAudio,      // the audio started (silent)
    kStageSynth,      // the synth set up
    kStageCard,       // the card mounted
    kStagePanel,      // the panel set up, the presets read
    kStageRunning,    // the main loop running, the synth playing
    kStageFirstBlock, // the first block the synth played
    kStageSettled,    // 1000 blocks played: none of the above to report
};

struct CrashRecord
{
    uint32_t magic;  // kAlive, kFaulted, or anything else: none
    uint32_t stage;  // Stage
    uint32_t itcm; // 1: the ITCM build
    uint32_t blocks; // audio blocks run
    uint32_t vector; // 3 hard, 4 memory, 5 bus, 6 usage
    uint32_t cfsr, hfsr, mmfar, bfar, afsr;
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr, sp, exc_return;
    uint32_t itcm_stage; // stage the ITCM's code was found changed (0: never)
    uint32_t itcm_at, itcm_words; // offset and count of changed words
    uint32_t itcm_got, itcm_want; // the first changed word, and the expected value
    uint32_t null_hits; // writes to the ITCM's first 1 KB
    uint32_t null_stage, null_pc, null_lr; // the first one's stage, pc and lr
};

static constexpr uint32_t kAlive   = 0x50484d41; // "PHMA"
static constexpr uint32_t kFaulted = 0x50484d46; // "PHMF"

/** In the backup SRAM, after libDaisy's boot_info. */
inline volatile CrashRecord& Crash() { return *reinterpret_cast<volatile CrashRecord*>(0x38800100U); }

/** The previous run's record, read at startup. */
inline CrashRecord& LastRun()
{
    static CrashRecord r;
    return r;
}
inline bool& LastRunToReport()
{
    static bool b = false;
    return b;
}

/** First thing in main(): keeps the previous run's record if it's worth
 *  reporting, and starts this run's. */
inline void CrashBegin(bool itcm)
{
    RCC->AHB4ENR |= RCC_AHB4ENR_BKPRAMEN; // backup SRAM clock and write access
    PWR->CR1 |= PWR_CR1_DBP;
    while(!(PWR->CR1 & PWR_CR1_DBP)) {}
    const bool dog = RCC->RSR & RCC_RSR_IWDG1RSTF;
    RCC->RSR |= RCC_RSR_RMVF;

    volatile CrashRecord& c = Crash();
    CrashRecord&          l = LastRun();
    const volatile uint32_t* from = &c.magic;
    uint32_t*                to   = &l.magic;
    for(size_t i = 0; i < sizeof(CrashRecord) / 4; i++)
        to[i] = from[i];
    LastRunToReport() = l.magic == kFaulted || (l.magic == kAlive && (l.stage < kStageSettled || dog))
                        || ((l.magic == kAlive || l.magic == kFaulted) && (l.itcm_stage || l.null_hits));
    if(dog && l.magic == kAlive)
        l.vector = 0xd06; // the watchdog's

    c.magic  = kAlive;
    c.stage  = kStageMain;
    c.itcm   = itcm ? 1 : 0;
    c.blocks = 0;
    c.vector = 0;
    c.itcm_stage = c.itcm_at = c.itcm_words = c.itcm_got = c.itcm_want = 0;
    c.null_hits = c.null_stage = c.null_pc = c.null_lr = 0;
    __DSB();
}

/** Checks the ITCM against its image; records the first change with `s`. */
inline void ItcmCheck(Stage s)
{
    volatile CrashRecord& c = Crash();
    if(c.itcm_stage)
        return;
    const volatile uint32_t* code  = reinterpret_cast<const volatile uint32_t*>(&_sitcmram);
    const volatile uint32_t* image = reinterpret_cast<const volatile uint32_t*>(&_siitcmdata);
    const uint32_t           n     = 2 * static_cast<uint32_t>(&_eitcmram - &_sitcmram);
    uint32_t                 bad = 0, first = 0;
    for(uint32_t i = 0; i < n; i++)
        if(code[i] != image[i] && bad++ == 0)
            first = i;
    if(bad)
    {
        c.itcm_at    = first * 4;
        c.itcm_words = bad;
        c.itcm_got   = code[first];
        c.itcm_want  = image[first];
        c.itcm_stage = s;
        __DSB();
    }
}

/** A DWT watchpoint on writes to 0x000-0x3ff (the ITCM's empty first 1 KB):
 *  each counted, the first's writer recorded. */
inline void CrashWatchNull()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk | CoreDebug_DEMCR_MON_EN_Msk;
    DWT->COMP0     = 0x00000000;
    DWT->MASK0     = 10; // ignore the low 10 bits: 1 KB
    DWT->FUNCTION0 = 0x6; // watch writes
    __DSB();
}

/** Makes the ITCM read-only past its first 8 KB (MPU), so a write faults. */
inline void ItcmProtect()
{
    MPU_Region_InitTypeDef r = {};
    HAL_MPU_Disable();
    r.Enable           = MPU_REGION_ENABLE;
    r.Number           = MPU_REGION_NUMBER3;
    r.BaseAddress      = 0x00000000;
    r.Size             = MPU_REGION_SIZE_64KB;
    r.AccessPermission = MPU_REGION_PRIV_RO_URO;
    r.TypeExtField     = MPU_TEX_LEVEL1; // normal memory, not cached
    r.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
    r.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
    r.SubRegionDisable = 0x01; // first 8 KB writable (the watchpoint logs those)
    r.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

inline void CrashStage(Stage s)
{
    Crash().stage = s;
    __DSB();
}

/** The previous run's record as text into `buf`; returns its length. */
inline int CrashText(char* buf, size_t size)
{
    const CrashRecord& l = LastRun();
    static const char* const kStageName[] = {"?",          "main",         "ITCM copy",   "hardware", "audio start",
                                             "synth init", "card",         "panel",       "main loop",
                                             "first block", "settled"};
    const char* what = l.magic == kFaulted ? (l.vector == 3   ? "a hard fault"
                                              : l.vector == 4 ? "a memory fault"
                                              : l.vector == 5 ? "a bus fault"
                                              : l.vector == 6 ? "a usage fault"
                                                              : "a fault")
                       : l.vector == 0xd06 ? "the watchdog (stuck for 8 s)"
                                           : "stopped without a fault (reset or power during startup)";
    const unsigned st = l.stage <= kStageSettled ? l.stage : 0;
    return std::snprintf(buf, size,
                         "PHYMO crash log\n"
                         "build: %s\n"
                         "what: %s\n"
                         "last stage: %u (%s)\n"
                         "audio blocks: %lu\n"
                         "pc=0x%08lx lr=0x%08lx sp=0x%08lx xpsr=0x%08lx exc_return=0x%08lx\n"
                         "cfsr=0x%08lx hfsr=0x%08lx mmfar=0x%08lx bfar=0x%08lx afsr=0x%08lx\n"
                         "r0=0x%08lx r1=0x%08lx r2=0x%08lx r3=0x%08lx r12=0x%08lx\n"
                         "itcm changed: found at stage %lu (0 never), first at +0x%04lx (0x%08lx, should be 0x%08lx), %lu words\n"
                         "null-guard writes: %lu, the first at stage %lu, pc=0x%08lx lr=0x%08lx\n",
                         l.itcm ? "ITCM" : "no ITCM", what, st, kStageName[st], (unsigned long)l.blocks,
                         (unsigned long)l.pc, (unsigned long)l.lr, (unsigned long)l.sp, (unsigned long)l.xpsr,
                         (unsigned long)l.exc_return, (unsigned long)l.cfsr, (unsigned long)l.hfsr,
                         (unsigned long)l.mmfar, (unsigned long)l.bfar, (unsigned long)l.afsr, (unsigned long)l.r0,
                         (unsigned long)l.r1, (unsigned long)l.r2, (unsigned long)l.r3, (unsigned long)l.r12,
                         (unsigned long)l.itcm_stage, (unsigned long)l.itcm_at, (unsigned long)l.itcm_got,
                         (unsigned long)l.itcm_want, (unsigned long)l.itcm_words, (unsigned long)l.null_hits,
                         (unsigned long)l.null_stage, (unsigned long)l.null_pc, (unsigned long)l.null_lr);
}

/** The independent watchdog, ~8 s, fed while the audio runs. */
inline void WatchdogStart()
{
    IWDG1->KR  = 0xCCCC;
    IWDG1->KR  = 0x5555;
    IWDG1->PR  = 6;
    IWDG1->RLR = 1000;
    while(IWDG1->SR) {}
    IWDG1->KR = 0xAAAA;
}
inline void WatchdogFeed() { IWDG1->KR = 0xAAAA; }

} // namespace chompi

/** A fault: records the stacked frame and fault registers, then resets. */
extern "C" void CrashRecordFault(const uint32_t* frame, uint32_t exc_return)
{
    volatile chompi::CrashRecord& c = chompi::Crash();
    c.vector     = SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk;
    c.cfsr       = SCB->CFSR;
    c.hfsr       = SCB->HFSR;
    c.mmfar      = SCB->MMFAR;
    c.bfar       = SCB->BFAR;
    c.afsr       = SCB->AFSR;
    c.r0         = frame[0];
    c.r1         = frame[1];
    c.r2         = frame[2];
    c.r3         = frame[3];
    c.r12        = frame[4];
    c.lr         = frame[5];
    c.pc         = frame[6];
    c.xpsr       = frame[7];
    c.sp         = reinterpret_cast<uint32_t>(frame);
    c.exc_return = exc_return;
    c.magic      = chompi::kFaulted;
    __DSB();
    NVIC_SystemReset();
}

/** The watchpoint hit: counted, the first's frame kept. */
extern "C" void CrashWatchHit(const uint32_t* frame)
{
    volatile chompi::CrashRecord& c = chompi::Crash();
    if(c.null_hits++ == 0)
    {
        c.null_stage = c.stage;
        c.null_pc    = frame[6];
        c.null_lr    = frame[5];
    }
    (void)DWT->FUNCTION0; // reading it clears MATCHED
    __DSB();
}
extern "C" __attribute__((naked)) void PhymoWatch()
{
    __asm volatile("tst lr, #4      \n"
                   "ite eq          \n"
                   "mrseq r0, msp   \n"
                   "mrsne r0, psp   \n"
                   "push {r4, lr}   \n"
                   "bl CrashWatchHit \n"
                   "pop {r4, pc}    \n");
}

/** Fault handler: passes the stack and return code to CrashRecordFault. */
extern "C" __attribute__((naked)) void PhymoFault()
{
    __asm volatile("tst lr, #4      \n"
                   "ite eq          \n"
                   "mrseq r0, msp   \n"
                   "mrsne r0, psp   \n"
                   "mov r1, lr      \n"
                   "b CrashRecordFault \n");
}

namespace chompi
{
/** Installs the fault handlers in the vector table (in SRAM_EXEC), before
 *  the caches are on. */
inline void CrashHandlersOn()
{
    volatile uint32_t* const table = reinterpret_cast<volatile uint32_t*>(0x24000000U);
    for(int v = 3; v <= 6; v++) // hard, memory, bus, usage
        table[v] = reinterpret_cast<uint32_t>(&PhymoFault);
    table[12] = reinterpret_cast<uint32_t>(&PhymoWatch); // debug monitor
    SCB->VTOR = 0x24000000U;
    __DSB();
    __ISB();
}
} // namespace chompi
