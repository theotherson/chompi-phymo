/** @file storage.h
 *  @brief Files on the SD card, in /PHYMO:
 *
 *    BANK1/ ... BANK8/     preset banks, P01.txt ... P15.txt each (preset.h)
 *    SEQ1.txt ... SEQ3.txt sequencer banks of 15 patterns (preset.h)
 *    options.txt           MIDI channels, created with defaults if missing:
 *                            midi_in=1
 *                            midi_out=1
 *
 *  Older presets (Pnn.txt in /PHYMO) are moved into the banks at startup,
 *  never over existing ones. Writes go to a temporary file renamed over the
 *  old one, so a power cut never leaves half a file. Main loop only. Must be
 *  a global: the card's DMA can't reach the stack (DTCM).
 */
#pragma once
#include "fatfs.h"
#include "panel.h"
#include "preset.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace chompi
{

struct Options
{
    int midi_in = 1, midi_out = 1; // channels, 1-16
    int protect = 0; // pattern write protect
};

class Storage
{
  public:
    /** @return true if the card is there and /PHYMO is the working folder */
    bool Mount(FatFSInterface& fsi)
    {
        ok_ = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
        if(ok_ && f_chdir("/PHYMO") != FR_OK)
        {
            f_mkdir("/PHYMO");
            ok_ = f_chdir("/PHYMO") == FR_OK;
        }
        return ok_;
    }

    bool Ok() const { return ok_; }

    /** Loads the options and which slots hold presets (startup). */
    void Load(Options& o, Panel& panel)
    {
        panel.SetCardOk(ok_);
        opts_ = &o;
        if(!ok_)
            return;
        if(Read("options.txt"))
            ReadOptions(o);
        else
            SaveOptions(o);
        panel.SetProtect(o.protect != 0);
        Banks();
        for(int i = 0; i < Panel::kSlots; i++)
        {
            FILINFO info;
            panel.SetSlotUsed(i, f_stat(SlotName(i), &info) == FR_OK);
        }
        for(int b = 0; b < 3; b++)
            if(Read(BankName(b)))
                ReadBank(buf_, panel.Bank(b));
        panel.BanksLoaded();
    }

    /** The panel's saves, deletes and loads (main loop). True if a preset was saved. */
    bool Service(Panel& panel)
    {
        PresetState s;
        int         slot  = panel.TakeSave(s);
        bool        saved = false;
        if(slot >= 0)
        {
            const size_t n = WritePreset(s, buf_, sizeof buf_);
            saved          = ok_ && n > 0 && Write(SlotName(slot), n);
            panel.SaveDone(slot, saved);
        }
        slot = panel.TakeDelete();
        if(slot >= 0)
        {
            const FRESULT r = ok_ ? f_unlink(SlotName(slot)) : FR_NOT_READY;
            panel.DeleteDone(slot, r == FR_OK || r == FR_NO_FILE);
        }
        static phymo::SeqPattern bank[kBankPatterns]; // not on the stack (DTCM)
        const int               b = panel.TakeBankSave(bank);
        if(b >= 0)
        {
            const size_t n = WriteBank(bank, buf_, sizeof buf_);
            panel.BankSaved(ok_ && n > 0 && Write(BankName(b), n));
        }
        int protect;
        if(panel.TakeProtect(protect) && opts_)
        {
            opts_->protect = protect;
            if(ok_)
                SaveOptions(*opts_);
        }
        slot = panel.TakeLoad();
        if(slot >= 0)
        {
            PresetState p = Panel::Defaults();
            const bool  ok = ok_ && Read(SlotName(slot)) && ReadPreset(buf_, p);
            panel.Loaded(slot, p, ok);
        }
        return saved;
    }

    /** Writes `text` (`n` bytes) as file `name`. */
    bool WriteText(const char* name, const char* text, size_t n)
    {
        if(!ok_ || n == 0 || n > sizeof buf_)
            return false;
        std::memcpy(buf_, text, n);
        return Write(name, n);
    }

  private:
    /** Slot `slot`'s file (0 to kSlots - 1, bank by bank): BANKn/Pnn.txt. */
    static const char* SlotName(int slot)
    {
        static char name[24];
        std::snprintf(name, sizeof name, "BANK%d/P%02d.txt", slot / Panel::kBankSlots + 1,
                      slot % Panel::kBankSlots + 1);
        return name;
    }

    /** Makes the bank folders and moves older presets into them, never over
     *  existing ones. */
    void Banks()
    {
        char dir[8];
        for(int b = 0; b < Panel::kPresetBanks; b++)
        {
            std::snprintf(dir, sizeof dir, "BANK%d", b + 1);
            f_mkdir(dir); // FR_EXIST once it's there
        }
        char    old[12];
        FILINFO info;
        for(int i = 0; i < Panel::kSlots; i++)
        {
            std::snprintf(old, sizeof old, "P%02d.txt", i + 1);
            if(f_stat(old, &info) == FR_OK && f_stat(SlotName(i), &info) != FR_OK)
                f_rename(old, SlotName(i));
        }
    }

    static const char* BankName(int b)
    {
        static const char* kNames[3] = {"SEQ1.txt", "SEQ2.txt", "SEQ3.txt"};
        return kNames[b];
    }

    void ReadOptions(Options& o)
    {
        auto get = [&](const char* key, int& out) {
            const char* p = std::strstr(buf_, key);
            if(p)
            {
                const int v = std::atoi(p + std::strlen(key));
                if(v >= 1 && v <= 16)
                    out = v;
            }
        };
        get("midi_in=", o.midi_in);
        get("midi_out=", o.midi_out);
        const char* p = std::strstr(buf_, "protect=");
        if(p)
            o.protect = std::atoi(p + 8) != 0;
    }

    bool SaveOptions(const Options& o)
    {
        const int n = std::snprintf(buf_, sizeof buf_, "midi_in=%d\nmidi_out=%d\nprotect=%d\n", o.midi_in,
                                    o.midi_out, o.protect);
        return n > 0 && Write("options.txt", static_cast<size_t>(n));
    }

    /** A whole file into buf_ (0-terminated); false if it can't. */
    bool Read(const char* name)
    {
        if(f_open(&file_.fil, name, FA_READ) != FR_OK)
            return false;
        UINT n = 0;
        f_read(&file_.fil, buf_, sizeof(buf_) - 1, &n);
        f_close(&file_.fil);
        buf_[n] = 0;
        return n > 0;
    }

    /** Writes buf_'s first `len` bytes as `name` via a temporary file. */
    bool Write(const char* name, size_t len)
    {
        if(f_open(&file_.fil, "save.tmp", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            return false;
        UINT n = 0;
        f_write(&file_.fil, buf_, static_cast<UINT>(len), &n);
        const bool done = f_close(&file_.fil) == FR_OK && n == len;
        if(!done)
            return false;
        f_unlink(name);
        return f_rename("save.tmp", name) == FR_OK;
    }

    // the SD driver DMAs into these and invalidates the cache in 32-byte
    // lines, so each owns its lines
    struct alignas(32) File
    {
        FIL fil;
    };
    Options*         opts_ = nullptr;
    alignas(32) char buf_[16384]; // a full bank of patterns: up to ~15 KB
    File             file_;
    bool             ok_ = false;
};

} // namespace chompi
