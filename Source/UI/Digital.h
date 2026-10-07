#pragma once

// Displays used by the digital skin: a spectrum with an editable EQ curve, and a compressor transfer curve with
// draggable threshold, ratio and make-up handles. They read and write the same parameters as the analogue knobs.

#include <juce_dsp/juce_dsp.h>

#include <complex>
#include <set>

#include "../Plugin/PluginProcessor.h"
#include "Widgets.h"

namespace tc::ui
{
namespace pid = tc::params::id;

inline double dbToNorm(double db) { return db / 24.0 + 0.5; }

// Show the same parameter edits the knobs make: begin a gesture on the first touch, end it on release.
struct ParamEditor
{
    explicit ParamEditor(juce::AudioProcessorValueTreeState& a) : apvts(a) {}
    void set(const juce::String& id, float norm01)
    {
        auto* p = apvts.getParameter(id);
        if (p == nullptr) return;
        if (touched.find(id) == touched.end()) { p->beginChangeGesture(); touched.insert(id); }
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm01));
    }
    void setBool(const juce::String& id, bool v)
    {
        auto* p = apvts.getParameter(id);
        if (p == nullptr) return;
        const float want = v ? 1.0f : 0.0f;
        if (p->getValue() == want) return;
        p->beginChangeGesture(); p->setValueNotifyingHost(want); p->endChangeGesture();
    }
    void release()
    {
        for (auto& id : touched)
            if (auto* p = apvts.getParameter(id)) p->endChangeGesture();
        touched.clear();
    }
    float get(const juce::String& id) const { return apvts.getRawParameterValue(id)->load(); }
    juce::AudioProcessorValueTreeState& apvts;
    std::set<juce::String> touched;
};

// RBJ biquad magnitude, for drawing the curve only (the audio is the circuit, not these filters)
inline double biquadDb(double b0, double b1, double b2, double a0, double a1, double a2, double w)
{
    const std::complex<double> z1 = std::polar(1.0, -w), z2 = std::polar(1.0, -2.0 * w);
    const auto num = b0 + b1 * z1 + b2 * z2, den = a0 + a1 * z1 + a2 * z2;
    return 20.0 * std::log10(std::max(1e-9, std::abs(num / den)));
}
inline double peakDb(double f, double g, double q, double fq, double fs = 48000.0)
{
    const double A = std::pow(10.0, g / 40.0), w0 = 2.0 * juce::MathConstants<double>::pi * f / fs, al = std::sin(w0) / (2.0 * q), c = std::cos(w0);
    return biquadDb(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A, 2.0 * juce::MathConstants<double>::pi * fq / fs);
}
inline double shelfDb(bool low, double f, double g, double fq, double fs = 48000.0)
{
    const double A = std::pow(10.0, g / 40.0), w0 = 2.0 * juce::MathConstants<double>::pi * f / fs, c = std::cos(w0), sn = std::sin(w0);
    const double al = sn / 2.0 * std::sqrt(2.0), tsa = 2.0 * std::sqrt(A) * al;
    const double w = 2.0 * juce::MathConstants<double>::pi * fq / fs;
    if (low)
        return biquadDb(A * ((A + 1) - (A - 1) * c + tsa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - tsa),
                        (A + 1) + (A - 1) * c + tsa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - tsa, w);
    return biquadDb(A * ((A + 1) + (A - 1) * c + tsa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - tsa),
                    (A + 1) - (A - 1) * c + tsa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - tsa, w);
}

// ---- spectrum with the EQ curve ---------------------------------------------------------------------------
class EqDisplay : public juce::Component, private juce::Timer
{
public:
    EqDisplay(TubeChannelProcessor& p, const Skin* s, std::function<bool()> channelB)
        : proc(p), skin(s), chB(std::move(channelB)), editor(p.apvts), fft(kOrder), window(size_t(kN), juce::dsp::WindowingFunction<float>::hann)
    {
        startTimerHz(30);
        mag.assign(size_t(kN / 2), -120.0f);
    }

    void paint(Graphics& g) override
    {
        const auto& sk = *skin;
        auto r = getLocalBounds().toFloat();
        g.setColour(Colour(0xff050a12));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(sk.line.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        area = r.reduced(8.0f, 8.0f);
        g.saveState();
        g.reduceClipRegion(r.reduced(1).toNearestInt());

        // grid
        g.setFont(font(8.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xOf(f);
            g.setColour(sk.line.withAlpha(f == 100 || f == 1000 || f == 10000 ? 0.45f : 0.2f));
            g.drawVerticalLine(int(x), area.getY(), area.getBottom());
            if (f == 100 || f == 1000 || f == 10000)
            {
                g.setColour(sk.legendDim);
                g.drawText(f >= 1000 ? juce::String(int(f / 1000)) + "k" : juce::String(int(f)), Rectangle<float>(x - 14, area.getBottom() - 11, 28, 10), juce::Justification::centred, false);
            }
        }
        for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
        {
            const float y = yOfDb(db);
            g.setColour(sk.line.withAlpha(db == 0 ? 0.55f : 0.2f));
            g.drawHorizontalLine(int(y), area.getX(), area.getRight());
            if (db != 0)
            {
                g.setColour(sk.legendDim);
                g.drawText((db > 0 ? "+" : "") + juce::String(int(db)), Rectangle<float>(area.getX() + 1, y - 5, 22, 10), juce::Justification::centredLeft, false);
            }
        }

        // spectrum, filled
        juce::Path spec;
        const int w = int(area.getWidth());
        spec.startNewSubPath(area.getX(), area.getBottom());
        for (int px = 0; px <= w; ++px)
        {
            const double f0 = fOf(area.getX() + float(px) - 0.5f), f1 = fOf(area.getX() + float(px) + 0.5f);
            const int b0 = juce::jlimit(1, kN / 2 - 1, int(f0 / binHz)), b1 = juce::jlimit(b0, kN / 2 - 1, int(std::ceil(f1 / binHz)));
            float m = -120.0f;
            for (int b = b0; b <= b1; ++b) m = std::max(m, mag[size_t(b)]);
            const float t = juce::jlimit(0.0f, 1.0f, (m + 100.0f) / 90.0f);
            spec.lineTo(area.getX() + float(px), area.getBottom() - t * area.getHeight() * 0.9f);
        }
        spec.lineTo(area.getRight(), area.getBottom());
        spec.closeSubPath();
        g.setGradientFill(juce::ColourGradient(sk.accent.withAlpha(0.42f), 0, area.getY(), sk.accent.withAlpha(0.03f), 0, area.getBottom(), false));
        g.fillPath(spec);
        g.setColour(sk.accent.withAlpha(0.65f));
        g.strokePath(spec, juce::PathStrokeType(1.0f));

        // EQ curve
        const bool eqOn = editor.get(id("eq")) > 0.5f;
        const auto bands = readBands();
        juce::Path curve;
        for (int px = 0; px <= w; px += 2)
        {
            const double f = fOf(area.getX() + float(px));
            const float y = yOfDb(eqOn ? responseDb(bands, f) : 0.0);
            px == 0 ? curve.startNewSubPath(area.getX(), y) : curve.lineTo(area.getX() + float(px), y);
        }
        g.setColour(Colour(0xffffffff).withAlpha(eqOn ? 0.18f : 0.06f));
        g.strokePath(curve, juce::PathStrokeType(5.0f));
        g.setColour(Colour(0xffffffff).withAlpha(eqOn ? 1.0f : 0.45f));
        g.strokePath(curve, juce::PathStrokeType(1.8f));

        // handles
        for (int i = 0; i < 4; ++i)
        {
            const auto p = handlePos(i, bands);
            const auto c = bandColour(i);
            g.setColour(c.withAlpha(0.28f));
            g.fillEllipse(p.x - 11, p.y - 11, 22, 22);
            g.setColour(c);
            g.fillEllipse(p.x - 6, p.y - 6, 12, 12);
            g.setColour(Colour(0xff050a12));
            g.fillEllipse(p.x - 2.5f, p.y - 2.5f, 5, 5);
            g.setColour(c);
            g.setFont(font(8.5f, true));
            const char* names[] = { "LOW", "LO-MID", "HI-MID", "HIGH" };
            const float dy = (i == 1 || i == 3) ? 9.0f : -19.0f;   // alternate above / below so neighbouring labels never collide
            g.drawText(names[i], Rectangle<float>(p.x - 28, p.y + dy, 56, 10), juce::Justification::centred, false);
        }
        g.restoreState();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto bands = readBands();
        float best = 20.0f;
        active = -1;
        for (int i = 0; i < 4; ++i)
        {
            const float d = handlePos(i, bands).getDistanceFrom(e.position);
            if (d < best) { best = d; active = i; }
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (active < 0) return;
        const double f = fOf(e.position.x);
        const double db = juce::jlimit(-12.0, 12.0, dbOfY(e.position.y));
        const bool b = chB();
        switch (active)
        {
            case 0: editor.setBool(id("loshift"), f < 70.0); editor.set(id("low"), float(dbToNorm(db))); break;
            case 3: editor.setBool(id("hishift"), f < 10500.0); editor.set(id("high"), float(dbToNorm(db))); break;
            case 1:
            {
                bool x10 = editor.get(id("lmx10")) > 0.5f;
                if (f > 214.0) x10 = true; else if (f < 178.0) x10 = false;
                editor.setBool(id("lmx10"), x10);
                const double lo = x10 ? 178.0 : 16.0, hi = x10 ? 2350.0 : 214.0;
                editor.set(id("lmfreq"), float(std::log(juce::jlimit(lo, hi, f) / lo) / std::log(hi / lo)));
                editor.set(id("lmlevel"), float(dbToNorm(db)));
                break;
            }
            case 2:
            {
                bool x10 = editor.get(id("hmx10")) > 0.5f;
                if (f > 2100.0) x10 = true; else if (f < 1800.0) x10 = false;
                editor.setBool(id("hmx10"), x10);
                const double lo = x10 ? 1800.0 : 162.0, hi = x10 ? 23000.0 : 2100.0;
                editor.set(id("hmfreq"), float(std::log(juce::jlimit(lo, hi, f) / lo) / std::log(hi / lo)));
                editor.set(id("hmlevel"), float(dbToNorm(db)));
                break;
            }
        }
        (void) b;
    }
    void mouseUp(const juce::MouseEvent&) override { editor.release(); active = -1; }
    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // double-click a mid band to flip narrow / wide, any band to zero it
        mouseDown(e);
        if (active == 1) editor.setBool(id("lmnarrow"), editor.get(id("lmnarrow")) < 0.5f);
        if (active == 2) editor.setBool(id("hmnarrow"), editor.get(id("hmnarrow")) < 0.5f);
        static const char* lv[] = { "low", "lmlevel", "hmlevel", "high" };
        if (active >= 0) { editor.set(id(lv[active]), 0.5f); editor.release(); }
        active = -1;
    }
    const Skin* skin;

private:
    struct Bands { double lowF, lowG, highF, highG, lmF, lmG, lmQ, hmF, hmG, hmQ; };

    juce::String id(const char* base) const { return tc::params::idFor(base, chB()); }
    Bands readBands() const
    {
        Bands b;
        b.lowF = editor.get(id("loshift")) > 0.5f ? 40.0 : 120.0;
        b.lowG = tc::params::eqGainDb(editor.get(id("low")));
        b.highF = editor.get(id("hishift")) > 0.5f ? 6000.0 : 18000.0;
        b.highG = tc::params::eqGainDb(editor.get(id("high")));
        b.lmF = tc::params::lmFreqHz(editor.get(id("lmfreq")), editor.get(id("lmx10")) > 0.5f);
        b.lmG = tc::params::eqGainDb(editor.get(id("lmlevel")));
        b.lmQ = editor.get(id("lmnarrow")) > 0.5f ? 2.5 : 0.7;
        b.hmF = tc::params::hmFreqHz(editor.get(id("hmfreq")), editor.get(id("hmx10")) > 0.5f);
        b.hmG = tc::params::eqGainDb(editor.get(id("hmlevel")));
        b.hmQ = editor.get(id("hmnarrow")) > 0.5f ? 2.5 : 0.7;
        return b;
    }
    static double responseDb(const Bands& b, double f)
    {
        return shelfDb(true, b.lowF, b.lowG, f) + shelfDb(false, b.highF, b.highG, f) + peakDb(b.lmF, b.lmG, b.lmQ, f) + peakDb(b.hmF, b.hmG, b.hmQ, f);
    }
    juce::Point<float> handlePos(int i, const Bands& b) const
    {
        switch (i)
        {
            case 0: return { xOf(b.lowF), yOfDb(b.lowG) };
            case 1: return { xOf(b.lmF), yOfDb(b.lmG) };
            case 2: return { xOf(b.hmF), yOfDb(b.hmG) };
            default: return { xOf(b.highF), yOfDb(b.highG) };
        }
    }
    static Colour bandColour(int i)
    {
        static const Colour c[] = { Colour(0xffff6a5a), Colour(0xff5ae07a), Colour(0xff5ab4ff), Colour(0xffffc04a) };
        return c[i];
    }
    float xOf(double f) const { return area.getX() + float(std::log(f / 20.0) / std::log(1000.0)) * area.getWidth(); }
    double fOf(float x) const { return 20.0 * std::pow(1000.0, double((x - area.getX()) / area.getWidth())); }
    float yOfDb(double db) const { return area.getCentreY() - float(db / 12.0) * area.getHeight() * 0.36f; }
    double dbOfY(float y) const { return double((area.getCentreY() - y) / (area.getHeight() * 0.36f)) * 12.0; }

    void timerCallback() override
    {
        if (!isVisible()) return;
        const int w = proc.specWrite.load(std::memory_order_acquire);
        for (int i = 0; i < kN; ++i)
            data[size_t(i)] = proc.specRing[(w - kN + i + 2 * TubeChannelProcessor::kSpecSize) % TubeChannelProcessor::kSpecSize].load(std::memory_order_relaxed);
        std::fill(data.begin() + kN, data.end(), 0.0f);
        window.multiplyWithWindowingTable(data.data(), size_t(kN));
        fft.performFrequencyOnlyForwardTransform(data.data());
        const double fs = std::max(8000.0, proc.getSampleRate());
        binHz = fs / double(kN);
        for (int b = 0; b < kN / 2; ++b)
        {
            const float db = 20.0f * std::log10(std::max(1e-9f, data[size_t(b)] / float(kN) * 4.0f));
            float& m = mag[size_t(b)];
            m = db > m ? db : m + (db - m) * 0.25f;
        }
        repaint();
    }

    static constexpr int kOrder = 11, kN = 1 << kOrder;
    TubeChannelProcessor& proc;
    std::function<bool()> chB;
    mutable ParamEditor editor;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, 2 * kN> data {};
    std::vector<float> mag;
    double binHz = 23.4;
    Rectangle<float> area;
    int active = -1;
};

// ---- compressor transfer curve -----------------------------------------------------------------------------------
class CompDisplay : public juce::Component, private juce::Timer
{
public:
    CompDisplay(TubeChannelProcessor& p, const Skin* s, std::function<bool()> channelB)
        : proc(p), skin(s), chB(std::move(channelB)), editor(p.apvts) { startTimerHz(30); }

    void paint(Graphics& g) override
    {
        const auto& sk = *skin;
        auto r = getLocalBounds().toFloat();
        g.setColour(Colour(0xff050a12));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(sk.line.withAlpha(0.7f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        area = r.reduced(22.0f, 14.0f).withTrimmedBottom(4.0f);
        const auto st = state();

        g.setFont(font(8.0f));
        for (double db = kLo; db <= kHi + 0.01; db += 10.0)
        {
            g.setColour(sk.line.withAlpha(db == 0 ? 0.5f : 0.2f));
            g.drawVerticalLine(int(xOf(db)), area.getY(), area.getBottom());
            g.drawHorizontalLine(int(yOf(db)), area.getX(), area.getRight());
            g.setColour(sk.legendDim);
            g.drawText(juce::String(int(db)), Rectangle<float>(xOf(db) - 12, area.getBottom() + 1, 24, 10), juce::Justification::centred, false);
            g.drawText(juce::String(int(db)), Rectangle<float>(1, yOf(db) - 5, 19, 10), juce::Justification::centredRight, false);
        }
        g.setColour(sk.legendDim);
        g.drawText("IN dBm", Rectangle<float>(area.getRight() - 50, area.getBottom() - 12, 50, 10), juce::Justification::centredRight, false);
        g.drawText("OUT", Rectangle<float>(area.getX() + 2, area.getY() + 1, 30, 10), juce::Justification::centredLeft, false);

        g.saveState();
        g.reduceClipRegion(area.expanded(1).toNearestInt());
        // unity line
        g.setColour(sk.line.withAlpha(0.6f));
        const float dash[] = { 3.0f, 3.0f };
        g.drawDashedLine({ { xOf(kLo), yOf(kLo) }, { xOf(kHi), yOf(kHi) } }, dash, 2, 1.0f);
        // curve
        const bool lim = editor.get(id("ratiolimit")) > 0.5f;
        juce::Path curve;
        for (double in = kLo; in <= kHi + 0.01; in += 0.5)
        {
            const float x = xOf(in), y = yOf(lim ? std::min(in, st.thr) : outDb(st, in));
            in == kLo ? curve.startNewSubPath(x, y) : curve.lineTo(x, y);
        }
        juce::Path fill(curve);
        fill.lineTo(xOf(kHi), area.getBottom());
        fill.lineTo(xOf(kLo), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(sk.accent.withAlpha(0.22f), 0, area.getY(), sk.accent.withAlpha(0.0f), 0, area.getBottom(), false));
        g.fillPath(fill);
        g.setColour(sk.accent.withAlpha(0.3f));
        g.strokePath(curve, juce::PathStrokeType(5.0f));
        g.setColour(Colours_white());
        g.strokePath(curve, juce::PathStrokeType(1.8f));
        // live gain reduction
        const float gr = std::max(0.0f, proc.telemetry[chB() ? 1 : 0].grDb.load());
        shownGr += (gr - shownGr) * 0.3f;
        g.setColour(Colour(0xffff9a2a));
        g.fillRect(Rectangle<float>(area.getRight() - 5.0f, area.getY(), 4.0f, area.getHeight() * juce::jlimit(0.0f, 1.0f, shownGr / 24.0f)));
        // handles
        const struct { juce::Point<float> p; const char* label; Colour c; } hs[3] = {
            { handle(0, st), lim ? "CEILING" : "THRESH", Colour(0xff5ab4ff) }, { handle(1, st), "RATIO", Colour(0xffffc04a) }, { handle(2, st), "OUT", Colour(0xff5ae07a) } };
        for (int hi = 0; hi < (lim ? 1 : 3); ++hi)
        {
            const auto& h = hs[hi];
            g.setColour(h.c.withAlpha(0.28f)); g.fillEllipse(h.p.x - 10, h.p.y - 10, 20, 20);
            g.setColour(h.c); g.fillEllipse(h.p.x - 5.5f, h.p.y - 5.5f, 11, 11);
            g.setColour(Colour(0xff050a12)); g.fillEllipse(h.p.x - 2.2f, h.p.y - 2.2f, 4.4f, 4.4f);
            g.setColour(h.c); g.setFont(font(8.0f, true));
            g.drawText(h.label, Rectangle<float>(h.p.x - 24, h.p.y + (h.label[0] == 'R' ? -18.0f : 7.0f), 48, 10), juce::Justification::centred, false);
        }
        g.restoreState();
        g.setColour(sk.accent);
        g.setFont(font(9.5f, true));
        g.drawText("GR " + juce::String(shownGr, 1) + " dB", r.reduced(8, 3).removeFromTop(12), juce::Justification::centredRight, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto st = state();
        float best = 18.0f;
        active = -1;
        const int nh = editor.get(id("ratiolimit")) > 0.5f ? 1 : 3;     // as a limiter only the ceiling can be moved
        for (int i = 0; i < nh; ++i)
        {
            const float d = handle(i, st).getDistanceFrom(e.position);
            if (d < best) { best = d; active = i; }
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (active < 0) return;
        const auto st = state();
        const double in = juce::jlimit(kLo, kHi, inOf(e.position.x)), out = juce::jlimit(kLo, kHi, outOf(e.position.y));
        if (active == 0)
            editor.set(id("threshold"), float((juce::jlimit(-20.0, 20.0, in) + 20.0) / 40.0));
        else if (active == 1)
        {
            const double run = kRatioX(st) - st.thr, rise = out - st.makeup - st.thr;
            const double R = rise <= run / 40.0 ? 40.0 : run / rise;
            const float k = float(tc::params::ratioKnobFor(R));
            editor.set(id("ratio"), k);
        }
        else
        {
            const double refIn = st.thr - 14.0;
            const double dbMakeup = juce::jlimit(-12.0, 4.5, out - refIn);
            const double a88 = (std::pow(10.0, 1.76) - 1.0) / 99.0;
            const double a = juce::jlimit(0.0, 1.0, a88 * std::pow(10.0, dbMakeup / 20.0));
            editor.set(id("compout"), float(std::log10(99.0 * a + 1.0) / 2.0));
        }
    }
    void mouseUp(const juce::MouseEvent&) override { editor.release(); active = -1; }
    const Skin* skin;

private:
    static constexpr double kLo = -40.0, kHi = 20.0;
    struct State { double thr, ratio, makeup, knee; };
    static Colour Colours_white() { return Colour(0xffffffff); }
    juce::String id(const char* base) const { return tc::params::idFor(base, chB()); }
    State state() const
    {
        State s;
        s.thr = tc::params::thresholdDbm(editor.get(id("threshold")));
        s.ratio = tc::params::ratioValue(editor.get(id("ratio")));
        const double v = editor.get(id("compout")), a88 = (std::pow(10.0, 1.76) - 1.0) / 99.0, a = (std::pow(10.0, 2.0 * v) - 1.0) / 99.0;
        s.makeup = v < 0.01 ? -60.0 : 20.0 * std::log10(a / a88);
        s.knee = 6.0;
        return s;
    }
    static double kRatioX(const State& s) { return std::min(s.thr + 18.0, kHi - 2.0); }
    static double outDb(const State& s, double in)
    {
        const double over = in - s.thr, w = s.knee;
        double y;
        if (over <= -w * 0.5) y = in;
        else if (over >= w * 0.5) y = s.thr + over / s.ratio;
        else y = in + (1.0 / s.ratio - 1.0) * (over + w * 0.5) * (over + w * 0.5) / (2.0 * w);
        return y + s.makeup;
    }
    juce::Point<float> handle(int i, const State& s) const
    {
        if (i == 0) return { xOf(s.thr), yOf(outDb(s, s.thr)) };
        if (i == 1) return { xOf(kRatioX(s)), yOf(outDb(s, kRatioX(s))) };
        return { xOf(s.thr - 14.0), yOf(outDb(s, s.thr - 14.0)) };
    }
    float xOf(double db) const { return area.getX() + float((db - kLo) / (kHi - kLo)) * area.getWidth(); }
    float yOf(double db) const { return area.getBottom() - float((db - kLo) / (kHi - kLo)) * area.getHeight(); }
    double inOf(float x) const { return kLo + double((x - area.getX()) / area.getWidth()) * (kHi - kLo); }
    double outOf(float y) const { return kLo + double((area.getBottom() - y) / area.getHeight()) * (kHi - kLo); }
    void timerCallback() override { if (isVisible()) repaint(); }

    TubeChannelProcessor& proc;
    std::function<bool()> chB;
    mutable ParamEditor editor;
    Rectangle<float> area;
    int active = -1;
    float shownGr = 0.f;
};
} // namespace tc::ui
