// SPDX-License-Identifier: MIT
#pragma once

// C ABI around tc::Chain (Source/Plugin/Chain.h; no JUCE) for the Rust plugin (src/engine.rs).
// Every function is noexcept. tc_chain_create allocates and takes up to a few hundred milliseconds
// (the circuit solves are compiled there): never call it on the audio thread. tc_chain_process
// neither allocates nor locks.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct TcChain TcChain;
typedef struct TcTelemetry TcTelemetry;

// What a chain is built for: a change of any field needs a new chain.
typedef struct TcChainConfig
{
    double sampleRate;
    int maxBlock, channels;
    int quality;              // tubechannel::dsp::Quality: 0 Circuit, 1 HQ, 2 legacy Derp, 3 Eco
    int oversamplingStages;   // -1 legacy adaptive, 0..3 for 1x..8x
    int linearPhase, tolerances, seed;
    double mainsHz, mainsScale;
} TcChainConfig;

TcChain* tc_chain_create(const TcChainConfig*);   // null on failure
void tc_chain_destroy(TcChain*);
int tc_chain_latency(const TcChain*);              // samples at the base rate
int tc_chain_factor(const TcChain*);
int tc_chain_channels(const TcChain*);
int tc_chain_compact_detector(const TcChain*);     // the Eco / prototype compressor is in use
// The largest latency of the linear-phase oversampler at 2x, 4x and 8x (the constant offline budget).
int tc_linear_reference_latency(double sampleRate);
// The latency a chain built for these settings has (its oversampler's), without building it.
int tc_oversampler_latency(double sampleRate, int quality, int stages, int linear);
// The largest latency any quality, oversampling and phase setting can have (for the shells' bypass delay).
int tc_max_latency(double sampleRate);

// values: the plugin's whole parameter table in plain units, tc_param_count() entries in table order.
// io: the chain's channels, in place. sc: nsc (0..2) sidechain buffers, or null.
// tel: two telemetry blocks (channel A and B), written for the editor.
void tc_chain_process(TcChain*, float* const* io, int frames, const float* const* sc, int nsc, const float* values,
                      TcTelemetry* telA, TcTelemetry* telB);
int tc_param_count(void);

TcTelemetry* tc_telemetry_create(void);
void tc_telemetry_destroy(TcTelemetry*);

enum { kTcScope = 256 };
// A copy of one channel's telemetry for the editor (any thread).
typedef struct TcMeters
{
    float outPeak, grDb, tubeDrive, clip, clipPre, inPeak, outRms;
    float p15, n15, p25, p45, p75, p150, tubeP;
    float mainsPhase, heaterCurrent, loadP15, loadTube;
    float vuTap[4];                     // rms volts: preamp out, compressor out, EQ out, main out
    float scope25[kTcScope], scope150[kTcScope], scopeLoad[kTcScope];
    int scopeWrite;
} TcMeters;
void tc_telemetry_read(const TcTelemetry*, TcMeters* out);

#ifdef __cplusplus
}
#endif
