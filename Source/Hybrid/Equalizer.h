#pragma once

#include "FilterBank.h"

namespace tubechannel::hybrid
{
// Four measured bands plus the common input/bypass coupling response. No generic
// knob-to-EQ approximation is installed: callers supply coefficients for a measured state.
class Equalizer
{
public:
    using Band = FilterBank<4>;
    struct Profile
    {
        std::array<Band::Profile, 4> bands {};
        Band::Profile coupling;
        bool valid() const
        {
            if (!coupling.valid()) return false;
            for (const auto& band : bands) if (!band.valid()) return false;
            return true;
        }
    };
    struct Taps { double wet, dry; };

    bool configure(const Profile& p)
    {
        if (!p.valid()) return false;
        coupling.configure(p.coupling);
        for (std::size_t i = 0; i < bands.size(); ++i) bands[i].configure(p.bands[i]);
        return true;
    }
    void reset()
    {
        coupling.reset();
        for (auto& band : bands) band.reset();
    }
    Taps process(double x)
    {
        const double dry = coupling.process(x);
        double wet = dry;
        // Always update the wet path, even when the caller selects the dry tap.
        for (auto& band : bands) wet = band.process(wet);
        return { wet, dry };
    }

private:
    Band coupling;
    std::array<Band, 4> bands;
};
} // namespace tubechannel::hybrid
