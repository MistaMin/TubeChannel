#pragma once

#include "Detector.h"
#include "Supply.h"

namespace tubechannel::hybrid
{
// Resolved per-stage selections and profiles for ChannelModel. Chain chooses the
// detector from its quality mode; supply remains an optional comparison backend.
struct EngineConfig
{
    bool compactDetector = false;
    bool compactTubes = false; // offline experiment; never selected by normal quality modes
    int tubeExtrapolation = 2;
    bool guardedTubes = false; // offline convergence investigation
    bool compactSupply = false;
    Detector::Calibration detector;
    Supply::Calibration supply;
};
} // namespace tubechannel::hybrid
