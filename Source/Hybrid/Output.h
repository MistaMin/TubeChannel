#pragma once

#include "FilterBank.h"
#include <algorithm>

namespace tubechannel::hybrid
{
// Measured asymmetrical transfer table bracketed by fitted linear responses.
// The table uses volts, permits asymmetry, and saturates only beyond its measured range.
// It is a coloration prototype; it cannot describe every dynamic overload mechanism.
class Output
{
public:
    static constexpr std::size_t kPoints = 65;
    struct Profile
    {
        FilterBank<4>::Profile before, after;
        std::array<double, kPoints> volts {}, result {};
        bool useTransfer = false;
        bool valid() const
        {
            if (!before.valid() || !after.valid()) return false;
            if (!useTransfer) return true;
            for (std::size_t i = 0; i < kPoints; ++i)
                if (!std::isfinite(volts[i]) || !std::isfinite(result[i])
                    || (i > 0 && volts[i] <= volts[i - 1])) return false;
            return true;
        }
    };
    bool configure(const Profile& p)
    {
        if (!p.valid()) return false;
        profile = p;
        before.configure(p.before); after.configure(p.after);
        return true;
    }
    void reset() { before.reset(); after.reset(); }
    double process(double x)
    {
        x = before.process(x);
        if (profile.useTransfer)
        {
            if (x <= profile.volts.front()) x = profile.result.front();
            else if (x >= profile.volts.back()) x = profile.result.back();
            else
            {
                const auto hi = std::upper_bound(profile.volts.begin(), profile.volts.end(), x);
                const auto i = std::size_t(hi - profile.volts.begin() - 1);
                const double t = (x - profile.volts[i]) / (profile.volts[i + 1] - profile.volts[i]);
                x = profile.result[i] + t * (profile.result[i + 1] - profile.result[i]);
            }
        }
        return after.process(x);
    }

private:
    Profile profile;
    FilterBank<4> before, after;
};
} // namespace tubechannel::hybrid
