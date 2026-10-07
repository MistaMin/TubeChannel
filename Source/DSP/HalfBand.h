#pragma once

// Half-band 2x up/down stages for the oversampling engine.
//
//  - IirHalfband: minimum-phase polyphase all-pass pair (elliptic half-band), no latency.
//  - FirHalfband: linear-phase Kaiser half-band FIR, constant group delay.
//
// Everything is double precision: a 120 dB stop band needs more than float state.
// No JUCE dependency, so the engine can be tested headless on any platform.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

namespace tubechannel::dsp
{
constexpr double kPi = 3.14159265358979323846;

namespace design
{
// ---- elliptic half-band as a pair of all-pass polyphase branches --------------------------
// Closed-form coefficients for an odd-order elliptic half-band (nc coefficients, order 2nc+1).
// tbw = transition width / high sample rate, centred on fs/4.
inline double transitionParam(double tbw)
{
    const double k = std::tan((1.0 - tbw * 2.0) * kPi / 4.0);
    return k * k;
}

inline double nomeFromModulus(double k)
{
    const double kp = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kp) / (1.0 + kp);
    const double e4 = std::pow(e, 4.0);
    return e * (1.0 + e4 * (2.0 + e4 * (15.0 + e4 * 150.0)));
}

inline double accNum(double q, int order, int c)
{
    double acc = 0.0, sign = 1.0, term = 0.0;
    int i = 0;
    do
    {
        term = std::pow(q, double(i * (i + 1))) * std::sin((i * 2 + 1) * c * kPi / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 64);
    return acc;
}

inline double accDen(double q, int order, int c)
{
    double acc = 0.0, sign = -1.0, term = 0.0;
    int i = 1;
    do
    {
        term = std::pow(q, double(i * i)) * std::cos(i * 2 * c * kPi / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 64);
    return acc;
}

inline std::vector<double> iirCoefs(int nc, double tbw)
{
    const double k = transitionParam(tbw);
    const double q = nomeFromModulus(k);
    const int order = nc * 2 + 1;
    std::vector<double> coef(static_cast<size_t>(nc), 0.0);
    for (int index = 0; index < nc; ++index)
    {
        const int c = index + 1;
        const double num = accNum(q, order, c) * std::pow(q, 0.25);
        const double den = accDen(q, order, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        coef[size_t(index)] = (1.0 - x) / (1.0 + x);
    }
    return coef;
}

// |H| of the half-band at normalised frequency f (cycles per sample of the HIGH rate).
inline double iirMagnitude(const std::vector<double>& c, double f)
{
    const std::complex<double> z1 = std::polar(1.0, -2.0 * kPi * f);
    const std::complex<double> z2 = z1 * z1;
    auto path = [&](size_t start)
    {
        std::complex<double> a = 1.0;
        for (size_t i = start; i < c.size(); i += 2)
            a *= (c[i] + z2) / (1.0 + c[i] * z2);
        return a;
    };
    return std::abs(0.5 * (path(0) + z1 * path(1)));
}

// Smallest coefficient count whose stop band (from 0.25+tbw/2 to 0.5) is below -attenDb.
inline std::vector<double> iirForSpec(double attenDb, double tbw)
{
    const double limit = std::pow(10.0, -attenDb / 20.0);
    const double stopEdge = 0.25 + tbw * 0.5;
    for (int nc = 2; nc <= 24; ++nc)
    {
        auto c = iirCoefs(nc, tbw);
        double worst = 0.0;
        for (int i = 0; i <= 600; ++i)
            worst = std::max(worst, iirMagnitude(c, stopEdge + (0.5 - stopEdge) * i / 600.0));
        if (worst <= limit)
            return c;
    }
    return iirCoefs(24, tbw);
}

// ---- linear-phase Kaiser half-band -----------------------------------------------------------
inline double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    const double h = x * 0.5;
    for (int k = 1; k < 200; ++k)
    {
        term *= (h / k) * (h / k);
        sum += term;
        if (term < 1e-18 * sum)
            break;
    }
    return sum;
}

// Odd length 4m+3, taps at even offsets from the centre are exactly zero, centre tap is 0.5.
inline std::vector<double> firHalfband(double attenDb, double tbw)
{
    const double beta = attenDb > 50.0 ? 0.1102 * (attenDb - 8.7)
                                       : 0.5842 * std::pow(attenDb - 21.0, 0.4) + 0.07886 * (attenDb - 21.0);
    int taps = int(std::ceil((attenDb - 7.95) / (14.36 * tbw))) + 1;
    taps = std::max(taps, 3);
    while ((taps - 3) % 4 != 0)
        ++taps;
    const int c = (taps - 1) / 2;
    std::vector<double> h(size_t(taps), 0.0);
    const double i0b = besselI0(beta);
    for (int i = 0; i < taps; ++i)
    {
        const int off = i - c;
        double v;
        if (off == 0)
            v = 0.5;
        else if (off % 2 == 0)
            v = 0.0;
        else
            v = std::sin(kPi * off * 0.5) / (kPi * off);
        const double r = double(off) / double(c);
        h[size_t(i)] = v * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
    }
    return h;
}
} // namespace design

// ---- stages --------------------------------------------------------------------------------------
class HalfbandStage
{
public:
    virtual ~HalfbandStage() = default;
    virtual void prepare(int numChannels) = 0;
    virtual void reset() = 0;
    // in: n samples at the low rate, out: 2n samples at the high rate (gain 1).
    virtual void up(int ch, const double* in, int n, double* out) = 0;
    // in: 2n samples at the high rate, out: n samples at the low rate (gain 1).
    virtual void down(int ch, const double* in, int n, double* out) = 0;
    // Delay of one pass (up OR down) in HIGH-rate samples. 0 for the IIR stage.
    virtual int passDelayHi() const { return 0; }
    virtual int coefficientCount() const = 0;
};

class IirHalfband final : public HalfbandStage
{
public:
    explicit IirHalfband(std::vector<double> coefs)
    {
        for (size_t i = 0; i < coefs.size(); ++i)
            (i % 2 == 0 ? a0 : a1).push_back(coefs[i]);
    }

    void prepare(int numChannels) override
    {
        state.assign(size_t(numChannels), {});
        for (auto& s : state)
            s.alloc(a0.size(), a1.size());
    }

    void reset() override
    {
        for (auto& s : state)
            s.alloc(a0.size(), a1.size());
    }

    void up(int ch, const double* in, int n, double* out) override
    {
        auto& s = state[size_t(ch)];
        for (int i = 0; i < n; ++i)
        {
            const double u = in[i];
            out[2 * i] = run(a0, s.x0, s.y0, u);
            out[2 * i + 1] = run(a1, s.x1, s.y1, u);
        }
    }

    void down(int ch, const double* in, int n, double* out) override
    {
        auto& s = state[size_t(ch)];
        for (int i = 0; i < n; ++i)
        {
            const double p0 = run(a0, s.x0, s.y0, in[2 * i]);
            const double p1 = run(a1, s.x1, s.y1, in[2 * i + 1]);
            out[i] = 0.5 * (p0 + s.p1prev);
            s.p1prev = p1;
        }
    }

    int coefficientCount() const override { return int(a0.size() + a1.size()); }

private:
    struct State
    {
        std::vector<double> x0, y0, x1, y1;
        double p1prev = 0.0;
        void alloc(size_t n0, size_t n1)
        {
            x0.assign(n0, 0.0);
            y0.assign(n0, 0.0);
            x1.assign(n1, 0.0);
            y1.assign(n1, 0.0);
            p1prev = 0.0;
        }
    };

    static double run(const std::vector<double>& a, std::vector<double>& x, std::vector<double>& y, double in)
    {
        for (size_t k = 0; k < a.size(); ++k)
        {
            const double out = a[k] * (in - y[k]) + x[k];
            x[k] = in;
            y[k] = out;
            in = out;
        }
        return in;
    }

    std::vector<double> a0, a1;
    std::vector<State> state;
};

class FirHalfband final : public HalfbandStage
{
public:
    explicit FirHalfband(std::vector<double> taps)
        : h(std::move(taps)), centre((int(h.size()) - 1) / 2)
    {
        // Even-index taps are the non-zero side taps (centre index is odd).
        for (int i = 0; i < int(h.size()); i += 2)
            side.push_back(h[size_t(i)]);
    }

    void prepare(int numChannels) override
    {
        const int len = historyLength();
        hist.assign(size_t(numChannels), History(len));
    }

    void reset() override
    {
        for (auto& x : hist)
            x.clear();
    }

    void up(int ch, const double* in, int n, double* out) override
    {
        auto& H = hist[size_t(ch)];
        const int m = int(side.size());
        const int dc = (centre - 1) / 2;
        for (int i = 0; i < n; ++i)
        {
            H.push(in[i]);
            const double* x = H.newest();             // x[-k] = k samples ago
            double acc = 0.0;
            for (int k = 0; k < m; ++k)
                acc += side[size_t(k)] * x[-k];
            out[2 * i] = 2.0 * acc;
            out[2 * i + 1] = x[-dc];
        }
    }

    void down(int ch, const double* in, int n, double* out) override
    {
        auto& H = hist[size_t(ch)];                    // history of the high-rate input
        for (int i = 0; i < n; ++i)
        {
            H.push(in[2 * i]);
            H.push(in[2 * i + 1]);
            const double* x = H.newest();              // newest = in[2i+1]
            // out[n] = sum_i h[i] * x[2n+1-i]  (x[-j] is j samples before in[2i+1]); align so
            // the stage delay is exactly `centre` high-rate samples.
            double acc = 0.0;
            for (int k = 0; k < int(h.size()); k += 2)
                acc += h[size_t(k)] * x[-(k + 1)];
            acc += h[size_t(centre)] * x[-(centre + 1)];
            out[i] = acc;
        }
    }

    int passDelayHi() const override { return centre; }
    int coefficientCount() const override { return int(h.size()); }

private:
    struct History
    {
        History() = default;
        explicit History(int len) : n(len), buf(size_t(len) * 2, 0.0) {}
        void clear() { std::fill(buf.begin(), buf.end(), 0.0); pos = 0; }
        void push(double v)
        {
            pos = (pos + 1) % n;
            buf[size_t(pos)] = v;
            buf[size_t(pos + n)] = v;
        }
        const double* newest() const { return buf.data() + pos + n; }
        int n = 1;
        int pos = 0;
        std::vector<double> buf;
    };

    int historyLength() const { return int(h.size()) + 4; }

    std::vector<double> h, side;
    int centre;
    std::vector<History> hist;
};
} // namespace tubechannel::dsp
