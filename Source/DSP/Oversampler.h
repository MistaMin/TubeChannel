#pragma once

// Oversampling engine for Tube Channel.
//
// Nominal factors at 44.1 / 48 kHz (the oversampled rate is what matters, so the factor shrinks as
// the session rate rises and the effective rate stays the same):
//
//                      44.1/48k   88.2/96k   176.4/192k
//   Quality::Eco          1x         1x          1x     residual ADAA
//   Quality::Default      2x         1x          1x
//   Quality::HQ           2x         1x          1x
//   Quality::Derp         4x         2x          1x     "\( ᐖ)/ - Derp mode"
//
// The original modes can use minimum-phase IIR half-bands (zero reported latency) or, with linearPhase = true
// (the "+L" modes), linear-phase FIR half-bands. The whole circuit runs at the oversampled rate;
// the decimation filters bring it back to the base Nyquist.

#include "HalfBand.h"

namespace tubechannel::dsp
{
enum class Quality { Default = 0, HQ = 1, Derp = 2, Eco = 3 };

inline int nominalStages(Quality q)
{
    switch (q)
    {
        case Quality::Eco:     return 0;     // residual ADAA in the reduced engine
        case Quality::Default: return 1;     // 2x at 44.1 / 48 kHz
        case Quality::HQ:      return 1;     // 2x; original detector selected by the chain
        case Quality::Derp:    return 2;     // 4x; original detector
    }
    return 1;
}

// One stage fewer for every doubling of the session rate above 48 kHz (never below zero stages).
inline int stagesFor(Quality q, double sampleRate)
{
    const int shift = std::max(0, int(std::lround(std::log2(sampleRate / 48000.0))));
    return std::max(0, nominalStages(q) - shift);
}

class Oversampler
{
public:
    static constexpr double kStopbandDb = 120.0;
    static constexpr double kPassbandHz = 20000.0;

    // Not real-time safe: allocates and designs filters. Call from the message thread, then swap.
    void prepare(double baseSampleRate, int maxBlock, int channels, Quality q, bool linearPhase, int explicitStages = -1)
    {
        fs = baseSampleRate;
        maxBlockSize = maxBlock;
        numChannels = channels;
        quality = q;
        linear = linearPhase;

        const int S = explicitStages < 0 ? stagesFor(q, fs) : std::clamp(explicitStages, 0, 3);
        factor_ = 1 << S;
        stages.clear();
        upStages.clear();

        const double fp = std::min(kPassbandHz, 0.4535 * fs);
        for (int s = 0; s < S; ++s)
        {
            const double fLow = fs * double(1 << s);
            const double fHi = 2.0 * fLow;
            const double tbw = std::min(0.49, (fLow - 2.0 * fp) / fHi);
            // Separate filter instances for the up and down paths: each keeps its own state.
            std::unique_ptr<HalfbandStage> up, down;
            if (linearPhase)
            {
                const auto taps = design::firHalfband(kStopbandDb, tbw);
                up = std::make_unique<FirHalfband>(taps);
                down = std::make_unique<FirHalfband>(taps);
            }
            else
            {
                const auto coefs = design::iirForSpec(kStopbandDb, tbw);
                up = std::make_unique<IirHalfband>(coefs);
                down = std::make_unique<IirHalfband>(coefs);
            }
            up->prepare(channels);
            down->prepare(channels);
            upStages.push_back(std::move(up));
            stages.push_back(std::move(down));
        }

        // Latency, expressed in oversampled samples. Pad so it is a whole number of base samples.
        long total = 0;
        if (linearPhase)
            for (int s = 0; s < S; ++s)
                total += 2L * stages[size_t(s)]->passDelayHi() * (factor_ >> (s + 1));
        pad = linearPhase ? int((factor_ - total % factor_) % factor_) : 0;
        latencyBase = linearPhase ? int((total + pad) / factor_) : 0;

        scratchA.assign(size_t(maxBlock) * size_t(factor_), 0.0);
        scratchB.assign(size_t(maxBlock) * size_t(factor_), 0.0);
        padLine.assign(size_t(channels), std::vector<double>(size_t(std::max(pad, 1)), 0.0));
        padPos.assign(size_t(channels), 0);
    }

    void reset()
    {
        for (auto& s : stages)
            s->reset();
        for (auto& s : upStages)
            s->reset();
        for (auto& p : padLine)
            std::fill(p.begin(), p.end(), 0.0);
        std::fill(padPos.begin(), padPos.end(), 0);
    }

    static int factorFor(Quality q, double sampleRate) { return 1 << stagesFor(q, sampleRate); }

    int factor() const { return factor_; }
    int latencySamples() const { return latencyBase; }   // at the base rate, whole samples
    Quality getQuality() const { return quality; }
    bool isLinearPhase() const { return linear; }
    int stageCount() const { return int(stages.size()); }
    int stageCoefficients(int s) const { return stages[size_t(s)]->coefficientCount(); }

    // in: n base-rate samples. out: n * factor() oversampled samples.
    void upsample(int ch, const float* in, int n, double* out)
    {
        assert(n <= maxBlockSize);
        if (stages.empty())                            // 1x: the session rate is already high enough
        {
            for (int i = 0; i < n; ++i)
                out[i] = double(in[i]);
            return;
        }
        double* src = scratchA.data();
        for (int i = 0; i < n; ++i)
            src[i] = double(in[i]);

        const int S = int(stages.size());
        double* dst = scratchB.data();
        int len = n;
        for (int s = 0; s < S; ++s)
        {
            double* target = (s == S - 1) ? out : dst;
            upStages[size_t(s)]->up(ch, src, len, target);
            len *= 2;
            if (s != S - 1)
            {
                dst = (dst == scratchB.data()) ? scratchA.data() : scratchB.data();
                src = target;
            }
        }
    }

    // in: n * factor() oversampled samples (read-only). out: n base-rate samples.
    void downsample(int ch, const double* in, int n, float* out)
    {
        assert(n <= maxBlockSize);
        if (stages.empty())
        {
            for (int i = 0; i < n; ++i)
                out[i] = float(in[i]);
            return;
        }
        const int S = int(stages.size());
        int len = n * factor_;

        const double* src = in;
        if (pad > 0)
        {
            auto& line = padLine[size_t(ch)];
            int& pos = padPos[size_t(ch)];
            double* delayed = scratchA.data();
            for (int i = 0; i < len; ++i)
            {
                delayed[i] = line[size_t(pos)];
                line[size_t(pos)] = in[i];
                pos = (pos + 1) % pad;
            }
            src = delayed;
        }

        double* bufs[2] = { scratchA.data(), scratchB.data() };
        for (int s = S - 1; s >= 0; --s)
        {
            len /= 2;
            double* target = (src == bufs[0]) ? bufs[1] : bufs[0];
            stages[size_t(s)]->down(ch, src, len, target);
            src = target;
        }
        for (int i = 0; i < n; ++i)
            out[i] = float(src[i]);
    }

private:
    double fs = 48000.0;
    int maxBlockSize = 0, numChannels = 0, factor_ = 2, latencyBase = 0, pad = 0;
    Quality quality = Quality::Default;
    bool linear = false;
    std::vector<std::unique_ptr<HalfbandStage>> stages;     // decimation (down) path
    std::vector<std::unique_ptr<HalfbandStage>> upStages;   // interpolation (up) path
    std::vector<double> scratchA, scratchB;
    std::vector<std::vector<double>> padLine;
    std::vector<int> padPos;
};
} // namespace tubechannel::dsp
