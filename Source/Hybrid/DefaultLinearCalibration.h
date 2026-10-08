#pragma once
#include "Equalizer.h"
#include "Output.h"

namespace tubechannel::hybrid
{
// Measured at 96 kHz, default EQ/master, tolerances off. Other states/rates require new profiles.
struct DefaultLinearCalibration
{
    static constexpr double sampleRate = 96000.0;
    static Equalizer::Profile equalizer()
    {
        Equalizer::Profile p;
        p.coupling.count = 1;
        p.coupling.gain = 0.96675675509190762;
        p.coupling.sections[0] = { 1, -0.85384758521331428, 0, -0.85381748546181568, 0 };
        p.bands[0].count = 1;
        p.bands[0].gain = -0.99117599558253655;
        p.bands[0].sections[0] = { 1, 0.81818178948831555, 0, 0.80037410697926714, 0 };
        p.bands[1].count = 3;
        p.bands[1].gain = -0.98999630691322271;
        p.bands[1].sections[0] = { 1, 1.3200940764401858, 0.41065549176528349, 1.3022851265670576, 0.40171696108165017 };
        p.bands[1].sections[1] = { 1, -1.9849842187495619, 0.98498426471232725, -1.9848062843685141, 0.98480687275527767 };
        p.bands[1].sections[2] = { 1, -1.3411577860404513, 0.35538761446841094, -1.3413367909364178, 0.35555024978256422 };
        p.bands[2].count = 3;
        p.bands[2].gain = -0.91401498579357165;
        p.bands[2].sections[0] = { 1, -0.80544383446569912, -0.19455615645467128, -0.19001531814893957, -0.80970935452358306 };
        p.bands[2].sections[1] = { 1, -1.9258301384201868, 0.92754076511754724, -1.92604644344437, 0.92775205574577568 };
        p.bands[2].sections[2] = { 1, 1.6360102597788986, 0.66913241481322372, 0.86882433381401802, 0.13118302571446125 };
        p.bands[3].count = 1;
        p.bands[3].gain = -0.91629908779729441;
        p.bands[3].sections[0] = { 1, -0.17418673669994122, -0.82584376395488757, -0.32617995443750492, -0.67359616230114217 };
        return p;
    }
    static Output::Profile output()
    {
        Output::Profile p;
        p.before.count = 1;
        p.before.gain = 0.57221183614239401;
        p.before.sections[0] = { 1, -0.18187108928258811, -0.81821400935027577, -0.18957719115303429, -0.81004102065307737 };
        return p;
    }
};
} // namespace tubechannel::hybrid
