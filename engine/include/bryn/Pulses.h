// The Rise gate, the Fall gate and EOC. Plain C++, no plugin code. Adapted from Hillside's Pulses (same rules for
// length, merging, sending and stuck-note safety; the sources are new).
//
// Rules (shown in the README):
//   - Rise gate: high for the whole time the function rises (from the rise's first sample to the first sample of
//     whatever follows). Fall gate: the same for the fall. A stage that runs for less than 20 ms is stretched: its
//     gate stays high for 20 ms. If the same stage starts again while its gate is still high, the two merge into one
//     longer gate (so a fast Loop gives a gate that stays high).
//   - EOC: a 20 ms pulse at the sample at which a fall reaches 0 (the end of every cycle; a fall interrupted by a new
//     rise sends none). Pulses closer together than 20 ms merge into one longer pulse, as in Ogdoad and Hillside.
//   - Sent as (the "Send as" control): CC (127 while high, then 0), a note (velocity 100, released when low), or both,
//     on the MIDI out channel. Numbers in Outputs.h.
//   - Stuck-note safety: whatever is on is ended (CC 0 and/or note-off, on the channel it was sent on) when Send as or
//     the channel changes, when that output is switched off, and on rest (stop, reset, bypass, prepare). An output
//     that was ended that way is not started again until its stage next starts (or the next cycle ends): changing a
//     control never starts a note.

#pragma once

#include "bryn/CcStream.h"
#include "bryn/Function.h"
#include "bryn/Outputs.h"

namespace bryn
{
    class Pulses
    {
    public:
        // The pulse length. What is on stays on (and is ended by closeAll, which the Engine calls next).
        void prepare (double sampleRate);

        // Settings, applied at a block's first sample (offset). Ends what must end (see above).
        void configure (const bool enabled[kNumPulseOutputs], int channel, int sendAs, int offset, MidiSink& sink);

        // Every sample: the stage reported for this sample, whether a stage started and whether a cycle ended here.
        void step (long long absSample, int offset, Stage stage, bool started, bool cycleEnd, MidiSink& sink);

        // Ends everything that is on, at this offset.
        void closeAll (int offset, MidiSink& sink);

        bool isOn (int k) const       { return outs[k].cc || outs[k].note; }
        unsigned starts (int k) const { return outs[k].starts; }   // how many times it has been switched on
        bool anyOn() const;

    private:
        struct Out
        {
            bool cc = false, note = false;   // what is on now
            int channel = kDefaultChannel;   // where it was sent
            long long until = 0;             // stays on at least until this sample
            bool source = false;             // the stage is running now (gates only)
            unsigned starts = 0;
        };
        void open (int k, long long absSample, int offset, MidiSink& sink);
        void close (int k, int offset, MidiSink& sink);

        Out outs[kNumPulseOutputs];
        bool enabled[kNumPulseOutputs] = { true, true, true };
        int channel = kDefaultChannel;
        int mode = sendAsCc;
        long long length = 960;   // 20 ms in samples
    };
}
