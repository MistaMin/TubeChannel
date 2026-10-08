// Headless measurement of the oversampling engine. Exit code 0 only if every check passes.
#include "../Source/DSP/Oversampler.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace tubechannel::dsp;

static int failures = 0;
static void check(bool ok, const char* what, double value, const char* unit)
{
    if (!ok)
    {
        ++failures;
        std::printf("    FAIL %-44s %12.3f %s\n", what, value, unit);
    }
}

// Least-squares sine fit at a known frequency. Returns amplitude and residual RMS (everything else).
struct Fit { double amp, residualRms; };
static Fit fitSine(const std::vector<double>& x, double f, double fs, size_t skip)
{
    double sc = 0, ss = 0, cc = 0, ssn = 0, scn = 0;
    const size_t N = x.size();
    for (size_t n = skip; n < N; ++n)
    {
        const double ph = 2.0 * kPi * f / fs * double(n);
        const double s = std::sin(ph), c = std::cos(ph);
        ss += s * s; cc += c * c; ssn += s * c;
        sc += x[n] * s; scn += x[n] * c;
    }
    const double det = ss * cc - ssn * ssn;
    const double a = (sc * cc - scn * ssn) / det;
    const double b = (scn * ss - sc * ssn) / det;
    double res = 0.0;
    size_t cnt = 0;
    for (size_t n = skip; n < N; ++n)
    {
        const double ph = 2.0 * kPi * f / fs * double(n);
        const double e = x[n] - a * std::sin(ph) - b * std::cos(ph);
        res += e * e;
        ++cnt;
    }
    return { std::sqrt(a * a + b * b), std::sqrt(res / double(cnt)) };
}

static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-30)); }

// Run a base-rate signal through up -> down in blocks.
static std::vector<float> passThrough(Oversampler& os, const std::vector<float>& in, int block)
{
    std::vector<float> out(in.size());
    std::vector<double> osBuf(size_t(block) * size_t(os.factor()));
    for (size_t pos = 0; pos < in.size(); pos += size_t(block))
    {
        const int n = int(std::min<size_t>(size_t(block), in.size() - pos));
        os.upsample(0, in.data() + pos, n, osBuf.data());
        os.downsample(0, osBuf.data(), n, out.data() + pos);
    }
    return out;
}

static void runCase(double fs, Quality q, bool linear)
{
    const char* qn = q == Quality::Default ? "Default" : q == Quality::HQ ? "HQ" : "Derp";
    Oversampler os;
    const int block = 512;
    os.prepare(fs, block, 1, q, linear);

    check(os.factor() == Oversampler::factorFor(q, fs), "factor matches factorFor", double(os.factor()), "");
    std::printf("  fs %6.0f  %-7s %s  %2dx  stages:", fs, qn, linear ? "+L " : "min", os.factor());
    for (int s = 0; s < os.stageCount(); ++s)
        std::printf(" %d", os.stageCoefficients(s));
    std::printf("   latency %d samples\n", os.latencySamples());

    if (os.factor() == 1)
    {
        // No oversampling at this rate: the engine must be an exact passthrough with no latency.
        std::vector<float> in(2000);
        for (size_t n = 0; n < in.size(); ++n) in[n] = float(0.7 * std::sin(0.31 * double(n)));
        auto out = passThrough(os, in, block);
        double worst = 0.0;
        for (size_t n = 0; n < in.size(); ++n) worst = std::max(worst, std::fabs(double(out[n]) - double(in[n])));
        std::printf("      1x passthrough: max error %.3g, latency %d\n", worst, os.latencySamples());
        check(worst == 0.0, "1x is an exact passthrough", worst, "");
        check(os.latencySamples() == 0, "1x reports zero latency", double(os.latencySamples()), "samples");
        return;
    }

    // 1. Pass-band flatness through up -> down.
    const double fp = std::min(Oversampler::kPassbandHz, 0.4535 * fs);
    double worstRipple = 0.0, worstRes = -300.0;
    for (double f : { 31.25, 1000.0, 5000.0, 10000.0, fp * 0.9, fp })
    {
        const size_t N = size_t(fs * 0.6);
        // choose f so an integer number of cycles fits (reduces leakage): snap to a bin
        const double fb = std::round(f / fs * double(N)) / double(N) * fs;
        std::vector<float> in(N);
        for (size_t n = 0; n < N; ++n)
            in[n] = float(0.5 * std::sin(2.0 * kPi * fb / fs * double(n)));
        os.reset();
        auto out = passThrough(os, in, block);
        std::vector<double> od(out.begin(), out.end());
        auto fit = fitSine(od, fb, fs, size_t(fs * 0.2));
        worstRipple = std::max(worstRipple, std::fabs(db(fit.amp / 0.5)));
        worstRes = std::max(worstRes, db(fit.residualRms / (0.5 / std::sqrt(2.0))));
    }
    std::printf("      passband: worst gain error %.5f dB, worst residual %.1f dB (float output)\n", worstRipple, worstRes);
    check(worstRipple < 0.01, "passband gain error < 0.01 dB", worstRipple, "dB");
    check(worstRes < -100.0, "passband residual < -100 dB", worstRes, "dB");

    // 2. Alias rejection: tones in the oversampled domain that would fold into 0..fp at the base
    //    rate. Energy that folds into fp..Nyquist (ultrasonic, outside the audio band) is allowed.
    double worstAlias = -300.0;
    int tested = 0;
    const double fOs = fs * os.factor();
    const double stopEdge = fs - fp;
    const size_t N = size_t(fs * 0.5) * size_t(os.factor());
    const size_t blocks = (N / size_t(block * os.factor()));
    const int steps = 400;
    for (int i = 0; i <= steps; ++i)
    {
        double f = stopEdge + (fOs * 0.5 - stopEdge) * double(i) / double(steps);
        f = std::min(f, fOs * 0.5 - 1.0);
        double alias = std::fabs(f - std::round(f / fs) * fs);
        if (alias > fp || alias < 100.0)
            continue;                                   // folds out of the audio band, or onto DC
        ++tested;
        std::vector<double> sig(N);
        for (size_t n = 0; n < N; ++n)
            sig[n] = std::sin(2.0 * kPi * f / fOs * double(n));
        os.reset();
        std::vector<float> out(blocks * size_t(block));
        for (size_t b2 = 0; b2 < blocks; ++b2)
            os.downsample(0, sig.data() + b2 * size_t(block * os.factor()), block, out.data() + b2 * size_t(block));
        std::vector<double> od(out.begin(), out.end());
        const auto fit = fitSine(od, alias, fs, od.size() / 4);
        // Total in-band error: fitted alias plus broadband residual (both are unwanted).
        worstAlias = std::max(worstAlias, db(std::hypot(fit.amp, fit.residualRms * std::sqrt(2.0))));
    }
    std::printf("      alias rejection: worst %.1f dB over %d in-band alias tones\n", worstAlias, tested);
    check(worstAlias < -110.0, "alias rejection < -110 dB", worstAlias, "dB");

    // 3. Image rejection: base-rate tones up-sampled; everything except the tone must be tiny.
    double worstImage = -300.0;
    for (double f : { 1000.0, 9000.0, fp })
    {
        const size_t M = size_t(fs * 0.4);
        const double fb = std::round(f / fs * double(M)) / double(M) * fs;
        std::vector<float> in(M);
        for (size_t n = 0; n < M; ++n) in[n] = float(0.5 * std::sin(2.0 * kPi * fb / fs * double(n)));
        os.reset();
        std::vector<double> up(M * size_t(os.factor()));
        for (size_t pos = 0; pos + size_t(block) <= M; pos += size_t(block))
            os.upsample(0, in.data() + pos, block, up.data() + pos * size_t(os.factor()));
        const size_t used = (M / size_t(block)) * size_t(block) * size_t(os.factor());
        up.resize(used);
        auto fit = fitSine(up, fb, fOs, size_t(fs * 0.1) * size_t(os.factor()));
        worstImage = std::max(worstImage, db(fit.residualRms / (fit.amp / std::sqrt(2.0))));
    }
    std::printf("      image rejection: worst %.1f dB relative to the tone\n", worstImage);
    check(worstImage < -100.0, "image rejection < -100 dB", worstImage, "dB");

    // 4. Latency.
    {
        std::vector<float> in(4096, 0.0f);
        in[100] = 1.0f;
        os.reset();
        auto out = passThrough(os, in, block);
        size_t peak = 0;
        for (size_t n = 0; n < out.size(); ++n)
            if (std::fabs(out[n]) > std::fabs(out[peak])) peak = n;
        const int measured = int(peak) - 100;
        std::printf("      latency: reported %d, measured %d, peak %.4f\n", os.latencySamples(), measured, double(out[peak]));
        if (linear)
        {
            check(measured == os.latencySamples(), "linear-phase latency matches report", double(measured), "samples");
            check(out[peak] > 0.85f && out[peak] < 1.01f, "impulse peak in 0.85..1.01", double(out[peak]), "");
        }
        else
        {
            check(os.latencySamples() == 0, "min-phase reports zero latency", double(os.latencySamples()), "samples");
        }
    }

    // 5. Block-size independence (same output with 1-sample and 512-sample blocks).
    {
        const size_t N2 = 6000;
        std::vector<float> in(N2);
        for (size_t n = 0; n < N2; ++n) in[n] = float(0.3 * std::sin(0.05 * double(n)) + 0.1 * std::sin(1.3 * double(n)));
        os.reset();
        auto a = passThrough(os, in, 512);
        Oversampler os2;
        os2.prepare(fs, 7, 1, q, linear);
        auto b = passThrough(os2, in, 7);
        double worst = 0.0;
        for (size_t n = 0; n < N2; ++n) worst = std::max(worst, std::fabs(double(a[n]) - double(b[n])));
        check(worst < 1e-6, "block-size independent", worst, "");
    }

    // 6. Throughput (one channel, up + down only; the circuit comes on top of this).
    {
        const size_t N3 = size_t(fs * 5.0);
        std::vector<float> in(N3, 0.1f);
        for (size_t n = 0; n < N3; ++n) in[n] = float(0.5 * std::sin(0.01 * double(n)));
        os.reset();
        const auto t0 = std::chrono::steady_clock::now();
        auto out = passThrough(os, in, block);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("      speed: %.0fx realtime (1 ch, filters only)\n", 5.0 / secs);
        (void) out;
    }
}

int main()
{
    std::printf("Oversampler test\n");

    // The oversampled rate stays put as the session rate rises.
    struct { double fs; int d, h, x; } expect[] = {
        { 44100, 2, 2, 4 }, { 48000, 2, 2, 4 }, { 88200, 1, 1, 2 },
        { 96000, 1, 1, 2 }, { 176400, 1, 1, 1 }, { 192000, 1, 1, 1 }, { 384000, 1, 1, 1 } };
    for (const auto& e : expect)
    {
        check(Oversampler::factorFor(Quality::Default, e.fs) == e.d, "Default factor for rate", e.fs, "Hz");
        check(Oversampler::factorFor(Quality::HQ, e.fs) == e.h, "HQ factor for rate", e.fs, "Hz");
        check(Oversampler::factorFor(Quality::Derp, e.fs) == e.x, "Derp factor for rate", e.fs, "Hz");
    }
    std::printf("  factor table (Default/HQ/Derp) checked for 44.1k to 384k\n");

    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (Quality q : { Quality::Default, Quality::HQ, Quality::Derp })
            for (bool linear : { false, true })
                runCase(fs, q, linear);

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
