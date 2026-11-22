// Adapted from Hillside 0.2.0 (engine/src/Engine.cpp).
#include "bryn/Engine.h"

#include <utility>

namespace bryn
{
    Engine::Engine()
    {
        cc.setLane (outFunction, kFunctionCoarseCc, kFunctionFineCc);
        queue.items.reserve (4096);
        prepare (48000.0);
    }

    void Engine::prepare (double sampleRate)
    {
        fn.prepare (sampleRate);
        fn.setSettings (active.fn);
        cc.prepare (sampleRate);
        pulse.prepare (sampleRate);
        pulsesToClose = pulse.anyOn();   // a gate note left on is ended at the start of the next block
        lastRun = fn.runs();
        keys.clear();
        abs = 0;
        gateNow = false;
        manualHeld = manualBlocked = false;
        panicPending = false;
        settingsChanged = true;
    }

    void Engine::setSettings (const Settings& s)
    {
        pending = s;
        if (pending.outChannel < 1) pending.outChannel = 1;
        if (pending.outChannel > 16) pending.outChannel = 16;
        if (pending.sendAs < sendAsCc || pending.sendAs > sendAsBoth) pending.sendAs = sendAsCc;
        settingsChanged = true;
    }

    void Engine::panic() { panicPending = true; }

    void Engine::Queue::handOn (MidiSink& sink)
    {
        // Stable insertion sort by sample (the list is short and nearly in order already).
        for (std::size_t a = 1; a < items.size(); ++a)
            for (std::size_t b = a; b > 0 && items[b].offset < items[b - 1].offset; --b)
                std::swap (items[b], items[b - 1]);
        for (const auto& it : items)
        {
            if (it.note) sink.note (it.offset, it.channel, it.number, it.value);
            else         sink.cc (it.offset, it.channel, it.number, it.value);
        }
        items.clear();
    }

    void Engine::applySettings (MidiSink& sink)
    {
        settingsChanged = false;
        if (pending.outChannel != active.outChannel)
            cc.moveChannel (active.outChannel, 0, sink);
        for (int i = 0; i < kNumSmoothOutputs; ++i)
            if (pending.outOn[i] != cc.isEnabled (i))
                cc.setEnabled (i, pending.outOn[i], abs, 0);
        pulse.configure (pending.pulseOn, pending.outChannel, pending.sendAs, 0, sink);
        active = pending;
        fn.setSettings (active.fn);
    }

    void Engine::restNow (bool manualGate, MidiSink& sink)
    {
        panicPending = false;
        fn.reset();
        keys.clear();
        gateNow = false;
        manualBlocked = manualGate;   // a button held through a stop must be let go and pressed again
        pulse.closeAll (0, sink);
        lastRun = fn.runs();
        for (int i = 0; i < kNumSmoothOutputs; ++i) cc.force (i, abs, 0, 0);
    }

    void Engine::flushOnly (int numSamples, MidiSink& sink)
    {
        queue.items.clear();
        if (pulsesToClose) { pulse.closeAll (0, queue); pulsesToClose = false; }
        if (settingsChanged) applySettings (queue);
        if (panicPending) restNow (manualHeld, queue);
        cc.flush (active.outChannel, queue);
        queue.handOn (sink);
        if (numSamples > 0) abs += numSamples;
    }

    void Engine::render (int numSamples, const InputEvent* events, int numEvents, bool manualGate, MidiSink& sink)
    {
        queue.items.clear();
        if (pulsesToClose) { pulse.closeAll (0, queue); pulsesToClose = false; }
        if (settingsChanged) applySettings (queue);
        manualHeld = manualGate;
        if (panicPending) restNow (manualGate, queue);
        if (! manualGate) manualBlocked = false;
        const bool manual = manualGate && ! manualBlocked;

        if (numSamples <= 0)
        {   // a block of no samples: its key events still count (a release must never be lost)
            if (numEvents > 0)
            {
                keys.beginSample (abs);
                for (int k = 0; k < numEvents; ++k)
                {
                    const auto& ev = events[k];
                    if (ev.kind == InputEvent::noteOn)       keys.press (ev.channel, ev.note);
                    else if (ev.kind == InputEvent::noteOff) keys.release (ev.channel, ev.note);
                    else                                     keys.releaseChannel (ev.channel);
                }
                const bool want = keys.held() > 0 || manual;
                if (want && ! gateNow)       fn.gateRise();
                else if (! want && gateNow)  fn.gateFall();
                gateNow = want;
            }
            cc.flush (active.outChannel, queue);
            queue.handOn (sink);
            return;
        }

        int e = 0;
        const int last = numSamples - 1;
        for (int i = 0; i < numSamples; ++i)
        {
            const bool keyEvents = e < numEvents && (events[e].offset <= i || i == last);
            if (keyEvents)
            {
                keys.beginSample (abs);
                while (e < numEvents && (events[e].offset <= i || i == last))
                {
                    const auto& ev = events[e++];
                    if (ev.kind == InputEvent::noteOn)       keys.press (ev.channel, ev.note);
                    else if (ev.kind == InputEvent::noteOff) keys.release (ev.channel, ev.note);
                    else                                     keys.releaseChannel (ev.channel);
                }
            }

            bool want = keys.held() > 0 || manual;
            const bool retrigger = keyEvents && ! manual && gateNow && want && keys.backToBack();
            if (keyEvents && ! want && ! gateNow && keys.blip())
                want = true;   // a zero-length note: high for this one sample, falls at the next

            if (retrigger)               { fn.gateFall(); fn.gateRise(); }
            else if (want && ! gateNow)  fn.gateRise();
            else if (! want && gateNow)  fn.gateFall();
            gateNow = want;

            const FnSample s = fn.next();
            const bool started = s.run != lastRun;
            lastRun = s.run;
            pulse.step (abs, i, s.stage, started, s.cycleEnd, queue);
            cc.consider (outFunction, abs, i, toFourteenBit (s.level), s.corner);
            if (trace != nullptr) trace->push_back ({ s.level, s.stage, gateNow, s.corner, started, s.cycleEnd });
            ++abs;
        }
        cc.flush (active.outChannel, queue);
        queue.handOn (sink);
    }
}
