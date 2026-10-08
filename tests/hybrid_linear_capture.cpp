// Small-signal stage measurements for fitted filter profiles.
#include "Channel/ChannelModel.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstdio>

static std::string slurp(const std::string& p) { std::ifstream f(p); if (!f) throw std::runtime_error("cannot read netlist"); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try
    {
        using namespace tubechannel;
        channel::NetlistSet n;
        std::string d=argv[1];
        n.mic=slurp(d+"/mic.net");n.inst=slurp(d+"/inst.net");n.tube=slurp(d+"/tube.net");n.preout=slurp(d+"/preout.net");
        n.comp=slurp(d+"/comp.net");n.det=slurp(d+"/det.net");n.eq1=slurp(d+"/eq1.net");n.eq2=slurp(d+"/eq2.net");
        n.eq3=slurp(d+"/eq3.net");n.eq4=slurp(d+"/eq4.net");n.out=slurp(d+"/out.net");n.psu=slurp(d+"/psu.net");
        channel::ChannelModel m; m.prepare(96000.0,n,1,false);m.setNoiseEnabled(false);m.setPsuEnabled(false);
        circuit::Block* blocks[]={&m.eq1,&m.eq2,&m.eq3,&m.eq4,&m.out};
        const char* inputs[]={"COUT","X","O10A","O8A","O8C"};
        const char* outputs[]={"A10B","A10A","A8A","A8C","OUTP"};
        std::ofstream csv(argv[2]); if (!csv) return 2;
        csv << std::setprecision(17) << "stage,sample,input_volts,output_volts,dry_volts\n";
        for (int stage=0;stage<5;++stage)
        {
            auto& b=*blocks[stage];const int input=b.inputHandle(inputs[stage]);const int output=stage<4?b.opampHandle(outputs[stage]):b.nodeId(outputs[stage]);
            const int dry=stage==0?b.nodeId("EQIN"):0;const int negative=stage==4?b.nodeId("OUTN"):0;
            b.setSourceH(input,0);if(stage==4)b.setInput("EQIN",0);
            if(!b.dc())throw std::runtime_error("stage initialization failed");
            for(int i=0;i<65536;++i)
            {
                const double vin=i==0?0.001:0;b.setSourceH(input,vin);
                if(!b.step())throw std::runtime_error("stage solve failed");
                const double y=stage<4?b.opampOutH(output):b.nodeV(output)-b.nodeV(negative);
                csv<<stage<<','<<i<<','<<vin<<','<<y<<','<<(dry?b.nodeV(dry):0)<<'\n';
            }
            std::printf("captured stage %d at 96000 Hz, default controls, no tolerances\n",stage);std::fflush(stdout);
        }
    }
    catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
