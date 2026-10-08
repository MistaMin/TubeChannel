// Independent circuit/model captures for fitting. This executable is not plugin DSP.
#include "Channel/ChannelModel.h"
#include "Hybrid/Detector.h"

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::puts("usage: hybrid_detector_capture <netlist directory> <output.csv>");
        return 2;
    }
    try
    {
        std::ifstream file(std::string(argv[1]) + "/det.net");
        if (!file) throw std::runtime_error("cannot read detector netlist");
        std::stringstream text; text << file.rdbuf();
        std::ofstream csv(argv[2]);
        if (!csv) throw std::runtime_error("cannot create capture");
        csv << std::setprecision(17)
            << "case,time_seconds,input_volts,reference_led_amps,prototype_led_amps,reference_gr_db,prototype_gr_db\n";
        constexpr double rate = 48000.0;
        int failures = 0;
        for (int scenario = 0; scenario < 6; ++scenario)
        {
            using namespace tubechannel;
            channel::ChannelControls controls;
            controls.attack = scenario % 3 == 0 ? 0.0 : scenario % 3 == 1 ? 0.5 : 1.0;
            controls.release = scenario % 3 == 0 ? 0.0 : scenario % 3 == 1 ? 0.5 : 1.0;
            controls.autoDetect = scenario >= 3;
            circuit::Block reference;
            circuit::ToleranceSettings tol;
            tol.enabled = false;
            reference.build(text.str(), 1.0 / rate, tol, { "VP", "VN" }, { { "O6B", 10.0 } });
            reference.c.guardJunctionUpdates = true;
            reference.c.maxIter = 50;
            reference.c.extrapOrder = 0;
            const double wiper = channel::ChannelModel::thresholdWiper(controls.threshold);
            const double ratio = channel::ChannelModel::ratioPotOhms(controls.ratio);
            const double attack = 50000.0 * (std::pow(10.0, 2.0 * controls.attack) - 1.0) / 99.0;
            reference.setParam("THRW", wiper); reference.setParam("RATR", ratio);
            reference.setParam("ATTR", attack); reference.setParam("RELW", controls.release);
            reference.setParam("AUTO", controls.autoDetect ? 1.0 : 0.0);
            reference.applyParams(); reference.setRails(15.0, -15.0, 45.0, 150.0); reference.setSupply(15.0, -15.0);
            if (!reference.dc()) throw std::runtime_error("reference initialization failed");
            const int input = reference.inputHandle("O6B"), led = reference.resistorHandle("RLED");
            hybrid::Detector candidate;
            candidate.prepare(rate, {});
            candidate.configureElectrical(wiper, ratio, attack, controls.release, controls.autoDetect);
            channel::Vactrol opticalReference, opticalCandidate;
            opticalReference.prepare(rate); opticalCandidate.prepare(rate);
            double squaredError = 0.0, squaredReference = 0.0, maxGrError = 0.0;
            for (int i = 0; i < int(2.0 * rate); ++i)
            {
                const double t = double(i) / rate;
                const double envelope = (t >= 0.1 && t < 0.5) || (t >= 0.9 && t < 1.1) ? 1.0 : 0.0;
                const double vin = 1.8 * envelope * std::sin(6.283185307179586 * 1000.0 * t);
                reference.setSourceH(input, vin);
                if (!reference.step()) ++failures;
                const double a = reference.c.resistorCurrent(led), b = candidate.process(vin);
                opticalReference.process(a); opticalCandidate.process(b);
                const double grA = opticalReference.gainReductionDb(), grB = opticalCandidate.gainReductionDb();
                if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(grA) || !std::isfinite(grB)) ++failures;
                squaredError += (b - a) * (b - a); squaredReference += a * a;
                maxGrError = std::max(maxGrError, std::fabs(grB - grA));
                if (i % 16 == 0) csv << scenario << ',' << t << ',' << vin << ',' << a << ',' << b << ',' << grA << ',' << grB << '\n';
            }
            std::printf("case %d: attack %.1f release %.1f %s; LED relative error %.2f dB, max optical GR difference %.3f dB\n",
                        scenario, controls.attack, controls.release, controls.autoDetect ? "Auto" : "manual",
                        10.0 * std::log10(squaredError / std::max(squaredReference, 1e-30) + 1e-30), maxGrError);
        }
        std::printf("reference/model finite or convergence failures: %d; accuracy is reported, not certified\n", failures);
        return failures ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
