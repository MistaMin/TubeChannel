#pragma once

#include <algorithm>
#include <cmath>

namespace tc::ui
{
// Display-only peak response: immediate attack, 24 dB/s release, one-second peak hold.
// elapsed is wall time, so the display responds consistently at different timer rates.
struct MeterBallistics
{
    static constexpr float floorDb = -60.f;
    float db = floorDb, heldDb = floorDb;
    double holdRemaining = 0.0;

    void update(float amplitude, double elapsed)
    {
        const float target = std::isfinite(amplitude) && amplitude > 1e-3f
                                 ? 20.f * std::log10(amplitude) : floorDb;
        const float fall = float(24.0 * std::max(0.0, elapsed));
        db = std::max(target, db - fall);
        if (target >= heldDb)
        {
            heldDb = target;
            holdRemaining = 1.0;
        }
        else
        {
            const double releaseTime = std::max(0.0, elapsed - holdRemaining);
            holdRemaining = std::max(0.0, holdRemaining - elapsed);
            heldDb = std::max(db, heldDb - float(24.0 * releaseTime));
        }
    }
};
} // namespace tc::ui
