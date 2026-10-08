#include "Channel/PowerSupply.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <iomanip>

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    std::ifstream file(std::string(argv[1]) + "/psu.net"); std::stringstream text; text << file.rdbuf();
    if (!file) return 2;
    tubechannel::circuit::ToleranceSettings tol; tol.enabled = false;
    tubechannel::channel::PowerSupply supply;
    supply.prepare(text.str(), {}, tol); supply.settle(0.8, true);
    std::ofstream csv(argv[2]); if (!csv) return 2;
    csv << std::setprecision(17) << "time_seconds,phase,load_p,load_n,load_aux,load_plate,p,n,raw,aux,multiplier,high,plate\n";
    for (int i = 0; i < 64000; ++i)
    {
        const double t = double(i) / 8000.0;
        const double elevated = (t >= 1 && t < 3) || (t >= 5 && t < 6.5) ? 1.0 : 0.0;
        const double p = 0.075 + 0.04 * elevated, n = 0.075 + 0.04 * elevated;
        const double aux = 0.012 + 0.004 * elevated, plate = 0.006 + 0.006 * elevated;
        supply.step(p, n, aux, plate, true);
        if (!std::isfinite(supply.p15()) || !std::isfinite(supply.tubeP())) return 1;
        csv << t << ',' << supply.mainsPhase() << ',' << p << ',' << n << ',' << aux << ',' << plate << ','
            << supply.p15() << ',' << supply.n15() << ',' << supply.p25() << ',' << supply.p45() << ','
            << supply.p75() << ',' << supply.p150() << ',' << supply.tubeP() << '\n';
    }
}
