#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace tubechannel::hybrid
{
// Normalized second-order sections supplied by measurement/fitting tools. Coefficient
// changes retain state; topology changes require reset outside playback.
struct Section
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    bool valid() const
    {
        return std::isfinite(b0) && std::isfinite(b1) && std::isfinite(b2)
            && std::isfinite(a1) && std::isfinite(a2)
            && std::fabs(a2) < 1.0 && 1.0 + a1 + a2 > 0.0 && 1.0 - a1 + a2 > 0.0;
    }
};

template <std::size_t Capacity>
class FilterBank
{
public:
    struct Profile
    {
        std::array<Section, Capacity> sections {};
        std::size_t count = 0;
        double gain = 1.0;
        bool valid() const
        {
            if (count > Capacity || !std::isfinite(gain)) return false;
            for (std::size_t i = 0; i < count; ++i)
                if (!sections[i].valid()) return false;
            return true;
        }
    };

    bool configure(const Profile& p)
    {
        if (!p.valid()) return false;
        profile = p;
        return true;
    }
    void reset() { z1.fill(0.0); z2.fill(0.0); }
    double process(double x)
    {
        for (std::size_t i = 0; i < profile.count; ++i)
        {
            const auto& c = profile.sections[i];
            const double y = c.b0 * x + z1[i];
            z1[i] = c.b1 * x - c.a1 * y + z2[i];
            z2[i] = c.b2 * x - c.a2 * y;
            x = y;
        }
        return profile.gain * x;
    }

private:
    Profile profile;
    std::array<double, Capacity> z1 {}, z2 {};
};
} // namespace tubechannel::hybrid
