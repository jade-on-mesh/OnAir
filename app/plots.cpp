// spectrum, waterfall, histogram and constellation plots
#include "app.h"
#include "dect2/specan_dsp.h"

// Click on the spectrum or the waterfall to tune to that frequency (a drag still pans). The grid follows the mode: FM 0.1 MHz, DMR 12.5 kHz, else 1 kHz.
static void clickToTune(App& a, int plotId) {
    if (a.family == 55) { generalPlotMouse(a, plotId); return; }   // the general receiver: its channel marker, without retuning the radio
    if (!plt::IsPlotHovered() || a.fmScan.running) return;
    double mhz = plt::GetPlotMousePos().x;
    double lo = 0.001, hi = dect2::kMaxTuneMhz;
    double grid = 0.001;
    if (a.fmMode) grid = 0.1;
    else if (a.family == 9) grid = 0.0125;
    if (a.family >= 6) if (const ModeTuning* mt = modeTuning(a.family + 2)) { lo = mt->minMhz; hi = mt->maxMhz; }
    mhz = std::round(std::min(hi, std::max(lo, mhz)) / grid) * grid;
    if (a.family == 4 || a.family == 1 || a.family == 3) return;   // ATSC and ISDB-T channels sit on a fixed raster: use the frequency field
    const ImVec2 d = ImGui::GetMouseDragDelta(0, 0);
    if (ImGui::IsMouseReleased(0) && std::fabs(d.x) < 4 && std::fabs(d.y) < 4 && std::fabs(mhz - a.freqMhz) > 1e-6) {
        spectrumTune(a, mhz);
        return;
    }
    if (std::fabs(mhz - a.freqMhz) > 1e-6) {
        const plt::Rect lim = plt::GetPlotLimits();   // a red line where a click would tune to
        if (!ImGui::IsMouseDown(0)) {   // the ghost of the channel box: where it will sit after the click, so the whole signal can be put inside it
            const ChanWidth cw = channelWidth(a);
            if (cw.mhz > 0) {
                const double g0 = mhz - cw.mhz / 2, g1 = mhz + cw.mhz / 2;
                if (plotId == 0) { plt::Spec gb; gb.FillColor = ImVec4(0.7f, 0.7f, 0.7f, 0.10f); plt::PlotVBand("##ghost", g0, g1, gb); }
                const double ge[2] = {g0, g1};
                plt::Spec es; es.LineColor = ImVec4(0.8f, 0.8f, 0.8f, 0.45f); es.LineWeight = 1.f;
                plt::PlotInfLines("##ghostedges", ge, 2, es);
                if (plotId == 0) {
                    char lb[48]; snprintf(lb, sizeof lb, "%.4f MHz  (%s)", mhz, widthText(cw.mhz).c_str());
                    plt::PlotText(lb, mhz, lim.Y.Max, ImVec2(0, bandPlanStripHeight() + 4 * gUi));
                }
            }
        }
        const double lx[2] = {mhz, mhz}, ly[2] = {lim.Y.Min, lim.Y.Max};
        plt::Spec ls; ls.LineColor = ImVec4(0.95f, 0.25f, 0.25f, 0.9f); ls.LineWeight = 1.2f;
        plt::PlotLine("##tunehere", lx, ly, 2, ls);
        if (a.tuningMode == 1) ImGui::SetTooltip(tr("click to decode %.4f MHz (radio centre stays)"), mhz);
        else ImGui::SetTooltip(tr("click to tune to %.4f MHz"), mhz);
    }
}

ChanWidth channelWidth(const App& a) {
    ChanWidth w;
    if (a.family == 0) {   // what the engine uses while it runs (Automatic may have just switched), else the choice
        const double act = a.engine.running() ? a.engine.activeBandwidth() : 0;
        w.mhz = act > 0 ? act : kBw[a.bwIdx].mhz;
        w.settable = w.hasAuto = true; w.manual = !a.bwAuto;
    } else if (a.isdbtMode) {   // ISDB-T: the width set, or on automatic the one the receiver works with
        w.settable = w.hasAuto = true; w.manual = a.isdbtBwMhz != 0;
        w.mhz = w.manual ? a.isdbtBwMhz : a.engine.running() && a.rx.standard == 5 ? a.rx.isdbt.bwMhz : 6;
    } else if (a.atscMode) w.mhz = 6;   // ATSC 1.0 and 3.0
    else if (a.dabMode) w.mhz = 1.536;
    else if (a.fmMode) {
        w.settable = w.hasAuto = true; w.manual = a.fmChanKhz > 0;
        w.mhz = (w.manual ? a.fmChanKhz * 1e3 : FmReceiver::kDefaultChannelWidthHz) / 1e6;
    } else if (a.family == 7) { w.mhz = a.dtmbBwMhz; w.settable = w.manual = true; }
    else if (a.family == 40 && a.engine.running() && a.rx.standard == 41 && a.rx.dvbc.symbolRate > 0) w.mhz = a.rx.dvbc.symbolRate * (1 + a.rx.dvbc.rollOff) / 1e6;
    else if (a.family == 6 && a.engine.running() && a.rx.standard == 7 && a.rx.dvbs.symbolRate > 0) w.mhz = a.rx.dvbs.symbolRate * (1 + a.rx.dvbs.rollOff) / 1e6;
    else if (a.family == 55) w.mhz = generalChannelWidthMhz(a);
    else if (const ModeTuning* mt = modeTuning(a.family + 2)) w.mhz = mt->bandwidthMhz;
    return w;
}

std::string widthText(double mhz) {
    char b[32];
    if (mhz >= 1) snprintf(b, sizeof b, "%.4g MHz", mhz); else snprintf(b, sizeof b, "%.4g kHz", mhz * 1e3);
    return b;
}

void channelWidthPopup(App& a) {
    const ChanWidth cw = channelWidth(a);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330 * gUi);
    ImGui::TextDisabled(tr("Channel width: %s"), widthText(cw.mhz).c_str());
    ImGui::Separator();
    if (a.family == 0) {
        if (ImGui::RadioButton(tr("Automatic"), a.bwAuto) && !a.bwAuto) setDvbBandwidth(a, -1);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("The width of the signal is measured in the spectrum and the receiver switches between 1.7, 5, 6, 7 and 8 MHz by itself."));
        if (ImGui::RadioButton(tr("Manual"), !a.bwAuto) && a.bwAuto) setDvbBandwidth(a, a.bwIdx);
        ImGui::BeginDisabled(a.bwAuto);
        ImGui::Indent(ImGui::GetFrameHeight());
        for (int i = 0; i < (int)(sizeof kBw / sizeof *kBw); i++) {
            if (i) ImGui::SameLine();
            if (ImGui::RadioButton(kBw[i].label, !a.bwAuto && i == a.bwIdx)) setDvbBandwidth(a, i);
        }
        ImGui::Unindent(ImGui::GetFrameHeight());
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", tr("DVB-T2 and DVB-T channels sit on this raster; a running radio may be opened again at another sample rate."));
    } else if (a.fmMode) {
        if (ImGui::RadioButton(tr("Automatic (220 kHz)"), a.fmChanKhz == 0) && a.fmChanKhz != 0) { a.fmChanKhz = 0; savePrefs(a); }
        if (ImGui::RadioButton(tr("Manual"), a.fmChanKhz > 0) && a.fmChanKhz == 0) { a.fmChanKhz = 180; savePrefs(a); }
        ImGui::BeginDisabled(a.fmChanKhz == 0);
        ImGui::SameLine();
        int k = a.fmChanKhz > 0 ? a.fmChanKhz : 220;
        ImGui::SetNextItemWidth(150 * gUi);
        if (ImGui::SliderInt("##fmchw", &k, (int)(FmReceiver::kMinChannelWidthHz / 1e3), (int)(FmReceiver::kMaxChannelWidthHz / 1e3), "%d kHz")) a.fmChanKhz = (k + 5) / 10 * 10;
        if (ImGui::IsItemDeactivatedAfterEdit()) savePrefs(a);
        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", tr("The receiver's channel filter. Narrower keeps a strong neighbour out (150 kHz copes with one 10 dB stronger 150 kHz away); "
                            "below about 180 kHz loud passages distort a little and RDS suffers first."));
    } else if (a.isdbtMode) {
        if (ImGui::RadioButton(tr("Automatic"), a.isdbtBwMhz == 0) && a.isdbtBwMhz != 0) setIsdbtBandwidth(a, 0);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("The width is measured in the spectrum, and 6, 8 and 7 MHz are tried in turn while nothing locks."));
        for (int w : {6, 7, 8}) {
            ImGui::SameLine();
            if (ImGui::RadioButton(isdbtWidthLabel(w), a.isdbtBwMhz == w) && a.isdbtBwMhz != w) setIsdbtBandwidth(a, w);
        }
        ImGui::TextDisabled("%s", tr("6 MHz in Japan and the Americas, 8 MHz in the Maldives (ISDB-T International). The signal is the same, scaled in time by the width."));
    } else if (a.family == 7) {
        if (ImGui::RadioButton(tr("8 MHz"), a.dtmbBwMhz != 6) && a.dtmbBwMhz == 6) setDtmbBandwidth(a, 8);
        ImGui::SameLine();
        if (ImGui::RadioButton(tr("6 MHz (Cuba)"), a.dtmbBwMhz == 6) && a.dtmbBwMhz != 6) setDtmbBandwidth(a, 6);
        ImGui::TextDisabled("%s", tr("Set by the country: there is nothing to measure it from. A receiver set to the wrong width finds nothing."));
    } else if (a.family == 6) {
        ImGui::TextUnformatted(tr("Follows the symbol rate: symbol rate x (1 + roll-off)."));
        ImGui::TextDisabled("%s", tr("Choose automatic or a manual symbol rate in the tuner to change it."));
    } else {
        const char* name = tr("This mode");
        for (int i = 0; i < kNumModes; i++) if (modeSelected(a, kModes[i])) name = kModes[i].name;
        ImGui::Text(tr("%s: fixed for this mode."), name);
        ImGui::TextDisabled("%s", tr("The receiver's filters are made for the signal (or for the group of channels it watches), so a manual width would only make reception worse."));
    }
    ImGui::PopTextWrapPos();
}

// Mesh radio2 (a second MeshCore frequency decoded from the same capture): a pink band and centre line, and its name at the top of the plot
static void radio2Band(const App& a, bool fill) {
    double mhz, bw;
    if (a.family + 2 != 22 || !meshRadio2Band(a, mhz, bw)) return;
    const ImVec4 pink = meshRadio2Colour();
    if (fill) { plt::Spec b; b.FillColor = ImVec4(pink.x, pink.y, pink.z, 0.16f); plt::PlotVBand("r2band", mhz - bw / 2, mhz + bw / 2, b); }
    const double e[2] = {mhz - bw / 2, mhz + bw / 2}, c[1] = {mhz};
    plt::Spec es; es.LineColor = ImVec4(pink.x, pink.y, pink.z, fill ? 0.55f : 0.70f); es.LineWeight = 1.f;
    plt::PlotInfLines("r2edges", e, 2, es);
    plt::Spec cs; cs.LineColor = ImVec4(pink.x, pink.y, pink.z, 0.85f); cs.LineWeight = 1.2f;
    plt::PlotInfLines("r2centre", c, 1, cs);
    const plt::Rect lim = plt::GetPlotLimits();
    if (!(lim.X.Max > lim.X.Min) || mhz < lim.X.Min || mhz > lim.X.Max) return;   // off the visible span
    const ImVec2 pp = plt::GetPlotPos(), ps = plt::GetPlotSize();
    const float x = pp.x + (float)((mhz - lim.X.Min) / (lim.X.Max - lim.X.Min)) * ps.x;
    const char* lb = "radio2";
    const ImVec2 ts = ImGui::CalcTextSize(lb);
    if (ps.y < ts.y + 8 * gUi) return;
    const ImVec2 q(std::min(x + 4 * gUi, pp.x + ps.x - ts.x - 4 * gUi), pp.y + (fill ? 4 : 2) * gUi);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(q.x - 3 * gUi, q.y - 1 * gUi), ImVec2(q.x + ts.x + 3 * gUi, q.y + ts.y + 1 * gUi), IM_COL32(20, 22, 26, 200), 3.f * gUi);
    dl->AddText(q, ImGui::GetColorU32(pink), lb);
}

// The channel band behind the trace (spectrum: a light fill and its edges) or on top of the picture (waterfall: the edges only)
static void channelBand(const App& a, bool fill) {
    if (a.family == 55) { generalChannelBand(a, fill); return; }
    const ChanWidth cw = channelWidth(a);
    if (cw.mhz <= 0) return;
    const double x0 = a.freqMhz - cw.mhz / 2, x1 = a.freqMhz + cw.mhz / 2;
    if (fill) { plt::Spec band; band.FillColor = pal::accent(pal::dev() ? 0.14f : 0.16f); plt::PlotVBand("band", x0, x1, band); }
    const double edges[2] = {x0, x1};
    plt::Spec es; es.LineColor = pal::accent(fill ? 0.40f : 0.55f); es.LineWeight = 1.f;
    plt::PlotInfLines("bandedges", edges, 2, es);
    radio2Band(a, fill);
}

// "BW 7 MHz auto" in the top right corner of the spectrum: the width the band shows, and a click opens channelWidthPopup().
// Left out when it would cover yLabel, which the plot draws in its top left corner.
float gBwBtnW = 0, gWideBtnW = 0;   // width of the BW button as drawn this frame (0: none), for the switch beside it
void channelWidthButton(App& a, ImVec2 plotMin, ImVec2 plotSize, const char* yLabel) {
    gBwBtnW = 0;
    const ChanWidth cw = channelWidth(a);
    if (cw.mhz <= 0) return;
    char b[64];
    snprintf(b, sizeof b, "BW %s%s###bwbtn", widthText(cw.mhz).c_str(), cw.hasAuto ? (cw.manual ? " manual" : " auto") : "");
    const ImVec2 sz = ImGui::CalcTextSize(b, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    if (plotSize.x < w + 2 * m + ImGui::CalcTextSize(yLabel).x + 12 * gUi || plotSize.y < sz.y + 2 * m) return;   // no room
    gBwBtnW = w + m;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(plotMin.x + plotSize.x - w - m, plotMin.y + m));
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    ImGui::PushStyleColor(ImGuiCol_Text, cw.manual && cw.hasAuto ? pal::warnAmber() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
    if (ImGui::SmallButton(b)) ImGui::OpenPopup("##bwpop");
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\n\n%s", tr(cw.settable ? "Channel width: the band the receiver works with (the light band).\nClick to choose automatic or a manual width."
                                      : "Channel width: the band the receiver works with (the light band), set by the standard."),
                          tr("Zoom: mouse wheel (both axes), Ctrl+wheel (frequency), Shift+wheel (level).\nCtrl+PageUp / PageDown: step the view by one screen in frequency. Double click: reset."));
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 2 * gUi), ImGuiCond_Appearing, ImVec2(1, 0));   // under the button, right-aligned
    if (ImGui::BeginPopup("##bwpop")) { channelWidthPopup(a); ImGui::EndPopup(); }
    ImGui::SetCursorScreenPos(keep);
}

// "Whole span" switch left of the BW button, for the modes that declare support (modeSupportsWide): decode every signal in the span
void wideSwitch(App& a, ImVec2 plotMin, ImVec2 plotSize) {
    gWideBtnW = 0;
    if (!modeSupportsWide(a.family)) return;
    const char* lb = a.wideDecode ? "Whole span: on###widebtn" : "Whole span###widebtn";
    const ImVec2 sz = ImGui::CalcTextSize(lb, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    const float x = plotMin.x + plotSize.x - gBwBtnW - w - m;
    if (x < plotMin.x + 40 * gUi || plotSize.y < sz.y + 2 * m) return;   // no room
    gWideBtnW = w + m;
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, plotMin.y + m));
    ImVec4 bg = a.wideDecode ? pal::accent(0.55f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); if (!a.wideDecode) bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    if (ImGui::SmallButton(lb)) {
        a.wideDecode = !a.wideDecode;
        if (a.family >= 0 && a.family < 64) { if (a.wideDecode) a.wideFamMask |= 1ull << a.family; else a.wideFamMask &= ~(1ull << a.family); }
        savePrefs(a);
    }
    ImGui::PopStyleColor();
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetTooltip("%s", tr("decode every signal in the visible span, not just the tuned one"));
    if (wideActive(a)) {   // how many decoders run now (each one costs CPU); orange at the limit, where the weakest carriers are dropped
        const int n = (int)a.rx.wide.size(), cap = WideModes::cfgFor(a.family + 1).maxCarriers;
        char cb[40]; snprintf(cb, sizeof cb, "%d decoders", n);
        const ImVec2 cs = ImGui::CalcTextSize(cb);
        if (plotSize.y > 3 * sz.y + 6 * m) {   // under the row of buttons, at the right edge
            const ImVec2 q(plotMin.x + plotSize.x - cs.x - 10 * gUi, plotMin.y + m + sz.y + 2 * ImGui::GetStyle().FramePadding.y + 4 * gUi);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q.x - 4 * gUi, q.y - 1 * gUi), ImVec2(q.x + cs.x + 4 * gUi, q.y + cs.y + 1 * gUi), IM_COL32(20, 22, 26, 200), 3.f * gUi);
            ImGui::GetWindowDrawList()->AddText(q, n >= cap ? IM_COL32(240, 120, 80, 255) : IM_COL32(245, 190, 70, 255), cb);
        }
        if (n >= cap && hovered) ImGui::SetTooltip("%s", tr("The limit of decoders is reached: the weakest carriers are not decoded. Each decoder uses CPU."));
    }
    ImGui::SetCursorScreenPos(keep);
}

// "Multi-channel" switch (DVB-T2 / DVB-T): when the capture holds more than one channel of the chosen width, decode every occupied channel of it, one
// receiver each. Same place as "Whole span" (the TV modes do not have that one). Right click: how many at once, and where the channel raster sits.
static bool multiAvailable(const App& a) {
    if (a.family != 0) return false;
    const double rate = a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate;
    return Engine::slotsInSpan(rate, channelWidth(a).mhz, 0) >= 2;
}
void multiSwitch(App& a, ImVec2 plotMin, ImVec2 plotSize) {
    if (!multiAvailable(a)) return;
    const bool run = a.engine.multiChannelActive();
    const char* lb = a.mtvOn ? "Multi-channel: on###mtvbtn" : "Multi-channel###mtvbtn";
    const ImVec2 sz = ImGui::CalcTextSize(lb, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    const float x = plotMin.x + plotSize.x - gBwBtnW - w - m;
    if (x < plotMin.x + 40 * gUi || plotSize.y < sz.y + 2 * m) return;   // no room
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, plotMin.y + m));
    ImVec4 bg = a.mtvOn ? pal::accent(0.55f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); if (!a.mtvOn) bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    if (ImGui::SmallButton(lb)) { a.mtvOn = !a.mtvOn; savePrefs(a); }
    ImGui::PopStyleColor();
    if (run) {   // how many decoders run now
        int n = 0;
        for (const TvChannelRow& r : a.rx.tvChannels) if (r.running) n++;
        char cb[32]; snprintf(cb, sizeof cb, "%d decoders", n);
        const ImVec2 cs = ImGui::CalcTextSize(cb);
        if (x - cs.x - 10 * gUi > plotMin.x) ImGui::GetWindowDrawList()->AddText(ImVec2(x - cs.x - 8 * gUi, plotMin.y + m + 2 * gUi), IM_COL32(245, 190, 70, 255), cb);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##mtvpop");
    if (ImGui::IsItemHovered()) {
        const std::string note = a.engine.multiChannelNote();
        ImGui::SetTooltip("%s%s%s", tr("decode every channel in the span: a receiver for each occupied channel, one plays, the others keep decoding\n(right click: how many at once, where the channels sit)"),
                          a.mtvOn && !run && !note.empty() ? "\n\n" : "", a.mtvOn && !run ? note.c_str() : "");
    }
    if (ImGui::BeginPopup("##mtvpop")) {
        int mx = a.mtvMax > 0 ? a.mtvMax : Engine::defaultMaxChannels();
        ImGui::SetNextItemWidth(120 * gUi);
        if (ImGui::SliderInt(tr("Receivers at once"), &mx, 1, 8)) { a.mtvMax = mx; savePrefs(a); }
        static const char* kGrid[3] = {"find out", "capture centred on a channel", "capture between two channels"};
        ImGui::SetNextItemWidth(220 * gUi);
        if (ImGui::BeginCombo(tr("Channel raster"), tr(kGrid[a.mtvGrid]))) {
            for (int i = 0; i < 3; i++) if (ImGui::Selectable(tr(kGrid[i]), a.mtvGrid == i)) { a.mtvGrid = i; savePrefs(a); }
            ImGui::EndCombo();
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(keep);
}

// The channels of multi-channel TV: the channel edges and the centre of each on the spectrum (the playing one in the accent colour)
static void multiMarkers(App& a) {
    if (!a.engine.multiChannelActive()) return;
    const double c = radioCenterMhz(a), halfW = channelWidth(a).mhz * 0.5;
    int k = 0;
    for (const TvChannelRow& r : a.rx.tvChannels) {
        if (!r.occupied && !r.running) continue;
        const double f = c + r.offsetHz / 1e6;
        const ImVec4 col = r.selected ? pal::accent(0.9f) : r.locked ? ImVec4(0.55f, 0.85f, 0.55f, 0.6f) : ImVec4(0.85f, 0.75f, 0.45f, 0.5f);
        for (int e = -1; e <= 1; e += 2) {
            const double xx[2] = {f + e * halfW, f + e * halfW}, yy[2] = {a.yMin, a.yMax};
            plt::Spec s; s.LineColor = col; s.LineWeight = r.selected ? 1.5f : 1.f;
            char id[24]; snprintf(id, sizeof id, "##mc%d", k++);
            plt::PlotLine(id, xx, yy, 2, s);
        }
        char lb[48]; snprintf(lb, sizeof lb, "%.3f", f);
        plt::PlotText(lb, f, a.yMax, ImVec2(0, 10));
    }
}

// The carriers the whole-span decoder follows: a thin line and the frequency on the spectrum
static void wideMarkers(App& a) {
    if (!wideActive(a)) return;
    const double c = radioCenterMhz(a);
    int k = 0;
    for (const WideCarrier& w : a.rx.wide) {
        const double f = c + w.hz / 1e6, xx[2] = {f, f}, yy[2] = {a.yMin, a.yMax};
        plt::Spec s; s.LineColor = w.selected ? pal::accent(0.9f) : ImVec4(0.55f, 0.85f, 0.55f, w.active ? 0.6f : 0.3f); s.LineWeight = w.selected ? 1.5f : 1.f;
        char id[24]; snprintf(id, sizeof id, "##wc%d", k++);
        plt::PlotLine(id, xx, yy, 2, s);
    }
}

// The spectrum and the waterfall share one frequency axis: panning or zooming either moves both, and a retune brings both back to the band.
static double gXLo = 0, gXHi = 0;
static void setupFreqAxis(const App& a) {
    const double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
    static double lastC = 0, lastFs = 0;
    const double rc = radioCenterMhz(a);   // the axis shows the radio's real centre, so a signal sits at its true frequency
    if (lastC != rc || lastFs != fs || !(gXHi > gXLo)) { gXLo = rc - fs / 2; gXHi = rc + fs / 2; }   // follow a retune (otherwise keep the user's own zoom)
    lastC = rc; lastFs = fs;
    plt::SetupAxisLimits(plt::X1, rc - fs / 2, rc + fs / 2, plt::Cond_Once);
    plt::SetupAxisLinks(plt::X1, &gXLo, &gXHi);
    plt::SetupAxisFormat(plt::X1, "%.2f");
    plt::SetupAxisConstraints(plt::X1, rc - fs / 2, rc + fs / 2);   // pan and zoom stay inside the band the radio hears
}

// The trace at the full panel width: only the visible bins, and where they are denser than the pixels, the lowest and the highest of
// each pixel column (an envelope), so no peak is lost to a coarse picture. The bin i sits at x0 + i * dx: no array of frequencies is built.
static void envelope(const std::vector<float>& y, double x0, double dx, double lo, double hi, int px, std::vector<double>& ox, std::vector<double>& oy) {
    ox.clear(); oy.clear();
    const size_t n = y.size();
    if (n < 2 || !(hi > lo) || !(dx > 0)) return;
    size_t i0 = (size_t)std::max(0.0, std::floor((lo - x0) / dx) - 1), i1 = (size_t)std::min((double)n - 1, std::ceil((hi - x0) / dx) + 1);
    if (i0 >= n) i0 = n - 1;
    if (i1 <= i0) { i1 = std::min(n - 1, i0 + 1); }
    const size_t cnt = i1 - i0 + 1;
    if (px < 1 || cnt <= (size_t)px * 2) { for (size_t i = i0; i <= i1; i++) { ox.push_back(x0 + i * dx); oy.push_back(y[i]); } return; }
    for (int c = 0; c < px; c++) {
        const size_t a0 = i0 + cnt * c / px, a1 = std::max(a0 + 1, i0 + cnt * (c + 1) / px);
        size_t imn = a0, imx = a0;
        for (size_t i = a0; i < a1 && i < n; i++) { if (y[i] < y[imn]) imn = i; if (y[i] > y[imx]) imx = i; }
        const size_t f = std::min(imn, imx), g = std::max(imn, imx);
        ox.push_back(x0 + f * dx); oy.push_back(y[f]);
        if (g != f) { ox.push_back(x0 + g * dx); oy.push_back(y[g]); }
    }
}

constexpr double kFreqSpanMinMhz = 0.001;   // 1 kHz at full zoom-in

static double sampleRateMhz(const App& a) {
    return (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
}

static double freqSpanFromZoom(const App& a, float zoom) {
    const double maxSpan = std::max(kFreqSpanMinMhz, sampleRateMhz(a));
    const double minSpan = std::min(kFreqSpanMinMhz, maxSpan);
    const double z = std::max(0.0, std::min(1.0, (double)zoom));
    return minSpan * std::pow(maxSpan / minSpan, 1.0 - z);
}

static float freqZoomFromSpan(const App& a, double spanMhz) {
    const double maxSpan = std::max(kFreqSpanMinMhz, sampleRateMhz(a));
    const double minSpan = std::min(kFreqSpanMinMhz, maxSpan);
    spanMhz = std::max(minSpan, std::min(maxSpan, spanMhz));
    const double t = std::log(spanMhz / minSpan) / std::log(maxSpan / minSpan);
    return (float)std::max(0.0, std::min(1.0, 1.0 - t));
}

static void clampFreqViewToBand(const App& a) {
    const double fs = sampleRateMhz(a);
    const double rc = radioCenterMhz(a);
    const double bLo = rc - fs * 0.5, bHi = rc + fs * 0.5;
    if (!(gXHi > gXLo)) { gXLo = bLo; gXHi = bHi; return; }
    double span = std::min(fs, gXHi - gXLo);
    double center = (gXLo + gXHi) * 0.5;
    double lo = center - span * 0.5, hi = center + span * 0.5;
    if (lo < bLo) { lo = bLo; hi = lo + span; }
    if (hi > bHi) { hi = bHi; lo = hi - span; }
    if (lo < bLo) lo = bLo;
    if (hi > bHi) hi = bHi;
    gXLo = lo;
    gXHi = hi;
}

static void applyFreqZoomSlider(App& a, float zoom) {
    specFreqViewUpdate(a);
    double span = std::min(sampleRateMhz(a), freqSpanFromZoom(a, zoom));
    const double center = (gXLo + gXHi) * 0.5;
    gXLo = center - span * 0.5;
    gXHi = center + span * 0.5;
    clampFreqViewToBand(a);
    a.specZoom = zoom;
}

static void syncSpecZoomFromFreqView(App& a) {
    specFreqViewUpdate(a);
    if (gXHi > gXLo) a.specZoom = freqZoomFromSpan(a, gXHi - gXLo);
}

static bool gFreqZoomSliderActive = false;

static void specFreqZoomBeforePlots(App& a) {
    if (gFreqZoomSliderActive) applyFreqZoomSlider(a, a.specZoom);
    else syncSpecZoomFromFreqView(a);
}

namespace {
constexpr float kDbAbsLo = -160.f, kDbAbsHi = 20.f, kDbMinSpan = 10.f;

static void clampDbEndpoints(float& lo, float& hi) {
    lo = std::max(kDbAbsLo, std::min(kDbAbsHi - kDbMinSpan, lo));
    hi = std::max(lo + kDbMinSpan, std::min(kDbAbsHi, hi));
}

static void setLinkedDbRange(App& a, float lo, float hi) {
    clampDbEndpoints(lo, hi);
    a.yMin = lo;
    a.yMax = hi;
    a.wf.autoTrack = false;
    a.wf.minDb = lo;
    a.wf.maxDb = hi;
}

// SDR++-style vertical Zoom / Max / Min beside the combined spectrum and waterfall.
static void dbRangeSliders(App& a, ImVec2 colMin, float colW, float h) {
    if (colW < 28 * gUi || h < 120 * gUi) return;
    gFreqZoomSliderActive = false;
    const float sw = std::min(22.f * gUi, colW - 4 * gUi);
    const float labelH = ImGui::GetTextLineHeight();
    const float gap = 4 * gUi;
    const float trackH = std::max(32.f * gUi, (h - 3 * (labelH + gap) - 2 * gap) / 3.f);
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    auto centerX = [&](const char* t) {
        const float tw = ImGui::CalcTextSize(t).x;
        return colMin.x + std::max(2 * gUi, (colW - tw) * 0.5f);
    };
    auto sliderX = [&] { return colMin.x + (colW - sw) * 0.5f; };
    float y = colMin.y;
    bool save = false;

    ImGui::SetCursorScreenPos(ImVec2(centerX(tr("Zoom")), y));
    ImGui::TextUnformatted(tr("Zoom"));
    y += labelH + gap;
    ImGui::SetCursorScreenPos(ImVec2(sliderX(), y));
    ImGui::PushID("dbz");
    float zoom = a.specZoom;
    if (ImGui::VSliderFloat("##specfreqzoom", ImVec2(sw, trackH), &zoom, 0.f, 1.f, "")) a.specZoom = zoom;
    if (ImGui::IsItemActive()) a.specZoom = zoom;
    gFreqZoomSliderActive = ImGui::IsItemActive();
    if (gFreqZoomSliderActive) applyFreqZoomSlider(a, a.specZoom);
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Frequency zoom (top = narrowest view, down to about 1 kHz)"));
    ImGui::PopID();
    y += trackH + gap;

    ImGui::SetCursorScreenPos(ImVec2(centerX(tr("Max")), y));
    ImGui::TextUnformatted(tr("Max"));
    y += labelH + gap;
    float hiKnob = kDbAbsHi + kDbAbsLo - a.yMax;   // vertical slider: up = lower ceiling (SDR++-style)
    ImGui::SetCursorScreenPos(ImVec2(sliderX(), y));
    ImGui::PushID("dbmax");
    const bool maxDrag = ImGui::VSliderFloat("##mx", ImVec2(sw, trackH), &hiKnob, kDbAbsLo, kDbAbsHi, "");
    if (maxDrag || ImGui::IsItemActive()) {
        float hi = kDbAbsHi + kDbAbsLo - hiKnob;
        hi = std::max(hi, a.yMin + kDbMinSpan);
        a.yMax = hi;
        a.wf.maxDb = hi;
        a.wf.autoTrack = false;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Top of the dB range (higher = more vertical stretch)"));
    ImGui::PopID();
    y += trackH + gap;

    ImGui::SetCursorScreenPos(ImVec2(centerX(tr("Min")), y));
    ImGui::TextUnformatted(tr("Min"));
    y += labelH + gap;
    float loKnob = kDbAbsHi + kDbAbsLo - a.yMin;   // up = lower floor
    ImGui::SetCursorScreenPos(ImVec2(sliderX(), y));
    ImGui::PushID("dbmin");
    const bool minDrag = ImGui::VSliderFloat("##mn", ImVec2(sw, trackH), &loKnob, kDbAbsLo, kDbAbsHi, "");
    if (minDrag || ImGui::IsItemActive()) {
        float lo = kDbAbsHi + kDbAbsLo - loKnob;
        lo = std::min(lo, a.yMax - kDbMinSpan);
        a.yMin = lo;
        a.wf.minDb = lo;
        a.wf.autoTrack = false;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Bottom of the dB range (raises or lowers the trace on screen)"));
    ImGui::PopID();

    ImGui::SetCursorScreenPos(keep);
    if (save) savePrefs(a);
}

// Ctrl+Up / Ctrl+Down move the dB range of the spectrum by 5 dB, Ctrl+Shift+Up / Down widen or narrow it, Ctrl+Home puts it back
// to the default. Only while the spectrum is hovered, like the frequency keys below (#91).
static bool dbRangeKeys(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput) return false;
    const float step = 5;
    float lo = a.yMin, hi = a.yMax;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) { if (io.KeyShift) { lo -= step; hi += step; } else { lo += step; hi += step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) { if (io.KeyShift) { lo += step; hi -= step; } else { lo -= step; hi -= step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_Home)) { lo = -110; hi = -30; }
    else return false;
    lo = std::max(-160.f, std::min(20.f, lo)); hi = std::max(-160.f, std::min(20.f, hi));
    if (hi - lo < 10) return false;
    setLinkedDbRange(a, lo, hi);
    return true;
}
} // namespace

bool specDbRangeKeys(App& a) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput) return false;
    const float step = 5;
    float lo = a.yMin, hi = a.yMax;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) { if (io.KeyShift) { lo -= step; hi += step; } else { lo += step; hi += step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) { if (io.KeyShift) { lo += step; hi -= step; } else { lo -= step; hi -= step; } }
    else if (ImGui::IsKeyPressed(ImGuiKey_Home)) { lo = -110; hi = -30; }
    else return false;
    lo = std::max(-160.f, std::min(20.f, lo)); hi = std::max(-160.f, std::min(20.f, hi));
    if (hi - lo < 10) return false;
    a.yMin = lo;
    a.yMax = hi;
    a.wf.autoTrack = false;
    a.wf.minDb = lo;
    a.wf.maxDb = hi;
    return true;
}

// Ctrl+PageUp / Ctrl+PageDown: the view steps up or down in frequency by its own width
static void freqPageKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.KeyCtrl || io.WantTextInput || !(gXHi > gXLo)) return;
    const double span = gXHi - gXLo;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) { gXLo += span; gXHi += span; }
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) { gXLo -= span; gXHi -= span; }
}

// A small button in a corner of a plot that opens a popup (body draws it). rightUsed: the width of the buttons already at the right edge.
// Returns the width it took (0: no room, nothing drawn).
static float cornerPopup(App& a, const char* label, const char* popId, ImVec2 plotMin, ImVec2 plotSize, float rightUsed, float reserveLeft, float dropY, bool on, const char* tip, void (*body)(App&)) {
    const ImVec2 sz = ImGui::CalcTextSize(label, nullptr, true);
    const float w = sz.x + 2 * ImGui::GetStyle().FramePadding.x, m = 4 * gUi;
    const float x = plotMin.x + plotSize.x - rightUsed - w - m;
    if (x < plotMin.x + reserveLeft || plotSize.y < sz.y + 2 * m + dropY) return 0;   // no room
    const ImVec2 keep = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x, plotMin.y + m + dropY));
    ImVec4 bg = on ? pal::accent(0.55f) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); if (!on) bg.w = 0.85f;
    ImGui::PushStyleColor(ImGuiCol_Button, bg);
    if (ImGui::SmallButton(label)) ImGui::OpenPopup(popId);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 2 * gUi), ImGuiCond_Appearing, ImVec2(1, 0));
    if (ImGui::BeginPopup(popId)) { body(a); ImGui::EndPopup(); }
    ImGui::SetCursorScreenPos(keep);
    return w + m;
}

void specFreqViewUpdate(const App& a) {
    const double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
    static double lastC = 0, lastFs = 0;
    const double rc = radioCenterMhz(a);
    if (lastC != rc || lastFs != fs || !(gXHi > gXLo)) { gXLo = rc - fs / 2; gXHi = rc + fs / 2; }
    lastC = rc;
    lastFs = fs;
    const double bLo = rc - fs / 2, bHi = rc + fs / 2;
    if (gXLo < bLo) { const double s = gXHi - gXLo; gXLo = bLo; gXHi = gXLo + s; }
    if (gXHi > bHi) { const double s = gXHi - gXLo; gXHi = bHi; gXLo = gXHi - s; }
}

void specFreqViewGet(double& loMhz, double& hiMhz) {
    loMhz = gXLo;
    hiMhz = gXHi;
}

void specFreqViewWheel(App& a, float mouseNorm, float wheel, double& loMhz, double& hiMhz) {
    const double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
    const double rc = radioCenterMhz(a);
    const double bLo = rc - fs / 2, bHi = rc + fs / 2;
    loMhz = gXLo;
    hiMhz = gXHi;
    if (!(hiMhz > loMhz)) { loMhz = bLo; hiMhz = bHi; }
    mouseNorm = std::max(0.f, std::min(1.f, mouseNorm));
    const double span = hiMhz - loMhz;
    const double center = loMhz + mouseNorm * span;
    const double factor = wheel > 0 ? 0.88 : 1.0 / 0.88;
    double nspan = std::min(fs, std::max(fs / 512.0, span * factor));
    loMhz = center - mouseNorm * nspan;
    hiMhz = loMhz + nspan;
    if (loMhz < bLo) { loMhz = bLo; hiMhz = loMhz + nspan; }
    if (hiMhz > bHi) { hiMhz = bHi; loMhz = hiMhz - nspan; }
    gXLo = loMhz;
    gXHi = hiMhz;
    syncSpecZoomFromFreqView(a);
}

float specPlotBwBtnW() { return gBwBtnW; }
float specPlotWideBtnW() { return gWideBtnW; }

float specCornerPopup(App& a, const char* label, const char* popId, ImVec2 plotMin, ImVec2 plotSize, float rightUsed, float reserveLeft, float dropY, bool on, const char* tip, void (*body)(App&)) {
    return cornerPopup(a, label, popId, plotMin, plotSize, rightUsed, reserveLeft, dropY, on, tip, body);
}

float& specWaterfallSplit() {
    static float g = 0.5f;
    return g;
}

void resetSpectrum(App& a) {
    a.yMin = -110.f;
    a.yMax = -30.f;
    a.specZoom = 0.f;
    a.wf.autoTrack = true;
    if (!a.smooth.empty()) {
        a.wf.fitRange(a.smooth, true);
        a.peak = a.smooth;
    } else {
        a.wf.minDb = -110.f;
        a.wf.maxDb = -30.f;
    }
    a.wf.paused = false;
    a.wf.nextRowT = 0;
    specWaterfallSplit() = 0.5f;
    savePrefs(a);
}

void wfAutoButton(App& a) {
    const bool on = a.wf.autoTrack;
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, pal::accent(0.55f));
    if (ImGui::SmallButton(tr("Auto"))) { a.wf.autoTrack = !on; if (a.wf.autoTrack) a.wf.fitRange(a.smooth, true); savePrefs(a); }
    if (on) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Keep the waterfall range on the noise floor and the strongest signal (click again to stop following)"));
}

// the colour maps as a list with a strip of each one's colours
static void colourMapList(App& a) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < Waterfall::NumMaps; i++) {
        ImGui::PushID(i);
        const bool sel = a.wf.effMap() == i;
        const float h = ImGui::GetFrameHeight(), w = 130 * gUi;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##map", sel, 0, ImVec2(w + 80 * gUi, h))) { a.wf.map = i; a.wf.palette(); savePrefs(a); }
        dl->AddText(ImVec2(p.x + 4 * gUi, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), tr(Waterfall::mapName(i)));
        const float x0 = p.x + 80 * gUi, y0 = p.y + 3 * gUi, y1 = p.y + h - 3 * gUi;
        const int seg = 16;
        for (int s = 0; s < seg; s++)
            dl->AddRectFilledMultiColor(ImVec2(x0 + w * s / seg, y0), ImVec2(x0 + w * (s + 1) / seg, y1),
                                        Waterfall::color(i, (float)s / seg), Waterfall::color(i, (float)(s + 1) / seg), Waterfall::color(i, (float)(s + 1) / seg), Waterfall::color(i, (float)s / seg));
        ImGui::PopID();
    }
}

void wfDisplayPopup(App& a) {
    if (ImGui::Button(tr("Reset spectrum"))) resetSpectrum(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Restore the default spectrum and waterfall levels and the usual half-and-half split (does not change panel layout)"));
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Colour map"));
    colourMapList(a);
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Intensity (dB)"));
    ImGui::SetNextItemWidth(170 * gUi);
    if (ImGui::DragFloatRange2("##wfdbp", &a.wf.minDb, &a.wf.maxDb, 1, -160, 20, "%.0f", "%.0f")) a.wf.autoTrack = false;
    if (ImGui::IsItemDeactivatedAfterEdit()) savePrefs(a);
    ImGui::SameLine(0, 6 * gUi);
    wfAutoButton(a);
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Speed (rows a second)"));
    static const float rates[] = {0, 1, 2, 5, 10, 15};
    for (int i = 0; i < 6; i++) {
        if (i) ImGui::SameLine(0, 4 * gUi);
        char lb[24]; if (rates[i] == 0) snprintf(lb, sizeof lb, "%s", tr("max")); else snprintf(lb, sizeof lb, "%g", rates[i]);
        if (ImGui::RadioButton(lb, a.wf.rowsPerSec == rates[i])) { a.wf.setRate(rates[i]); savePrefs(a); }
    }
    const double secs = Waterfall::H * (a.wf.timeKnown() || a.wf.nStamps > 8 ? a.wf.rowDt : 1.0 / 30);
    ImGui::TextDisabled(tr("The picture holds the last %.0f s."), secs);
    ImGui::Separator();
    if (ImGui::Checkbox(tr("Pause"), &a.wf.paused)) { if (!a.wf.paused) a.wf.nextRowT = 0; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Stop adding rows: the picture stays, and drag or the wheel on the waterfall scroll and zoom back through it"));
}

// the unit, the offset of the radio and the other choices of the spectrum line
void specDisplayPopup(App& a) {
    if (ImGui::Button(tr("Reset spectrum"))) resetSpectrum(a);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Restore the default spectrum and waterfall levels and the usual half-and-half split (does not change panel layout)"));
    ImGui::Separator();
    bool save = false;
    ImGui::TextDisabled("%s", tr("Trace"));
    ImGui::SetNextItemWidth(150 * gUi);
    ImGui::SliderFloat(tr("Average"), &a.specAvgSec, 0.f, 2.f, a.specAvgSec <= 0.001f ? tr("off") : "%.2f s", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Time constant of the averaged line: longer is calmer"));
    if (ImGui::Checkbox(tr("Max hold"), &a.peakHold)) save = true;
    if (a.peakHold) {
        ImGui::SameLine(0, 8 * gUi);
        ImGui::SetNextItemWidth(110 * gUi);
        ImGui::SliderFloat("##hd", &a.holdDecay, 0.f, 30.f, a.holdDecay <= 0.01f ? tr("holds") : "%.1f dB/s", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemDeactivatedAfterEdit()) save = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("How fast the max-hold line falls; 0: it stays until reset"));
        ImGui::SameLine(0, 8 * gUi);
        if (ImGui::SmallButton(tr("Reset"))) a.peak = a.smooth;
    }
    if (ImGui::Checkbox(tr("Peak markers"), &a.specPeaks)) save = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Frequency and level of the strongest peaks in view"));
    if (ImGui::Checkbox(tr("Level at the cursor"), &a.specReadout)) save = true;
    if (ImGui::Checkbox(tr("Fill under the line"), &a.specFill)) save = true;
    if (ImGui::Checkbox(tr("Grid"), &a.specGrid)) save = true;
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Spectrum click"));
    if (ImGui::RadioButton(tr("Retune radio"), a.tuningMode == 0)) { a.tuningMode = 0; save = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Move the radio to the clicked frequency (the usual behaviour)"));
    if (ImGui::RadioButton(tr("Decode offset only"), a.tuningMode == 1)) { a.tuningMode = 1; save = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Keep the radio centre fixed and shift the decoder to the clicked frequency (avoid moving the LO / DC spike)"));
    if (a.engine.running() && std::fabs(a.engine.decodeShiftHz()) > 1) {
        ImGui::TextDisabled(tr("Decode shift %+.1f kHz"), a.engine.decodeShiftHz() / 1e3);
        if (ImGui::SmallButton(tr("Centre decode"))) { a.engine.setDecodeShiftHz(0); savePrefs(a); }
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", tr("Unit"));
    if (ImGui::RadioButton("dBFS", !a.specDbm)) { a.specDbm = false; save = true; }
    ImGui::SameLine(0, 8 * gUi);
    if (ImGui::RadioButton("dBm", a.specDbm)) { a.specDbm = true; save = true; }
    if (a.specDbm) {
        float off = dbmOffset(a);
        ImGui::SetNextItemWidth(110 * gUi);
        if (ImGui::InputFloat(tr("dBm offset"), &off, 0.5f, 5.f, "%+.1f dB")) setDbmOffset(a, std::max(-200.f, std::min(200.f, off)));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("What to add to a level in dBFS to get dBm for this radio (its calibration: a known signal into the antenna port shows it). Also in the radio's settings."));
    }
    if (save) savePrefs(a);
}

static float gSpecBtnW = 0;

float bandPlanStripHeight() {
    const float lh = ImGui::GetTextLineHeight();
    const float pad = std::max(2.f * gUi, ImGui::GetStyle().FramePadding.y);
    return std::ceil(lh + 2.f * pad);
}

void drawBandPlanStrip(ImDrawList* dl, float x0, float x1, float y, float h, double loMhz, double hiMhz) {
    if (!dl || x1 <= x0 || h < 1 || !(hiMhz > loMhz)) return;
    dl->PushClipRect(ImVec2(x0, y), ImVec2(x1, y + h), true);
    dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), IM_COL32(14, 16, 18, 255));
    int nb = 0;
    const dect2::SaBand* bd = dect2::saAllocations(nb);
    auto X = [&](double mhz) { return (float)(x0 + (mhz - loMhz) / (hiMhz - loMhz) * (x1 - x0)); };
    const float lh = ImGui::GetTextLineHeight();
    const float ty = y + std::max(0.f, (h - lh) * 0.5f);
    for (int i = 0; i < nb; i++) {
        if (bd[i].hiMhz < loMhz || bd[i].loMhz > hiMhz) continue;
        const float a = std::max(x0, X(bd[i].loMhz)), b = std::min(x1, X(bd[i].hiMhz));
        const ImU32 fill = pal::remap((i % 2) ? IM_COL32(70, 110, 160, 150) : IM_COL32(150, 110, 60, 150));
        dl->AddRectFilled(ImVec2(a, y + 1), ImVec2(std::max(a + 1.f, b), y + h - 1), fill);
        const ImVec2 ts = ImGui::CalcTextSize(bd[i].name);
        if (ts.x + 4 < b - a && ts.y <= h + 0.5f) dl->AddText(ImVec2(a + 2, ty), IM_COL32(220, 222, 224, 230), bd[i].name);
    }
    dl->PopClipRect();
    if (ImGui::IsMouseHoveringRect(ImVec2(x0, y), ImVec2(x1, y + h), false)) {
        const float mx = ImGui::GetIO().MousePos.x;
        const double mhz = loMhz + (mx - x0) / (x1 - x0) * (hiMhz - loMhz);
        if (const dect2::SaBand* at = dect2::saAllocationAt(mhz)) ImGui::SetTooltip("%s — %s", at->name, at->use);
    }
}

static void spectrumPlot(App& a, ImVec2 size, int extraFlags) {
    const bool dbm = a.specDbm;
    const float dbmOff = dbm ? dbmOffset(a) : 0.f;
    const char* const kYLabel = dbm ? tr("power (dBm)") : tr("power (dBFS/bin)");
    ImVec2 plotMin, plotSize;
    bool drawn = false, openCtx = false;
    double visLo = 0, visHi = 0;
    static double ctxMhz = 0;
    if (plt::BeginPlot("##spec", size, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Overlay | plt::Flags_SmoothZoom | extraFlags | (a.family == 55 && generalDragging(0) ? plt::Flags_NoPan : 0))) {
        drawn = true;
        plt::SetupAxes(nullptr, kYLabel, plt::AxisFlags_Inside | (a.specGrid ? 0 : plt::AxisFlags_NoGridLines), plt::AxisFlags_Inside | (a.specGrid ? 0 : plt::AxisFlags_NoGridLines));   // the frequencies along the bottom edge, inside, as in SDR++
        setupFreqAxis(a);
        // The dB range follows the spectrum dB fields and the keys: a change there sets the axis at once (the plot otherwise keeps the
        // range the mouse dragged or zoomed to, and that range is written back into the fields, so the two never disagree) (#91)
        static float setMin = 1, setMax = 0;
        const bool rangeEdited = a.yMin != setMin || a.yMax != setMax;
        plt::SetupAxisLimits(plt::Y1, a.yMin, a.yMax, rangeEdited ? plt::Cond_Always : plt::Cond_Once);
        if (rangeEdited) { setMin = a.yMin; setMax = a.yMax; }
        if (dbm) {   // the axis stays in dBFS (the fields and the keys too); its labels are dBm: marks every 1, 2, 5 ... dB
            static double tp[24]; static char tb[24][12]; static const char* tl[24];
            const double span = std::max(1.0, (double)a.yMax - a.yMin);
            double step = 50;
            for (double s : {1., 2., 5., 10., 20., 25., 50.}) if (span / s <= 8) { step = s; break; }
            int nt = 0;
            for (double v = std::ceil((a.yMin + dbmOff) / step) * step; v <= a.yMax + dbmOff && nt < 24; v += step, nt++) {
                tp[nt] = v - dbmOff; snprintf(tb[nt], sizeof tb[nt], "%.0f", v); tl[nt] = tb[nt];
            }
            plt::SetupAxisTicks(plt::Y1, tp, nt, tl);
        }
        channelBand(a, true);
        plotMin = plt::GetPlotPos(); plotSize = plt::GetPlotSize();
        if (!rangeEdited) {
            const plt::Rect lim = plt::GetPlotLimits();
            if (std::fabs(lim.Y.Min - a.yMin) > 0.01 || std::fabs(lim.Y.Max - a.yMax) > 0.01) { a.yMin = setMin = (float)lim.Y.Min; a.yMax = setMax = (float)lim.Y.Max; }
        }
        if (plt::IsPlotHovered() && dbRangeKeys(a)) savePrefs(a);
        if (!a.smooth.empty()) {
            const size_t nb = a.smooth.size();
            const plt::Rect vis = plt::GetPlotLimits();
            const int wpx = std::max(1, (int)plotSize.x);
            const double fsMhz = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6, rc = radioCenterMhz(a);
            const double x0 = rc - fsMhz / 2, dx = fsMhz / (double)nb;   // bin i sits at x0 + i * dx (the same positions xs() lists)
            static std::vector<double> ex, ey;   // kept between frames: no allocation per frame
            if (a.peakHold && a.peak.size() == nb) {
                plt::Spec ps; ps.LineColor = pal::grey(0.40f); ps.LineWeight = 1.0f;
                envelope(a.peak, x0, dx, vis.X.Min, vis.X.Max, wpx, ex, ey);
                plt::PlotLine("peak", ex.data(), ey.data(), (int)ex.size(), ps);
            }
            envelope(a.smooth, x0, dx, vis.X.Min, vis.X.Max, wpx, ex, ey);
            if (a.specFill && !ex.empty()) {
                plt::Spec fs; fs.FillColor = pal::accent(0.16f); fs.LineColor = ImVec4(0, 0, 0, 0);
                plt::PlotShaded("fill", ex.data(), ey.data(), (int)ex.size(), vis.Y.Min, fs);
            }
            plt::Spec ss; ss.LineColor = pal::accent(); ss.LineWeight = 1.3f;
            plt::PlotLine("spectrum", ex.data(), ey.data(), (int)ex.size(), ss);
            if (a.specPeaks && ex.size() >= 8) {   // the strongest peaks in view, at least 6% of the view apart, and 12 dB over the mean level
                double mean = 0;
                for (double v : ey) mean += v;
                mean /= (double)ey.size();
                const double minSep = (vis.X.Max - vis.X.Min) * 0.06;
                double px[4]; int np = 0;
                for (int pass = 0; pass < 4; pass++) {
                    int best = -1; double bv = mean + 12;
                    for (size_t i = 0; i < ex.size(); i++) {
                        if (ey[i] <= bv) continue;
                        bool tooClose = false;
                        for (int k = 0; k < np; k++) if (std::fabs(ex[i] - px[k]) < minSep) tooClose = true;
                        if (!tooClose) { best = (int)i; bv = ey[i]; }
                    }
                    if (best < 0) break;
                    px[np++] = ex[best];
                    const double mx = ex[best], my = ey[best];
                    plt::Spec ms; ms.Marker = plt::Marker_Circle; ms.MarkerSize = 3.f; ms.MarkerFillColor = ms.MarkerLineColor = ms.LineColor = pal::accent();
                    plt::PlotScatter("##pk", &mx, &my, 1, ms);
                    char lb[48]; snprintf(lb, sizeof lb, dbm ? "%.4f  %.1f dBm" : "%.4f  %.1f dBFS", mx, my + dbmOff);
                    plt::PlotText(lb, mx, my, ImVec2(0, -11 * gUi));
                }
            }
            if (a.specReadout && plt::IsPlotHovered() && !ImGui::IsMouseDown(0) && nb > 1) {   // the frequency and level under the mouse
                const plt::Point mp = plt::GetPlotMousePos();
                const double bi = (mp.x - x0) / dx;
                if (bi >= 0 && bi < (double)nb - 1) {
                    const size_t i = (size_t)bi;
                    const double f = bi - (double)i, lv = a.smooth[i] * (1 - f) + a.smooth[i + 1] * f;
                    const double mx = mp.x, my = lv;
                    plt::Spec ms; ms.Marker = plt::Marker_Circle; ms.MarkerSize = 3.5f; ms.MarkerFillColor = ms.MarkerLineColor = ms.LineColor = ImVec4(1, 1, 1, 0.9f);
                    plt::PlotScatter("##rd", &mx, &my, 1, ms);
                    char lb[64]; snprintf(lb, sizeof lb, dbm ? "%.4f MHz  %.1f dBm" : "%.4f MHz  %.1f dBFS", mx, lv + dbmOff);
                    const float tw = ImGui::CalcTextSize(lb).x;
                    const bool right = ImGui::GetMousePos().x + tw + 24 * gUi > plotMin.x + plotSize.x;   // beside the point, left of it near the right edge
                    plt::PlotText(lb, mx, my, ImVec2(right ? -tw * 0.5f - 8 * gUi : tw * 0.5f + 8 * gUi, 14 * gUi));
                }
            }
            double cx[2] = {a.freqMhz, a.freqMhz}, cy[2] = {a.yMin, a.yMax};
            plt::Spec cs; cs.LineColor = pal::grey(0.35f);
            plt::PlotLine("centre", cx, cy, 2, cs);
            if (a.fmMode && pal::dev()) {   // the stations the scan found, as bookmarks (as in SDR++)
                const plt::Rect lim = plt::GetPlotLimits();
                int k = 0;
                for (const auto& r : a.fmScan.results) {
                    if (!r.found || r.mhz < lim.X.Min || r.mhz > lim.X.Max) continue;
                    double mx[2] = {r.mhz, r.mhz}, my[2] = {a.yMin, a.yMax};
                    plt::Spec ms; ms.LineColor = ImVec4(0.85f, 0.85f, 0.85f, 0.22f);
                    char id[16]; snprintf(id, sizeof id, "mark%d", k++);
                    plt::PlotLine(id, mx, my, 2, ms);
                    char lb[48]; snprintf(lb, sizeof lb, "%s", r.name.empty() ? "" : r.name.c_str());
                    if (lb[0]) plt::PlotText(lb, r.mhz, a.yMax, ImVec2(ImGui::CalcTextSize(lb).x * 0.5f + 4, bandPlanStripHeight() + 4 * gUi + (k % 2) * ImGui::GetTextLineHeight()));
                }
            }
        }
        wideMarkers(a);
        multiMarkers(a);
        clickToTune(a, 0);
        visLo = plt::GetPlotLimits().X.Min; visHi = plt::GetPlotLimits().X.Max;
        specxDrawBookmarks(ImGui::GetWindowDrawList(), visLo * 1e6, visHi * 1e6,
                           plotMin.x, plotMin.x + plotSize.x, plotMin.y, plotMin.y + plotSize.y);
        if (plt::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { ctxMhz = plt::GetPlotMousePos().x; openCtx = true; }
        plt::EndPlot();
    }
    if (drawn) {
        const float sh = bandPlanStripHeight();
        drawBandPlanStrip(ImGui::GetWindowDrawList(), plotMin.x, plotMin.x + plotSize.x, plotMin.y - sh, sh, visLo, visHi);
        channelWidthButton(a, plotMin, plotSize, kYLabel); wideSwitch(a, plotMin, plotSize); multiSwitch(a, plotMin, plotSize);
        gSpecBtnW = cornerPopup(a, "Spectrum###specbtn", "##specpop", plotMin, plotSize, gBwBtnW + gWideBtnW, ImGui::CalcTextSize(kYLabel).x + 24 * gUi, 0, false,
                                tr("Spectrum: averaging, max hold, peak markers, fill, grid and the unit (dBFS or dBm)"), specDisplayPopup);
    }
    // right-click: what is this signal?
    if (openCtx) ImGui::OpenPopup("##specctx");
    if (ImGui::BeginPopup("##specctx")) {
        if (ImGui::MenuItem(trw("Identify this signal"), nullptr, false, a.engine.running())) identifyAt(a, ctxMhz);
        if (!a.engine.running() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", tr("Start the receiver first"));
        ImGui::EndPopup();
    }
}

static void waterfallPlot(App& a, ImVec2 size, int extraFlags) {
    const int H = Waterfall::H;
    // The time scale is worked out once and kept until the plot changes size (or the first real measure of the row time is in),
    // so the labels never move while the picture scrolls under them.
    struct Scale { ImVec2 avail{-1, -1}; bool measured = false; double dt = 0; std::vector<double> pos; std::vector<std::string> lab; std::vector<const char*> cstr; };
    static Scale sc;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool measured = a.wf.timeKnown();
    if (avail.x != sc.avail.x || avail.y != sc.avail.y || measured != sc.measured || (a.wf.slow() && std::fabs(a.wf.rowDt - sc.dt) > 1e-9)) {
        sc.avail = avail; sc.measured = measured; sc.dt = a.wf.rowDt > 0 ? a.wf.rowDt : 1.0 / 30;   // a zero would make the label step zero: an endless loop
        // a 1-2-5 step that gives a label about every three text lines
        const double secs = H * sc.dt, want = std::max(2.0, (double)avail.y / (ImGui::GetTextLineHeight() * 3));
        const double raw = secs / want, mag = std::pow(10.0, std::floor(std::log10(raw))), n = raw / mag;
        const double step = (n < 1.5 ? 1 : n < 3 ? 2 : n < 7 ? 5 : 10) * mag;
        const int dec = std::max(0, (int)std::ceil(-std::log10(step) - 1e-9));
        sc.pos.clear(); sc.lab.clear(); sc.cstr.clear();
        for (double s = 0; s <= secs + 1e-9; s += step) {
            char b[32]; snprintf(b, sizeof b, "%.*f", dec, s > 0 ? -s : 0.0);
            sc.pos.push_back(-s / sc.dt); sc.lab.push_back(b);
        }
        for (auto& s : sc.lab) sc.cstr.push_back(s.c_str());
    }
    ImVec2 plotMin, plotSize;
    bool drawn = false;
    if (plt::BeginPlot("##wf", size, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Overlay | plt::Flags_SmoothZoom | extraFlags | (a.family == 55 && generalDragging(1) ? plt::Flags_NoPan : 0))) {
        drawn = true;
        plt::SetupAxes(nullptr, tr("seconds ago"), plt::AxisFlags_NoTickLabels | (a.specGrid ? 0 : plt::AxisFlags_NoGridLines), plt::AxisFlags_Inside | (a.specGrid ? 0 : plt::AxisFlags_NoGridLines));   // the spectrum above shows the frequencies
        double fs = (a.engine.sampleRate() > 0 ? a.engine.sampleRate() : a.tune.sampleRate) / 1e6;
        const double rc = radioCenterMhz(a);
        double x0 = rc - fs / 2, x1 = rc + fs / 2;
        setupFreqAxis(a);
        // y is in rows, so the picture keeps its size while the measured row time wanders; the labels are the fixed scale above.
        // Paused, the view is free to scroll and zoom back through the rows (but not past the oldest or the newest), with ordinary tick labels.
        plt::SetupAxisLimits(plt::Y1, -H, 0, a.wf.paused ? plt::Cond_Once : plt::Cond_Always);
        if (a.wf.paused) plt::SetupAxisConstraints(plt::Y1, -H, 0);
        plt::SetupAxisUnit(plt::Y1, sc.dt);
        if (!a.wf.paused) plt::SetupAxisTicks(plt::Y1, sc.pos.data(), (int)sc.pos.size(), sc.cstr.data());
        // rows arrive at the engine's ~30 Hz, the screen draws faster: slide down by the part of a row that is due since the last one,
        // so the picture moves at an even speed instead of jumping a row at a time
        const double frac = a.wf.pushT > 0 && !a.wf.paused ? std::min(1.0, std::max(0.0, (glfwGetTime() - a.wf.pushT) / a.wf.rowDt)) : 0;
        int w = a.wf.writeRow;
        ImTextureID tex = a.wf.img->texture();
        plt::PlotImage("a", tex, plt::Point(x0, -(H - w) - frac), plt::Point(x1, -frac), ImVec2(0, (float)w / H), ImVec2(1, 1));
        if (w > 0)
            plt::PlotImage("b", tex, plt::Point(x0, -H - frac), plt::Point(x1, -(H - w) - frac), ImVec2(0, 0), ImVec2(1, (float)w / H));
        const float vNew = (w + 0.5f) / H;   // the gap above: the newest row, stretched
        if (frac > 0) plt::PlotImage("top", tex, plt::Point(x0, -frac), plt::Point(x1, 0), ImVec2(0, vNew), ImVec2(1, vNew));
        channelBand(a, false);
        plotMin = plt::GetPlotPos(); plotSize = plt::GetPlotSize();
        clickToTune(a, 1);
        plt::EndPlot();
    }
    if (drawn) cornerPopup(a, a.wf.paused ? "Paused###wfbtn" : "Waterfall###wfbtn", "##wfpop", plotMin, plotSize, 0, 40 * gUi, ImGui::GetTextLineHeight() + 6 * gUi, a.wf.paused,
                           tr("Waterfall: colour map, intensity range, speed and pause (then drag or use the wheel to look back)"), wfDisplayPopup);
}

// The spectrum on top of the waterfall as one panel (as in SDR++): no gap, the same edges and frequency axis.
// frac: the spectrum's share of the height; with drag set, the boundary line is a handle that changes it.
void spectrumWaterfall(App& a, float& frac, bool drag) {
    const double tPlot0 = glfwGetTime();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float sliderCol = std::min(52.f * gUi, std::max(0.f, avail.x - 60.f));
    const float w = std::max(60.f, avail.x - sliderCol), h = avail.y;
    const float stripH = bandPlanStripHeight();
    const float hs = std::floor(std::min(std::max(60.f + stripH, h * frac), std::max(60.f + stripH, h - 60.f)));
    const float y = p.y + hs;
    bool hot = false;
    if (drag) {   // first, so the handle takes the mouse before the plots under it
        ImGui::SetCursorScreenPos(ImVec2(p.x, y - 3 * gUi));
        ImGui::InvisibleButton("##split", ImVec2(w, 6 * gUi));
        hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) frac = std::min(0.8f, std::max(0.2f, frac + ImGui::GetIO().MouseDelta.y / std::max(1.f, h)));
        ImGui::SetCursorScreenPos(p);
    }
    freqPageKeys();
    specFreqZoomBeforePlots(a);
    {   // the ranges are kept when the mouse has let go of them (a drag or a wheel zoom changes them all the time)
        static float sMin = 1e9f, sMax = 1e9f, wMin = 1e9f, wMax = 1e9f; static double sT = 0;
        if (sMin == 1e9f) { sMin = a.yMin; sMax = a.yMax; wMin = a.wf.minDb; wMax = a.wf.maxDb; }
        const bool wfMoved = !a.wf.autoTrack && (a.wf.minDb != wMin || a.wf.maxDb != wMax);
        if ((a.yMin != sMin || a.yMax != sMax || wfMoved) && !ImGui::IsMouseDown(0) && glfwGetTime() - sT > 1.0) {
            sMin = a.yMin; sMax = a.yMax; wMin = a.wf.minDb; wMax = a.wf.maxDb; sT = glfwGetTime();
            savePrefs(a);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + stripH));
    spectrumPlot(a, ImVec2(w, std::max(40.f, hs - stripH)), plt::Flags_Flush | plt::Flags_OpenBottom);
    ImGui::SetCursorScreenPos(ImVec2(p.x, y));
    waterfallPlot(a, ImVec2(w, std::max(60.f, h - hs)), plt::Flags_Flush | plt::Flags_OpenTop);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, y), ImVec2(p.x + w, y), hot ? IM_COL32(200, 200, 200, 160) : IM_COL32(110, 110, 110, 110), hot ? 2.f : 1.f);
    if (sliderCol > 0) dbRangeSliders(a, ImVec2(p.x + w, p.y + stripH), sliderCol, std::max(40.f, h - stripH));
    // DECT2_FPS=1: what the spectrum and the waterfall cost on the CPU per frame (building their draw lists), every 2 seconds on stderr
    static const bool perf = getenv("DECT2_FPS") != nullptr;
    if (perf) {
        static double acc = 0, mx = 0, t0 = glfwGetTime(); static int n = 0;
        const double d = glfwGetTime() - tPlot0;
        acc += d; mx = std::max(mx, d); n++;
        if (glfwGetTime() - t0 > 2.0) { fprintf(stderr, "plots: %.3f ms avg / %.2f ms max per frame (%d frames, %.0fx%.0f px)\n", 1e3 * acc / n, 1e3 * mx, n, (double)w, (double)h); acc = mx = 0; n = 0; t0 = glfwGetTime(); }
    }
}

void histogramPlot(App& a, ImVec2 size) {
    if (plt::BeginPlot("##hist", size, plt::Flags_NoLegend | plt::Flags_NoTitle)) {
        plt::SetupAxes("|sample| (fraction of full scale)", "count");
        plt::SetupAxisScale(plt::Y1, plt::Scale_Log10);
        plt::SetupAxisLimits(plt::X1, 0, 1, plt::Cond_Once);
        float h[64], x[64];
        for (int i = 0; i < 64; i++) { h[i] = (float)std::max<uint32_t>(1, a.spec.stats.hist[i]); x[i] = (i + 0.5f) / 64; }
        plt::Spec bs; bs.FillColor = pal::accent(0.8f);
        plt::PlotBars("adc", x, h, 64, 1.0 / 64, bs);
        plt::EndPlot();
    }
}

// Decoded-cell views of the data constellation: density heat map and per-point clusters
void constDensityPlot(App& a, ImVec2 sz) {
    const App::ConstStats& c = a.cst;
    if (plt::BeginPlot("##c2d", sz, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Equal | plt::Flags_NoMouseText)) {
        plt::SetupAxes(nullptr, nullptr, plt::AxisFlags_NoTickLabels, plt::AxisFlags_NoTickLabels);
        plt::SetupAxisLimits(plt::X1, -1.4, 1.4, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, -1.4, 1.4, plt::Cond_Always);
        const int G = App::ConstStats::G;
        if ((int)c.grid.size() == G * G) {
            std::vector<float> v(c.grid.size());
            float mx = 0;
            for (size_t i = 0; i < v.size(); i++) { v[i] = std::sqrt(c.grid[i]); mx = std::max(mx, v[i]); }   // square root: faint areas stay visible
            static int cm = -1;
            if (cm < 0) {   // black -> accent -> white, instead of the rainbow-like "hot" map
                const ImVec4 cols[4] = {ImVec4(0.02f, 0.03f, 0.05f, 1), ImVec4(0.10f, 0.28f, 0.45f, 1), pal::accent(), ImVec4(0.95f, 0.98f, 1.f, 1)};
                cm = plt::AddColormap("onair", cols, 4);
            }
            plt::PushColormap(cm);
            plt::PlotHeatmap("density", v.data(), G, G, 0, std::max(1.f, mx * 0.85f), nullptr, plt::Point(-1.4, -1.4), plt::Point(1.4, 1.4));
            plt::PopColormap();
        }
        const int M = 2 * (a.rx.plpFec.mod + 1);
        std::vector<cf32> grid;
        for (unsigned l = 0; l < (1u << M); l++) grid.push_back(qamPoint(a.rx.plpFec.mod, false, l));
        plt::Spec gs; gs.Marker = plt::Marker_Cross; gs.MarkerSize = 4.f; gs.Stride = sizeof(cf32);
        gs.MarkerFillColor = gs.MarkerLineColor = gs.LineColor = ImVec4(1, 1, 1, 0.55f);
        const float* g = reinterpret_cast<const float*>(grid.data());
        plt::PlotScatter("ideal", g, g + 1, (int)grid.size(), gs);
        plt::EndPlot();
    }
}

void constClusterPlot(App& a, ImVec2 sz) {
    const App::ConstStats& c = a.cst;
    const int mod = a.rx.plpFec.mod;
    static const float kDmin[4] = {1.4142f, 0.6325f, 0.3086f, 0.1534f};
    const float half = 0.5f * kDmin[mod];
    if (plt::BeginPlot("##c2k", sz, plt::Flags_NoLegend | plt::Flags_NoTitle | plt::Flags_Equal | plt::Flags_NoMouseText)) {
        plt::SetupAxes(nullptr, nullptr, plt::AxisFlags_NoTickLabels, plt::AxisFlags_NoTickLabels);
        plt::SetupAxisLimits(plt::X1, -1.4, 1.4, plt::Cond_Always);
        plt::SetupAxisLimits(plt::Y1, -1.4, 1.4, plt::Cond_Always);
        int hover = -1;
        double meanRatio = 0; int nr = 0;
        for (auto& p : c.pts) if (p.n >= 4) { meanRatio += std::sqrt(p.e2 / (2 * p.n)); nr++; }
        meanRatio = nr ? meanRatio / nr : 1;
        const plt::Point mp = plt::GetPlotMousePos();
        double bestD = 1e9;
        for (size_t l = 0; l < c.pts.size(); l++) {
            const App::ConstStats::Pt& p = c.pts[l];
            const cf32 tx = qamPoint(mod, false, (unsigned)l);
            const double d = std::hypot(mp.x - tx.real(), mp.y - tx.imag());
            if (plt::IsPlotHovered() && d < bestD && d < half * 1.2) { bestD = d; hover = (int)l; }
            if (p.n < 4) continue;
            const double sg = std::sqrt(p.e2 / (2 * p.n));        // standard deviation per axis
            // colour relative to the average ring: green = tighter than average, red = looser (a coded signal is decoded at
            // noise levels where all rings overlap, so an absolute scale would show everything red)
            const float t = (float)std::min(1.0, std::max(0.0, 0.5 + (sg / meanRatio - 1.0) * 4.0));
            const ImVec4 col(0.25f + 0.7f * t, 0.85f - 0.5f * t, 0.45f - 0.15f * t, 0.9f);
            const double cx = tx.real() + p.ei / p.n, cy = tx.imag() + p.eq / p.n;
            float xs[25], ys[25];
            for (int k = 0; k < 25; k++) { const double th = k * 2 * M_PI / 24; xs[k] = (float)(cx + sg * std::cos(th)); ys[k] = (float)(cy + sg * std::sin(th)); }
            plt::Spec ls; ls.LineColor = col; ls.LineWeight = (int)l == hover ? 2.5f : 1.3f;
            plt::PlotLine("##ring", xs, ys, 25, ls);
            plt::Spec ds; ds.Marker = plt::Marker_Circle; ds.MarkerSize = 2.f; ds.MarkerFillColor = ds.MarkerLineColor = ds.LineColor = col;
            const float px = (float)cx, py = (float)cy;
            plt::PlotScatter("##centre", &px, &py, 1, ds);
        }
        {
            const int M = 2 * (mod + 1);
            std::vector<cf32> grid;
            for (unsigned l = 0; l < (1u << M); l++) grid.push_back(qamPoint(mod, false, l));
            plt::Spec gs; gs.Marker = plt::Marker_Cross; gs.MarkerSize = 4.f; gs.Stride = sizeof(cf32);
            gs.MarkerFillColor = gs.MarkerLineColor = gs.LineColor = ImVec4(1, 1, 1, 0.8f);
            const float* g = reinterpret_cast<const float*>(grid.data());
            plt::PlotScatter("ideal", g, g + 1, (int)grid.size(), gs);
        }
        if (hover >= 0 && c.pts[hover].n >= 4) {
            const App::ConstStats::Pt& p = c.pts[hover];
            const int M = 2 * (mod + 1);
            char bits[16];
            for (int b = 0; b < M; b++) bits[b] = ((hover >> (M - 1 - b)) & 1) ? '1' : '0';
            bits[M] = 0;
            ImGui::BeginTooltip();
            ImGui::Text(tr("point %s"), bits);
            ImGui::Text(tr("sigma %.3f per axis (%.0f%% of the half-distance to a neighbour)"), std::sqrt(p.e2 / (2 * p.n)), 100 * std::sqrt(p.e2 / (2 * p.n)) / half);
            ImGui::Text(tr("offset %+.3f %+.3f"), p.ei / p.n, p.eq / p.n);
            ImGui::Text(tr("EVM %.1f dB"), 10 * std::log10(std::max(1e-9, p.e2 / p.n)));
            ImGui::EndTooltip();
        }
        plt::EndPlot();
    }
}

