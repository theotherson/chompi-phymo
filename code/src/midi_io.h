/** @file midi_io.h
 *  @brief MIDI in and out over DIN (UART) and USB, on the channels in
 *  options.txt (1 and 1 by default).
 *
 *  In (Poll, audio interrupt, before the synth): notes, pitch bend, CCs to
 *  panel values (Panel::MidiCc), and clock / start / stop when sync is on
 *  (no clock for half a second returns the arp to its own rate).
 *  Out (Collect, audio interrupt, after the synth): played notes, clock and
 *  start / stop while running on the internal tempo, and turned knobs' CCs.
 *  DIN goes out by DMA; USB from the main loop (PumpUsb).
 */
#pragma once
#include "daisy_seed.h"
#include "panel.h"
#include "storage.h"
#include "../../src/synth.h"

namespace chompi
{

class MidiIo
{
  public:
    void Init(const Options& o)
    {
        ch_in_  = o.midi_in - 1;
        ch_out_ = o.midi_out - 1;

        MidiUartHandler::Config uart_cfg;
        uart_.Init(uart_cfg);
        uart_.StartReceive();
        uart_handle_ = uart_.GetMutableTransport().GetUartHandle();

        MidiUsbHandler::Config usb_cfg;
        usb_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
        usb_.Init(usb_cfg);
        usb_.Listen();
    }

    /** Audio interrupt: incoming MIDI. */
    void Poll(Panel& panel, phymo::Synth& synth, uint32_t now)
    {
        uart_.Listen();
        while(uart_.HasEvents())
            Handle(panel, synth, uart_.PopEvent(), now);
        while(usb_.HasEvents())
            Handle(panel, synth, usb_.PopEvent(), now);
            // clock stopped (or sync off): back to the internal rate
        if(synth.GetExternalClock() && (!panel.SyncOk() || now - last_clock_ > 500))
            synth.SetExternalClock(false);
    }

    /** Audio interrupt: what the synth played and the panel's CCs, queued
     *  out; DIN started. */
    void Collect(Panel& panel, phymo::Synth& synth)
    {
        phymo::OutEvent e;
        while(synth.PopEvent(e))
        {
            uint8_t m[3];
            int     len = 1;
            switch(e.type)
            {
                case phymo::OutEvent::NoteOn: m[0] = 0x90 | ch_out_, m[1] = e.note, m[2] = 100, len = 3; break;
                case phymo::OutEvent::NoteOff: m[0] = 0x80 | ch_out_, m[1] = e.note, m[2] = 0, len = 3; break;
                case phymo::OutEvent::Clock: m[0] = 0xF8; break;
                case phymo::OutEvent::Start: m[0] = 0xFA; break;
                case phymo::OutEvent::Stop: m[0] = 0xFC; break;
            }
            Queue(m, len);
        }
        uint8_t cc, value;
        while(panel.PopCc(cc, value))
        {
            const uint8_t m[3] = {static_cast<uint8_t>(0xB0 | ch_out_), cc, value};
            Queue(m, 3);
        }
        if(!busy_ && head_ != tail_)
            StartDma();
    }

    /** Main loop: queued USB messages out. */
    void PumpUsb()
    {
        while(usb_tail_ != usb_head_)
        {
            const Entry& e = usb_q_[usb_tail_];
            usb_.SendMessage(const_cast<uint8_t*>(e.data), e.len);
            usb_tail_ = (usb_tail_ + 1) % kFifo;
        }
    }

  private:
    void Handle(Panel& panel, phymo::Synth& synth, const MidiEvent& ev, uint32_t now)
    {
        if(ev.type == SystemRealTime)
        {
            if(!panel.SyncOk())
                return;
            switch(ev.srt_type)
            {
                case TimingClock:
                    if(!synth.GetExternalClock())
                        synth.SetExternalClock(true);
                    last_clock_ = now;
                    synth.ClockTick();
                    break;
                case Start:
                    synth.SetExternalClock(true);
                    last_clock_ = now;
                    synth.ClockStart();
                    break;
                case Stop: synth.ClockStop(); break;
                case Continue: synth.ClockContinue(); break;
                default: break;
            }
            return;
        }
        if(ev.channel != ch_in_)
            return;
        switch(ev.type)
        {
            case NoteOn:
                if(ev.data[1] == 0)
                    synth.NoteOff(ev.data[0], false);
                else
                    synth.NoteOn(ev.data[0], false, ev.data[1] / 127.f);
                break;
            case NoteOff: synth.NoteOff(ev.data[0], false); break;
            case ControlChange: panel.MidiCc(ev.data[0], ev.data[1]); break;
            case PitchBend:
            {
                const int v = (ev.data[1] << 7 | ev.data[0]) - 8192;
                synth.SetBend(v / 8192.f);
                break;
            }
            default: break;
        }
    }

    /** One message into both outs' queues (dropped if full). */
    void Queue(const uint8_t* m, int len)
    {
        if(((head_ + 1) % kFifo) != tail_)
        {
            Entry& e = fifo_[head_];
            for(int i = 0; i < len; i++)
                e.data[i] = m[i];
            e.len = static_cast<uint8_t>(len);
            head_ = (head_ + 1) % kFifo;
        }
        if(((usb_head_ + 1) % kFifo) != usb_tail_)
        {
            Entry& e = usb_q_[usb_head_];
            for(int i = 0; i < len; i++)
                e.data[i] = m[i];
            e.len     = static_cast<uint8_t>(len);
            usb_head_ = (usb_head_ + 1) % kFifo;
        }
    }

    /** Interrupts are either masked or this is the DMA's own end interrupt. */
    void StartDma()
    {
        busy_          = true;
        Entry&    e    = fifo_[tail_];
        uintptr_t from = reinterpret_cast<uintptr_t>(e.data) & ~static_cast<uintptr_t>(31);
        SCB_CleanDCache_by_Addr(reinterpret_cast<uint32_t*>(from), 32);
        uart_handle_.DmaTransmit(e.data, e.len, nullptr, TxEnd, this);
    }

    static void TxEnd(void* ctx, UartHandler::Result)
    {
        MidiIo* self = static_cast<MidiIo*>(ctx);
        self->tail_  = (self->tail_ + 1) % kFifo;
        self->busy_  = false;
        if(self->head_ != self->tail_)
            self->StartDma();
    }

    // each message owns a 32-byte cache line for the DMA
    struct alignas(32) Entry
    {
        uint8_t data[3];
        uint8_t len;
    };
    static constexpr int kFifo = 64;

    MidiUartHandler uart_;
    MidiUsbHandler  usb_;
    UartHandler     uart_handle_;
    Entry           fifo_[kFifo];
    volatile int    head_ = 0, tail_ = 0;
    volatile bool   busy_ = false;
    Entry           usb_q_[kFifo];
    volatile int    usb_head_ = 0, usb_tail_ = 0;
    uint8_t         ch_in_ = 0, ch_out_ = 0;
    uint32_t        last_clock_ = 0;
};

} // namespace chompi
