// Bryn engine self-test. Runs on the Linux checking computer before every Mac build.
//
// What it proves, without Logic:
//   1. the curve: exact ends, always rising, straight for Lin;
//   2. timing: every stage starts and ends within one sample of the ideal moment at 44.1, 48 and 96 kHz, with blocks
//      of 32 to 1024 samples and random sizes; AR cycles and Loop cycles exact (a Loop cycle is exactly Rise + Fall
//      samples, cycle after cycle); the function identical, sample for sample, at every block size;
//   3. the modes: AR (a whole cycle for any note, also a long one), ASR (holds the top while held; a release during
//      the rise falls from there at the same speed), Loop (runs without notes); a note during the fall turns it round
//      at that sample with no jump, a note during the rise changes nothing; changing the mode while it runs;
//   4. the curve: Lin rises and falls in straight lines; Log rises fast then slows, falls fast then tails; the stage
//      times do not depend on the curve;
//   5. no jump: in random sessions with everything changing, the level never jumps (except to 0 at rest);
//   6. notes: overlapping notes, back-to-back notes, velocity 0, All Notes Off; the Trigger button;
//   7. rest (stop, reset, bypass): straight to 0, 0 sent, every gate ended; Loop starts again from 0;
//   8. the CC rules: one value per block, every value sent equal to the function at that sample, coarse before fine
//      only when it changed, corners exact (the top and 0 at their samples), nothing while at rest;
//   9. the Rise gate, Fall gate and EOC: exact edges at every rate and block size, 20 ms stretching and merging (a
//      fast Loop keeps them high), no EOC for an interrupted fall; CC, Note and Both; Send as, channel and switch
//      changes end what is on and never start anything; rest ends everything;
//  10. stuck-gate and stuck-note fuzz: hundreds of thousands of random events with everything changing, then all
//      released: at rest, 0 sent, no gate or gate note left on;
//  11. CPU use and the MIDI rate.

#include "bryn/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace bryn;

namespace
{
    long long checks = 0, failures = 0;

    void expect (bool ok, const std::string& what)
    {
        ++checks;
        if (! ok) { ++failures; if (failures < 80) std::printf ("FAIL: %s\n", what.c_str()); }
    }
    std::string str (long long v) { return std::to_string (v); }
    std::string str (double v)    { char b[64]; std::snprintf (b, sizeof (b), "%.6g", v); return b; }

    struct Msg { long long at; int ch, num, val; bool note; long long block; };

    struct Collector : MidiSink
    {
        std::vector<Msg> msgs;
        long long blockStart = 0, block = 0;
        void cc (int off, int ch, int n, int v) override   { msgs.push_back ({ blockStart + off, ch, n, v, false, block }); }
        void note (int off, int ch, int n, int v) override { msgs.push_back ({ blockStart + off, ch, n, v, true, block }); }
    };

    struct Ev { long long at; InputEvent::Kind kind; int ch, note; };
    Ev on (long long at, int note = 60, int ch = 0)  { return { at, InputEvent::noteOn, ch, note }; }
    Ev off (long long at, int note = 60, int ch = 0) { return { at, InputEvent::noteOff, ch, note }; }

    struct Run
    {
        Engine eng;
        Collector col;
        std::vector<TracePoint> tr;
        std::vector<long long> edges;   // the first sample after each block
        std::function<int()> blockSize = [] { return 512; };
        std::function<void (Engine&, long long)> onBlock;      // before each block (settings changes, panic)
        std::function<bool (long long)> manual = [] (long long) { return false; };

        void go (double sr, const Settings& s, std::vector<Ev> evs, long long total)
        {
            std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.at < b.at; });
            eng.prepare (sr);
            eng.setSettings (s);
            eng.trace = &tr;
            tr.reserve ((size_t) total);
            long long pos = 0;
            size_t ei = 0;
            std::vector<InputEvent> buf;
            while (pos < total)
            {
                const int n = (int) std::min<long long> (blockSize(), total - pos);
                if (onBlock) onBlock (eng, pos);
                buf.clear();
                while (ei < evs.size() && evs[ei].at < pos + n)
                {
                    buf.push_back ({ (int) (evs[ei].at - pos), evs[ei].kind, (std::uint8_t) evs[ei].ch, (std::uint8_t) evs[ei].note });
                    ++ei;
                }
                col.blockStart = pos;
                eng.render (n, buf.data(), (int) buf.size(), manual (pos), col);
                ++col.block;
                pos += n;
                edges.push_back (pos);
            }
            eng.trace = nullptr;
        }
    };

    struct Change { long long at; Stage stage; };
    std::vector<Change> changes (const std::vector<TracePoint>& tr)
    {
        std::vector<Change> c;
        for (size_t i = 0; i < tr.size(); ++i)
            if (i == 0 || tr[i].stage != tr[i - 1].stage || (tr[i].started && i > 0)) c.push_back ({ (long long) i, tr[i].stage });
        return c;
    }
    const char* stageName (Stage s)
    {
        switch (s) { case Stage::idle: return "rest"; case Stage::rise: return "R"; case Stage::hold: return "H"; case Stage::fall: return "F"; }
        return "?";
    }
    std::string show (const std::vector<Change>& c)
    {
        std::string s;
        for (size_t i = 0; i < c.size() && i < 12; ++i) s += std::string (stageName (c[i].stage)) + "@" + str (c[i].at) + " ";
        return s;
    }
    bool sameChanges (const std::vector<Change>& a, const std::vector<Change>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) if (a[i].at != b[i].at || a[i].stage != b[i].stage) return false;
        return true;
    }

    Settings make (double rise, double fall, bool logCurve, int mode)
    {
        Settings s;
        s.fn.rise = rise; s.fn.fall = fall; s.fn.logCurve = logCurve; s.fn.mode = mode;
        return s;
    }
    long long N (double seconds, double sr) { return Function::samplesFor (seconds, sr); }

    // The receiver: what a Scripter mapper (or anything reading the 14-bit pair) knows after each message.
    struct Receiver
    {
        int coarse = 0;
        int value = -1;
        bool take (const Msg& m)   // true when a value was completed
        {
            if (m.note || m.num < 0) return false;
            if (m.num == kFunctionCoarseCc) { coarse = m.val; return false; }
            if (m.num == kFunctionFineCc) { value = coarse * 128 + m.val; return true; }
            return false;
        }
    };

    std::function<int()> randomBlocks (unsigned seed)
    {
        auto rng = std::make_shared<std::mt19937> (seed);
        return [rng] { return 1 + (int) ((*rng)() % 1024); };
    }
}

// ================================================================================================ 1. the curve
static void testCurve()
{
    for (double shape : { 0.0, 1.0 })
    {
        Curve c;
        c.setShape (shape);
        expect (c (0.0) == 0.0 && c (1.0) == 1.0, "curve ends exact (" + str (shape) + ")");
        bool rising = true;
        double prev = 0.0;
        for (int i = 1; i <= 1000; ++i) { const double v = c (i / 1000.0); rising = rising && v > prev; prev = v; }
        expect (rising, "curve always rising (" + str (shape) + ")");
    }
    Curve lin; lin.setShape (0.0);
    expect (std::abs (lin (0.37) - 0.37) < 1e-15, "Lin is straight");
    Curve lg; lg.setShape (1.0);
    expect (lg (0.5) > 0.9, "Log: past 90% after half the time (" + str (lg (0.5)) + ")");
}

// ================================================================================================ 2. timing
static void testTiming()
{
    long long edgesChecked = 0;
    const std::vector<int> sizes { 32, 64, 100, 128, 256, 441, 512, 1000, 1024, -1 };
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (bool lg : { false, true })
        {
            // AR: one short note at 1000.
            const double rise = 0.0123, fall = 0.0456;
            const long long R = N (rise, sr), F = N (fall, sr);
            std::vector<double> ref;
            for (int size : sizes)
            {
                Run r;
                r.blockSize = size > 0 ? std::function<int()> ([size] { return size; }) : randomBlocks (11);
                r.go (sr, make (rise, fall, lg, modeAr), { on (1000), off (1010) }, 1000 + R + F + 2000);
                const auto c = changes (r.tr);
                const bool ok = c.size() == 4 && c[1].at == 1000 && c[1].stage == Stage::rise && c[2].at == 1000 + R
                             && c[2].stage == Stage::fall && c[3].at == 1000 + R + F && c[3].stage == Stage::idle;
                expect (ok, "AR timing at " + str (sr) + " Hz, blocks " + str ((long long) size) + ": " + show (c));
                expect (r.tr[(size_t) (1000 + R)].level == 1.0 && r.tr[(size_t) (1000 + R)].corner, "AR: the top exactly, at its sample");
                expect (r.tr[(size_t) (1000 + R + F)].level == 0.0 && r.tr[(size_t) (1000 + R + F)].cycleEnd, "AR: 0 exactly at the end of the cycle");
                edgesChecked += 3;
                std::vector<double> lv;
                for (auto& t : r.tr) lv.push_back (t.level);
                if (ref.empty()) ref = lv;
                else expect (lv == ref, "AR identical sample for sample at block size " + str ((long long) size));
            }
            // Loop: no notes; cycles exact.
            const double lr = 0.0101, lf = 0.0237;
            const long long LR = N (lr, sr), LF = N (lf, sr);
            ref.clear();
            for (int size : sizes)
            {
                Run r;
                r.blockSize = size > 0 ? std::function<int()> ([size] { return size; }) : randomBlocks (12);
                const long long total = (long long) sr * 2;
                r.go (sr, make (lr, lf, lg, modeLoop), {}, total);
                bool ok = true;
                long long cycles = 0;
                for (long long i = 0; i < total; ++i)
                {
                    const long long x = i % (LR + LF);
                    const Stage want = x < LR ? Stage::rise : Stage::fall;
                    ok = ok && r.tr[(size_t) i].stage == want;
                    const bool end = i > 0 && x == 0;
                    ok = ok && r.tr[(size_t) i].cycleEnd == end;
                    if (end) { ok = ok && r.tr[(size_t) i].level == 0.0; ++cycles; }
                    if (x == LR) ok = ok && r.tr[(size_t) i].level == 1.0;
                }
                expect (ok && cycles > 50, "Loop cycles exactly Rise + Fall samples at " + str (sr) + " Hz, blocks " + str ((long long) size) + " (" + str (cycles) + " cycles)");
                edgesChecked += 2 * cycles;
                std::vector<double> lv;
                for (auto& t : r.tr) lv.push_back (t.level);
                if (ref.empty()) ref = lv;
                else expect (lv == ref, "Loop identical sample for sample at block size " + str ((long long) size));
            }
        }
    std::printf ("timing: %lld stage edges exact at 44.1 / 48 / 96 kHz, Lin and Log, 10 block sizes; identical at every block size\n", edgesChecked);
}

// ================================================================================================ 3. modes
static void testModes()
{
    const double sr = 48000.0;
    // AR: a long note: the fall comes after the rise while the key is still held, then rest while still held.
    {
        Run r;
        r.go (sr, make (0.1, 0.2, true, modeAr), { on (0), off (40000) }, 60000);
        const auto c = changes (r.tr);
        expect (c.size() == 3 && c[0].stage == Stage::rise && c[1].at == N (0.1, sr) && c[1].stage == Stage::fall
                && c[2].at == N (0.1, sr) + N (0.2, sr) && c[2].stage == Stage::idle, "AR long note: whole cycle while held: " + show (c));
        expect (r.tr[30000].gate && r.tr[30000].stage == Stage::idle, "AR: resting with the key still held");
    }
    // AR: a new gate edge during the rise changes nothing.
    {
        Run a, b;
        a.go (sr, make (0.1, 0.2, true, modeAr), { on (0), off (100) }, 20000);
        b.go (sr, make (0.1, 0.2, true, modeAr), { on (0), off (100), on (1000), off (1200), on (3000, 62), off (3100, 62) }, 20000);
        bool same = true;
        for (size_t i = 0; i < a.tr.size(); ++i) same = same && a.tr[i].level == b.tr[i].level && a.tr[i].stage == b.tr[i].stage;
        expect (same, "AR: notes during the rise change nothing");
    }
    // Every mode: a note during the fall turns it round at that sample, from where it is, at the same speed.
    for (int mode : { modeAr, modeAsr, modeLoop })
    {
        Run r;
        const double rise = 0.1, fall = 0.4;
        const long long R = N (rise, sr), t = R + 9000;   // during the fall
        std::vector<Ev> evs { on (0), off (mode == modeAsr ? R + 10 : 100), on (t, 64), off (t + (mode == modeAsr ? 19000 : 50), 64) };
        if (mode == modeLoop) evs = { on (t, 64), off (t + 50, 64) };
        r.go (sr, make (rise, fall, false, mode), evs, t + 20000);
        const double before = r.tr[(size_t) (t - 1)].level;
        const long long n = std::max<long long> (1, std::llround (rise * sr * (1.0 - before)));
        const bool turned = r.tr[(size_t) (t - 1)].stage == Stage::fall && r.tr[(size_t) t].stage == Stage::rise && r.tr[(size_t) t].started;
        expect (turned, "mode " + str ((long long) mode) + ": a note during the fall starts a rise at that sample");
        expect (std::abs (r.tr[(size_t) t].level - before) < 1.0e-12, "mode " + str ((long long) mode) + ": turned round with no jump");
        const double slope = r.tr[(size_t) (t + 10)].level - r.tr[(size_t) (t + 9)].level;
        expect (std::abs (slope - (1.0 - before) / (double) n) < 1e-12 && std::abs (slope * rise * sr - 1.0) < 0.002,
                "mode " + str ((long long) mode) + ": same rising speed from part-way (Lin)");
        const Stage after = mode == modeAsr ? Stage::hold : Stage::fall;   // ASR: the second key is still held
        expect (r.tr[(size_t) (t + n)].stage == after && r.tr[(size_t) (t + n - 1)].stage == Stage::rise && r.tr[(size_t) (t + n)].level == 1.0,
                "mode " + str ((long long) mode) + ": the rise from part-way takes Rise x the distance (" + str (n) + " samples)");
        expect (! r.tr[(size_t) t].cycleEnd, "mode " + str ((long long) mode) + ": an interrupted fall is no end of cycle");
    }
    // ASR: holds the top while held, falls on release.
    {
        Run r;
        r.go (sr, make (0.1, 0.2, true, modeAsr), { on (0), off (48000) }, 70000);
        const auto c = changes (r.tr);
        expect (c.size() == 4 && c[1].at == N (0.1, sr) && c[1].stage == Stage::hold && c[2].at == 48000 && c[2].stage == Stage::fall
                && c[3].at == 48000 + N (0.2, sr) && c[3].stage == Stage::idle, "ASR: rise, hold, fall on release: " + show (c));
        expect (r.tr[30000].level == 1.0, "ASR: the hold is the top");
    }
    // ASR: released during the rise: falls from there at once, at the same speed (Fall x the level).
    {
        Run r;
        r.go (sr, make (1.0, 1.0, false, modeAsr), { on (0), off (12000) }, 40000);
        const double L = r.tr[11999].level;
        const long long n = std::llround (1.0 * sr * L);
        expect (r.tr[12000].stage == Stage::fall && std::abs (r.tr[12000].level - L) < 1e-12, "ASR: release during the rise falls from there, no jump");
        expect (r.tr[(size_t) (12000 + n)].stage == Stage::idle && r.tr[(size_t) (12000 + n - 1)].stage == Stage::fall,
                "ASR: that fall takes Fall x the level (" + str (n) + " samples)");
        expect (std::abs (L - 0.25) < 0.001, "ASR: Lin rise 1 s at 0.25 s is at 25%");
    }
    // ASR: a second key while the first is held does not restart; the fall waits for the last key.
    {
        Run r;
        r.go (sr, make (0.05, 0.1, true, modeAsr), { on (0), on (10000, 62), off (20000), off (30000, 62) }, 50000);
        const auto c = changes (r.tr);
        expect (c.size() == 4 && c[1].stage == Stage::hold && c[2].at == 30000 && c[2].stage == Stage::fall, "ASR: overlapping keys: " + show (c));
    }
    // Loop: runs without notes; a note during the rise changes nothing.
    {
        Run a, b;
        a.go (sr, make (0.05, 0.1, true, modeLoop), {}, 30000);
        b.go (sr, make (0.05, 0.1, true, modeLoop), { on (1000), off (1500), on (8000), off (20000) }, 30000);   // 1000 and 8000: rising
        bool same = true;
        for (size_t i = 0; i < a.tr.size(); ++i) same = same && a.tr[i].level == b.tr[i].level;
        expect (same && a.tr[0].stage == Stage::rise, "Loop: runs by itself; notes during the rise change nothing");
    }
    // Changing the mode while it runs.
    {
        // ASR holding, switch to AR: falls from the top at once.
        Run r;
        r.onBlock = [] (Engine& e, long long pos) { if (pos == 20480) e.setSettings (make (0.05, 0.1, true, modeAr)); };
        r.go (sr, make (0.05, 0.1, true, modeAsr), { on (0), off (40000) }, 40000);
        expect (r.tr[20479].stage == Stage::hold && r.tr[20480].stage == Stage::fall && r.tr[20480].level == 1.0, "ASR hold, switched to AR: falls at once");
        expect (r.tr[20480 + N (0.1, sr)].stage == Stage::idle, "and rests after the fall");
    }
    {
        // Loop, switch to AR during the rise: finishes this cycle, then rests.
        Run r;
        r.onBlock = [] (Engine& e, long long pos) { if (pos == 1024) e.setSettings (make (0.05, 0.1, true, modeAr)); };
        r.go (sr, make (0.05, 0.1, true, modeLoop), {}, 20000);
        const auto c = changes (r.tr);
        expect (c.size() == 3 && c[1].at == N (0.05, sr) && c[2].at == N (0.05, sr) + N (0.1, sr) && c[2].stage == Stage::idle,
                "Loop switched to AR: the cycle finishes, then rest: " + show (c));
    }
    {
        // AR, switch to Loop at rest: starts by itself; switch to ASR during the rise with no gate: rise, fall, rest.
        Run r;
        r.onBlock = [] (Engine& e, long long pos) {
            if (pos == 4096) e.setSettings (make (0.05, 0.1, true, modeLoop));
            if (pos == 4096 + 1024) e.setSettings (make (0.05, 0.1, true, modeAsr));
        };
        r.go (sr, make (0.05, 0.1, true, modeAr), {}, 20000);
        const auto c = changes (r.tr);
        expect (c.size() == 4 && c[1].at == 4096 && c[1].stage == Stage::rise && c[2].at == 4096 + N (0.05, sr) && c[2].stage == Stage::fall
                && c[3].stage == Stage::idle, "AR -> Loop starts; Loop -> ASR with no gate ends after the cycle: " + show (c));
    }
}

// ================================================================================================ 4. the curve in use
static void testCurveInUse()
{
    const double sr = 48000.0;
    Run lin, lg;
    lin.go (sr, make (0.1, 0.2, false, modeAr), { on (0), off (10) }, 20000);
    lg.go (sr, make (0.1, 0.2, true, modeAr), { on (0), off (10) }, 20000);
    const long long R = N (0.1, sr), F = N (0.2, sr);
    bool straight = true;
    for (long long i = 2; i < R; ++i) straight = straight && std::abs ((lin.tr[(size_t) i].level - lin.tr[(size_t) i - 1].level) - 1.0 / (double) R) < 1e-12;
    for (long long i = R + 2; i < R + F; ++i) straight = straight && std::abs ((lin.tr[(size_t) i - 1].level - lin.tr[(size_t) i].level) - 1.0 / (double) F) < 1e-12;
    expect (straight, "Lin: straight rise and fall");
    expect (lg.tr[(size_t) (R / 2)].level > 0.9 && lg.tr[(size_t) (R + F / 2)].level < 0.1, "Log: fast rise slowing near the top; quick drop with a long tail");
    expect (sameChanges (changes (lin.tr), changes (lg.tr)), "the stage times do not depend on the curve");
}

// ================================================================================================ 5. no jump
static void testNoJump()
{
    const double sr = 48000.0;
    double worst = 0.0;
    long long samples = 0;
    for (unsigned seed = 1; seed <= 6; ++seed)
    {
        std::mt19937 rng (seed);
        std::vector<Ev> evs;
        const long long total = (long long) sr * 30;
        for (long long t = 0; t < total; t += 1 + rng() % 6000)
            evs.push_back (rng() % 2 ? on (t, 60 + (int) (rng() % 4)) : off (t, 60 + (int) (rng() % 4)));
        Run r;
        r.blockSize = randomBlocks (seed);
        auto settingsRng = std::make_shared<std::mt19937> (seed * 31);
        r.onBlock = [settingsRng] (Engine& e, long long) {
            auto& g = *settingsRng;
            if (g() % 20 != 0) return;
            const double t1 = 0.003 * std::pow (10000.0 / 3.0, (g() % 1000) / 999.0);
            const double t2 = 0.003 * std::pow (10000.0 / 3.0, (g() % 1000) / 999.0);
            e.setSettings (make (t1 < 1.0 ? t1 : 0.003, t2 < 1.0 ? t2 : 0.003, g() % 2 == 0, (int) (g() % 3)));
        };
        auto manualRng = std::make_shared<std::mt19937> (seed * 7);
        r.manual = [manualRng] (long long pos) { return (pos / 20000) % 5 == 2; };
        r.go (sr, make (0.01, 0.02, true, modeLoop), evs, total);
        for (size_t i = 1; i < r.tr.size(); ++i) worst = std::max (worst, std::abs (r.tr[i].level - r.tr[i - 1].level));
        samples += (long long) r.tr.size();
    }
    // The steepest legal step: Log starts at about 5.03 / (samples of the stage); 3 ms at 48 kHz is 144 samples, and a
    // part-way stage rounded to one sample covers less than 1.5 / 144 of the way.
    expect (worst < 0.06, "no jumps in random sessions with every setting changing (largest step " + str (worst) + ")");
    std::printf ("no jump: %lld samples of random playing, largest step between samples %.4f\n", samples, worst);
}

// ================================================================================================ 6. notes and the button
static void testNotes()
{
    const double sr = 48000.0;
    // Velocity 0 is a release (the plugin turns it into a note-off); All Notes Off releases the channel.
    {
        Run r;
        r.go (sr, make (0.01, 0.05, true, modeAsr), { on (0), on (100, 61), { 5000, InputEvent::allNotesOff, 0, 0 } }, 10000);
        expect (r.tr[4999].stage == Stage::hold && r.tr[5000].stage == Stage::fall, "ASR: All Notes Off releases");
    }
    // Back-to-back: a note starting where the last ends restarts in ASR (the top: a one-sample rise, the Rise gate).
    {
        Run r;
        r.go (sr, make (0.01, 0.05, true, modeAsr), { on (0), off (5000), on (5000, 62), off (9000, 62) }, 15000);
        expect (r.tr[5000].stage == Stage::rise && r.tr[5000].started && r.tr[5001].stage == Stage::hold && r.tr[9000].stage == Stage::fall,
                "ASR back-to-back: a new rise from the top, then the hold");
    }
    // A zero-length note: AR plays a whole cycle.
    {
        Run r;
        r.go (sr, make (0.01, 0.05, true, modeAr), { on (100), off (100) }, 5000);
        expect (r.tr[100].stage == Stage::rise && r.tr[100 + N (0.01, sr)].stage == Stage::fall, "AR: a zero-length note plays a whole cycle");
    }
    // The Trigger button: AR plays a cycle, ASR holds while held.
    {
        Run r;
        r.manual = [] (long long pos) { return pos >= 1024 && pos < 30720; };
        r.go (sr, make (0.01, 0.05, true, modeAsr), {}, 40000);
        expect (r.tr[1024].stage == Stage::rise && r.tr[20000].stage == Stage::hold && r.tr[30720].stage == Stage::fall, "Trigger button in ASR: rise, hold while held, fall");
        Run a;
        a.manual = [] (long long pos) { return pos >= 1024 && pos < 1536; };
        a.go (sr, make (0.05, 0.05, true, modeAr), {}, 10000);
        expect (a.tr[1024].stage == Stage::rise && a.tr[1024 + N (0.05, sr)].stage == Stage::fall, "Trigger button in AR: a whole cycle");
    }
}

// ================================================================================================ 7. rest
static void testRest()
{
    const double sr = 48000.0;
    for (int mode : { modeAsr, modeLoop })
    {
        Run r;
        Receiver rx;
        r.onBlock = [] (Engine& e, long long pos) { if (pos == 10240) e.panic(); };
        r.go (sr, make (0.2, 0.5, true, mode), { on (0), off (20000) }, 30000);
        long long zeroAt = -1;
        for (auto& m : r.col.msgs) if (rx.take (m) && rx.value == 0 && m.at >= 10240 && zeroAt < 0) zeroAt = m.at;
        expect (zeroAt == 10240, "rest (mode " + str ((long long) mode) + "): 0 sent at the first sample (" + str (zeroAt) + ")");
        if (mode == modeAsr)
            expect (r.tr[10240].stage == Stage::idle && r.tr[19999].stage == Stage::idle && r.tr[25000].stage == Stage::idle,
                    "rest: at rest at once, and the key's later release does nothing");
        else
            expect (r.tr[10240].stage == Stage::rise && r.tr[10240].level == 0.0 && r.tr[10240].started, "rest in Loop: starts again from 0");
    }
}

// ================================================================================================ 8. CC rules
static void testCcRules()
{
    long long values = 0, corners = 0;
    for (unsigned seed = 1; seed <= 8; ++seed)
    {
        const double sr = seed % 3 == 0 ? 44100.0 : (seed % 3 == 1 ? 48000.0 : 96000.0);
        std::mt19937 rng (seed + 100);
        std::vector<Ev> evs;
        const long long total = (long long) sr * 60;
        for (long long t = 0; t < total; t += 1 + rng() % 30000)
            evs.push_back (rng() % 2 ? on (t, 60) : off (t, 60));
        Run r;
        r.blockSize = randomBlocks (seed + 200);
        auto g = std::make_shared<std::mt19937> (seed + 300);
        r.onBlock = [g] (Engine& e, long long) {
            if ((*g)() % 300 != 0) return;
            const double t1 = 0.003 * std::pow (10000.0 / 3.0, ((*g)() % 1000) / 999.0 * 0.6);
            const double t2 = 0.003 * std::pow (10000.0 / 3.0, ((*g)() % 1000) / 999.0 * 0.6);
            e.setSettings (make (t1, t2, (*g)() % 2 == 0, (int) ((*g)() % 3)));
        };
        r.go (sr, make (0.05, 0.2, true, modeAr), evs, total);
        Receiver rx;
        bool everyValueExact = true, onePerBlock = true, coarseOk = true, cornersOk = true, restQuiet = true;
        std::map<long long, int> finesPerBlock;
        int lastCoarseSent = -1;
        for (size_t k = 0; k < r.col.msgs.size(); ++k)
        {
            const auto& m = r.col.msgs[k];
            if (m.note || (m.num != kFunctionCoarseCc && m.num != kFunctionFineCc)) continue;
            if (m.num == kFunctionCoarseCc)
            {
                const bool next = k + 1 < r.col.msgs.size() && r.col.msgs[k + 1].num == kFunctionFineCc && r.col.msgs[k + 1].at == m.at;
                coarseOk = coarseOk && next && m.val != lastCoarseSent;
                lastCoarseSent = m.val;
            }
            if (rx.take (m))
            {
                ++values;
                ++finesPerBlock[m.block];
                everyValueExact = everyValueExact && rx.value == toFourteenBit (r.tr[(size_t) m.at].level);
                if (m.num == kFunctionFineCc && (rx.value >> 7) != lastCoarseSent) coarseOk = false;
            }
        }
        for (auto& [b, n] : finesPerBlock) onePerBlock = onePerBlock && n == 1;
        // Corners: in every block with a corner, the last corner's value is what the receiver holds at the block's end.
        Receiver rx2;
        size_t mi = 0;
        long long blockStart = 0;
        for (size_t b = 0; b < r.edges.size(); ++b)
        {
            const long long end = r.edges[b];
            long long lastCorner = -1;
            bool anyMove = false;
            for (long long i = blockStart; i < end; ++i)
            {
                if (r.tr[(size_t) i].corner) lastCorner = i;
                if (i > 0 && r.tr[(size_t) i].level != r.tr[(size_t) i - 1].level) anyMove = true;
            }
            const int heldBefore = rx2.value;
            bool sentHere = false, sentBeforeCorner = false;
            while (mi < r.col.msgs.size() && r.col.msgs[mi].at < end)
            {
                const bool v = rx2.take (r.col.msgs[mi]);
                sentHere = v || sentHere;
                if (v && r.col.msgs[mi].at < lastCorner) sentBeforeCorner = true;
                ++mi;
            }
            if (lastCorner >= 0)
            {
                ++corners;
                const int want = toFourteenBit (r.tr[(size_t) lastCorner].level);
                // The corner's value arrives in its block, unless the receiver already held exactly that value (then a
                // later move in the same block may follow it).
                const bool good = rx2.value == want || (heldBefore == want && ! sentBeforeCorner);
                if (! good && cornersOk) std::printf ("corner block %zu [%lld,%lld) last corner %lld level %.6f stage %d, receiver %d, sentHere %d\n", b, blockStart, end, lastCorner, r.tr[(size_t) lastCorner].level, (int) r.tr[(size_t) lastCorner].stage, rx2.value, (int) sentHere);
                cornersOk = cornersOk && good;
            }
            if (! anyMove && blockStart > 0 && r.tr[(size_t) blockStart].stage == Stage::idle && rx2.value == 0
                && r.tr[(size_t) blockStart - 1].stage == Stage::idle)
                restQuiet = restQuiet && ! sentHere;
            blockStart = end;
        }
        expect (everyValueExact, "CC: every value sent is the function's value at that sample (seed " + str ((long long) seed) + ")");
        expect (onePerBlock, "CC: at most one value per block");
        expect (coarseOk, "CC: coarse only when it changed, right before its fine part");
        expect (cornersOk, "CC: corners exact (the top and 0 arrive exactly)");
        expect (restQuiet, "CC: nothing sent at rest");
    }
    std::printf ("CC rules: %lld values checked, %lld blocks with corners\n", values, corners);
}

// ================================================================================================ 9. gates and EOC
namespace
{
    struct PulseState
    {
        int ccVal[17][128] {};
        bool noteOn[17][128] {};
        int doubled = 0, strayOff = 0, starts = 0;
        void take (const Msg& m)
        {
            if (! m.note && (m.num == 82 || m.num == 83 || m.num == 90)) { starts += m.val == 127 ? 1 : 0; ccVal[m.ch][m.num] = m.val; }
            if (m.note && m.num >= 45 && m.num <= 47)
            {
                if (m.val > 0) { doubled += noteOn[m.ch][m.num] ? 1 : 0; noteOn[m.ch][m.num] = true; ++starts; }
                else           { strayOff += noteOn[m.ch][m.num] ? 0 : 1; noteOn[m.ch][m.num] = false; }
            }
        }
        bool anythingOn() const
        {
            for (int ch = 0; ch <= 16; ++ch) for (int n = 0; n < 128; ++n) if (ccVal[ch][n] != 0 || noteOn[ch][n]) return true;
            return false;
        }
    };
    // The samples at which output k (as CC) went on and off.
    std::vector<std::pair<long long, long long>> spans (const std::vector<Msg>& msgs, int cc)
    {
        std::vector<std::pair<long long, long long>> v;
        for (auto& m : msgs)
        {
            if (m.note || m.num != cc) continue;
            if (m.val == 127) v.push_back ({ m.at, -1 });
            else if (! v.empty()) v.back().second = m.at;
        }
        return v;
    }
}

static void testPulses()
{
    long long exactEdges = 0;
    const std::vector<int> sizes { 32, 64, 100, 128, 256, 512, 1000, 1024, -1 };
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int size : sizes)
        {
            const long long L = (long long) std::llround (kPulseSeconds * sr);
            const double rise = 0.05, fall = 0.1;
            const long long R = N (rise, sr), F = N (fall, sr);
            Run r;
            r.blockSize = size > 0 ? std::function<int()> ([size] { return size; }) : randomBlocks (77);
            r.go (sr, make (rise, fall, true, modeAr), { on (500), off (510) }, 500 + R + F + 3 * L);
            const auto rg = spans (r.col.msgs, 82), fg = spans (r.col.msgs, 83), eoc = spans (r.col.msgs, 90);
            const bool ok = rg.size() == 1 && rg[0].first == 500 && rg[0].second == 500 + R
                         && fg.size() == 1 && fg[0].first == 500 + R && fg[0].second == 500 + R + F
                         && eoc.size() == 1 && eoc[0].first == 500 + R + F && eoc[0].second == 500 + R + F + L;
            expect (ok, "gates exact at " + str (sr) + " Hz, blocks " + str ((long long) size));
            bool chOk = true;
            for (auto& m : r.col.msgs) chOk = chOk && m.ch == kDefaultChannel;
            expect (chOk, "everything on channel 14");
            exactEdges += 6;
        }
    const double sr = 48000.0;
    const long long L = 960;
    // Stretching: a 3 ms rise keeps the Rise gate high for 20 ms (the Fall gate starts at 3 ms all the same).
    {
        Run r;
        r.go (sr, make (0.003, 0.5, true, modeAr), { on (0), off (5) }, 30000);
        const auto rg = spans (r.col.msgs, 82), fg = spans (r.col.msgs, 83);
        expect (rg.size() == 1 && rg[0].second == L && fg.size() == 1 && fg[0].first == N (0.003, sr), "a short rise: its gate stretched to 20 ms");
    }
    // Fast Loop: the gates and EOC merge and stay high (one start each); slow Loop: one EOC per cycle.
    {
        Run r;
        r.go (sr, make (0.003, 0.003, true, modeLoop), {}, 48000);
        const auto rg = spans (r.col.msgs, 82), fg = spans (r.col.msgs, 83), eoc = spans (r.col.msgs, 90);
        expect (rg.size() == 1 && fg.size() == 1 && eoc.size() == 1 && rg[0].second < 0 && eoc[0].second < 0, "fast Loop (6 ms): gates and EOC stay high (merged)");
        Run s;
        s.go (sr, make (0.02, 0.03, true, modeLoop), {}, 48000);
        const auto e2 = spans (s.col.msgs, 90);
        const long long cyc = N (0.02, sr) + N (0.03, sr);
        bool every = (long long) e2.size() == 47999 / cyc;
        for (size_t i = 0; i < e2.size(); ++i) every = every && e2[i].first == (long long) (i + 1) * cyc;
        expect (every, "Loop 50 ms: one EOC at the end of every cycle (" + str ((long long) e2.size()) + ")");
    }
    // A fall interrupted by a new rise: no EOC; the Fall gate ends, the Rise gate starts.
    {
        Run r;
        const long long R = N (0.05, sr);
        r.go (sr, make (0.05, 0.3, true, modeAr), { on (0), off (10), on (R + 3000, 62), off (R + 3010, 62) }, 60000);
        const auto eoc = spans (r.col.msgs, 90), rg = spans (r.col.msgs, 82), fg = spans (r.col.msgs, 83);
        expect (eoc.size() == 1 && rg.size() == 2 && rg[1].first == R + 3000 && fg[0].second == R + 3000, "interrupted fall: no EOC, Fall gate ends, Rise gate starts");
    }
    // Note and Both.
    for (int mode : { sendAsNote, sendAsBoth })
    {
        Run r;
        Settings s = make (0.05, 0.1, true, modeLoop);
        s.sendAs = mode;
        r.go (sr, s, {}, 48000 * 3);
        PulseState ps;
        int notes = 0, ccs = 0;
        bool vel = true;
        for (auto& m : r.col.msgs)
        {
            ps.take (m);
            if (m.note) { ++notes; vel = vel && (m.val == 0 || m.val == 100) && m.num >= 45 && m.num <= 47; }
            if (! m.note && (m.num == 82 || m.num == 83 || m.num == 90)) ++ccs;
        }
        expect (notes > 100 && vel && ps.doubled == 0 && ps.strayOff == 0, std::string ("Send as ") + (mode == sendAsNote ? "Note" : "Both") + ": notes 45-47, velocity 100, balanced");
        expect ((mode == sendAsNote) == (ccs == 0), "Send as Note sends no gate CC; Both sends both");
    }
    // Changes while a gate is on end it at once and never start one; rest ends everything.
    {
        Run r;
        Settings s = make (2.0, 2.0, true, modeLoop);
        s.sendAs = sendAsBoth;
        std::vector<Settings> seq;
        r.onBlock = [&] (Engine& e, long long pos) {
            const long long b = pos / 512;
            Settings x = s;
            if (b == 20) { x.sendAs = sendAsCc; e.setSettings (x); }
            if (b == 30) { x.sendAs = sendAsCc; x.outChannel = 5; e.setSettings (x); }
            if (b == 40) { x.sendAs = sendAsCc; x.outChannel = 5; x.pulseOn[kRiseGateIndex] = false; e.setSettings (x); }
            if (b == 50) { x.sendAs = sendAsCc; x.outChannel = 5; e.setSettings (x); }
            if (b == 60) e.panic();
        };
        r.go (sr, s, {}, 512 * 70);
        PulseState ps;
        int startsAfterFirst = 0;
        for (auto& m : r.col.msgs)
        {
            ps.take (m);
            if (m.at > 0 && m.at < 60 * 512 && ((! m.note && m.val == 127 && m.num == 82) || (m.note && m.val > 0))) ++startsAfterFirst;
        }
        expect (startsAfterFirst == 0, "Send as, channel and switch changes during the rise never start a gate (" + str ((long long) startsAfterFirst) + ")");
        bool endedAt20 = false;
        for (auto& m : r.col.msgs) if (m.at == 20 * 512 && m.note && m.num == 45 && m.val == 0) endedAt20 = true;
        expect (endedAt20 && ps.doubled == 0 && ps.strayOff == 0, "Send as changed: the gate note ends at once");
        // After the rest the Loop starts again and its new rise opens the Rise gate, on channel 5.
        bool reopened = false;
        for (auto& m : r.col.msgs) if (m.at == 60 * 512 && ! m.note && m.num == 82 && m.val == 127 && m.ch == 5) reopened = true;
        expect (reopened, "after a rest, the Loop's new rise opens the Rise gate again");
    }
}

// ================================================================================================ 10. fuzz
static void testFuzz()
{
    long long events = 0;
    for (unsigned seed = 1; seed <= 4; ++seed)
    {
        const double sr = 48000.0;
        std::mt19937 rng (seed * 1000);
        Engine e;
        e.prepare (sr);
        Settings s = make (0.01, 0.02, true, modeAr);
        e.setSettings (s);
        Collector col;
        bool down[16][128] {};
        long long pos = 0;
        std::vector<InputEvent> buf;
        for (int b = 0; b < 18000; ++b)
        {
            const int n = rng() % 40 == 0 ? (int) (rng() % 2) : 1 + (int) (rng() % 1024);
            buf.clear();
            const int count = (int) (rng() % 6);
            std::vector<int> offs;
            for (int k = 0; k < count; ++k) offs.push_back (n > 0 ? (int) (rng() % (unsigned) n) : 0);
            std::sort (offs.begin(), offs.end());
            for (int at : offs)
            {
                const int ch = (int) (rng() % 3), nt = 58 + (int) (rng() % 6);
                const unsigned kind = rng() % 20;
                if (kind == 0)     { buf.push_back ({ at, InputEvent::allNotesOff, (std::uint8_t) ch, 0 }); for (auto& d : down[ch]) d = false; }
                else if (kind < 10) { buf.push_back ({ at, InputEvent::noteOn, (std::uint8_t) ch, (std::uint8_t) nt }); down[ch][nt] = true; }
                else               { buf.push_back ({ at, InputEvent::noteOff, (std::uint8_t) ch, (std::uint8_t) nt }); down[ch][nt] = false; }
                ++events;
            }
            if (rng() % 30 == 0)
            {
                s.fn.rise = 0.003 * std::pow (10.0, (rng() % 1000) / 999.0 * 2.0);
                s.fn.fall = 0.003 * std::pow (10.0, (rng() % 1000) / 999.0 * 2.0);
                s.fn.logCurve = rng() % 2 == 0;
                s.fn.mode = (int) (rng() % 3);
                s.outChannel = 1 + (int) (rng() % 16);
                s.sendAs = (int) (rng() % 3);
                s.outOn[0] = rng() % 5 != 0;
                for (auto& p : s.pulseOn) p = rng() % 4 != 0;
                e.setSettings (s);
            }
            if (rng() % 500 == 0) e.panic();
            if (rng() % 3000 == 0) e.prepare (rng() % 2 ? 44100.0 : 48000.0);
            col.blockStart = pos;
            const bool manual = (b / 700) % 6 == 3;
            if (rng() % 100 == 0) e.flushOnly (n, col);
            else e.render (n, buf.data(), (int) buf.size(), manual, col);
            ++col.block;
            pos += n;
        }
        // Release everything, AR, outputs on, wait.
        buf.clear();
        for (int ch = 0; ch < 16; ++ch) for (int nt = 0; nt < 128; ++nt) if (down[ch][nt]) buf.push_back ({ 0, InputEvent::noteOff, (std::uint8_t) ch, (std::uint8_t) nt });
        s.fn.mode = modeAr; s.fn.rise = 0.01; s.fn.fall = 0.01; s.outOn[0] = true;
        e.setSettings (s);
        col.blockStart = pos;
        e.render (512, buf.data(), (int) buf.size(), false, col);
        pos += 512;
        for (int k = 0; k < 200; ++k) { col.blockStart = pos; e.render (512, nullptr, 0, false, col); pos += 512; }
        PulseState ps;
        int coarse[17] {}, value[17];
        for (auto& v : value) v = -1;
        for (auto& m : col.msgs)
        {
            ps.take (m);
            if (! m.note && m.num == kFunctionCoarseCc) coarse[m.ch] = m.val;
            if (! m.note && m.num == kFunctionFineCc) value[m.ch] = coarse[m.ch] * 128 + m.val;
        }
        bool allZero = true;
        for (int ch = 1; ch <= 16; ++ch) allZero = allZero && value[ch] <= 0;
        expect (e.function().stage() == Stage::idle && ! e.gate(), "fuzz " + str ((long long) seed) + ": at rest after every key is released");
        expect (allZero, "fuzz " + str ((long long) seed) + ": every channel's receiver at 0");
        expect (! ps.anythingOn() && ps.doubled == 0 && ps.strayOff == 0 && ! e.pulses().anyOn(),
                "fuzz " + str ((long long) seed) + ": no gate CC or gate note left on, none doubled (" + str ((long long) ps.starts) + " starts)");
    }
    std::printf ("stuck-gate and stuck-note fuzz: %lld random events in 72,000 blocks, nothing stuck\n", events);
}

// ================================================================================================ 11. CPU and MIDI rate
static void testCpuAndRate()
{
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr);
    e.setSettings (make (0.4, 0.6, true, modeLoop));
    struct Null : MidiSink { long long n = 0; void cc (int, int, int, int) override { ++n; } void note (int, int, int, int) override { ++n; } } sink;
    const int seconds = 120;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long pos = 0; pos < (long long) sr * seconds; pos += 64) e.render (64, nullptr, 0, false, sink);
    const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double percent = 100.0 * used / seconds;
    expect (percent < 2.0, "CPU under 2% of one core (" + str (percent) + "%)");
    std::printf ("CPU: %.4f%% of one core at 48 kHz, 64-sample blocks, Loop 1 s, all outputs on\n", percent);
    std::printf ("MIDI: %.0f messages a second (Loop, Rise 400 ms, Fall 600 ms, all outputs, CC, 64-sample blocks)\n", sink.n / (double) seconds);
    Engine e2;
    e2.prepare (sr);
    e2.setSettings (make (0.4, 0.6, true, modeLoop));
    Null s2;
    for (long long pos = 0; pos < (long long) sr * seconds; pos += 256) e2.render (256, nullptr, 0, false, s2);
    std::printf ("MIDI at 256-sample blocks: %.0f messages a second\n", s2.n / (double) seconds);
    expect (s2.n / (double) seconds < 400.0, "MIDI rate reasonable at 256-sample blocks");
}

int main()
{
    testCurve();
    testTiming();
    testModes();
    testCurveInUse();
    testNoJump();
    testNotes();
    testRest();
    testCcRules();
    testPulses();
    testFuzz();
    testCpuAndRate();
    std::printf ("engine tests: %lld checks, %lld failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
