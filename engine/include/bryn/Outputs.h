// Bryn's MIDI numbers. Fixed in the first build (0.1.0) and never changed: the mapper and saved projects rely on them.
//
// Chosen so Bryn can share one track with Hillside (channel 16: CC 3, 35, 9, 41, 85-89, notes 40-44), Ogdoad
// (channel 16: CC 12-31, 44-63, 102-105, notes 36-39) and Strange Drift (channel 15: CC 106-119): none of Bryn's
// numbers is used by any of them, on any channel, and Bryn sends on its own channel (14) by default.
// CC 90 is the last CC that MIDI leaves without a job; CC 80-83 are the "general purpose" controllers, which have no
// fixed effect. The Function's coarse and fine parts are CC 80 and 81 (a direct pair, as Strange Drift's 106 / 113,
// since every coarse CC 0-31 with a standard fine partner is taken). The notes follow Hillside's 40-44.

#pragma once

namespace bryn
{
    enum OutputIndex
    {
        outFunction = 0,   // the function (rise and fall), as set by the Curve switch
        kNumSmoothOutputs = 1
    };

    inline constexpr int kFunctionCoarseCc = 80, kFunctionFineCc = 81;

    // The pulse outputs: Rise gate (high while rising), Fall gate (high while falling), EOC (20 ms at the end of
    // every cycle). CC: 127 while high, then 0. Note: velocity 100, same timing.
    inline constexpr int kNumPulseOutputs = 3;
    inline constexpr int kRiseGateIndex = 0, kFallGateIndex = 1, kEocIndex = 2;
    inline constexpr int kPulseCc[kNumPulseOutputs]   = { 82, 83, 90 };
    inline constexpr int kPulseNote[kNumPulseOutputs] = { 45, 46, 47 };   // A1, A#1, B1
    inline constexpr int kPulseVelocity = 100;
    inline constexpr double kPulseSeconds = 0.020;   // the EOC pulse's length, and the shortest gate

    // "Send as" (the control's menu order): CC, Note, Both.
    enum SendAs { sendAsCc = 0, sendAsNote = 1, sendAsBoth = 2 };

    inline constexpr int kDefaultChannel = 14;
}
