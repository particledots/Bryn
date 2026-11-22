// Runs the real Bryn Mapper script with a stand-in for Logic's Scripter, fed by the engine's real MIDI stream
// (from bryn_ccdump). Also chains it with copies of the Hillside, Ogdoad and Strange Drift Mappers (tests/fixtures),
// in every order, to show the four plugins can share a track. Adapted from Hillside's mapper test.
// Usage: BRYN_CCDUMP=<path to bryn_ccdump> node mapper_test.js <Bryn-Mapper.js> <Hillside-Mapper.js> <Ogdoad-Mapper.js> <StrangeDrift-Mapper.js>

"use strict";
const fs = require("fs");
const vm = require("vm");
const { execFileSync } = require("child_process");

let checks = 0, failures = 0;
function expect(ok, what) { checks++; if (!ok) { failures++; console.log("FAIL: " + what); } }

const scriptText = fs.readFileSync(process.argv[2], "utf8");
const hillsideText = fs.readFileSync(process.argv[3], "utf8");
const ogdoadText = fs.readFileSync(process.argv[4], "utf8");
const driftText = fs.readFileSync(process.argv[5], "utf8");
const dumpExe = process.env.BRYN_CCDUMP;

function readDump(seconds, channel, sendAs) {
    const lines = execFileSync(dumpExe, [String(seconds), String(channel), String(sendAs || 0)], { maxBuffer: 1 << 28 }).toString().trim().split("\n");
    const d = { events: [], notes: [], finals: [], pulsesOn: -1 };
    for (const line of lines) {
        const p = line.split(" ");
        if (p[0] === "final") d.finals[+p[1]] = +p[2];
        else if (p[0] === "pulses-on") d.pulsesOn = +p[1];
        else if (p[0] === "note") d.notes.push({ sample: +p[1], channel: +p[2], pitch: +p[3], velocity: +p[4] });
        else d.events.push({ sample: +p[0], channel: +p[1], cc: +p[2], value: +p[3] });
    }
    return d;
}
const CH = 14;
const dump = readDump(60, CH);
const events = dump.events, finals = dump.finals;
expect(events.length > 2000, "the engine produced a CC stream (" + events.length + " messages)");
const OUR_CC = [80, 81, 82, 83, 90];
expect(events.every(e => e.channel === CH && OUR_CC.includes(e.cc)), "the stream uses only CC 80, 81, 82, 83 and 90 on channel 14");
expect(dump.notes.length === 0, "Send as CC (the default): no notes are sent");
expect(finals[0] === 0, "the Function ends at 0");
expect(dump.pulsesOn === 0, "no gate or EOC left on at the end");

// The gates and EOC in the real stream: 127 / 0 only, strictly alternating, ending at 0; every pulse at least 20 ms
// (960 samples at 48 kHz).
const GATE_CC = [82, 83, 90];
const GATE_NAME = { 82: "Rise gate", 83: "Fall gate", 90: "EOC" };
const pulsesOf = n => events.filter(e => e.cc === n);
for (const n of GATE_CC) {
    const p = pulsesOf(n);
    const name = GATE_NAME[n];
    expect(p.length >= 20 && p.every((e, i) => e.value === (i % 2 === 0 ? 127 : 0)), name + ": CC " + n + " alternates 127 / 0 (" + p.length + " messages)");
    expect(p.length % 2 === 0, name + ": ends at 0");
    let shortest = Infinity;
    for (let i = 1; i < p.length; i += 2) shortest = Math.min(shortest, p[i].sample - p[i - 1].sample);
    expect(shortest >= 960, name + ": every pulse at least 20 ms (shortest " + shortest + " samples)");
}

// What the engine meant at each fine message (coarse * 128 + fine).
function decode(evs) {
    let coarse = 0;
    const values = [];
    for (const e of evs) {
        if (e.cc === 80) coarse = e.value;
        if (e.cc === 81) values.push((coarse * 128 + e.value) / 16383);
    }
    return values;
}
const truth = decode(events);
expect(truth.length > 500, "the Function moves (" + truth.length + " values)");
expect(Math.max(...truth) === 1 && Math.min(...truth) === 0, "the stream reaches the very top and 0");

// ---- A stand-in for Scripter ---------------------------------------------------------------------------------
function makeScripter(text, settings, callParameterChanged, passTo) {
    const out = { passed: [], targets: [] };
    class Event { send() { out.passed.push(this); if (passTo) passTo(this); } trace() {} }
    class ControlChange extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, number: 0, value: 0 }, o); } }
    class NoteOn extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, pitch: 60, velocity: 100 }, o); } }
    class NoteOff extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, pitch: 60, velocity: 0 }, o); } }
    class PitchBend extends Event { constructor(o) { super(); Object.assign(this, { channel: 1, value: 0 }, o); } }
    class TargetEvent { constructor() { this.target = ""; this.value = 0; } send() { out.targets.push({ target: this.target, value: this.value }); } }
    const values = {};
    const sandbox = {
        Event, ControlChange, NoteOn, NoteOff, PitchBend, TargetEvent, Math,
        GetParameter: (name) => { if (!(name in values)) throw new Error("GetParameter unknown name " + name); return values[name]; },
        Trace: () => {},
    };
    vm.createContext(sandbox);
    vm.runInContext(text, sandbox);
    const params = vm.runInContext("PluginParameters", sandbox);
    params.forEach(p => { if (p.type !== "target") values[p.name] = p.defaultValue !== undefined ? p.defaultValue : 0; });
    for (const [name, v] of Object.entries(settings)) {
        if (!(name in values)) throw new Error("test uses unknown setting " + name);
        values[name] = v;
        if (callParameterChanged) sandbox.ParameterChanged(params.findIndex(p => p.name === name), v);
    }
    return { sandbox, params, out, values };
}
const near = (a, b) => Math.abs(a - b) < 1e-9;
const lane = (s, n) => s.out.targets.filter(t => t.target === "Lane " + n + " target").map(t => t.value);

// ---- 1. The script itself -----------------------------------------------------------------------------------------
{
    const { params } = makeScripter(scriptText, {}, false);
    const names = params.map(p => p.name);
    expect(params.length === 33, "33 settings (channel + 8 lanes x 4)");
    expect(new Set(names).size === names.length, "setting names are unique");
    expect(params.filter(p => p.type === "target").length === 8, "8 Learn targets");
    expect(params.every(p => ["lin", "menu", "target"].includes(p.type)), "only setting types Scripter knows");
    expect(params[0].name === "Bryn MIDI channel" && params[0].defaultValue === 14, "channel setting, 14 by default");
    const src = params.find(p => p.name === "Lane 1 source").valueStrings;
    expect(src.join() === "Off,Function,Rise gate,Fall gate,EOC", "Source menu in its fixed order");
    expect(src.every(x => /^[ -~]+$/.test(x)) && /^[\x09\x0a\x0d -~]*$/.test(scriptText), "plain characters only (safe to paste)");
    expect(!/\.trace\(\)/.test(scriptText), "no trace printing in the mapper");
    const hsNames = makeScripter(hillsideText, {}, false).params.map(p => p.name);
    const ogNames = makeScripter(ogdoadText, {}, false).params.map(p => p.name);
    const sdNames = makeScripter(driftText, {}, false).params.map(p => p.name);
    expect(hsNames[0] !== names[0] && ogNames[0] !== names[0] && sdNames[0] !== names[0], "its channel setting has its own name");
}

// ---- 2. Nothing set up: Bryn's CC stops, everything else passes ------------------------------------------------------
{
    const s = makeScripter(scriptText, {}, false);
    const { ControlChange, NoteOn, NoteOff, PitchBend } = s.sandbox;
    for (const e of events) s.sandbox.HandleMIDI(new ControlChange({ channel: CH, number: e.cc, value: e.value }));
    expect(s.out.passed.length === 0 && s.out.targets.length === 0, "the whole stream stops at the mapper, no knob moves");
    s.sandbox.HandleMIDI(new NoteOn({ pitch: 64 }));
    s.sandbox.HandleMIDI(new NoteOff({ pitch: 64 }));
    s.sandbox.HandleMIDI(new NoteOn({ channel: CH, pitch: 45 }));            // a gate note plays the instrument
    s.sandbox.HandleMIDI(new PitchBend({ value: 1000 }));
    s.sandbox.HandleMIDI(new ControlChange({ channel: 1, number: 1, value: 90 }));
    s.sandbox.HandleMIDI(new ControlChange({ channel: 1, number: 80, value: 90 }));   // CC 80 on another channel
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 3, value: 5 }));    // Hillside
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 85, value: 127 })); // Hillside Gate A
    s.sandbox.HandleMIDI(new ControlChange({ channel: 16, number: 20, value: 5 }));   // Ogdoad LFO 1
    s.sandbox.HandleMIDI(new ControlChange({ channel: 15, number: 106, value: 5 }));  // Strange Drift
    s.sandbox.HandleMIDI(new ControlChange({ channel: CH, number: 7, value: 100 }));  // volume on 14
    expect(s.out.passed.length === 11, "notes, pitch bend, other CC, Hillside's, Ogdoad's and Strange Drift's CC pass through (" + s.out.passed.length + ")");
    expect(s.out.passed[0].pitch === 64 && s.out.passed[0] instanceof NoteOn, "notes pass unchanged");
}

// ---- 3. Lanes set up, real stream ----------------------------------------------------------------------------------
const dedupe = a => a.filter((v, i) => i === 0 || v !== a[i - 1]);
const eq = (a, b) => a.length === b.length && a.every((v, i) => near(v, b[i]));
for (const viaParameterChanged of [true, false]) {
    const how = viaParameterChanged ? " (settings changed in the panel)" : " (settings restored with a project)";
    const settings = {
        "Lane 1 source": 1,
        "Lane 2 source": 1, "Lane 2 min %": 30, "Lane 2 max %": 70,
        "Lane 3 source": 1, "Lane 3 min %": 100, "Lane 3 max %": 0,
        "Lane 4 source": 0,
    };
    const s = makeScripter(scriptText, settings, viaParameterChanged);
    const { ControlChange, NoteOn } = s.sandbox;
    let notesIn = 0;
    const t0 = process.hrtime.bigint();
    events.forEach((e, k) => {
        s.sandbox.HandleMIDI(new ControlChange({ channel: CH, number: e.cc, value: e.value }));
        if (k % 300 === 0) { s.sandbox.HandleMIDI(new NoteOn({ pitch: 40 + (k % 30) })); notesIn++; }
    });
    const usPerEvent = Number(process.hrtime.bigint() - t0) / 1000 / events.length;
    const l1 = lane(s, 1), l2 = lane(s, 2), l3 = lane(s, 3), l4 = lane(s, 4);
    expect(eq(l1, dedupe(truth)), "lane 1 follows the Function value for value" + how);
    expect(eq(l2, dedupe(truth.map(v => 0.3 + 0.4 * v))), "lane 2: the Function at 30..70% (around the middle, as the bipolar output)" + how);
    expect(eq(l3, dedupe(truth.map(v => 1 - v))), "lane 3: the Function upside down" + how);
    expect(l4.length === 0, "a lane set to Off never moves" + how);
    expect(near(l1[l1.length - 1], 0) && near(l2[l2.length - 1], 0.3) && near(l3[l3.length - 1], 1), "lanes end at rest (Min)" + how);
    expect(Math.max(...l1) === 1 && Math.min(...l1) === 0, "the Function sweeps the whole range, top and bottom exactly" + how);
    const distinct = new Set(l1.map(v => Math.round(v * 16383))).size;
    expect(distinct > 300, "14-bit detail reaches the target (" + distinct + " distinct values)" + how);
    const notCoarse = l1.filter(v => Math.round(v * 16383) % 128 !== 0).length > l1.length / 2;
    expect(notCoarse, "values arrive in 14-bit steps, not 128 coarse steps" + how);
    expect(s.out.passed.length === notesIn, "notes still pass with lanes in use" + how);
    console.log("mapper: " + usPerEvent.toFixed(2) + " microseconds per CC message in Node" + how);
}

// ---- 4. Gate and EOC sources, fed the engine's real gate stream -------------------------------------------------------
{
    const settings = {
        "Lane 1 source": 2, "Lane 1 min %": 10, "Lane 1 max %": 90,
        "Lane 2 source": 3,
        "Lane 3 source": 4, "Lane 3 min %": 100, "Lane 3 max %": 0,
        "Lane 4 source": 1,
    };
    const want = (n, min, max) => dedupe(pulsesOf(n).map(e => e.value === 127 ? max : min));
    for (const viaParameterChanged of [true, false]) {
        const how = viaParameterChanged ? " (settings changed in the panel)" : " (settings restored with a project)";
        const s = makeScripter(scriptText, settings, viaParameterChanged);
        const { ControlChange } = s.sandbox;
        for (const e of events) s.sandbox.HandleMIDI(new ControlChange({ channel: CH, number: e.cc, value: e.value }));
        expect(eq(lane(s, 1), want(82, 0.1, 0.9)) && lane(s, 1).length >= 20, "Rise gate lane follows the real Rise gate stream, 10..90%" + how);
        expect(eq(lane(s, 2), want(83, 0, 1)), "Fall gate lane follows the real Fall gate stream" + how);
        expect(eq(lane(s, 3), want(90, 1, 0)), "EOC lane follows the real EOC stream, upside down" + how);
        expect(eq(lane(s, 4), dedupe(truth)), "a Function lane alongside them is unaffected" + how);
        const ends = [1, 2, 3].map(n => { const l = lane(s, n); return l[l.length - 1]; });
        expect(near(ends[0], 0.1) && near(ends[1], 0) && near(ends[2], 1), "gate lanes end at rest (Min)" + how);
        expect(s.out.passed.length === 0, "gate and EOC CC stop at the mapper" + how);
    }
    // The gates line up with the function: while the Rise gate is high (longer than 20 ms), the Function never goes down.
    const fnAt = []; { let c = 0; for (const e of events) { if (e.cc === 80) c = e.value; if (e.cc === 81) fnAt.push({ sample: e.sample, v: (c * 128 + e.value) / 16383 }); } }
    const rOn = pulsesOf(82);
    let wrong = 0;
    for (let i = 0; i + 1 < rOn.length; i += 2) {
        if (rOn[i + 1].sample - rOn[i].sample <= 960) continue;   // a short rise is stretched into the fall
        const inside = fnAt.filter(x => x.sample >= rOn[i].sample && x.sample < rOn[i + 1].sample);
        for (let k = 1; k < inside.length; k++) if (inside[k].v < inside[k - 1].v) wrong++;
    }
    expect(wrong === 0, "the Function only rises while the Rise gate is high (" + wrong + " falls)");
}

// ---- 4b. Send as Note and Both: the gate notes reach the instrument; the CC (Both) still moves the lanes --------------
{
    const nd = readDump(20, CH, 1);
    expect(nd.events.every(e => [80, 81].includes(e.cc)), "Note: no gate CC is sent");
    expect(nd.notes.length > 50 && nd.notes.every(n => n.channel === CH && n.pitch >= 45 && n.pitch <= 47), "Note: gate notes 45-47 on channel 14 (" + nd.notes.length + ")");
    const open = {};
    let balanced = true;
    for (const n of nd.notes) {
        if (n.velocity > 0) { balanced = balanced && !open[n.pitch] && n.velocity === 100; open[n.pitch] = true; }
        else { balanced = balanced && !!open[n.pitch]; open[n.pitch] = false; }
    }
    expect(balanced && Object.values(open).every(v => !v) && nd.pulsesOn === 0, "Note: every gate note is released once, nothing left on");
    const s = makeScripter(scriptText, { "Lane 1 source": 2, "Lane 2 source": 4 }, true);
    const { NoteOn, NoteOff } = s.sandbox;
    for (const n of nd.notes) s.sandbox.HandleMIDI(n.velocity > 0 ? new NoteOn({ channel: CH, pitch: n.pitch, velocity: n.velocity }) : new NoteOff({ channel: CH, pitch: n.pitch }));
    expect(s.out.passed.length === nd.notes.length && lane(s, 1).length === 0 && lane(s, 2).length === 0,
           "Note: the gate notes pass the mapper to the instrument unchanged; gate lanes stay put");

    const bd = readDump(20, CH, 2);
    const bCc = bd.events.filter(e => e.cc === 82), bNotes = bd.notes.filter(n => n.pitch === 45);
    expect(bCc.length > 10 && bCc.length === bNotes.length && bCc.every((e, i) => e.sample === bNotes[i].sample && (e.value === 127) === (bNotes[i].velocity > 0)),
           "Both: the Rise gate as CC and as a note, at the same samples");
    const b = makeScripter(scriptText, { "Lane 1 source": 2 }, true);
    for (const e of bd.events) b.sandbox.HandleMIDI(new b.sandbox.ControlChange({ channel: CH, number: e.cc, value: e.value }));
    for (const n of bd.notes) b.sandbox.HandleMIDI(new b.sandbox.NoteOn({ channel: CH, pitch: n.pitch, velocity: n.velocity }));
    const l1 = lane(b, 1);
    expect(l1.length === bCc.length && near(l1[l1.length - 1], 0) && b.out.passed.length === bd.notes.length, "Both: the lane follows the CC, the notes pass");
}

// ---- 5. A different channel ------------------------------------------------------------------------------------------
{
    const d = readDump(10, 7);
    expect(d.events.every(e => e.channel === 7) && d.events.length > 100, "out channel 7: the engine sends on channel 7");
    const s = makeScripter(scriptText, { "Bryn MIDI channel": 7, "Lane 1 source": 1 }, true);
    const { ControlChange } = s.sandbox;
    for (const e of events.slice(0, 400)) s.sandbox.HandleMIDI(new ControlChange({ channel: CH, number: e.cc, value: e.value }));
    expect(s.out.targets.length === 0 && s.out.passed.length === 400, "channel 7 set: channel 14 CC is left alone");
    for (const e of d.events) s.sandbox.HandleMIDI(new ControlChange({ channel: e.channel, number: e.cc, value: e.value }));
    expect(s.out.targets.length > 50 && s.out.passed.length === 400, "channel 7 set: channel 7 CC is mapped and stopped");
}

// ---- 6. Sharing a track with Hillside, Ogdoad and Strange Drift: the four mappers chained, in all 24 orders ------------
//         Also with everyone on the same channel (16): the numbers never overlap.
for (const brynChannel of [CH, 16]) {
    // Bryn's real stream, with stand-in Hillside (channel 16: Envelope = CC 3 + 35, Gate A = CC 85), Ogdoad (channel 16:
    // LFO 1 = CC 20 + 52, trig = CC 102) and Strange Drift (channel 15: Ripple X = CC 106 + 113) streams mixed in.
    const mixed = [];
    let k = 0;
    for (const e of events) {
        mixed.push({ channel: brynChannel, number: e.cc, value: e.value });
        if (k++ % 2 === 0) {
            const v = (k * 37) % 16384, w = (k * 91) % 16384, h = (k * 53) % 16384;
            mixed.push({ channel: 16, number: 20, value: v >> 7 }, { channel: 16, number: 52, value: v & 127 });
            mixed.push({ channel: 15, number: 106, value: w >> 7 }, { channel: 15, number: 113, value: w & 127 });
            mixed.push({ channel: 16, number: 3, value: h >> 7 }, { channel: 16, number: 35, value: h & 127 });
            if (k % 301 === 1) mixed.push({ channel: 16, number: 102, value: 127 }, { channel: 16, number: 102, value: 0 });
            if (k % 401 === 1) mixed.push({ channel: 16, number: 85, value: 127 }, { channel: 16, number: 85, value: 0 });
        }
    }
    const lastOf = (ch, fine) => { for (let i = mixed.length - 1; i >= 0; i--) if (mixed[i].channel === ch && mixed[i].number === fine) return (mixed[i - 1].value * 128 + mixed[i].value) / 16383; };
    const lastOg = lastOf(16, 52), lastSd = lastOf(15, 113), lastHs = lastOf(16, 35);
    const carry = (to, ev) => new to.sandbox[ev.constructor.name](Object.assign({}, ev));
    const kinds = {
        B: { text: scriptText,   settings: { "Bryn MIDI channel": brynChannel, "Lane 1 source": 1, "Lane 2 source": 4 } },
        H: { text: hillsideText, settings: { "Lane 1 source": 1, "Lane 2 source": 3 } },
        O: { text: ogdoadText,   settings: { "Lane 1 source": 1, "Lane 2 source": 17 } },
        S: { text: driftText,    settings: { "Lane 1 source": 1 } },
    };
    const perms = a => a.length <= 1 ? [a] : a.flatMap((x, i) => perms(a.slice(0, i).concat(a.slice(i + 1))).map(p => [x].concat(p)));
    const names = { B: "Bryn", H: "Hillside", O: "Ogdoad", S: "Strange Drift" };
    for (const order of perms(["B", "H", "O", "S"])) {
        const instrument = [];
        const made = {};
        let next = ev => instrument.push(ev);
        for (const letter of order.slice().reverse()) {
            const to = next;
            const m = makeScripter(kinds[letter].text, kinds[letter].settings, true, to);
            made[letter] = m;
            next = ev => m.sandbox.HandleMIDI(carry(m, ev));
        }
        const first = made[order[0]];
        for (const m of mixed) first.sandbox.HandleMIDI(new first.sandbox.ControlChange(m));
        first.sandbox.HandleMIDI(new first.sandbox.NoteOn({ pitch: 50 }));
        const b1 = lane(made.B, 1), b2 = lane(made.B, 2), h1 = lane(made.H, 1), h2 = lane(made.H, 2), o1 = lane(made.O, 1), o2 = lane(made.O, 2), s1 = lane(made.S, 1);
        const tag = " (order " + order.map(c => names[c]).join(", ") + " Mapper; Bryn on channel " + brynChannel + ")";
        expect(eq(b1, dedupe(truth)), "Bryn lane follows the Function exactly" + tag);
        expect(eq(b2, want2(pulsesOf(90))), "Bryn EOC lane follows EOC" + tag);
        expect(near(h1[h1.length - 1], lastHs) && h1.length > 100 && h2.length > 4, "Hillside lanes follow Hillside's stream" + tag);
        expect(near(o1[o1.length - 1], lastOg) && o1.length > 100 && o2.length > 4, "Ogdoad lanes follow Ogdoad's stream" + tag);
        expect(near(s1[s1.length - 1], lastSd) && s1.length > 100, "Strange Drift lane follows Strange Drift's stream" + tag);
        expect(instrument.length === 1 && instrument[0].pitch === 50, "only the note reaches the instrument: each plugin's CC stops at its own mapper" + tag);
    }
}
function want2(p) { return dedupe(p.map(e => e.value === 127 ? 1 : 0)); }

if (failures === 0) console.log("mapper tests: " + checks + " checks passed");
else console.log("mapper tests: " + failures + " of " + checks + " checks FAILED");
process.exit(failures === 0 ? 0 : 1);
