// Mesh (LoRa), radio2: a second MeshCore channel decoded from the same capture as the first (MeshReceiver::setRadio2). MeshCore adverts on
// the US preset (910.525 MHz, SF7, 62.5 kHz) and on radio2's own channel go through one receiver: both decode, radio2's packets and decoder
// are named "radio2". Also: radio2 outside the capture is listed but not searched until the rate meshRadio2RateHz() asks for; radio2 on a
// setting the plan already searches adds no demodulator; MeshCore switched off (setProtocols) leaves radio2 out; radio2 beside a custom setting;
// the test signal's radio2 network (mesh_gen.h modeVal[2]) through the receiver for a whole cycle.
#include "dect2/gen_util.h"
#include "dect2/mesh_gen.h"
#include "dect2/modes.h"
#include "dect2/mesh_lora.h"
#include "dect2/mesh_presets.h"
#include "dect2/mesh_proto.h"
#include "dect2/mesh_rx.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace dect2;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

struct Tx { MeshLoraSettings s; std::vector<uint8_t> pkt; double startSec; };

static lora::Params params(const MeshLoraSettings& s) {
    lora::Params p;
    p.sf = s.sf; p.bwHz = s.bwHz; p.cr = s.cr; p.preamble = s.preamble; p.syncWord = s.syncWord; p.ldro = meshLoraLdro(s.sf, s.bwHz);
    return p;
}

// the frames at their own frequencies, the radio tuned so that `tuned` sits tuneOffsetHz below its centre (as the app tunes it)
static MeshTelemetry run(MeshReceiver& rx, const std::vector<Tx>& txs, double tuned, double rate) {
    const double off = -meshTuning().tuneOffsetHz, noise = 0.03;
    std::vector<lora::TxFrame> frs;
    double end = 0;
    for (const Tx& t : txs) {
        lora::TxFrame fr;
        fr.p = params(t.s);
        fr.data = lora::encode(fr.p, t.pkt.data(), t.pkt.size());
        fr.startSec = t.startSec;
        fr.freqHz = t.s.freqHz - tuned + off + 400;   // a small carrier error
        fr.sroPpm = 2;
        fr.amp = (float)std::sqrt(std::pow(10.0, 10 / 10.0) * noise * noise * t.s.bwHz / rate);   // 10 dB SNR in the channel
        end = std::max(end, fr.endSec() + 64 * lora::symbolSeconds(t.s.sf, t.s.bwHz));
        frs.push_back(fr);
    }
    std::vector<cf32> x((size_t)((end + 0.2) * rate), cf32(0, 0));
    for (const auto& fr : frs) fr.render(x.data(), x.size(), 0, rate);
    genutil::NoiseSource ns(11);
    ns.add(x.data(), x.size(), (float)(noise / std::sqrt(2.0)));
    rx.configure(rate);
    rx.setSignalOffset(off);
    rx.setTunedHz(tuned);
    for (size_t i = 0; i < x.size(); i += 65536) rx.feed(x.data() + i, std::min<size_t>(65536, x.size() - i));
    MeshTelemetry t;
    rx.telemetry(t, 0);
    return t;
}

static MeshLoraSettings mc(double mhz, int sf, double bwKhz, int cr) {
    MeshCorePresetEntry e;
    e.title = "t"; e.mhz = mhz; e.sf = sf; e.bwKhz = bwKhz; e.cr = cr;
    return meshCorePresetToLora(e);   // sync 0x12, preamble 32 up to SF8 and 16 above
}

static const MeshDecoderInfo* decoder(const MeshTelemetry& t, const std::string& name) {
    for (const auto& d : t.decoders) if (d.preset == name) return &d;
    return nullptr;
}
static int packets(const MeshTelemetry& t, const std::string& preset) {
    int n = 0;
    for (const auto& p : t.packets) n += p.preset == preset && p.crcOk;
    return n;
}
static bool node(const MeshTelemetry& t, const std::string& name) {
    for (const auto& n : t.nodes) if (n.longName == name) return true;
    return false;
}

int main() {
    const double tuned = 910.525e6;
    MeshCoreNode a, b;
    a.seed = 21; a.name = "Radio one node"; a.lat = 47.61; a.lon = -122.33;
    b.seed = 22; b.name = "Radio two node"; b.lat = 47.62; b.lon = -122.32; b.repeater = true;
    const MeshLoraSettings us = mc(910.525, 7, 62.5, 5);        // the band plan's US preset
    const MeshLoraSettings r2 = mc(911.150, 8, 62.5, 8);        // radio2: 625 kHz above, SF8 4/8
    const std::vector<Tx> both = {{us, meshcoreAdvert(a, 1790000000), 0.05}, {r2, meshcoreAdvert(b, 1790000000), 0.08}};   // overlapping in time

    // 1. the US plan and radio2 at the mesh mode's usual 2 Msps
    {
        MeshReceiver rx;
        rx.setRegion(1); rx.setProtocols(2);
        rx.setRadio2(true, r2);
        const double need = meshRadio2RateHz(tuned + meshTuning().tuneOffsetHz, r2.freqHz, r2.bwHz);
        const MeshTelemetry t = run(rx, both, tuned, 2e6);
        const MeshDecoderInfo* d = decoder(t, "radio2");
        printf("US + radio2 911.150 at 2 Msps (needs %.1f): %zu decoders, radio2 %s, %d / %d packets, nodes %d %d\n", need / 1e6, t.decoders.size(),
               d ? (d->inBand ? "in band" : "OUT") : "missing", packets(t, "radio2"), (int)t.packets.size(), node(t, a.name), node(t, b.name));
        CHECK(need <= 2e6, "911.150 fits at 2 Msps (%.2f)", need / 1e6);
        CHECK(d && d->inBand && d->protocol == 2 && d->sf == 8 && std::fabs(d->freqHz - r2.freqHz) < 1, "radio2 decoder");
        CHECK(packets(t, "radio2") == 1, "one packet on radio2 (%d)", packets(t, "radio2"));
        CHECK(node(t, a.name) && node(t, b.name), "both nodes heard");
        CHECK(t.blocksBad == 0, "%llu CRC errors", (unsigned long long)t.blocksBad);
    }
    // 2. radio2 4.5 MHz up: listed, not searched at 2 Msps; at the rate meshRadio2RateHz asks for, decoded
    {
        const MeshLoraSettings far = mc(915.0, 8, 62.5, 8);
        const double need = meshRadio2RateHz(tuned + meshTuning().tuneOffsetHz, far.freqHz, far.bwHz);
        const std::vector<Tx> txs = {{us, meshcoreAdvert(a, 1790000000), 0.05}, {far, meshcoreAdvert(b, 1790000000), 0.08}};
        MeshReceiver lo, hi;
        for (MeshReceiver* r : {&lo, &hi}) { r->setRegion(1); r->setProtocols(2); r->setRadio2(true, far); }
        const MeshTelemetry tl = run(lo, txs, tuned, 2e6), th = run(hi, txs, tuned, need);
        const MeshDecoderInfo* dl = decoder(tl, "radio2");
        const MeshDecoderInfo* dh = decoder(th, "radio2");
        printf("radio2 915.000: needs %.1f Msps; at 2 Msps %s, %d packets; at %.1f Msps %s, %d packets\n", need / 1e6, dl && dl->inBand ? "in band" : "out of band",
               packets(tl, "radio2"), need / 1e6, dh && dh->inBand ? "in band" : "out of band", packets(th, "radio2"));
        CHECK(dl && !dl->inBand && packets(tl, "radio2") == 0, "at 2 Msps radio2 is listed but not searched");
        CHECK(node(tl, a.name), "radio 1 still decodes");
        CHECK(need > 2e6 && need <= 20e6, "rate %.2f Msps", need / 1e6);
        CHECK(dh && dh->inBand && packets(th, "radio2") == 1 && node(th, b.name), "at %.1f Msps radio2 decodes", need / 1e6);
        // the margin: half a step less is not enough
        CHECK(meshRadio2RateHz(0, 4e6, 62.5e3) == 9.5e6, "rate for 4 MHz off %.2f", meshRadio2RateHz(0, 4e6, 62.5e3) / 1e6);
        CHECK(meshRadio2RateHz(0, 0, 0) == 0, "no width");
    }
    // 3. radio2 on the US preset itself: no second demodulator, the entry is renamed
    {
        MeshReceiver rx;
        rx.setRegion(1); rx.setProtocols(2);
        MeshReceiver base;
        base.setRegion(1); base.setProtocols(2);
        rx.setRadio2(true, us);
        const MeshTelemetry t = run(rx, {{us, meshcoreAdvert(a, 1790000000), 0.05}}, tuned, 2e6);
        const MeshTelemetry t0 = run(base, {{us, meshcoreAdvert(a, 1790000000), 0.05}}, tuned, 2e6);
        bool renamed = false;
        for (const auto& d : t.decoders) renamed |= d.preset.find(" + radio2") != std::string::npos && std::fabs(d.freqHz - us.freqHz) < 1 && d.sf == 7;
        printf("radio2 = US preset: %zu decoders (plan alone %zu), renamed %d, %llu frames\n", t.decoders.size(), t0.decoders.size(), renamed,
               (unsigned long long)t.blocksOk);
        CHECK(t.decoders.size() == t0.decoders.size() && renamed && t.blocksOk == 1, "no duplicate demodulator");
    }
    // 4. MeshCore off: no radio2
    {
        MeshReceiver rx;
        rx.setRegion(1); rx.setProtocols(1);
        rx.setRadio2(true, r2);
        const MeshTelemetry t = run(rx, both, tuned, 2e6);
        CHECK(!decoder(t, "radio2") && packets(t, "radio2") == 0, "Meshtastic only: radio2 not searched");
    }
    // 5. beside a custom setting (radio 1 on its own LoRa setting at the tuned frequency); off again: gone
    {
        MeshReceiver rx;
        rx.setProtocols(31);
        MeshLoraSettings c = mc(910.525, 9, 125, 5);
        c.freqHz = 0;   // the tuned frequency
        rx.setCustom(true, c);
        rx.setRadio2(true, r2);
        const MeshLoraSettings c1 = mc(910.525, 9, 125, 5);
        const MeshTelemetry t = run(rx, {{c1, meshcoreAdvert(a, 1790000000), 0.05}, {r2, meshcoreAdvert(b, 1790000000), 0.07}}, tuned, 2e6);
        printf("custom SF9 125 kHz + radio2: %zu decoders, %llu good frames, nodes %d %d\n", t.decoders.size(), (unsigned long long)t.blocksOk,
               node(t, a.name), node(t, b.name));
        CHECK(t.decoders.size() == 2 && decoder(t, "radio2") && node(t, a.name) && node(t, b.name), "custom + radio2");
        rx.setRadio2(false, r2);
        const MeshTelemetry t2 = run(rx, {{c1, meshcoreAdvert(a, 1790000000), 0.05}}, tuned, 2e6);
        CHECK(t2.decoders.size() == 1 && !decoder(t2, "radio2"), "radio2 off");
    }

    // 6. the test signal with radio2 (US scene placed for MeshCore, 910.525 MHz; radio2 911.150 MHz SF8 4/8), one whole cycle
    {
        SynthConfig cfg;
        cfg.mode = 22; cfg.snrDb = 20; cfg.modeVal[0] = 8;
        cfg.modeOpt[0] = 2; cfg.modeOpt[1] = 1; cfg.modeOpt[4] = 1;   // MeshCore only, US, tuned to MeshCore
        cfg.modeVal[2] = r2.freqHz; cfg.modeOpt[5] = r2.sf; cfg.modeOpt[6] = r2.cr; cfg.modeVal[3] = r2.bwHz;
        const double rate = 2e6;
        auto syn = makeMeshSynth(cfg, rate);
        MeshReceiver rx;
        rx.setRegion(1); rx.setProtocols(2); rx.setRadio2(true, r2);
        rx.configure(rate);
        rx.setSignalOffset(-meshTuning().tuneOffsetHz);
        rx.setTunedHz(tuned);
        std::vector<cf32> buf(65536);
        for (size_t done = 0; done < (size_t)(62 * rate); done += buf.size()) { syn->generate(buf.data(), buf.size()); rx.feed(buf.data(), buf.size()); }
        MeshTelemetry t;
        rx.telemetry(t, 0);
        const MeshDecoderInfo* d2 = decoder(t, "radio2");
        uint64_t f1 = 0;
        for (const auto& d : t.decoders) if (d.preset != "radio2" && d.protocol == 2) f1 += d.frames;
        printf("test signal with radio2: first channel %llu frames, radio2 %llu frames (%llu CRC errors), %zu nodes, %zu messages\n", (unsigned long long)f1,
               d2 ? (unsigned long long)d2->frames : 0ULL, d2 ? (unsigned long long)d2->crcBad : 0ULL, t.nodes.size(), t.messages.size());
        CHECK(d2 && d2->inBand && d2->frames >= 5 && d2->crcBad == 0, "radio2 frames from the test signal");
        CHECK(f1 >= 5 && t.blocksBad == 0 && t.nodes.size() == 3, "first channel and nodes");
        // without radio2 in the receiver, the radio2 frames go unheard (and do not disturb the first channel)
        auto syn2 = makeMeshSynth(cfg, rate);
        MeshReceiver rx0;
        rx0.setRegion(1); rx0.setProtocols(2);
        rx0.configure(rate);
        rx0.setSignalOffset(-meshTuning().tuneOffsetHz);
        rx0.setTunedHz(tuned);
        for (size_t done = 0; done < (size_t)(62 * rate); done += buf.size()) { syn2->generate(buf.data(), buf.size()); rx0.feed(buf.data(), buf.size()); }
        MeshTelemetry t0;
        rx0.telemetry(t0, 0);
        CHECK(!decoder(t0, "radio2") && t0.blocksOk == f1 && t0.blocksBad == 0, "without radio2: %llu frames (with: %llu)", (unsigned long long)t0.blocksOk,
              (unsigned long long)f1);
    }

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
