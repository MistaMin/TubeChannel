#pragma once

// The processing chain for one oversampling configuration: an oversampler per audio channel, a
// circuit model per channel and the scaling between digital samples and circuit volts.
//
//   0 VU = +4 dBu is aligned to a settable digital level (default -18 dBFS). Digital full scale is then
//   mapped 11 dB lower into the circuit (kInterfacePadDb), so a full-scale sine is +11 dBu at the XLR input
//   rather than +22 dBu: the preamp and tube stage keep their headroom at normal levels, and the output
//   is scaled back by the same amount so the default controls are unity gain. Input and output trims sit
//   outside the circuit.

#include <cmath>
#include <memory>
#include <vector>

#include "../Channel/ChannelModel.h"
#include "../DSP/Oversampler.h"

namespace tc
{
struct ChainConfig
{
    double sampleRate = 48000.0;
    int maxBlock = 512;
    int channels = 2;
    tubechannel::dsp::Quality quality = tubechannel::dsp::Quality::Default;
    bool linearPhase = false;
    bool tolerances = true;
    int seed = 1;
    bool lockstep = true;                // stereo: both channels share SIMD lanes in the circuit solves
    tubechannel::channel::PowerSupplySettings psu;
};

struct ChainGlobals
{
    double inputTrimDb = 0.0, outputTrimDb = 0.0;
    double alignmentDbfs = -18.0;        // digital level of 0 VU (+4 dBu)
    bool stereoLink = true;
    bool dualMono = false;
    bool noise = true;
    bool psuModel = true;                // false: rails stay at their settled values (much cheaper)
    double mainsHz = 60.0, mainsScale = 1.0;
};

class Chain
{
public:
    static constexpr double kInterfacePadDb = -11.0;

    void prepare(const ChainConfig& c, const tubechannel::channel::NetlistSet& nets)
    {
        using namespace tubechannel;
        cfg = c;
        nch = std::max(1, std::min(2, c.channels));
        for (int ch = 0; ch < nch; ++ch)
        {
            os[ch].prepare(c.sampleRate, c.maxBlock, 1, c.quality, c.linearPhase);
            osSc[ch].prepare(c.sampleRate, c.maxBlock, 1, c.quality, c.linearPhase);
            model[ch] = std::make_unique<channel::ChannelModel>();
            model[ch]->setControls({});
            model[ch]->prepare(c.sampleRate * os[ch].factor(), nets, unsigned(c.seed * 2 + ch + 1), c.tolerances, c.psu);
        }
        if (nch == 2)
            tubechannel::channel::ChannelModel::linkPair(*model[0], *model[1]);
        const int cap = c.maxBlock * os[0].factor();
        for (int ch = 0; ch < 2; ++ch)
        {
            scaled[ch].assign(size_t(c.maxBlock), 0.f);
            scaledSc[ch].assign(size_t(c.maxBlock), 0.f);
            osIn[ch].assign(size_t(cap), 0.0);
            osSc2[ch].assign(size_t(cap), 0.0);
            osOut[ch].assign(size_t(cap), 0.0);
        }
        rmsCoeff = 1.0 - std::exp(-1.0 / (0.3 * c.sampleRate));
        ms[0] = ms[1] = 0.0;
    }

    int factor() const { return os[0].factor(); }
    int latency() const { return os[0].latencySamples(); }
    int channels() const { return nch; }
    const ChainConfig& config() const { return cfg; }

    // audio: in place. sc: optional sidechain channels (may be null / fewer than nch).
    void process(float* const* audio, int n, const float* const* sc, int nsc, const tubechannel::channel::ChannelControls& ctrlA,
                 const tubechannel::channel::ChannelControls& ctrlB, const ChainGlobals& g,
                 tubechannel::channel::Telemetry* const* tel)
    {
        using tubechannel::channel::ChannelControls;
        const double fsPeak = 0.775 * 1.4142135623730951 * std::pow(10.0, (4.0 - g.alignmentDbfs + kInterfacePadDb) / 20.0);
        const float inGain = float(std::pow(10.0, g.inputTrimDb / 20.0) * fsPeak);
        const bool linked = nch == 2 && !g.dualMono && g.stereoLink;
        ChannelControls ca = ctrlA, cb = (nch == 2 && (g.dualMono || !g.stereoLink)) ? ctrlB : ctrlA;
        if (nch == 2 && !g.dualMono && !g.stereoLink)
            cb = ctrlA;                                  // stereo without link still shares one set of controls
        const double outScale = std::pow(10.0, g.outputTrimDb / 20.0) / fsPeak;
        const float outA = float(outScale * (ca.outPlus4 ? 1.0 : 0.2570)), outB = float(outScale * (cb.outPlus4 ? 1.0 : 0.2570));
        const float outGain[2] = { outA, outB };

        for (int ch = 0; ch < nch; ++ch)
        {
            auto& m = *model[ch];
            m.setControls(ch == 0 ? ca : cb);
            m.setNoiseEnabled(g.noise);
            m.setPsuEnabled(g.psuModel);
            m.setMains(g.mainsHz, g.mainsScale);
            m.attach(tel[ch]);
        }

        for (int start = 0; start < n; start += cfg.maxBlock)
        {
            const int m = std::min(cfg.maxBlock, n - start);
            const int N = os[0].factor();
            for (int ch = 0; ch < nch; ++ch)
            {
                const float* in = audio[ch] + start;
                float peak = 0.f;
                for (int i = 0; i < m; ++i)
                {
                    scaled[ch][size_t(i)] = in[i] * inGain;
                    peak = std::max(peak, std::fabs(in[i]));
                }
                tel[ch]->inPeak.store(std::max(tel[ch]->inPeak.load() * 0.9f, peak));
                os[ch].upsample(0, scaled[ch].data(), m, osIn[ch].data());
                const float* scSrc = nullptr;
                if (sc != nullptr && nsc > 0)
                    scSrc = sc[std::min(ch, nsc - 1)];
                if (scSrc != nullptr)
                {
                    for (int i = 0; i < m; ++i)
                        scaledSc[ch][size_t(i)] = scSrc[start + i] * inGain;
                    osSc[ch].upsample(0, scaledSc[ch].data(), m, osSc2[ch].data());
                }
                else
                    std::fill(osSc2[ch].begin(), osSc2[ch].begin() + m * N, 0.0);
            }
            const int total = m * N;
            if (nch == 1)
            {
                auto& A = *model[0];
                for (int k = 0; k < total; ++k)
                    osOut[0][size_t(k)] = A.process(osIn[0][size_t(k)], osSc2[0][size_t(k)]);
            }
            else
            {
                auto& A = *model[0];
                auto& B = *model[1];
                for (int k = 0; k < total; ++k)
                {
                    A.setLink(linked, B.detectorLevel());
                    B.setLink(linked, A.detectorLevel());
                    if (cfg.lockstep)
                        tubechannel::channel::ChannelModel::processPair(A, B, osIn[0][size_t(k)], osSc2[0][size_t(k)], osIn[1][size_t(k)],
                                                                        osSc2[1][size_t(k)], osOut[0][size_t(k)], osOut[1][size_t(k)]);
                    else
                    {
                        osOut[0][size_t(k)] = A.process(osIn[0][size_t(k)], osSc2[0][size_t(k)]);
                        osOut[1][size_t(k)] = B.process(osIn[1][size_t(k)], osSc2[1][size_t(k)]);
                    }
                }
            }
            for (int ch = 0; ch < nch; ++ch)
            {
                float* out = audio[ch] + start;
                os[ch].downsample(0, osOut[ch].data(), m, out);
                double acc = ms[ch];
                for (int i = 0; i < m; ++i)
                {
                    out[i] *= outGain[ch];
                    acc += (double(out[i]) * out[i] - acc) * rmsCoeff;
                }
                ms[ch] = acc;
            }
        }
        for (int ch = 0; ch < nch; ++ch)
        {
            model[ch]->publish(*tel[ch]);
            tel[ch]->outRms.store(float(std::sqrt(ms[ch])));
        }
    }

private:
    ChainConfig cfg;
    int nch = 2;
    tubechannel::dsp::Oversampler os[2], osSc[2];
    std::unique_ptr<tubechannel::channel::ChannelModel> model[2];
    std::vector<float> scaled[2], scaledSc[2];
    std::vector<double> osIn[2], osSc2[2], osOut[2];
    double ms[2] = { 0.0, 0.0 };
    double rmsCoeff = 0.001;
};
} // namespace tc
