// MeshCore adjustable bandwidths: meshCoreSearchSettings / meshCorePresetWithBandwidth, the receiver's extra MeshCore bandwidths
// (setMeshCoreBandwidths) and the test signal's MeshCore bandwidth (modeVal[1]). A MeshCore network moved to 125 kHz is not heard by the
// band plan's presets alone (EU: 62.5 kHz on 869.618 MHz), and is heard once 125 kHz is added to the search; a wrong extra bandwidth
// does not help.
#include "dect2/gen_util.h"
#include "dect2/mesh_gen.h"
#include "dect2/mesh_presets.h"
#include "dect2/mesh_rx.h"
#include "dect2/modes.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace dect2;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int count(const std::vector<MeshLoraSettings>& v, double mhz, int sf, double bwHz) {
    int n = 0;
    for (const auto& s : v) n += std::fabs(s.freqHz - mhz * 1e6) < 1 && s.sf == sf && std::fabs(s.bwHz - bwHz) < 1;
    return n;
}

int main() {
    // ---- the helpers
    std::vector<MeshCorePresetEntry> eu;
    for (const auto& p : meshCoreBuiltinPresets()) if (p.region == "Europe") eu.push_back(p);
    CHECK(!eu.empty(), "no European presets");
    const auto base = meshCoreSearchSettings(eu, {});
    const auto plus = meshCoreSearchSettings(eu, {125e3, 250e3, 0, -5, 5e6, 125e3});   // invalid values and a repeat are skipped
    printf("Europe: %zu presets, %zu settings, %zu with 125 + 250 kHz\n", eu.size(), base.size(), plus.size());
    CHECK(count(base, 869.618, 8, 62.5e3) == 1, "EU narrow once (it is in several presets)");
    CHECK(count(base, 869.618, 8, 125e3) == 0, "125 kHz not in the presets");
    CHECK(count(plus, 869.618, 8, 125e3) == 1, "SF8 at 125 kHz added once");
    CHECK(count(plus, 869.618, 7, 125e3) == 1, "SF7 at 125 kHz added");
    CHECK(count(plus, 869.525, 11, 250e3) == 1, "the deprecated 250 kHz preset not repeated");
    CHECK(count(plus, 433.650, 11, 125e3) == 1, "433 long range at 125 kHz");
    for (size_t i = 0; i < base.size(); i++) CHECK(plus[i].name == base[i].name, "the presets come first (%s)", plus[i].name.c_str());
    bool named = false;
    for (const auto& s : plus) named |= s.name == "EU/UK (Narrow) @ 125 kHz";
    CHECK(named, "extra setting named after its preset");
    for (const auto& s : plus) {
        CHECK(s.syncWord == 0x12 && s.preamble == (s.sf <= 8 ? 32 : 16), "%s: sync / preamble", s.name.c_str());
        CHECK(s.ldro == meshLoraLdro(s.sf, s.bwHz), "%s: LDRO", s.name.c_str());
    }
    CHECK(meshCoreBandwidthValid(7812.5) && meshCoreBandwidthValid(203125) && meshCoreBandwidthValid(1625e3), "valid bandwidths");
    CHECK(!meshCoreBandwidthValid(0) && !meshCoreBandwidthValid(999) && !meshCoreBandwidthValid(1626e3) && !meshCoreBandwidthValid(NAN), "invalid bandwidths");
    const MeshCorePresetEntry w = meshCorePresetWithBandwidth(eu[0], 125e3);
    CHECK(w.title == eu[0].title && w.mhz == eu[0].mhz && w.sf == eu[0].sf && w.cr == eu[0].cr && w.bwKhz == 125, "override keeps the rest");
    CHECK(meshCorePresetWithBandwidth(eu[0], 0).bwKhz == eu[0].bwKhz, "invalid override keeps the preset's bandwidth");
    // SF11 at 62.5 kHz needs LDRO (32.8 ms symbols); at 250 kHz it does not
    MeshCorePresetEntry lr; lr.title = "t"; lr.mhz = 433.65; lr.bwKhz = 250; lr.sf = 11; lr.cr = 5;
    CHECK(!meshCorePresetToLora(lr).ldro && meshCorePresetToLora(meshCorePresetWithBandwidth(lr, 62.5e3)).ldro, "LDRO follows the bandwidth");

    // ---- through the receiver: the test signal's MeshCore network at 125 kHz (EU, 869.618 MHz, SF8, 4/8)
    const double rate = 2e6, secs = 62;
    SynthConfig cfg;
    cfg.mode = 22;
    cfg.snrDb = 20;
    cfg.modeVal[0] = 10;
    cfg.modeOpt[0] = 2;                    // MeshCore only
    cfg.modeVal[1] = 125e3;
    auto syn = makeMeshSynth(cfg, rate);
    struct Rx { const char* what; std::vector<double> bw; MeshReceiver rx; };
    Rx rxs[3] = {{"presets only", {}, {}}, {"+125 kHz", {125e3}, {}}, {"+250 kHz", {250e3}, {}}};
    for (auto& r : rxs) {
        r.rx.setProtocols(2);
        r.rx.setMeshCoreBandwidths(r.bw);
        r.rx.configure(rate);
        r.rx.setSignalOffset(-meshTuning().tuneOffsetHz);
    }
    std::vector<cf32> buf(65536);
    for (size_t done = 0; done < (size_t)(secs * rate); done += buf.size()) {
        syn->generate(buf.data(), buf.size());
        for (auto& r : rxs) r.rx.feed(buf.data(), buf.size());
    }
    MeshTelemetry t[3];
    for (int i = 0; i < 3; i++) {
        rxs[i].rx.telemetry(t[i], 0);
        size_t nodes = 0;
        for (const auto& n : t[i].nodes) nodes += n.protocol == 2;
        bool has125 = false;
        for (const auto& d : t[i].decoders) has125 |= d.protocol == 2 && std::fabs(d.bwHz - 125e3) < 1 && d.inBand;
        printf("%-13s %2zu decoders (125 kHz %s), %llu good frames, %zu MeshCore nodes, %zu messages\n", rxs[i].what, t[i].decoders.size(),
               has125 ? "yes" : "no", (unsigned long long)t[i].blocksOk, nodes, t[i].messages.size());
    }
    size_t nodes1 = 0;
    for (const auto& n : t[1].nodes) nodes1 += n.protocol == 2;
    CHECK(t[0].blocksOk == 0 && t[0].messages.empty(), "presets only: the 125 kHz network should not decode");
    CHECK(t[1].blocksOk >= 5 && nodes1 == 3 && !t[1].messages.empty(), "+125 kHz: %llu frames, %zu nodes", (unsigned long long)t[1].blocksOk, nodes1);
    CHECK(t[1].blocksBad == 0, "+125 kHz: %llu CRC errors", (unsigned long long)t[1].blocksBad);
    CHECK(t[2].blocksOk == 0, "+250 kHz: the wrong bandwidth should not decode");
    CHECK(t[1].decoders.size() > t[0].decoders.size(), "extra demodulators listed");

    // the setter drops invalid values and repeats; setting the same list again changes nothing
    MeshReceiver r;
    r.setProtocols(2);
    r.setMeshCoreBandwidths({125e3, 125e3, 0, 2e6});
    r.configure(rate);
    r.setSignalOffset(-meshTuning().tuneOffsetHz);
    std::vector<cf32> z(65536, cf32(0, 0));
    for (int i = 0; i < 10; i++) r.feed(z.data(), z.size());   // past the first report (every 0.25 s of input)
    MeshTelemetry tz;
    r.telemetry(tz, 0);
    size_t at125 = 0;
    for (const auto& d : tz.decoders) at125 += std::fabs(d.bwHz - 125e3) < 1 && std::fabs(d.freqHz - 869.618e6) < 1;
    CHECK(at125 == 2, "SF7 and SF8 on 869.618 MHz at 125 kHz, once each (%zu)", at125);

    printf(fails ? "FAILED (%d)\n" : "OK\n", fails);
    return fails ? 1 : 0;
}
