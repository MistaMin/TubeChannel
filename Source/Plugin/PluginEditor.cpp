#include "PluginEditor.h"

#include "../UI/Digital.h"

namespace id = tc::params::id;
using namespace tc::ui;

// ---------------------------------------------------------------------------------------------------------------------
// Small key row for UI-only state kept in the plugin state tree (VU source).
class StateKeys : public juce::Component
{
public:
    StateKeys(TubeChannelProcessor& p, const Skin* s, const char* prop, juce::StringArray n, int def)
        : proc(p), skin(s), property(prop), names(std::move(n)), dflt(def) {}
    void paint(Graphics& g) override
    {
        const int sel = int(proc.apvts.state.getProperty(property, dflt));
        const float w = float(getWidth()) / float(names.size());
        for (int i = 0; i < names.size(); ++i)
        {
            auto r = Rectangle<float>(w * float(i), 0, w, float(getHeight())).reduced(2);
            const bool lit = i == sel;
            g.setColour(Colour(0xff050505));
            g.fillRoundedRectangle(r.expanded(1), 3);
            g.setGradientFill(juce::ColourGradient(lit ? skin->accent.darker(0.15f) : Colour(skin->digital ? 0xff1a2c42 : 0xff303234), r.getX(), r.getY(),
                                                   lit ? skin->accent.darker(0.6f) : Colour(skin->digital ? 0xff0e1a2a : 0xff161718), r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 2.5f);
            g.setColour(lit ? juce::Colours::black : skin->legend);
            g.setFont(font(9.5f, true));
            g.drawText(names[i], r, juce::Justification::centred, false);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit(0, names.size() - 1, int(float(e.x) / (float(getWidth()) / float(names.size()))));
        proc.apvts.state.setProperty(property, i, nullptr);
        repaint();
    }
private:
    TubeChannelProcessor& proc;
    const Skin* skin;
    const char* property;
    juce::StringArray names;
    int dflt;
};

// ---------------------------------------------------------------------------------------------------------------------
class TubeChannelEditor::Content : public juce::Component
{
public:
    explicit Content(TubeChannelProcessor& p) : proc(p), skin(Skin::make(p.skin() == 1))
    {
        systemPage = int(proc.apvts.state.getProperty("sysPage", 0)) == 1;
        build();
        applySkin();
        bindAll();
    }

    void toggleSkin()
    {
        proc.setSkin(skin.digital ? 0 : 1);
        applySkin();
    }

    void drawPlate(Graphics& g, Rectangle<float> r)
    {
        if (skin.digital)
        {
            g.setGradientFill(juce::ColourGradient(skin.extraTop, r.getX(), r.getY(), skin.extraBottom, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r, 5.0f);
            g.setColour(Colour(0xff000000).withAlpha(0.7f));
            g.drawRoundedRectangle(r, 5.0f, 1.5f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawRoundedRectangle(r.reduced(1.5f), 4.0f, 1.0f);
        }
        else
        {
            gla::hardware::drawBrushedPanel(g, r, Colour(0xff26272a), Colour(0xff111213), unsigned(r.getX() + r.getY()));
            g.setColour(Colour(0xff000000).withAlpha(0.8f));
            g.drawRect(r, 1.5f);
            for (auto pt : { r.getTopLeft().translated(9, 9), r.getTopRight().translated(-9, 9), r.getBottomLeft().translated(9, -9), r.getBottomRight().translated(-9, -9) })
                gla::drawScrew(g, pt.x, pt.y);
        }
    }

    void line(Graphics& g, float x0, float x1, float y, const char* title)
    {
        if (skin.digital) drawSectionLine(g, skin, x0, x1, y, title);
        else gla::hardware::drawLegendLine(g, skin.legend.withAlpha(0.85f), x0, x1, y, juce::String(title).toUpperCase());
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(skin.digital ? 0xff060d16 : 0xff0b0b0c));
        g.setColour(skin.digital ? Colour(0xff0a1420) : Colour(0xff0a0b0c));
        g.fillRect(0, 0, kW, kTop);
        g.setColour(skin.legend);
        g.setFont(font(20.0f, true));
        g.drawText("TUBE CHANNEL", 16, 4, 220, 32, juce::Justification::centredLeft);
        g.setColour(skin.legendDim);
        g.setFont(font(10.0f));
        g.drawText("OpenGrid", 200, 12, 90, 20, juce::Justification::centredLeft);

        if (systemPage)
        {
            for (auto* r : { &powerP, &osP, &unitP })
                drawPlate(g, r->toFloat());
            line(g, float(powerP.getX() + 14), float(powerP.getRight() - 14), float(powerP.getY() + 14), "POWER SUPPLY");
            line(g, float(osP.getX() + 14), float(osP.getRight() - 14), float(osP.getY() + 14), "OVERSAMPLING");
            line(g, float(unitP.getX() + 14), float(unitP.getRight() - 14), float(unitP.getY() + 14), "UNIT");
            g.setColour(skin.legend);
            g.setFont(font(11.0f, true));
            g.drawText(status, osP.getX() + 14, osP.getY() + 112, osP.getWidth() - 28, 16, juce::Justification::centredLeft, false);
            g.setColour(skin.legendDim);
            g.setFont(font(9.5f));
            g.drawText(status2, osP.getX() + 14, osP.getY() + 128, osP.getWidth() - 28, 14, juce::Justification::centredLeft, false);
            return;
        }

        if (!skin.digital)
        {
            const auto face = Rectangle<float>(float(kEar), float(kTop), float(kW - 2 * kEar), float(kH - kTop));
            gla::hardware::drawBrushedPanel(g, face, Colour(0xff2a2b2e), Colour(0xff111213));
            gla::hardware::drawRackEar(g, Rectangle<float>(0, float(kTop), float(kEar), float(kH - kTop)), true);
            gla::hardware::drawRackEar(g, Rectangle<float>(float(kW - kEar), float(kTop), float(kEar), float(kH - kTop)), false);
            for (auto pt : { juce::Point<float>(float(kEar + 10), float(kTop + 10)), juce::Point<float>(float(kW - kEar - 10), float(kTop + 10)),
                             juce::Point<float>(float(kEar + 10), float(kH - 10)), juce::Point<float>(float(kW - kEar - 10), float(kH - 10)) })
                gla::drawScrew(g, pt.x, pt.y);
            g.setColour(skin.legend.withAlpha(0.8f));
            g.setFont(font(7.0f, true));
            g.drawText("TUBE MICROPHONE PREAMPLIFIER  -  OPTICAL COMPRESSOR  -  PARAMETRIC EQUALIZER", Rectangle<float>(float(kEar), float(kTop + 1), float(kW - 2 * kEar), 9.0f), juce::Justification::centred, false);
            line(g, 52.0f, 330.0f, 137.0f, "Mic Preamp");
            line(g, 342.0f, 568.0f, 137.0f, "Compressor");
            line(g, 582.0f, 930.0f, 137.0f, "Equalizer");
            // legends that sit on the panel
            g.setColour(skin.legend.withAlpha(0.8f));
            g.setFont(font(8.0f, true));
            g.drawText("GAIN REDUCTION", 985, 174, 155, 10, juce::Justification::centred, false);
            g.drawText("PREAMP TUBE WARMTH", 985, 204, 155, 10, juce::Justification::centred, false);
            g.setFont(font(24.0f, true));
            g.drawText("TUBE CHANNEL", kEar + 20, kH - 40, 260, 28, juce::Justification::centredLeft, false);
            g.setColour(skin.legendDim);
            g.setFont(font(9.0f, true));
            g.drawText("OPENGRID", kEar + 22, kH - 17, 120, 12, juce::Justification::centredLeft, false);
            // the meter sits in a recessed black bezel
            g.setColour(Colour(0xff050505));
            g.fillRoundedRectangle(vuBounds.toFloat().expanded(5.0f), 6.0f);
            g.setColour(Colour(0x33ffffff));
            g.drawRoundedRectangle(vuBounds.toFloat().expanded(5.0f), 6.0f, 1.0f);
            return;
        }
        // digital main page
        drawPlate(g, Rectangle<float>(8.0f, float(kTop + 6), float(kW - 16), float(kH - kTop - 12)));
        g.setColour(skin.line.withAlpha(0.35f));
        for (int x : { 329, 659, 1029 })
            g.fillRect(float(x), float(kTop + 24), 1.2f, float(kH - kTop - 44));
        line(g, 22.0f, 322.0f, 58.0f, "INPUT");
        line(g, 336.0f, 652.0f, 58.0f, "COMPRESSOR");
        line(g, 666.0f, 1022.0f, 58.0f, "EQUALIZER");
        line(g, 1036.0f, 1178.0f, 58.0f, "OUTPUT");
        g.setColour(skin.legendDim);
        g.setFont(font(8.5f, true));
        g.drawText("TUBE WARMTH", 142, 184, 180, 12, juce::Justification::centredLeft, false);
        g.drawText("GAIN REDUCTION", 488, 184, 164, 12, juce::Justification::centredLeft, false);
    }

    void setStatus(const juce::String& a, const juce::String& b)
    {
        if (a != status || b != status2) { status = a; status2 = b; repaint(osP); }
    }
    void syncChannelTabs()
    {
        const bool dual = proc.apvts.getRawParameterValue(id::dualMono)->load() > 0.5f && proc.activeChannels() == 2;
        tabA.setVisible(dual);
        tabB.setVisible(dual);
        if (!dual && channelB) { channelB = false; bindAll(); }
        tabA.setLit(!channelB);
        tabB.setLit(channelB);
    }

    TubeChannelProcessor& proc;
    Skin skin;
    bool channelB = false;

private:
    struct Bound { std::function<void(bool)> rebind; };
    static constexpr int kTop = 40, kEar = 40;

    template <class W> W* add(std::unique_ptr<W> w)
    {
        auto* raw = w.get();
        addAndMakeVisible(*raw);
        owned.push_back(std::move(w));
        page->push_back(raw);
        return raw;
    }

    Knob* knob(const char* base, const char* title, const char* legends, int cx, int top, int d, bool value = false, bool perChannel = true)
    {
        auto* k = add(std::make_unique<Knob>(&skin, title, legends, d, value));
        const int w = Knob::widthFor(d);
        k->setBounds(cx - w / 2, top, w, Knob::heightFor(d, value));
        bindings.push_back({ [k, base, perChannel, this](bool b) { k->bind(proc.apvts, perChannel ? tc::params::idFor(base, b) : juce::String(base)); } });
        return k;
    }
    HSlider* slider(const char* base, const char* title, int x, int y, int w, bool perChannel = true, int titleW = 74)
    {
        auto* s = add(std::make_unique<HSlider>(&skin, title, titleW, 52));
        s->setBounds(x, y, w, 24);
        bindings.push_back({ [s, base, perChannel, this](bool b) { s->bind(proc.apvts, perChannel ? tc::params::idFor(base, b) : juce::String(base)); } });
        return s;
    }
    LedButton* toggle(const char* base, const char* on, const char* off, int cx, int top, int w = 52, bool perChannel = true, Colour led = Colour())
    {
        auto* t = add(std::make_unique<LedButton>(&skin, on, off, led));
        t->setBounds(cx - w / 2, top, w, LedButton::kHeight);
        bindings.push_back({ [t, base, perChannel, this](bool b) { t->bind(proc.apvts, perChannel ? tc::params::idFor(base, b) : juce::String(base)); } });
        return t;
    }
    void build()
    {
        powerP = { 8, 46, 612, 236 }; osP = { 628, 46, 290, 236 }; unitP = { 926, 46, 266, 236 };

        // ---- toolbar (always visible) ----
        presetBtn = add(std::make_unique<PushButton>(&skin, "Init"));
        presetBtn->setBounds(300, 7, 220, 26);
        presetBtn->onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < proc.getNumPrograms(); ++i)
                m.addItem(i + 1, proc.getProgramName(i), true, i == proc.getCurrentProgram());
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(presetBtn), [this](int r)
            {
                if (r > 0) { proc.setCurrentProgram(r - 1); presetBtn->setText(proc.getProgramName(r - 1)); }
            });
        };
        abBtn = add(std::make_unique<PushButton>(&skin, "A"));
        abBtn->setBounds(530, 7, 40, 26);
        abBtn->onClick = [this] { proc.toggleAB(); abBtn->setText(proc.isB() ? "B" : "A"); abBtn->setLit(proc.isB()); };
        copyBtn = add(std::make_unique<PushButton>(&skin, "COPY"));
        copyBtn->setBounds(574, 7, 52, 26);
        copyBtn->onClick = [this] { proc.copyToOther(); };
        undoBtn = add(std::make_unique<PushButton>(&skin, "UNDO"));
        undoBtn->setBounds(640, 7, 52, 26);
        undoBtn->onClick = [this] { proc.undo.undo(); };
        redoBtn = add(std::make_unique<PushButton>(&skin, "REDO"));
        redoBtn->setBounds(696, 7, 52, 26);
        redoBtn->onClick = [this] { proc.undo.redo(); };
        addAndMakeVisible(tabA);
        addAndMakeVisible(tabB);
        tabA.setBounds(770, 7, 52, 26);
        tabB.setBounds(826, 7, 52, 26);
        tabA.onClick = [this] { channelB = false; bindAll(); syncChannelTabs(); };
        tabB.onClick = [this] { channelB = true; bindAll(); syncChannelTabs(); };
        tabA.setVisible(false);
        tabB.setVisible(false);
        sysBtn = add(std::make_unique<PushButton>(&skin, "SYSTEM"));
        sysBtn->setBounds(950, 7, 100, 26);
        sysBtn->onClick = [this] { systemPage = !systemPage; showPage(); };
        skinBtn = add(std::make_unique<PushButton>(&skin, "ANALOGUE"));
        skinBtn->setBounds(1070, 7, 116, 26);
        skinBtn->onClick = [this] { toggleSkin(); };

        // ================= analogue front panel =================
        page = &aMain;
        const int r1 = 46, r2 = 144, keys = 228;
        knob(id::gain, "GAIN", "0|10|20|30|40", 110, r1, 46);
        knob(id::lowcut, "LOW CUT", "10||250", 195, r1 + 4, 38);
        knob(id::preOut, "PREAMP", "MUTE||0", 265, r1 + 4, 38);
        knob(id::impedance, "IMPEDANCE", "150||3.4k", 110, r2, 38);
        knob(id::threshold, "THRESHOLD", "-20|0|+20", 350, r1 + 4, 38);
        {
            auto* rk = knob(id::ratio, "RATIO", "2:1||7:1", 420, r1 + 4, 38);
            bindings.push_back({ [rk, this](bool b) { rk->bindClick(proc.apvts, tc::params::idFor(id::ratioLimit, b), "LIMITER"); } });
        }
        knob(id::compOut, "OUTPUT", "MUTE||+5", 490, r1 + 4, 38);
        knob(id::attack, "ATTACK", "4ms||33ms", 395, r2, 38);
        knob(id::release, "RELEASE", ".1s||1.1s", 465, r2, 38);
        knob(id::low, "LOW", "-12|0|+12", 620, r1 + 4, 38);
        knob(id::lmLevel, "LOW-MID", "-12|0|+12", 690, r1 + 4, 38);
        knob(id::hmLevel, "HIGH-MID", "-12|0|+12", 760, r1 + 4, 38);
        knob(id::high, "HIGH", "-12|0|+12", 830, r1 + 4, 38);
        knob(id::lmFreq, "LM FREQ", "", 690, r2, 38);
        knob(id::hmFreq, "HM FREQ", "", 760, r2, 38);
        knob(id::master, "MASTER", "MIN||MAX", 915, r1, 46);
        const int k2 = r2 + 30;
        toggle(id::instrument, "INST", "MIC", 170, k2);
        toggle(id::gain20, "+20 dB", "", 222, k2);
        toggle(id::phantom, "+48 V", "", 274, k2, 52, true, Colour(0xffe5382a));
        toggle(id::invert, "PHASE", "", 326, k2);
        toggle(id::plateHigh, "PLATE HI", "NORM", 336, keys, 56);
        toggle(id::comp, "ACTIVE", "BYPASS", 400, keys, 56);
        toggle(id::autoDetect, "AUTO", "MANUAL", 460, keys, 56);
        toggle(id::extSc, "EXT SC", "INT SC", 520, keys, 56, true, Colour(0xff5ac8ff));
        toggle(id::loShift, "40Hz", "120Hz", 600, keys, 46);
        toggle(id::lmX10, "X10", "", 648, keys, 46);
        toggle(id::lmNarrow, "NARROW", "WIDE", 696, keys, 46);
        toggle(id::hmX10, "X10", "", 752, keys, 46);
        toggle(id::hmNarrow, "NARROW", "WIDE", 800, keys, 46);
        toggle(id::hiShift, "6kHz", "18kHz", 848, keys, 46);
        toggle(id::eq, "EQ IN", "EQ OUT", 896, keys, 46);
        clip = add(std::make_unique<ClipLamp>(proc, "CLIP"));
        clip->setBounds(525, r1 + 14, 46, 52);
        vuBounds = { 985, 54, 155, 100 };
        vu = add(std::make_unique<VuView>(proc, &skin));
        vu->setBounds(vuBounds);
        grA = add(std::make_unique<LadderView>(proc, &skin, LadderView::Kind::GainReduction));
        grA->setBounds(985, 160, 155, 12);
        warmA = add(std::make_unique<LadderView>(proc, &skin, LadderView::Kind::Warmth));
        warmA->setBounds(985, 190, 155, 12);
        vuSrcA = add(std::make_unique<StateKeys>(proc, &skin, "vuSrc", juce::StringArray { "PRE", "COMP", "EQ", "OUT" }, 3));
        vuSrcA->setBounds(985, 218, 155, 20);
        toggle(id::outLevel, "+4 dBu", "-10 dBV", 1100, 244, 56);

        // ================= digital front page =================
        // One grid: four columns, hero area y 66..216, slider rows 36 apart, ladder row at 198, key row at 232.
        page = &dMain;
        {
            const int kRow = 232;
            auto keyRow = [&](int x0, int spacing, int i) { return x0 + 25 + i * spacing; };
            // input: Gain is the only knob
            knob(id::gain, "GAIN", "", 80, 62, 56, true);
            slider(id::impedance, "IMPEDANCE", 142, 74, 180, true, 66);
            slider(id::lowcut, "LOW CUT", 142, 110, 180, true, 66);
            slider(id::preOut, "PREAMP OUT", 142, 146, 180, true, 66);
            warmD = add(std::make_unique<LadderView>(proc, &skin, LadderView::Kind::Warmth));
            warmD->setBounds(142, 198, 180, 10);
            toggle(id::instrument, "INST", "MIC", keyRow(22, 60, 0), kRow, 50);
            toggle(id::gain20, "+20 dB", "", keyRow(22, 60, 1), kRow, 50);
            toggle(id::phantom, "+48 V", "", keyRow(22, 60, 2), kRow, 50, true, Colour(0xffe5382a));
            toggle(id::invert, "PHASE", "", keyRow(22, 60, 3), kRow, 50);
            toggle(id::plateHigh, "PLATE HI", "NORM", keyRow(22, 60, 4), kRow, 50);
            // compressor: transfer curve plus the rest
            comp = add(std::make_unique<CompDisplay>(proc, &skin, [this] { return channelB; }));
            comp->setBounds(336, 66, 140, 150);
            slider(id::attack, "ATTACK", 488, 74, 164, true, 56);
            slider(id::release, "RELEASE", 488, 110, 164, true, 56);
            slider(id::compOut, "OUTPUT", 488, 146, 164, true, 56);
            grD = add(std::make_unique<LadderView>(proc, &skin, LadderView::Kind::GainReduction));
            grD->setBounds(488, 198, 164, 10);
            const int cx0 = 336 + (316 - 4 * 60 + 10) / 2 - 5;
            toggle(id::comp, "ACTIVE", "BYPASS", keyRow(cx0, 60, 0), kRow, 50);
            toggle(id::autoDetect, "AUTO", "MANUAL", keyRow(cx0, 60, 1), kRow, 50);
            toggle(id::extSc, "EXT SC", "INT SC", keyRow(cx0, 60, 2), kRow, 50, true, Colour(0xff39d0ff));
            toggle(id::ratioLimit, "LIMIT", "", keyRow(cx0, 60, 3), kRow, 50, true, Colour(0xffffc04a));
            // equalizer
            eqDisp = add(std::make_unique<EqDisplay>(proc, &skin, [this] { return channelB; }));
            eqDisp->setBounds(666, 66, 356, 150);
            toggle(id::loShift, "40Hz", "120Hz", keyRow(666, 51, 0) - 2, kRow, 50);
            toggle(id::lmX10, "LM X10", "", keyRow(666, 51, 1) - 2, kRow, 50);
            toggle(id::lmNarrow, "NARROW", "WIDE", keyRow(666, 51, 2) - 2, kRow, 50);
            toggle(id::hmX10, "HM X10", "", keyRow(666, 51, 3) - 2, kRow, 50);
            toggle(id::hmNarrow, "NARROW", "WIDE", keyRow(666, 51, 4) - 2, kRow, 50);
            toggle(id::hiShift, "6kHz", "18kHz", keyRow(666, 51, 5) - 2, kRow, 50);
            toggle(id::eq, "EQ IN", "EQ OUT", keyRow(666, 51, 6) - 2, kRow, 50);
            // output
            knob(id::master, "MASTER", "", 1107, 62, 44, true);
            vuD = add(std::make_unique<VuView>(proc, &skin));
            vuD->setBounds(1036, 168, 142, 40);
            vuSrcD = add(std::make_unique<StateKeys>(proc, &skin, "vuSrc", juce::StringArray { "PRE", "COMP", "EQ", "OUT" }, 3));
            vuSrcD->setBounds(1036, 212, 142, 18);
            toggle(id::outLevel, "+4 dBu", "-10 dBV", 1036 + 40, kRow, 56);
            clipD = add(std::make_unique<ClipLamp>(proc, "CLIP"));
            clipD->setBounds(1036 + 100, kRow, 42, 36);
        }

        // ================= system page: shared =================
        page = &sysCommon;
        power = add(std::make_unique<PowerPanel>(proc, &skin));
        power->setBounds(powerP.getX() + 8, powerP.getY() + 26, powerP.getWidth() - 16, powerP.getHeight() - 32);
        quality = add(std::make_unique<KeyBank>(&skin, juce::StringArray { "Default", "HQ", "Derp mode" }, 2, std::vector<float> { 1.0f, 1.0f, 1.9f }));
        quality->setBounds(osP.getX() + 12, osP.getY() + 28, 266, 44);
        toggle(id::linearPhase, "+L LINEAR", "+L MIN PH", osP.getX() + 12 + 50, osP.getY() + 76, 100, false);
        toggle(id::stereoLink, "LINK", "UNLINK", osP.getX() + 145, osP.getY() + 76, 76, false);
        toggle(id::dualMono, "DUAL", "MONO", osP.getX() + 232, osP.getY() + 76, 76, false);
        mains = add(std::make_unique<KeyBank>(&skin, juce::StringArray { "60 Hz", "50 Hz" }));
        mains->setBounds(unitP.getX() + 12, unitP.getY() + 28, 100, 30);
        toggle(id::tolerances, "TOL", "IDEAL", unitP.getX() + 134, unitP.getY() + 26, 48, false);
        toggle(id::noise, "NOISE", "SILENT", unitP.getX() + 186, unitP.getY() + 26, 48, false);
        toggle(id::psuModel, "PSU SIM", "FIXED", unitP.getX() + 238, unitP.getY() + 26, 48, false);

        // ---- system page, analogue: knobs ----
        page = &aSys;
        knob(id::inputTrim, "IN TRIM", "", osP.getX() + 50, osP.getY() + 142, 34, true, false);
        knob(id::alignment, "0 VU", "", osP.getX() + 145, osP.getY() + 142, 34, true, false);
        knob(id::outputTrim, "OUT TRIM", "", osP.getX() + 240, osP.getY() + 142, 34, true, false);
        {
            const int kx = unitP.getX() + 12 + 33, ky = unitP.getY() + 98;
            knob(id::mainsVolts, "MAINS V", "", kx, ky, 34, true, false);
            knob(id::seed, "SEED", "", kx + 63, ky, 34, true, false);
            knob(id::micZ, "MIC Z", "", kx + 126, ky, 34, true, false);
            knob(id::instZ, "INST Z", "", kx + 189, ky, 34, true, false);
        }

        // ---- system page, digital: sliders ----
        page = &dSys;
        slider(id::inputTrim, "IN TRIM", osP.getX() + 12, osP.getY() + 150, 266, false);
        slider(id::alignment, "0 VU", osP.getX() + 12, osP.getY() + 178, 266, false);
        slider(id::outputTrim, "OUT TRIM", osP.getX() + 12, osP.getY() + 206, 266, false);
        slider(id::mainsVolts, "MAINS V", unitP.getX() + 12, unitP.getY() + 100, 242, false);
        slider(id::seed, "SEED", unitP.getX() + 12, unitP.getY() + 134, 242, false);
        slider(id::micZ, "MIC Z", unitP.getX() + 12, unitP.getY() + 168, 242, false);
        slider(id::instZ, "INST Z", unitP.getX() + 12, unitP.getY() + 202, 242, false);
        page = &alwaysPage;
    }

    void showPage()
    {
        const bool d = skin.digital, s = systemPage;
        auto vis = [](std::vector<juce::Component*>& v, bool on) { for (auto* c : v) c->setVisible(on); };
        vis(aMain, !d && !s);
        vis(dMain, d && !s);
        vis(aSys, !d && s);
        vis(dSys, d && s);
        vis(sysCommon, s);
        sysBtn->setLit(s);
        proc.apvts.state.setProperty("sysPage", s ? 1 : 0, nullptr);
        repaint();
    }

    void applySkin()
    {
        skin = Skin::make(skin.digital);
        gla::retro::glowEnabled = false;
        skinBtn->setText(skin.digital ? "DIGITAL" : "ANALOGUE");
        skinBtn->setLit(skin.digital);
        showPage();
        for (auto& c : owned) c->repaint();
    }

    void bindAll()
    {
        for (auto& b : bindings) b.rebind(channelB);
        quality->bind(proc.apvts, id::quality);
        mains->bind(proc.apvts, id::mains);
    }

    std::vector<juce::Component*> alwaysPage, aMain, dMain, aSys, dSys, sysCommon;
    std::vector<juce::Component*>* page = &alwaysPage;
    bool systemPage = false;
    PushButton* sysBtn = nullptr;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Bound> bindings;
    PushButton *presetBtn = nullptr, *abBtn = nullptr, *copyBtn = nullptr, *undoBtn = nullptr, *redoBtn = nullptr, *skinBtn = nullptr;
    PushButton tabA { &skin, "CH A" }, tabB { &skin, "CH B" };
    LadderView *warmA = nullptr, *grA = nullptr, *warmD = nullptr, *grD = nullptr;
    VuView *vu = nullptr, *vuD = nullptr;
    StateKeys *vuSrcA = nullptr, *vuSrcD = nullptr;
    ClipLamp *clip = nullptr, *clipD = nullptr;
    PowerPanel* power = nullptr;
    CompDisplay* comp = nullptr;
    EqDisplay* eqDisp = nullptr;
    KeyBank *quality = nullptr, *mains = nullptr;
    juce::String status, status2;
    Rectangle<int> powerP, osP, unitP, vuBounds;
};

// ---------------------------------------------------------------------------------------------------------------------
TubeChannelEditor::TubeChannelEditor(TubeChannelProcessor& p) : AudioProcessorEditor(&p), proc(p)
{
    content = std::make_unique<Content>(p);
    addAndMakeVisible(*content);
    constrainer.setFixedAspectRatio(double(kW) / double(kH));
    constrainer.setSizeLimits(kW / 2, kH / 2, kW * 2, kH * 2);
    setConstrainer(&constrainer);
    setResizable(true, true);
    setSize(kW, kH);
    startTimerHz(8);
}

TubeChannelEditor::~TubeChannelEditor() { stopTimer(); }

void TubeChannelEditor::resized()
{
    const float s = float(getWidth()) / float(kW);
    content->setBounds(0, 0, kW, kH);
    content->setTransform(juce::AffineTransform::scale(s));
}

void TubeChannelEditor::timerCallback()
{
    const int f = proc.currentFactor();
    const double fs = proc.getSampleRate();
    const bool lin = proc.apvts.getRawParameterValue(id::linearPhase)->load() > 0.5f;
    juce::String a = juce::String(f) + "x oversampling, " + juce::String(f * fs / 1000.0, 1) + " kHz";
    if (proc.isBuilding())
        a = "building the circuit model...";
    content->setStatus(a, (lin ? "linear phase, " : "minimum phase, ") + juce::String(proc.currentLatency()) + " samples latency, CPU " +
                              juce::String(juce::roundToInt(proc.cpuLoad() * 100.0f)) + " %");
    content->syncChannelTabs();
}
