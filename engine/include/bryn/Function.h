// The two-stage function (rise and fall), worked out sample by sample. Plain C++, no plugin code.
// Adapted from Hillside's Envelope (same timing method; the stages and modes are new).
//
// Stages: rest, Rise (to the top, 1), Hold (ASR only: the top, while the gate is high), Fall (to 0).
// Speed: Rise is the time of a whole climb from 0 to the top, Fall the time of a whole drop from the top to 0. A stage
// that starts part-way takes proportionally less: a rise from level L takes Rise x (1 - L), a fall from L takes
// Fall x L (the same speed, as on an analog function generator).
// Modes (the module's toggle: left ASR, centre AR, right Loop):
//   AR    a rising gate starts a whole rise and fall, however short the gate (the fall comes after the rise even while
//         the key is still held).
//   ASR   rises while the gate is high, holds the top, falls when the gate falls (at once, also during the rise).
//   Loop  rises and falls over and over without any gate (it starts by itself, from rest).
// In every mode a rising gate during the fall turns the function round: a new rise starts from where it is, at that
// sample, with no jump ("charge direction" on the module). A rising gate during the rise or the hold changes nothing.
// End of cycle: the sample at which a fall reaches 0. A fall interrupted by a new rise is not the end of a cycle.
// Curve: Lin = straight lines; Log = Hillside's Shape 100% (log rise: fast, then slowing near the top; exponential
// fall: quick drop, long tail). The stage times never depend on the curve. Switching the curve while a stage runs
// carries on from the current level (the rest of the stage follows the new curve, at the same speed rule): no jump.
//
// Timing: every stage lasts a whole number of samples (its time rounded to the nearest sample, at least one), so a
// Loop cycle is exactly Rise + Fall samples at any block size. Changing a time while a stage runs changes its speed
// from that sample on, without a jump.

#pragma once

#include "bryn/Curve.h"
#include <cstdint>

namespace bryn
{
    enum class Stage : std::uint8_t { idle = 0, rise = 1, hold = 2, fall = 3 };

    enum Mode { modeAsr = 0, modeAr = 1, modeLoop = 2 };   // the Mode control's menu order

    struct FnSettings
    {
        double rise = 0.100;      // seconds, 0.003 .. 10 (a whole climb from 0 to the top)
        double fall = 0.400;      // seconds, 0.003 .. 10 (a whole drop from the top to 0)
        bool   logCurve = true;   // false = Lin
        int    mode = modeAr;
    };

    struct FnSample
    {
        double level;            // 0 .. 1
        Stage stage;
        bool corner;             // a stage reached its target at this sample (the top or 0)
        bool cycleEnd;           // a fall reached 0 at this sample (end of cycle)
        unsigned run;            // counts every stage that starts; a new number at this sample = a stage started here
    };

    class Function
    {
    public:
        void prepare (double sampleRate);
        void setSettings (const FnSettings& s);   // applies from the next sample; a running stage keeps its progress
        const FnSettings& settings() const { return set; }

        void gateRise();   // events at a sample are applied before that sample is worked out
        void gateFall();
        void reset();      // straight to rest: level 0, no stage, gate low (Loop starts again from 0 at the next sample)

        FnSample next();   // the values at this sample, then one sample on

        Stage stage() const       { return st; }
        unsigned runs() const     { return runCount; }
        unsigned cycles() const   { return cycleCount; }   // falls completed so far
        double progress() const   { return p; }           // 0 .. 1 through the current stage
        double level() const      { return lvl; }
        bool gate() const         { return gateHigh; }
        double sampleRate() const { return sr; }

        // Stage length in samples (at least one): the time rounded to the nearest sample.
        static long long samplesFor (double seconds, double sampleRate);

    private:
        void enter (Stage s);
        void finishStage (bool& cycleEnd);
        void updateIncrement();
        void rebase();

        double sr = 48000.0;
        FnSettings set;
        Curve curve;
        Stage st = Stage::idle;
        double p = 0.0, inc = 0.0;
        double start = 0.0;      // the level at which the current stage began
        double lvl = 0.0;        // the latest level
        double fraction = 1.0;   // the part of a whole climb or drop this stage covers
        bool gateHigh = false;
        unsigned runCount = 0, cycleCount = 0;
    };
}
