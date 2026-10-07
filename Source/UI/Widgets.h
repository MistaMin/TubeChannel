#pragma once

// UI building blocks, drawn procedurally on GoodLookinUI. Two skins share one layout:
//   analogue: a black rack panel with white printed legends, black knurled knobs with white pointers,
//             small push buttons with LEDs, an orange-backlit VU meter;
//   digital:  the same layout as flat vector graphics with seven-segment readouts.
// Nothing here touches the audio thread.

#include <GoodLookinUI.h>
#include <Hardware.h>
#include <Meters.h>
#include <Retro.h>
#include <Switches.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace tc::ui
{
namespace gl = goodlookinui;
namespace gla = goodlookinui::juce_adapter;
using juce::Colour;
using juce::Graphics;
using juce::Rectangle;

struct Skin
{
    bool digital = false;
    Colour panelTop, panelBottom, legend, legendDim, led, ledOff, accent, line, extraTop, extraBottom;
    Colour text, textMid, bgBottom;     // aliases used by the meter panels

    static Skin make(bool digital)
    {
        Skin s;
        s.digital = digital;
        if (!digital)
        {
            s.panelTop = Colour(0xff1f2123); s.panelBottom = Colour(0xff121415);
            s.legend = Colour(0xfff0eee8); s.legendDim = Colour(0xff9a9a96);
            s.led = Colour(0xffff7a22); s.ledOff = Colour(0xff3a2412);
            s.accent = Colour(0xffff9a2a); s.line = Colour(0xffd9d7d0);
            s.extraTop = Colour(0xff26282a); s.extraBottom = Colour(0xff17191a);
        }
        else
        {
            s.panelTop = Colour(0xff0e1a2a); s.panelBottom = Colour(0xff08111c);
            s.legend = Colour(0xffdbe8ff); s.legendDim = Colour(0xff6f86a8);
            s.led = Colour(0xff39d0ff); s.ledOff = Colour(0xff10283a);
            s.accent = Colour(0xff39d0ff); s.line = Colour(0xff3a5679);
            s.extraTop = Colour(0xff0f1d2f); s.extraBottom = Colour(0xff09131f);
        }
        s.text = s.legend; s.textMid = s.legendDim; s.bgBottom = s.panelBottom;
        return s;
    }
};

inline juce::Font font(float size, bool bold = false)
{
    return juce::Font(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font labelFont(float size, bool bold = true) { return font(size, bold); }

// A section heading: a line with the title set into it, as on the front panel.
inline void drawSectionLine(Graphics& g, const Skin& s, float x0, float x1, float y, const juce::String& title)
{
    g.setFont(font(10.5f, true));
    const float tw = juce::GlyphArrangement::getStringWidth(font(10.5f, true), title) + 12.0f;
    const float cx = 0.5f * (x0 + x1);
    g.setColour(s.line);
    g.fillRect(x0, y, cx - tw * 0.5f - x0, 1.2f);
    g.fillRect(cx + tw * 0.5f, y, x1 - cx - tw * 0.5f, 1.2f);
    g.setColour(s.legend);
    g.drawText(title, juce::Rectangle<float>(cx - tw * 0.5f, y - 7.0f, tw, 14.0f), juce::Justification::centred, false);
}

// ---- knob ---------------------------------------------------------------------------------------------
// Title above, printed scale around (analogue: tick ring plus numbers; legends "a|b|c" are spread evenly over the
// sweep), optional value readout below. The body diameter is the knob itself.
class Knob : public juce::Slider
{
public:
    Knob(const Skin* s, juce::String title, juce::String legends = {}, int bodyDiameter = 38, bool showValue = true)
        : skin(s), titleText(std::move(title)), legend(splitLegends(legends)), body(bodyDiameter), withValue(showValue)
    {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setMouseDragSensitivity(220);
        setPopupDisplayEnabled(true, true, nullptr);
        setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        setWantsKeyboardFocus(false);
    }

    static juce::StringArray splitLegends(const juce::String& t)
    {
        juce::StringArray out;
        if (t.isEmpty()) return out;
        int start = 0;
        for (int i = 0; i <= t.length(); ++i)
            if (i == t.length() || t[i] == '|') { out.add(t.substring(start, i)); start = i + 1; }
        return out;
    }
    static int widthFor(int d) { return d + 42; }
    static int heightFor(int d, bool withValue) { return int(float(d) * 0.5f + 28.0f + (float(d) * 0.5f + 11.0f) * 0.81f + 12.0f) + (withValue ? 12 : 0); }
    // centre of the knob body inside the widget
    static int centreY(int d) { return 28 + d / 2; }

    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        attachment.reset();
        param = apvts.getParameter(id);
        attachment = std::make_unique<juce::SliderParameterAttachment>(*static_cast<juce::RangedAudioParameter*>(param), *this);
        setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
        textFromValueFunction = [this](double v) { return param ? param->getText(param->convertTo0to1(float(v)), 0) : juce::String(v); };
    }

    // A plain click (no drag) on the knob flips this on/off parameter: the knob pops up and takes the other function
    void bindClick(juce::AudioProcessorValueTreeState& apvts, const juce::String& id, juce::String poppedTitle)
    {
        clickAttachment.reset();
        clickParam = static_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        poppedText = std::move(poppedTitle);
        clickAttachment = std::make_unique<juce::ParameterAttachment>(*clickParam, [this](float v) { popped = v > 0.5f; repaint(); });
        clickAttachment->sendInitialUpdate();
    }
    void mouseDown(const juce::MouseEvent& e) override { dragged = false; juce::Slider::mouseDown(e); }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 3) dragged = true;
        juce::Slider::mouseDrag(e);
    }
    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp(e);
        if (!dragged && clickAttachment != nullptr && !e.mods.isPopupMenu())
            clickAttachment->setValueAsCompleteGesture(popped ? 0.0f : 1.0f);
    }

    juce::String valueText() const { return param ? param->getText(param->getValue(), 0) : juce::String(); }
    void valueChanged() override { repaint(); }

    void paint(Graphics& g) override
    {
        const auto& sk = *skin;
        const float d = float(body), pi = juce::MathConstants<float>::pi;
        const float ring = d * 0.5f + 11.0f;
        const juce::Point<float> c(float(getWidth()) * 0.5f, float(centreY(body)));
        g.setColour(popped ? sk.accent : sk.legend);
        g.setFont(font(9.5f, true));
        g.drawText(popped ? poppedText : titleText, Rectangle<float>(0, 0, float(getWidth()), 12.0f), juce::Justification::centred, false);
        auto dirOf = [&](float a) { return juce::Point<float>(std::sin(a), -std::cos(a)); };

        if (!sk.digital)
        {
            // printed tick ring, longer at the legend positions
            const int n = 11;
            for (int i = 0; i < n; ++i)
            {
                const float a = (-0.8f + 1.6f * float(i) / float(n - 1)) * pi;
                const bool major = legend.size() > 1 ? (i * (legend.size() - 1)) % (n - 1) == 0 : (i % 5 == 0);
                const float r0 = d * 0.5f + 4.0f, r1 = r0 + (major ? 4.5f : 2.5f);
                g.setColour(sk.legend.withAlpha(major ? 0.8f : 0.5f));
                g.drawLine({ c + dirOf(a) * r0, c + dirOf(a) * r1 }, major ? 1.1f : 0.8f);
            }
        }
        gl::Item item;
        if (sk.digital) { item.style = "Neo"; item.colour = "#39d0ff"; }
        else            { item.style = "Rck"; item.colour = "#f2f2f0"; }
        if (popped)
        {
            // the knob is pulled up: a longer, softer shadow underneath and the whole knob a little higher and larger
            for (int i = 5; i > 0; --i)
            {
                g.setColour(Colour(0x16000000));
                const float e = d * 0.5f + float(i) * 1.6f;
                g.fillEllipse(c.x - e, c.y - e + 7.0f, e * 2, e * 2);
            }
        }
        gla::drawKnob(g, Rectangle<float>(d, d).withCentre(c).translated(0, popped ? -3.0f : 0.0f).expanded(popped ? 2.0f : 0.0f), popped ? 0.5 : getNormalisableRange().convertTo0to1(getValue()), item, true);

        g.setColour(sk.legend.withAlpha(0.85f));
        g.setFont(font(8.0f, true));
        for (int i = 0; i < legend.size(); ++i)
        {
            const float t = legend.size() > 1 ? float(i) / float(legend.size() - 1) : 0.5f;
            const float a = (-0.8f + 1.6f * t) * pi;
            const auto p = c + dirOf(a) * ring;
            g.drawText(legend[i], Rectangle<float>(34.0f, 10.0f).withCentre(p + juce::Point<float>(0, a * a > 0.8f ? 2.0f : 0.0f)), juce::Justification::centred, false);
        }
        if (withValue)
        {
            g.setColour(sk.accent.withAlpha(0.9f));
            g.setFont(font(9.0f, sk.digital));
            g.drawText(valueText(), Rectangle<float>(0, float(getHeight()) - 12.0f, float(getWidth()), 12.0f), juce::Justification::centred, false);
        }
    }

    const Skin* skin;
    juce::String titleText;
    juce::StringArray legend;
    int body;
    bool withValue;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::RangedAudioParameter* clickParam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> clickAttachment;
    juce::String poppedText;
    bool popped = false, dragged = false;
};

// ---- horizontal slider (digital skin) -----------------------------------------------------------------------------
class HSlider : public juce::Component
{
public:
    HSlider(const Skin* s, juce::String title, int titleWidth = 74, int valueWidth = 58)
        : skin(s), titleText(std::move(title)), tw(titleWidth), vw(valueWidth) {}

    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        attachment.reset();
        param = static_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        attachment = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { norm = param->convertTo0to1(v); repaint(); });
        attachment->sendInitialUpdate();
    }

    void paint(Graphics& g) override
    {
        const auto& sk = *skin;
        auto r = getLocalBounds().toFloat();
        g.setColour(sk.legend);
        g.setFont(font(9.5f, true));
        g.drawText(titleText, r.removeFromLeft(float(tw)), juce::Justification::centredLeft, false);
        auto val = r.removeFromRight(float(vw));
        g.setColour(sk.accent);
        g.setFont(font(9.5f, true));
        g.drawText(param ? param->getText(norm, 0) : juce::String(), val, juce::Justification::centredRight, false);
        track = r.reduced(2.0f, 0.0f);
        const float cy = track.getCentreY();
        g.setColour(Colour(0xff050a12));
        g.fillRoundedRectangle(track.getX(), cy - 3.0f, track.getWidth(), 6.0f, 3.0f);
        const float x = track.getX() + track.getWidth() * norm;
        g.setGradientFill(juce::ColourGradient(sk.accent.darker(0.5f), track.getX(), 0, sk.accent, x, 0, false));
        g.fillRoundedRectangle(track.getX(), cy - 3.0f, std::max(6.0f, x - track.getX()), 6.0f, 3.0f);
        g.setColour(sk.line.withAlpha(0.6f));
        g.drawRoundedRectangle(track.getX(), cy - 3.0f, track.getWidth(), 6.0f, 3.0f, 0.8f);
        g.setColour(sk.accent.withAlpha(0.28f));
        g.fillEllipse(x - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        g.setColour(juce::Colours::white);
        g.fillEllipse(x - 5.0f, cy - 5.0f, 10.0f, 10.0f);
        g.setColour(sk.accent);
        g.drawEllipse(x - 5.0f, cy - 5.0f, 10.0f, 10.0f, 1.4f);
    }

    void mouseDown(const juce::MouseEvent& e) override { if (attachment) attachment->beginGesture(); drag(e); }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e); }
    void mouseUp(const juce::MouseEvent&) override { if (attachment) attachment->endGesture(); }
    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (!attachment) return;
        attachment->beginGesture();
        attachment->setValueAsPartOfGesture(param->convertFrom0to1(param->getDefaultValue()));
        attachment->endGesture();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (!attachment) return;
        const float n = juce::jlimit(0.0f, 1.0f, norm + w.deltaY * 0.1f);
        attachment->setValueAsCompleteGesture(param->convertFrom0to1(n));
    }
    const Skin* skin;

private:
    void drag(const juce::MouseEvent& e)
    {
        if (!attachment || track.getWidth() <= 0) return;
        const float n = juce::jlimit(0.0f, 1.0f, (float(e.x) - track.getX()) / track.getWidth());
        attachment->setValueAsPartOfGesture(param->convertFrom0to1(n));
    }
    juce::String titleText;
    int tw, vw;
    float norm = 0.f;
    Rectangle<float> track;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// ---- LED push button: legend above (state when on), key with a lamp, legend below (state when off) -----------------------------
class LedButton : public juce::Button
{
public:
    static constexpr int kHeight = 34;
    LedButton(const Skin* s, juce::String top, juce::String bottom = {}, Colour ledColour = Colour())
        : juce::Button(top), skin(s), topText(std::move(top)), bottomText(std::move(bottom)), led(ledColour) {}

    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        attachment.reset();
        attachment = std::make_unique<juce::ButtonParameterAttachment>(*static_cast<juce::RangedAudioParameter*>(apvts.getParameter(id)), *this);
        setClickingTogglesState(true);
    }

    void paintButton(Graphics& g, bool over, bool down) override
    {
        const auto& sk = *skin;
        const bool on = getToggleState();
        const Colour lc = led.isTransparent() ? sk.led : led;
        const float w = float(getWidth());
        auto key = Rectangle<float>(22.0f, 12.0f).withCentre({ w * 0.5f, 17.0f });
        if (sk.digital)
        {
            g.setColour(on ? lc.withAlpha(0.9f) : sk.extraTop);
            g.fillRoundedRectangle(key, 2.5f);
            g.setColour(on ? lc : sk.line);
            g.drawRoundedRectangle(key, 2.5f, 1.0f);
        }
        else
            gla::hardware::drawLedKey(g, key, lc, on, down);
        g.setFont(font(8.0f, true));
        auto topArea = Rectangle<float>(0, 0, w, 10.0f);
        auto botArea = Rectangle<float>(0, 25.0f, w, 9.0f);
        g.setColour(bottomText.isEmpty() ? sk.legend : (on ? sk.legend : sk.legendDim.withAlpha(0.7f)));
        g.drawText(topText, topArea, juce::Justification::centred, false);
        if (bottomText.isNotEmpty())
        {
            g.setColour(!on ? sk.legend : sk.legendDim.withAlpha(0.7f));
            g.drawText(bottomText, botArea, juce::Justification::centred, false);
        }
        if (over && !down)
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRect(getLocalBounds());
        }
    }

    const Skin* skin;
private:
    juce::String topText, bottomText;
    Colour led;
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

// momentary push button for the toolbar
class PushButton : public juce::Button
{
public:
    PushButton(const Skin* s, juce::String t) : juce::Button(t), skin(s), text(std::move(t)) {}
    void setText(juce::String t) { text = std::move(t); repaint(); }
    void setLit(bool l) { lit = l; repaint(); }
    void paintButton(Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced(1);
        const auto& sk = *skin;
        g.setColour(Colour(0xff050505));
        g.fillRoundedRectangle(r.expanded(1), 3);
        g.setGradientFill(juce::ColourGradient(lit ? sk.accent.darker(0.3f) : Colour(sk.digital ? 0xff1a2c42 : 0xff303234), r.getX(), r.getY(),
                                               lit ? sk.accent.darker(0.7f) : Colour(sk.digital ? 0xff0e1a2a : 0xff161718), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r.translated(0, down ? 1.0f : 0.0f), 2.5f);
        if (over) { g.setColour(juce::Colours::white.withAlpha(0.06f)); g.fillRoundedRectangle(r, 2.5f); }
        g.setColour(lit ? Colours_black() : sk.legend);
        g.setFont(font(10.0f, true));
        g.drawText(text, r, juce::Justification::centred, false);
    }
    const Skin* skin;
private:
    static Colour Colours_black() { return juce::Colours::black; }
    juce::String text;
    bool lit = false;
};

// ---- the \( ᐖ)/ face as vector art ---------------------------------------------------------------------------------
// Drawn from the glyph shapes: a raised backslash arm, "(", a play-triangle with a stem and two dots, ")", a raised slash arm.
inline void drawDerpFace(Graphics& g, Rectangle<float> r, Colour c)
{
    const float s = std::min(r.getWidth() / 334.0f, r.getHeight() / 182.0f);
    const auto o = r.getCentre() - juce::Point<float>(167.0f, 105.0f) * s;
    auto P = [&](float x, float y) { return juce::Point<float>(o.x + x * s, o.y + y * s); };
    const float w = std::max(2.0f, 12.0f * s);
    g.setColour(c);
    juce::Path p;
    p.startNewSubPath(P(12, 62));  p.lineTo(P(44, 190));                         // \ arm
    p.startNewSubPath(P(94, 62));  p.quadraticTo(P(58, 126), P(94, 190));          // (
    p.startNewSubPath(P(147, 65)); p.lineTo(P(228, 112)); p.lineTo(P(147, 165)); p.closeSubPath();   // triangle
    p.startNewSubPath(P(147, 65)); p.lineTo(P(147, 42));                           // stem
    p.startNewSubPath(P(250, 62)); p.quadraticTo(P(286, 126), P(250, 190));        // )
    p.startNewSubPath(P(290, 190)); p.lineTo(P(322, 62));                          // / arm
    g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (auto d : { P(147, 25), P(207, 56) })
        g.fillEllipse(d.x - 6.0f * s - w * 0.15f, d.y - 6.0f * s - w * 0.15f, 12.0f * s + w * 0.3f, 12.0f * s + w * 0.3f);
}

// ---- key bank for choice parameters -----------------------------------------------------------------------------------
class KeyBank : public juce::Component
{
public:
    KeyBank(const Skin* s, juce::StringArray names, int derpIndex = -1, std::vector<float> keyWeights = {})
        : skin(s), keys(std::move(names)), derp(derpIndex), weights(std::move(keyWeights))
    {
        weights.resize(size_t(keys.size()), 1.0f);
        total = 0; for (auto w : weights) total += w;
    }
    void bind(juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        attachment.reset();
        param = static_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
        attachment = std::make_unique<juce::ParameterAttachment>(*param, [this](float v) { selected = juce::roundToInt(v); repaint(); });
        attachment->sendInitialUpdate();
    }
    void paint(Graphics& g) override
    {
        const auto& sk = *skin;
        float x = 0;
        for (int i = 0; i < keys.size(); ++i)
        {
            const float w = float(getWidth()) * weights[size_t(i)] / total;
            auto r = Rectangle<float>(x, 0, w, float(getHeight())).reduced(2);
            x += w;
            const bool lit = i == selected;
            g.setColour(Colour(0xff050505));
            g.fillRoundedRectangle(r.expanded(1), 3);
            g.setGradientFill(juce::ColourGradient(lit ? sk.accent.darker(0.15f) : Colour(sk.digital ? 0xff1a2c42 : 0xff303234), r.getX(), r.getY(),
                                                   lit ? sk.accent.darker(0.6f) : Colour(sk.digital ? 0xff0e1a2a : 0xff161718), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 2.5f);
            const Colour ink = lit ? juce::Colours::black : sk.legend;
            if (i == derp)
                drawDerpFace(g, r.reduced(8, 5), ink);
            else
            {
                g.setColour(ink);
                g.setFont(font(11.0f, true));
                g.drawText(keys[i], r, juce::Justification::centred, false);
            }
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        float x = 0;
        int hit = keys.size() - 1;
        for (int i = 0; i < keys.size(); ++i)
        {
            x += float(getWidth()) * weights[size_t(i)] / total;
            if (float(e.x) < x) { hit = i; break; }
        }
        if (attachment != nullptr)
            attachment->setValueAsCompleteGesture(float(hit));
    }
    const Skin* skin;
private:
    juce::StringArray keys;
    int derp, selected = 0;
    std::vector<float> weights;
    float total = 1;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
} // namespace tc::ui
