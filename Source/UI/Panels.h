#pragma once

#include "../Plugin/PluginProcessor.h"
#include "Widgets.h"
#include "MeterBallistics.h"

namespace tc::ui
{
// Analogue: calibrated RMS VU. Digital OUT: stereo sample peaks in dBFS after all output gain.
// PRE / COMP / EQ retain their internal RMS VU references in both skins.
class VuView : public juce::Component, private juce::Timer
{
public:
    VuView(TubeChannelProcessor& p, const Skin* s, std::function<bool()> channelB)
        : proc(p), skin(s), chB(std::move(channelB))
    {
        source = juce::jlimit(0, 3, int(proc.apvts.state.getProperty("vuSrc", 3)));
        startTimerHz(30);
    }
    void visibilityChanged() override
    {
        if (!isVisible()) { wasHidden = true; return; }
        if (!wasHidden) return;
        // Resume with fresh audio, rather than a maximum accumulated while another page was open.
        if (skin->digital)
            for (int ch = 0; ch < 2; ++ch) proc.takeOutputMeterPeak(ch);
        meters[0] = {}; meters[1] = {};
        needle = 0.f; displayDb = MeterBallistics::floorDb; lastTick = 0.0;
        source = juce::jlimit(0, 3, int(proc.apvts.state.getProperty("vuSrc", 3)));
        wasHidden = false;
    }
    void paint(Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        if (!skin->digital)
        {
            gla::meters::drawVuMeter(g, r, needle, gla::meters::VuFace::Black);
            return;
        }
        g.setColour(Colour(0xff050a12));
        g.fillRoundedRectangle(r, 3);
        auto inner = r.reduced(6, 4);
        const auto bars = inner.removeFromTop(10);
        const float floor = source == 3 ? -60.f : -30.f, ceiling = source == 3 ? 0.f : 3.f;
        const int channels = std::max(1, proc.activeChannels());
        // Compact rows fit the actual available height; the gallery renderer needs a taller panel.
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto row = bars.withY(bars.getY() + float(ch) * 6.f).withHeight(4.f);
            const float level = gla::meters::dbPosition(meters[ch].db, floor, ceiling);
            const float held = gla::meters::dbPosition(meters[ch].heldDb, floor, ceiling);
            constexpr int count = 24;
            const float step = row.getWidth() / float(count);
            for (int i = 0; i < count; ++i)
            {
                const float f = float(i + 1) / float(count);
                const Colour c = f > 0.95f ? Colour(0xffe5382a) : f > 0.8f ? Colour(0xfff0b323) : skin->accent;
                g.setColour(level >= f ? c : c.withAlpha(0.13f));
                g.fillRect(Rectangle<float>(row.getX() + float(i) * step, row.getY(), step - 1.f, row.getHeight()));
            }
            g.setColour(skin->legend);
            g.fillRect(Rectangle<float>(row.getX() + held * (row.getWidth() - 1.f), row.getY(), 1.f, row.getHeight()));
        }
        auto readout = inner.removeFromBottom(14);
        const auto units = readout.removeFromRight(36);
        g.setColour(displayDb >= 0.f ? Colour(0xffe5382a) : skin->accent);
        g.setFont(font(11.f, true));
        g.drawFittedText(readoutText(), readout.toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour(skin->legendDim);
        g.setFont(labelFont(9.f));
        g.drawText(source == 3 ? "dBFS" : "VU", units, juce::Justification::centredRight, false);
    }
private:
    juce::String readoutText() const
    {
        if (displayDb <= MeterBallistics::floorDb) return "-inf";
        const float rounded = std::round(displayDb * 10.f) / 10.f;
        return (rounded > 0.f ? "+" : "") + juce::String(rounded == 0.f ? 0.f : rounded, 1);
    }
    void timerCallback() override
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const double elapsed = lastTick > 0.0 ? now - lastTick : 1.0 / 30.0;
        lastTick = now;
        if (!isVisible()) return;
        const int src = juce::jlimit(0, 3, int(proc.apvts.state.getProperty("vuSrc", 3)));
        if (source != src)
        {
            meters[0] = {}; meters[1] = {};
            source = src;
        }
        const int channels = std::max(1, proc.activeChannels());
        const bool dual = proc.apvts.getRawParameterValue(tc::params::id::dualMono)->load() > 0.5f && channels == 2;
        const int selected = dual && chB() ? 1 : 0;
        float relativeRms[2] = {};
        const float outputRef = std::pow(10.f, tcAlign() / 20.f) / 1.41421356f;
        for (int ch = 0; ch < channels; ++ch)
        {
            const auto& t = proc.telemetry[ch];
            relativeRms[ch] = src == 3 ? t.outRms.load() / outputRef : t.vuTap[src].load() / 0.347f;
            if (skin->digital)
            {
                const float outputPeak = proc.takeOutputMeterPeak(ch);
                meters[ch].update(src == 3 ? outputPeak : relativeRms[ch], elapsed);
            }
        }
        const float rms = dual ? relativeRms[selected] : std::max(relativeRms[0], relativeRms[1]);
        const float vuDb = rms > 1e-6f ? 20.f * std::log10(rms) : -60.f;
        needle += (gla::meters::vuPositionForDb(vuDb) - needle) * float(1.0 - std::exp(-elapsed / 0.08));
        displayDb = dual || channels == 1 ? meters[selected].heldDb : std::max(meters[0].heldDb, meters[1].heldDb);
        setTitle(skin->digital ? readoutText() + (source == 3 ? " dBFS" : " VU") : juce::String(vuDb, 1) + " VU");
        repaint();
    }
    float tcAlign() const
    {
        static const float a[] = { -14.f, -16.f, -18.f, -20.f, -22.f };
        return a[juce::jlimit(0, 4, juce::roundToInt(proc.apvts.getRawParameterValue(tc::params::id::alignment)->load()))];
    }
    TubeChannelProcessor& proc;
    const Skin* skin;
    std::function<bool()> chB;
    MeterBallistics meters[2];
    int source = -1;
    float needle = 0.f, displayDb = -60.f;
    double lastTick = 0.0;
    bool wasHidden = false;
};

// ---- horizontal LED ladders (gain reduction, tube warmth) -------------------------------------------------------
class LadderView : public juce::Component, private juce::Timer
{
public:
    enum class Kind { GainReduction, Warmth };
    LadderView(TubeChannelProcessor& p, const Skin* s, Kind k, std::function<bool()> channelB)
        : proc(p), skin(s), kind(k), chB(std::move(channelB)) { startTimerHz(30); }
    void paint(Graphics& g) override
    {
        const int segs = kind == Kind::Warmth ? 10 : 12;
        const bool vertical = getHeight() > getWidth();
        auto r = getLocalBounds().toFloat();
        g.setColour(Colour(0xff08090a));
        g.fillRoundedRectangle(r, 2);
        r = r.reduced(2);
        const float step = (vertical ? r.getHeight() : r.getWidth()) / float(segs);
        for (int i = 0; i < segs; ++i)
        {
            const bool lit = float(i) < shown * float(segs) - 0.001f;
            Colour c;
            if (kind == Kind::Warmth)
                c = i >= 8 ? Colour(0xffe5382a) : i >= 4 ? Colour(0xfff0b323) : Colour(0xff3ccf5a);
            else
                c = Colour(0xfff0b323).interpolatedWith(Colour(0xffe5382a), float(i) / float(segs));
            Rectangle<float> cell = vertical ? Rectangle<float>(r.getX(), r.getY() + float(i) * step, r.getWidth(), step).reduced(1.5f)
                                             : Rectangle<float>(r.getX() + float(i) * step, r.getY(), step, r.getHeight()).reduced(1.5f);
            g.setColour(lit ? c : c.darker(0.85f).withAlpha(0.55f));
            g.fillRoundedRectangle(cell, 1.5f);
            if (lit && !skin->digital)
            {
                g.setColour(c.withAlpha(0.25f));
                g.fillRoundedRectangle(cell.expanded(1.5f), 2.0f);
            }
        }
    }
private:
    void timerCallback() override
    {
        if (!isVisible()) return;
        const bool dual = proc.activeChannels() == 2 && proc.apvts.getRawParameterValue(tc::params::id::dualMono)->load() > 0.5f;
        const int selected = dual && chB() ? 1 : 0;
        float target = 0.f;
        if (kind == Kind::GainReduction)
        {
            float gr = std::max(0.f, proc.telemetry[selected].grDb.load());
            if (!dual && proc.activeChannels() == 2) gr = std::max(gr, proc.telemetry[1].grDb.load());
            target = juce::jlimit(0.f, 1.f, gr / 24.f);        // top segment = 24 dB (the hardware scale reads far further)
            // the segments fill from the top in the original orientation: here simply bottom-up
        }
        else
        {
            float v = proc.telemetry[selected].tubeDrive.load();
            if (!dual && proc.activeChannels() == 2) v = std::max(v, proc.telemetry[1].tubeDrive.load());
            const float db = v > 1e-4f ? 20.0f * std::log10(v / 12.0f) : -80.0f;
            target = juce::jlimit(0.f, 1.f, (db + 40.0f) / 40.0f);
        }
        shown += (target - shown) * (target > shown ? 0.5f : 0.12f);
        repaint();
    }
    TubeChannelProcessor& proc;
    const Skin* skin;
    Kind kind;
    std::function<bool()> chB;
    float shown = 0.f;
};

// ---- lamp that follows a telemetry value ---------------------------------------------------------------------------
class ClipLamp : public juce::Component, private juce::Timer
{
public:
    ClipLamp(TubeChannelProcessor& p, juce::String t, bool finalOutput = false) : proc(p), text(std::move(t)), digitalOutput(finalOutput) { startTimerHz(20); }
    void visibilityChanged() override
    {
        if (!isVisible()) { wasHidden = true; return; }
        if (!wasHidden) return;
        if (digitalOutput) { proc.takeOutputClip(0); proc.takeOutputClip(1); }
        on = 0.f;
        wasHidden = false;
    }
    void paint(Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        gla::meters::drawLamp(g, { r.getCentreX(), r.getHeight() * 0.38f }, std::min(r.getWidth(), r.getHeight()) * 0.22f, Colour(0xffe5382a), on > 0.05f);
        g.setColour(Colour(0xff8a96a0));
        g.setFont(labelFont(9.0f));
        g.drawText(text, r.removeFromBottom(r.getHeight() * 0.32f), juce::Justification::centred);
    }
private:
    void timerCallback() override
    {
        if (!isVisible()) return;
        bool c;
        if (digitalOutput)
        {
            const bool left = proc.takeOutputClip(0), right = proc.takeOutputClip(1);
            c = left || right;
        }
        else c = proc.telemetry[0].clip.load() > 0.5f ||
                 (proc.activeChannels() > 1 && proc.telemetry[1].clip.load() > 0.5f);
        on = c ? 1.0f : on * 0.85f;
        repaint();
    }
    TubeChannelProcessor& proc;
    juce::String text;
    float on = 0.f;
    bool digitalOutput;
    bool wasHidden = false;
};

// ---- the animated power supply ---------------------------------------------------------------------------------------
// Everything shown here is the live state of the simulated supply: rails, ripple history, heater current and
// the loads the audio is putting on it. Dots along the wires move with the load current.
class PowerPanel : public juce::Component, private juce::Timer
{
public:
    PowerPanel(TubeChannelProcessor& p, const Skin* s) : proc(p), skin(s) { startTimerHz(30); }

    void paint(Graphics& g) override
    {
        const auto& t = proc.telemetry[0];
        const bool live = proc.apvts.getRawParameterValue(tc::params::id::psuModel)->load() > 0.5f;
        auto area = getLocalBounds().toFloat();
        const Colour ink = skin->text, mid = skin->textMid, lit = skin->digital ? skin->accent : Colour(0xffffc65c);

        // --- flow diagram -------------------------------------------------------------------------------
        auto flow = area.removeFromTop(66).reduced(4, 6);
        const char* names[] = { "MAINS", "TRANSFORMER", "RECTIFIERS", "RESERVOIRS", "MULTIPLIERS", "REGULATORS" };
        const char* subs[] = { "", "22-0-22 V", "2 x 2 diodes", "2 x 1000 uF", "+75 / +150 V", "+/-15 / +45 V" };
        const int n = 6;
        const float bw = (flow.getWidth() - float(n - 1) * 14.0f) / float(n);
        std::vector<Rectangle<float>> boxes;
        for (int i = 0; i < n; ++i)
            boxes.push_back(Rectangle<float>(flow.getX() + float(i) * (bw + 14.0f), flow.getY() + 6, bw, flow.getHeight() - 12));
        const float loadA = juce::jlimit(0.0f, 0.5f, t.loadP15.load() + t.loadTube.load());
        for (int i = 0; i + 1 < n; ++i)
        {
            const auto a = boxes[size_t(i)].getRight(), b = boxes[size_t(i) + 1].getX(), y = boxes[size_t(i)].getCentreY();
            g.setColour(mid.withAlpha(0.6f));
            g.drawLine(a, y, b, y, 1.5f);
            // dots: spacing fixed, speed proportional to the load current
            for (int k = 0; k < 3; ++k)
            {
                const float ph = std::fmod(flowPhase + float(k) / 3.0f + float(i) * 0.17f, 1.0f);
                g.setColour(lit.withAlpha(0.55f + 0.4f * juce::jlimit(0.f, 1.f, loadA * 4.f)));
                g.fillEllipse(a + (b - a) * ph - 2.0f, y - 2.0f, 4.0f, 4.0f);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            auto bx = boxes[size_t(i)];
            g.setColour(skin->bgBottom.withAlpha(0.85f));
            g.fillRoundedRectangle(bx, 3);
            g.setColour(mid.withAlpha(0.7f));
            g.drawRoundedRectangle(bx, 3, 1.0f);
            g.setColour(ink);
            g.setFont(labelFont(8.5f));
            g.drawText(names[i], bx.removeFromTop(bx.getHeight() * 0.5f), juce::Justification::centred);
            g.setColour(mid);
            g.setFont(labelFont(8.0f, false));
            g.drawText(subs[i], bx, juce::Justification::centred);
        }
        // mains phase marker on the first box
        {
            auto bx = boxes[0].reduced(6, 3).removeFromBottom(10);
            juce::Path sine;
            for (int x = 0; x <= int(bx.getWidth()); ++x)
            {
                const float ph = float(x) / bx.getWidth() * 6.2831853f;
                const float y = bx.getCentreY() - std::sin(ph) * bx.getHeight() * 0.45f;
                x == 0 ? sine.startNewSubPath(bx.getX(), y) : sine.lineTo(bx.getX() + float(x), y);
            }
            g.setColour(mid.withAlpha(0.7f));
            g.strokePath(sine, juce::PathStrokeType(1.0f));
            const float mx = bx.getX() + std::fmod(t.mainsPhase.load(), 6.2831853f) / 6.2831853f * bx.getWidth();
            g.setColour(lit);
            g.fillEllipse(mx - 2.5f, bx.getCentreY() - std::sin(std::fmod(t.mainsPhase.load(), 6.2831853f)) * bx.getHeight() * 0.45f - 2.5f, 5, 5);
        }

        // --- rails -----------------------------------------------------------------------------------------------
        auto lower = area.reduced(4, 2);
        auto bars = lower.removeFromLeft(lower.getWidth() * 0.52f);
        struct Rail { const char* name; float value, nominal, avg; };
        const Rail rails[] = { { "+25 raw", t.p25.load(), 26.0f, avg[0] }, { "+15 reg", t.p15.load(), 15.0f, avg[1] },
                               { "-15 reg", -t.n15.load(), 15.0f, avg[2] }, { "+45 reg", t.p45.load(), 45.8f, avg[3] },
                               { "+75", t.p75.load(), 74.0f, avg[4] }, { "plate", t.tubeP.load(), 165.0f, avg[5] } };
        const float rowH = bars.getHeight() / 6.0f;
        for (int i = 0; i < 6; ++i)
        {
            auto row = Rectangle<float>(bars.getX(), bars.getY() + float(i) * rowH, bars.getWidth(), rowH).reduced(0, 2);
            auto lab = row.removeFromLeft(52);
            auto val = row.removeFromRight(86);
            g.setColour(mid);
            g.setFont(labelFont(9.5f));
            g.drawText(rails[i].name, lab, juce::Justification::centredLeft);
            g.setColour(Colour(0xff06080a));
            g.fillRoundedRectangle(row, 2);
            const float frac = juce::jlimit(0.f, 1.f, rails[i].value / (rails[i].nominal * 1.25f));
            const float nomX = row.getX() + row.getWidth() * (rails[i].nominal / (rails[i].nominal * 1.25f));
            g.setColour(lit.withAlpha(0.85f));
            g.fillRoundedRectangle(row.withWidth(row.getWidth() * frac).reduced(1), 1.5f);
            g.setColour(ink.withAlpha(0.7f));
            g.drawVerticalLine(int(nomX), row.getY(), row.getBottom());
            const float sag = rails[i].value - rails[i].avg;
            g.setColour(ink);
            g.setFont(labelFont(9.5f, false));
            g.drawText(juce::String(rails[i].value, i < 3 ? 2 : 1) + " V", val.removeFromTop(val.getHeight() * 0.55f), juce::Justification::centredRight);
            g.setColour(std::fabs(sag) > (i == 5 ? 0.6f : 0.15f) ? Colour(0xffff8a5c) : mid);
            g.setFont(labelFont(8.5f, false));
            g.drawText((sag >= 0 ? "+" : "") + juce::String(sag * 1000.0f, 0) + " mV", val, juce::Justification::centredRight);
        }

        // --- scope: ripple history of the raw rail and the plate supply ------------------------------------------------
        auto scope = lower.reduced(6, 2);
        auto heaters = scope.removeFromBottom(34);
        g.setColour(Colour(0xff050708));
        g.fillRoundedRectangle(scope, 3);
        g.setColour(mid.withAlpha(0.25f));
        for (int i = 1; i < 4; ++i) g.drawHorizontalLine(int(scope.getY() + scope.getHeight() * float(i) / 4.0f), scope.getX(), scope.getRight());
        for (int i = 1; i < 8; ++i) g.drawVerticalLine(int(scope.getX() + scope.getWidth() * float(i) / 8.0f), scope.getY(), scope.getBottom());
        drawTrace(g, scope, t, true, lit);
        drawTrace(g, scope, t, false, Colour(0xff6fd4ff));
        g.setColour(mid);
        g.setFont(labelFont(8.5f, false));
        g.drawText("rail ripple, 32 ms", scope.reduced(4), juce::Justification::topLeft);
        g.setColour(lit);
        g.drawText("+25 V", scope.reduced(4).translated(0, 11), juce::Justification::topLeft);
        g.setColour(Colour(0xff6fd4ff));
        g.drawText("plate", scope.reduced(4).translated(46, 11), juce::Justification::topLeft);

        // --- heaters ----------------------------------------------------------------------------------------------------------
        const float heat = juce::jlimit(0.0f, 1.2f, t.heaterCurrent.load() / 0.135f);
        for (int k = 0; k < 2; ++k)
        {
            auto tb = Rectangle<float>(heaters.getX() + float(k) * heaters.getWidth() * 0.5f, heaters.getY(), heaters.getWidth() * 0.5f, heaters.getHeight()).reduced(4, 2);
            const float glow = heat * (0.78f + 0.22f * std::sin(flowPhase * 6.2831853f * 0.5f + float(k)));
            const float cx = tb.getX() + 16.0f;
            juce::Path bulb;
            bulb.addRoundedRectangle(cx - 9, tb.getY() + 1, 18, tb.getHeight() - 2, 8);
            g.setColour(Colour(0xff11161a));
            g.fillPath(bulb);
            g.setColour(Colour(0xffff9d3c).withAlpha(0.20f + 0.65f * glow));
            g.fillEllipse(cx - 5, tb.getCentreY() - 6, 10, 12);
            if (!skin->digital)
            {
                g.setColour(Colour(0xffff9d3c).withAlpha(0.12f * glow));
                g.fillEllipse(cx - 16, tb.getCentreY() - 14, 32, 28);
            }
            g.setColour(mid);
            g.setFont(labelFont(8.5f, false));
            g.drawText(k == 0 ? "V1  12AX7" : "V3  12AT7", tb.withTrimmedLeft(34).removeFromTop(11), juce::Justification::centredLeft);
            g.setColour(ink.withAlpha(0.8f));
            g.drawText(juce::String(t.heaterCurrent.load() * 1000.0f, 0) + " mA heater", tb.withTrimmedLeft(34).removeFromBottom(11), juce::Justification::centredLeft);
        }
        if (!live)
        {
            g.setColour(Colour(0xd0000000));
            g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(2), 4.0f);
            g.setColour(mid);
            g.setFont(labelFont(12.0f));
            g.drawText("Supply model off: the rails stay at their settled values (less CPU)", getLocalBounds().toFloat(), juce::Justification::centred);
        }
    }

private:
    static float trace(const std::atomic<float>* arr, int i) { return arr[i].load(std::memory_order_relaxed); }

    void drawTrace(Graphics& g, Rectangle<float> r, const tubechannel::channel::Telemetry& t, bool raw, Colour c) const
    {
        constexpr int N = tubechannel::channel::Telemetry::kScope;
        const int w = t.scopeWrite.load(std::memory_order_acquire);
        float mean = 0.f;
        for (int i = 0; i < N; ++i) mean += trace(raw ? t.scope25 : t.scope150, i);
        mean /= float(N);
        const float span = raw ? 4.0f : 3.0f;          // volts shown peak to peak, centred on the mean
        juce::Path p;
        for (int i = 0; i < N; ++i)
        {
            const float v = trace(raw ? t.scope25 : t.scope150, (w + i) % N) - mean;
            const float x = r.getX() + r.getWidth() * float(i) / float(N - 1);
            const float y = r.getCentreY() - juce::jlimit(-0.5f, 0.5f, v / span) * r.getHeight() * 0.9f;
            i == 0 ? p.startNewSubPath(x, y) : p.lineTo(x, y);
        }
        g.setColour(c.withAlpha(0.9f));
        g.strokePath(p, juce::PathStrokeType(1.3f));
    }

    void timerCallback() override
    {
        if (!isVisible()) return;
        const auto& t = proc.telemetry[0];
        const float load = juce::jlimit(0.0f, 0.5f, t.loadP15.load() + t.loadTube.load());
        flowPhase = std::fmod(flowPhase + (0.004f + load * 0.06f), 1.0f);
        const float v[6] = { t.p25.load(), t.p15.load(), -t.n15.load(), t.p45.load(), t.p75.load(), t.tubeP.load() };
        for (int i = 0; i < 6; ++i)
            avg[i] += (v[i] - avg[i]) * (initialised ? 0.02f : 1.0f);
        initialised = true;
        repaint();
    }

    TubeChannelProcessor& proc;
    const Skin* skin;
    float flowPhase = 0.f, avg[6] = { 26.f, 15.f, 15.f, 45.8f, 74.f, 165.f };
    bool initialised = false;
};
} // namespace tc::ui
