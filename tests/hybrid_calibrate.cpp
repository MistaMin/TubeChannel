// Offline calibration and independent evaluation of the compact detector.
#include "Channel/ChannelModel.h"
#include "Hybrid/Detector.h"
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace tubechannel;
struct Fixture
{
    double rate = 48000.0;
    channel::ChannelControls controls;
    std::vector<float> input, gr;
};
using Member = double hybrid::Detector::Calibration::*;
static constexpr std::array<Member, 16> members {
    &hybrid::Detector::Calibration::rectifierDrop,
    &hybrid::Detector::Calibration::rectifierLogDrop,
    &hybrid::Detector::Calibration::rectifierFloor,
    &hybrid::Detector::Calibration::driveScale,
    &hybrid::Detector::Calibration::driverCeiling,
    &hybrid::Detector::Calibration::fastFraction,
    &hybrid::Detector::Calibration::fastDrop,
    &hybrid::Detector::Calibration::slowDrop,
    &hybrid::Detector::Calibration::fastSourceOhms,
    &hybrid::Detector::Calibration::slowSourceOhms,
    &hybrid::Detector::Calibration::timingFarads,
    &hybrid::Detector::Calibration::releaseBaseOhms,
    &hybrid::Detector::Calibration::releaseSpanOhms,
    &hybrid::Detector::Calibration::releaseFeedback,
    &hybrid::Detector::Calibration::autoFastOhms,
    &hybrid::Detector::Calibration::autoSlowOhms };
static constexpr const char* names[] { "rectifierDrop", "rectifierLogDrop", "rectifierFloor", "driveScale", "driverCeiling", "fastFraction", "fastDrop", "slowDrop", "fastSourceOhms", "slowSourceOhms", "timingFarads", "releaseBaseOhms", "releaseSpanOhms", "releaseFeedback", "autoFastOhms", "autoSlowOhms" };
static constexpr double lo[] { 0.01, 1e-06, 0.001, 0.2, 1, 0.1, 1e-06, 1e-06, 0.1, 1000, 1e-08, 1000, 1000, 0.005, 100, 1000 };
static constexpr double hi[] { 2, 0.5, 0.6, 5, 13, 1, 2, 2, 10000, 100000, 1e-05, 200000, 500000, 2, 25000, 300000 };
static_assert(std::size(lo) == members.size() && std::size(hi) == members.size());

template<class T> void write(std::ostream& out, const T& x) { out.write(reinterpret_cast<const char*>(&x), sizeof(x)); }
template<class T> T read(std::istream& in) { T x {}; in.read(reinterpret_cast<char*>(&x), sizeof(x)); if (!in) throw std::runtime_error("truncated dataset"); return x; }

static std::vector<Fixture> load(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in || read<unsigned>(in) != 0x54434432u) throw std::runtime_error("invalid dataset");
    const auto count = read<unsigned>(in);
    if (!count || count > 100) throw std::runtime_error("invalid fixture count");
    std::vector<Fixture> fixtures(count);
    for (auto& f : fixtures)
    {
        f.rate = read<double>(in); f.controls.threshold = read<double>(in); f.controls.ratio = read<double>(in);
        f.controls.attack = read<double>(in); f.controls.release = read<double>(in); f.controls.autoDetect = read<unsigned>(in) != 0;
        auto n = read<unsigned>(in);
        if (!std::isfinite(f.rate) || f.rate < 8000 || f.rate > 192000 || !n || n > 2000000)
            throw std::runtime_error("invalid fixture size/rate");
        f.input.resize(n); f.gr.resize(n);
        in.read(reinterpret_cast<char*>(f.input.data()), std::streamsize(n * sizeof(float)));
        in.read(reinterpret_cast<char*>(f.gr.data()), std::streamsize(n * sizeof(float)));
        if (!in) throw std::runtime_error("truncated samples");
    }
    return fixtures;
}

static void capture(const std::string& dir, const std::string& output, const std::string& set)
{
    const std::array<std::string, 11> sets { "timing", "train", "train2", "train3", "train4", "holdout", "holdout2", "holdout3", "holdout4", "holdout5", "holdout6" };
    if (std::find(sets.begin(), sets.end(), set) == sets.end()) throw std::runtime_error("unknown capture fixture set");
    std::ifstream file(dir + "/det.net"); if (!file) throw std::runtime_error("cannot read detector");
    std::stringstream text; text << file.rdbuf();
    const bool training = set == "train" || set == "timing";
    const bool expanded = set == "train2";
    const bool factorial = set == "train3" || set == "train4";
    const bool endpoints = set == "train4";
    const bool fresh = set == "holdout3" || set == "holdout4" || set == "holdout5" || set == "holdout6";
    const int count = training ? 6 : expanded ? 24 : factorial ? (endpoints ? 72 : 54) : fresh ? 24 : 18;
    std::ofstream out(output, std::ios::binary); if (!out) throw std::runtime_error("cannot create dataset");
    write(out, 0x54434432u); write(out, unsigned(count));
    std::ofstream trace(output + ".trace.csv");
    trace << "case,time_seconds,input_volts,stored_volts,driver_volts,emitter_volts,rectified_volts,led_amps\n";
    int failures = 0;
    for (int k = 0; k < count; ++k)
    {
        Fixture f;
        double frequency = 1000.0, amplitude = 1.8;
        if (training)
        {
            f.controls.attack = f.controls.release = (k % 3) * 0.5;
            f.controls.autoDetect = k >= 3;
        }
        else
        {
            const bool second = set == "holdout2";
            f.rate = k % 3 == 0 ? 44100.0 : 48000.0;
            f.controls.threshold = k % 3 == 0 ? 0.1 : k % 3 == 1 ? 0.25 : 0.5;
            f.controls.ratio = k % 3 == 0 ? 0.0 : k % 3 == 1 ? 0.6 : 1.0;
            f.controls.attack = k % 2 ? 0.75 : 0.2;
            f.controls.release = (k / 3) % 2 ? 0.75 : 0.2;
            f.controls.autoDetect = k >= 9;
            constexpr double frequencies[] = { 80.0, 220.0, 4000.0 };
            frequency = frequencies[k % 3] * (1.0 + 0.13 * (k / 3)) * (second ? 1.37 : 1.0);
            amplitude = (k % 3 == 0 ? 0.45 : k % 3 == 1 ? 1.0 : 3.2) * (1.0 + 0.1 * (k / 3)) * (second ? 0.83 : 1.0);
        }
        if (expanded)
        {
            f.rate = 48000.0;
            f.controls.threshold = k % 4 == 0 ? 0.12 : k % 4 == 1 ? 0.25 : k % 4 == 2 ? 0.4 : 0.55;
            f.controls.ratio = k % 3 == 0 ? 0.1 : k % 3 == 1 ? 0.5 : 0.9;
            f.controls.attack = (k % 4) / 3.0;
            f.controls.release = ((k / 3) % 4) / 3.0;
            f.controls.autoDetect = k >= 12;
            frequency = k % 3 == 0 ? 150.0 : k % 3 == 1 ? 1000.0 : 6500.0;
            amplitude = k % 4 == 0 ? 0.4 : k % 4 == 1 ? 1.3 : k % 4 == 2 ? 2.7 : 5.0;
        }
        if (factorial)
        {
            f.rate = 48000.0;
            const int j = k < 36 ? k : k - 36;
            f.controls.threshold = (j % 3) * 0.2 + 0.1;
            f.controls.ratio = ((j / 3) % 3) * 0.5;
            f.controls.attack = k < 36 ? ((j / 9) % 2 ? 0.8 : 0.2) : 0.5;
            f.controls.release = k < 36 ? ((j / 18) % 2 ? 0.8 : 0.2) : 0.5;
            f.controls.autoDetect = k >= 36;
            constexpr double freqs[] = { 150.0, 1200.0, 5000.0 };
            frequency = freqs[(j * 7 + j / 5) % 3];
            const double onset = 1.0960 * std::pow(10.0, (-20.0 + 40.0 * f.controls.threshold) / 20.0);
            amplitude = onset * (j % 2 ? 3.0 : 6.0);
        }
        if (endpoints && k >= 54)
        {
            const int j = k - 54;
            f.controls.threshold = (j % 3) * 0.2 + 0.1;
            f.controls.ratio = ((j / 3) % 3) * 0.5;
            f.controls.attack = j % 2 ? 1.0 : 0.0;
            f.controls.release = (j / 3) % 2 ? 1.0 : 0.0;
            f.controls.autoDetect = false;
            constexpr double frequencies[] = { 220.0, 2100.0, 7100.0 };
            frequency = frequencies[(j * 5 + j / 2) % 3];
            amplitude = 1.0960 * std::pow(10.0, (-20.0 + 40.0 * f.controls.threshold) / 20.0) * (j % 3 == 0 ? 2.0 : j % 3 == 1 ? 4.0 : 8.0);
        }
        if (fresh)
        {
            f.rate = k % 2 ? 48000.0 : 44100.0;
            const int j = set == "holdout6" ? (k * 19 + 9) % 24 : set == "holdout5" ? (k * 13 + 3) % 24 : set == "holdout4" ? (k * 11 + 7) % 24 : k;
            f.controls.threshold = 0.05 + 0.6 * ((j * 17) % 23) / 22.0;
            f.controls.ratio = ((j * 7) % 23) / 22.0;
            constexpr double extremes[] = { 0.0, 0.35, 0.9, 1.0 };
            f.controls.attack = extremes[k % 4]; f.controls.release = extremes[(k / 3) % 4];
            f.controls.autoDetect = k >= 12;
            constexpr double freqs[] = { 57.0, 370.0, 1700.0, 8000.0 };
            constexpr double multiples[] = { 1.3, 2.7, 4.8, 7.5 };
            frequency = freqs[(k * 3 + k / 5) % 4];
            amplitude = 1.0960 * std::pow(10.0, (-20.0 + 40.0 * f.controls.threshold) / 20.0) * multiples[(j * 5 + j / 7) % 4];
            if (set == "holdout4") { frequency *= 1.17; amplitude *= 0.91; }
            if (set == "holdout5") { frequency *= 0.93; amplitude *= 0.79; }
            if (set == "holdout6") { frequency *= 1.31; amplitude *= 1.13; }
        }
        circuit::Block ref; circuit::ToleranceSettings tol; tol.enabled = false;
        ref.build(text.str(), 1.0 / f.rate, tol, { "VP", "VN" }, { { "O6B", 10.0 } });
        ref.c.guardJunctionUpdates = true; ref.c.maxIter = 50; ref.c.extrapOrder = 0;
        ref.setParam("THRW", channel::ChannelModel::thresholdWiper(f.controls.threshold));
        ref.setParam("RATR", channel::ChannelModel::ratioPotOhms(f.controls.ratio));
        ref.setParam("ATTR", 50000.0 * (std::pow(10.0, 2.0 * f.controls.attack) - 1.0) / 99.0);
        ref.setParam("RELW", f.controls.release); ref.setParam("AUTO", f.controls.autoDetect ? 1.0 : 0.0);
        ref.applyParams(); ref.setRails(15.0, -15.0, 45.0, 150.0); ref.setSupply(15.0, -15.0);
        if (!ref.dc()) throw std::runtime_error("reference initialization failed");
        const int input = ref.inputHandle("O6B"), led = ref.resistorHandle("RLED");
        const int stored = ref.nodeId("NC"), driver = ref.nodeId("N14"), emitter = ref.nodeId("E14"), rectified = ref.nodeId("NT");
        channel::Vactrol optical; optical.prepare(f.rate);
        const unsigned n = unsigned(((set == "holdout6" || set == "timing") ? 6.0 : 2.0) * f.rate);
        f.input.resize(n); f.gr.resize(n);
        for (unsigned i = 0; i < n; ++i)
        {
            const double t = double(i) / f.rate;
            double envelope = (t >= 0.1 && t < 0.5) || (t >= 0.9 && t < 1.1) ? 1.0 : 0.0;
            if (!training) envelope *= 0.7 + 0.3 * std::sin(6.283185307179586 * 7.0 * t);
            const double carrier = std::sin(6.283185307179586 * frequency * t);
            const double waveform = (set == "holdout5" || set == "holdout6") ? 0.85 * carrier + 0.15 * std::sin(6.283185307179586 * frequency * 2.7 * t) : carrier;
            f.input[i] = float(amplitude * envelope * waveform);
            ref.setSourceH(input, f.input[i]);
            if (!ref.step()) ++failures;
            optical.process(ref.c.resistorCurrent(led));
            f.gr[i] = float(optical.gainReductionDb());
            if (i % 24 == 0) trace << k << "," << t << "," << f.input[i] << "," << ref.nodeV(stored) << ","
                << ref.nodeV(driver) << "," << ref.nodeV(emitter) << "," << ref.nodeV(rectified) << "," << ref.c.resistorCurrent(led) << "\n";
            if (!std::isfinite(f.gr[i])) throw std::runtime_error("nonfinite reference");
        }
        write(out, f.rate); write(out, f.controls.threshold); write(out, f.controls.ratio);
        write(out, f.controls.attack); write(out, f.controls.release); write(out, unsigned(f.controls.autoDetect)); write(out, n);
        out.write(reinterpret_cast<const char*>(f.input.data()), n * sizeof(float));
        out.write(reinterpret_cast<const char*>(f.gr.data()), n * sizeof(float));
        std::printf("captured %s case %d: %.0f Hz, tone %.1f Hz, amplitude %.3f, threshold %.2f ratio %.2f, attack %.2f release %.2f, %s\n",
                    set.c_str(), k, f.rate, frequency, amplitude, f.controls.threshold, f.controls.ratio,
                    f.controls.attack, f.controls.release, f.controls.autoDetect ? "Auto" : "manual");
        std::fflush(stdout);
    }
    if (failures) throw std::runtime_error("reference convergence failures: " + std::to_string(failures));
}

static double evaluate(const std::vector<Fixture>& fixtures, const hybrid::Detector::Calibration& calibration,
                       bool report = false, const std::string& csvPath = {})
{
    std::ofstream csv;
    if (!csvPath.empty()) { csv.open(csvPath); csv << "case,time_seconds,reference_gr_db,prototype_gr_db\n"; }
    double total = 0.0; std::size_t count = 0;
    for (std::size_t k = 0; k < fixtures.size(); ++k)
    {
        const auto& f = fixtures[k]; hybrid::Detector detector;
        if (!detector.prepare(f.rate, calibration)) throw std::runtime_error("invalid calibration");
        detector.configureElectrical(channel::ChannelModel::thresholdWiper(f.controls.threshold),
            channel::ChannelModel::ratioPotOhms(f.controls.ratio),
            50000.0 * (std::pow(10.0, 2.0 * f.controls.attack) - 1.0) / 99.0,
            f.controls.release, f.controls.autoDetect);
        const double rise = -std::expm1(-1.0 / (f.rate * 0.0035));
        const double fall = -std::expm1(-1.0 / (f.rate * 0.072));
        double light = 0.0, squared = 0.0, maxError = 0.0; std::size_t steps = 0;
        // During fitting evaluate the unchanged optical equation at 500 Hz rather
        // than its 4-sample table cadence. Final checks use the production Vactrol.
        channel::Vactrol optical;
        optical.prepare(f.rate);
        const unsigned stride = report ? 4u : unsigned(std::lround(f.rate / 500.0));
        for (std::size_t i = 0; i < f.input.size(); ++i)
        {
            const double led = detector.process(f.input[i]);
            light += (led - light) * (led > light ? rise : fall);
            if (report) optical.process(led);
            if (i % stride) continue;
            const double resistance = 1.0 / (1e-6 + 1.0 / channel::Vactrol::onResistance(light * 1e3));
            const double gr = report ? optical.gainReductionDb()
                : -20.0 * std::log10((resistance / (resistance + 14700.0)) / (1e6 / (1e6 + 14700.0)));
            const double error = gr - f.gr[i];
            if (!std::isfinite(error)) return 1e9;
            squared += error * error; maxError = std::max(maxError, std::fabs(error)); ++steps;
            if (csv && i % unsigned(std::lround(f.rate / 500.0)) == 0) csv << k << ',' << double(i) / f.rate << ',' << f.gr[i] << ',' << gr << '\n';
        }
        total += squared; count += steps;
        if (report) std::printf("case %zu: GR RMS %.4f dB, max %.4f dB\n", k, std::sqrt(squared / steps), maxError);
    }
    return std::sqrt(total / count);
}
static hybrid::Detector::Calibration readCalibration(const std::string& path)
{
    std::ifstream f(path); hybrid::Detector::Calibration c;
    for (auto m : members) if (!(f >> c.*m)) throw std::runtime_error("invalid calibration file");
    double extra; if (f >> extra) throw std::runtime_error("calibration schema has extra values");
    if (!c.valid()) throw std::runtime_error("invalid calibration values"); return c;
}
int main(int argc, char** argv)
{
    try
    {
        if (argc >= 2 && std::string(argv[1]) == "capture" && argc == 5)
            capture(argv[2], argv[3], argv[4]);
        else if (argc >= 2 && std::string(argv[1]) == "fit" && (argc == 4 || argc == 5))
        {
            const auto fixtures = load(argv[2]);
            auto c = argc == 5 ? readCalibration(argv[4]) : hybrid::Detector::Calibration {};
            double best = evaluate(fixtures, c), step = 0.5;
            for (int pass = 0; pass < 95 && step > 0.0005; ++pass)
            {
                bool improved = false;
                for (std::size_t k = 0; k < members.size(); ++k)
                {
                    auto chosen = c; double loss = best;
                    for (double direction : { -1.0, 1.0 })
                    {
                        auto trial = c; trial.*members[k] = std::clamp((c.*members[k]) * std::exp(direction * step), lo[k], hi[k]);
                        const double error = evaluate(fixtures, trial);
                        if (error < loss) { loss = error; chosen = trial; }
                    }
                    if (loss < best) { best = loss; c = chosen; improved = true; }
                }
                if (!improved) step *= 0.6;
                std::printf("fit pass %d RMS %.6f dB, step %.5f\n", pass, best, step); std::fflush(stdout);
            }
            std::ofstream output(argv[3]); output << std::setprecision(17);
            for (std::size_t k = 0; k < members.size(); ++k)
            { output << c.*members[k] << '\n'; std::printf("%s = %.17g\n", names[k], c.*members[k]); }
            std::printf("final training GR RMS %.6f dB\n", evaluate(fixtures, c, true));
        }
        else if (argc >= 2 && std::string(argv[1]) == "evaluate" && (argc == 4 || argc == 5))
            std::printf("aggregate GR RMS %.6f dB\n", evaluate(load(argv[2]), readCalibration(argv[3]), true, argc == 5 ? argv[4] : ""));
        else { std::puts("usage: hybrid_calibrate capture <netdir> <dataset> <timing|train[2-4]|holdout[2-6]>; fit <dataset> <params> [initial]; evaluate <dataset> <params> [csv]"); return 2; }
    }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
