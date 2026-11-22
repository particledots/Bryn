// Prints the engine's real MIDI stream, for the mapper test: "sample channel cc value" per CC, and
// "note sample channel number velocity" per note (the gates and EOC in Note or Both mode).
// Plays a fixed pattern of notes (short, long, overlapping) through phases of AR, ASR and Loop, Lin and Log, with
// times that change between phases. At the end it switches to AR and lets the last cycle finish.
// Usage: bryn_ccdump [seconds] [out channel] [send as: 0 CC, 1 Note, 2 Both]

#include "bryn/Engine.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace bryn;

struct Printer : MidiSink
{
    long long blockStart = 0;
    void cc (int offset, int channel, int number, int v) override { std::printf ("%lld %d %d %d\n", blockStart + offset, channel, number, v); }
    void note (int offset, int channel, int number, int v) override { std::printf ("note %lld %d %d %d\n", blockStart + offset, channel, number, v); }
};

int main (int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof (argv[1]) : 30.0;
    const int channel = argc > 2 ? std::atoi (argv[2]) : kDefaultChannel;
    const int sendAs = argc > 3 ? std::atoi (argv[3]) : 0;
    const double sr = 48000.0;
    Engine e;
    e.prepare (sr);
    Settings s;
    s.outChannel = channel;
    s.sendAs = sendAs;
    s.fn.rise = 0.05; s.fn.fall = 0.4; s.fn.logCurve = true; s.fn.mode = modeAr;
    e.setSettings (s);
    Printer p;
    const long long total = (long long) (seconds * sr);
    std::vector<InputEvent> buf;
    long long pos = 0;
    int phase = 0;
    while (pos < total)
    {
        const int n = (int) (total - pos > 512 ? 512 : total - pos);
        buf.clear();
        for (int i = 0; i < n; ++i)
        {
            const long long t = pos + i, cycle = (long long) (2.0 * sr), x = t % cycle;
            if (x == 0) buf.push_back ({ i, InputEvent::noteOn, 0, (std::uint8_t) (48 + (t / cycle) % 24) });
            if (x == (long long) (0.9 * sr)) buf.push_back ({ i, InputEvent::noteOff, 0, (std::uint8_t) (48 + (t / cycle) % 24) });
            if (x == (long long) (1.2 * sr)) buf.push_back ({ i, InputEvent::noteOn, 1, 70 });
            if (x == (long long) (1.25 * sr)) buf.push_back ({ i, InputEvent::noteOff, 1, 70 });
        }
        const int newPhase = (int) (pos / (long long) (5.0 * sr));
        if (newPhase != phase)
        {
            phase = newPhase;
            s.fn.mode = phase % 3 == 0 ? modeAr : (phase % 3 == 1 ? modeAsr : modeLoop);
            s.fn.logCurve = phase % 2 == 0;
            s.fn.rise = phase % 4 < 2 ? 0.003 : 0.3;
            s.fn.fall = phase % 4 == 1 ? 0.15 : 0.6;
            e.setSettings (s);
        }
        p.blockStart = pos;
        e.render (n, buf.data(), (int) buf.size(), false, p);
        pos += n;
    }
    // AR, nothing held: let the last cycle finish, then report where the output ended (14-bit), for the test.
    s.fn.mode = modeAr;
    e.setSettings (s);
    for (int k = 0; k < 400; ++k) { p.blockStart = pos; e.render (512, nullptr, 0, false, p); pos += 512; }
    std::printf ("final 0 %d\n", e.ccStream().lastSent (outFunction));
    std::printf ("pulses-on %d\n", e.pulses().anyOn() ? 1 : 0);
    return 0;
}
