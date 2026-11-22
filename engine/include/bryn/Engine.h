// The whole Bryn engine: notes in, the function out as 14-bit MIDI CC, sample-accurate. Plain C++, no plugin code,
// so it is tested without Logic (engine/tests). Adapted from Hillside 0.2.0's Engine (the Function replaces the
// Envelope; Loop runs without notes; one smooth output and three pulse outputs).
//
// Per block: the plugin passes the block's note events (with their sample offsets), the Trigger button, and the
// settings; the engine works out every sample, and sends the chosen CC messages (and the Rise gate, Fall gate and
// EOC as CC and/or notes) to a MidiSink at their exact samples, in sample order.
// It never touches the incoming MIDI: passing everything through is the plugin's job.

#pragma once

#include "bryn/CcStream.h"
#include "bryn/Function.h"
#include "bryn/Keys.h"
#include "bryn/Outputs.h"
#include "bryn/Pulses.h"

#include <vector>

namespace bryn
{
    struct InputEvent
    {
        enum Kind : std::uint8_t { noteOn, noteOff, allNotesOff };
        int offset;            // sample within the block
        Kind kind;
        std::uint8_t channel;  // 0..15
        std::uint8_t note;     // 0..127 (ignored for allNotesOff)
    };

    struct Settings
    {
        FnSettings fn;
        int  outChannel = kDefaultChannel;          // 1..16
        bool outOn[kNumSmoothOutputs] = { true };   // Function
        bool pulseOn[kNumPulseOutputs] = { true, true, true };   // Rise gate, Fall gate, EOC
        int  sendAs = sendAsCc;                     // how the gates and EOC go out: CC, Note or Both
    };

    // One sample, for the tests and the panel.
    struct TracePoint { double level; Stage stage; bool gate; bool corner; bool started; bool cycleEnd; };   // started: a stage began here

    class Engine
    {
    public:
        Engine();

        void prepare (double sampleRate);         // also goes to rest and sends everything afresh
        void setSettings (const Settings& s);     // applied at the start of the next block
        const Settings& settings() const { return pending; }

        // Events must be in sample order. manualGate = the panel's Trigger button (applied at the block's first sample).
        void render (int numSamples, const InputEvent* events, int numEvents, bool manualGate, MidiSink& sink);

        // Straight to rest at the start of the next rendered block (or of flushOnly): level 0, every key released, the
        // Trigger button forgotten until it is let go, every gate ended, and 0 sent if the output is not already at 0.
        // Used for transport stop, reset, bypass. (In Loop mode the function starts again from 0 straight away.)
        void panic();

        // For the bypassed path: no notes are read and nothing moves; a pending rest is still sent.
        void flushOnly (int numSamples, MidiSink& sink);

        // What the panel shows.
        const Function& function() const { return fn; }
        bool gate() const { return gateNow; }
        int heldKeys() const { return keys.held(); }
        const CcStream& ccStream() const { return cc; }
        const Pulses& pulses() const { return pulse; }
        long long samplesDone() const { return abs; }

        // Tests only: when set, every sample is appended here.
        std::vector<TracePoint>* trace = nullptr;

    private:
        // Everything the engine sends in a block is collected here first, then handed on in sample order (the
        // gates are worked out sample by sample, the CC values at the end of the block). Never allocates while
        // running: the space is reserved once.
        struct Queue final : MidiSink
        {
            struct Item { int offset, channel, number, value; bool note; };
            std::vector<Item> items;
            void cc (int o, int ch, int n, int v) override   { items.push_back ({ o, ch, n, v, false }); }
            void note (int o, int ch, int n, int v) override { items.push_back ({ o, ch, n, v, true }); }
            void handOn (MidiSink& sink);
        };

        void applySettings (MidiSink& sink);
        void restNow (bool manualGate, MidiSink& sink);
        void setGate (bool high);

        Settings pending, active;
        bool settingsChanged = true;
        Function fn;
        Keys keys;
        CcStream cc;
        Pulses pulse;
        Queue queue;
        unsigned lastRun = 0;
        bool pulsesToClose = false;                        // after prepare: end whatever is on, at the next block
        long long abs = 0;
        bool manualHeld = false, manualBlocked = false;   // after a panic the button must be let go first
        bool gateNow = false;
        bool panicPending = false;
        bool blipPending = false;                          // a zero-length note: the gate falls one sample later
    };
}
