#include "bryn/Function.h"

#include <cmath>

namespace bryn
{
    namespace
    {
        constexpr double kDone = 1.0 - 1.0e-9;   // progress counted as complete (rounding of N steps of 1/N)
        bool timed (Stage s) { return s == Stage::rise || s == Stage::fall; }
    }

    long long Function::samplesFor (double seconds, double sampleRate)
    {
        const long long n = (long long) std::llround (seconds * sampleRate);
        return n < 1 ? 1 : n;
    }

    void Function::prepare (double sampleRate)
    {
        sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        reset();
        updateIncrement();
    }

    void Function::setSettings (const FnSettings& s)
    {
        const bool curveChanged = s.logCurve != set.logCurve;
        set = s;
        if (set.mode < modeAsr || set.mode > modeLoop) set.mode = modeAr;
        curve.setShape (set.logCurve ? 1.0 : 0.0);
        if (curveChanged && timed (st))
            rebase();          // the new curve carries on from the current level: no jump
        else
            updateIncrement();
    }

    void Function::rebase()
    {
        // The running stage goes on from where it is, as if it had started here (same target, same speed rule).
        start = lvl;
        p = 0.0;
        fraction = st == Stage::rise ? 1.0 - start : start;
        if (fraction < 0.0) fraction = 0.0;
        updateIncrement();
    }

    void Function::updateIncrement()
    {
        double seconds = 0.0;
        switch (st)
        {
            case Stage::rise: seconds = set.rise * fraction; break;
            case Stage::fall: seconds = set.fall * fraction; break;
            default: inc = 0.0; return;
        }
        const long long n = (long long) std::llround (seconds * sr);
        inc = 1.0 / (double) (n < 1 ? 1 : n);
    }

    void Function::reset()
    {
        st = Stage::idle;
        p = 0.0; inc = 0.0;
        start = lvl = 0.0;
        fraction = 1.0;
        gateHigh = false;
    }

    void Function::enter (Stage s)
    {
        st = s;
        p = 0.0;
        start = lvl;
        if (s == Stage::rise) fraction = 1.0 - start;
        if (s == Stage::fall) fraction = start;
        if (fraction < 0.0) fraction = 0.0;
        if (s == Stage::fall && start <= 0.0)
            st = Stage::idle;   // nothing to fall: no fall stage, no end of cycle
        if (st != Stage::idle) ++runCount;
        updateIncrement();
    }

    void Function::gateRise()
    {
        gateHigh = true;
        if (st == Stage::idle || st == Stage::fall)
            enter (Stage::rise);   // from rest, or turned round during the fall
        // Rise or hold: carries on.
    }

    void Function::gateFall()
    {
        gateHigh = false;
        if (set.mode == modeAsr && (st == Stage::rise || st == Stage::hold))
            enter (Stage::fall);
        // AR and Loop: the gate's end changes nothing.
    }

    void Function::finishStage (bool& cycleEnd)
    {
        switch (st)
        {
            case Stage::rise:
                lvl = 1.0;
                enter (set.mode == modeAsr && gateHigh ? Stage::hold : Stage::fall);
                break;
            case Stage::fall:
                lvl = 0.0;
                cycleEnd = true;
                ++cycleCount;
                enter (set.mode == modeLoop ? Stage::rise : Stage::idle);
                break;
            default: break;
        }
    }

    FnSample Function::next()
    {
        // The hold only lasts in ASR mode while the gate is high (the mode may have been changed).
        if (st == Stage::hold && (set.mode != modeAsr || ! gateHigh))
            enter (Stage::fall);
        // Loop runs by itself.
        if (st == Stage::idle && set.mode == modeLoop)
            enter (Stage::rise);

        bool corner = false, cycleEnd = false;
        while (timed (st) && p >= kDone)
        {
            finishStage (cycleEnd);
            corner = true;
        }

        switch (st)
        {
            case Stage::idle: lvl = 0.0; break;
            case Stage::rise: lvl = start + (1.0 - start) * curve (p); break;
            case Stage::hold: lvl = 1.0; break;
            case Stage::fall: lvl = start * (1.0 - curve (p)); break;
        }

        const FnSample out { lvl, st, corner, cycleEnd, runCount };
        if (timed (st)) p += inc;
        return out;
    }
}
