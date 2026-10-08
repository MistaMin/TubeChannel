#include "Channel/ChannelModel.h"
#include "Hybrid/DefaultLinearCalibration.h"
#include <fstream>
#include <sstream>
#include <cstdio>

static std::string slurp(const std::string& p) { std::ifstream f(p); if(!f)throw std::runtime_error("missing netlist");std::stringstream s;s<<f.rdbuf();return s.str(); }
int main(int argc,char**argv)
{
 if(argc!=2)return 2;
 try
 {
  using namespace tubechannel;std::string d=argv[1];channel::NetlistSet n;
  n.mic=slurp(d+"/mic.net");n.inst=slurp(d+"/inst.net");n.tube=slurp(d+"/tube.net");n.preout=slurp(d+"/preout.net");
  n.comp=slurp(d+"/comp.net");n.det=slurp(d+"/det.net");n.eq1=slurp(d+"/eq1.net");n.eq2=slurp(d+"/eq2.net");
  n.eq3=slurp(d+"/eq3.net");n.eq4=slurp(d+"/eq4.net");n.out=slurp(d+"/out.net");n.psu=slurp(d+"/psu.net");
  channel::ChannelModel m;m.prepare(96000.,n,1,false);
  const int ins[]={m.eq1.inputHandle("COUT"),m.eq2.inputHandle("X"),m.eq3.inputHandle("O10A"),m.eq4.inputHandle("O8A")};
  const int outs[]={m.eq1.opampHandle("A10B"),m.eq2.opampHandle("A10A"),m.eq3.opampHandle("A8A"),m.eq4.opampHandle("A8C")};
  circuit::Block* blocks[]={&m.eq1,&m.eq2,&m.eq3,&m.eq4};
  const int outputIn=m.out.inputHandle("O8C"),dryIn=m.out.inputHandle("EQIN"),p=m.out.nodeId("OUTP"),nn=m.out.nodeId("OUTN");
  int failures=0;
  for(int k=0;k<5;++k)
  {
   hybrid::Equalizer eq;hybrid::Output output;
   if(!eq.configure(hybrid::DefaultLinearCalibration::equalizer())||!output.configure(hybrid::DefaultLinearCalibration::output()))
    throw std::runtime_error("unstable fitted profile");
   for(int j=0;j<4;++j){blocks[j]->setSourceH(ins[j],0);if(!blocks[j]->dc())++failures;}
   m.out.setSourceH(outputIn,0);m.out.setSourceH(dryIn,0);if(!m.out.dc())++failures;
   constexpr double frequencies[]={37.,997.,4001.,14003.,997.};
   const double amplitude=k==4?17.:k%2?2.:0.25;
   double signal=0,error=0,eqSignal=0,eqError=0;int count=0;
   for(int i=0;i<96000;++i)
   {
    const double t=double(i)/96000.;
    const double env=t<.1?0.:t<.6?1.:0.;
    const double vin=amplitude*env*std::sin(6.283185307179586*frequencies[k]*t);
    double x=vin;
    for(int j=0;j<4;++j){blocks[j]->setSourceH(ins[j],x);if(!blocks[j]->step())++failures;x=blocks[j]->opampOutH(outs[j]);}
    const double dry=m.eq1.node("EQIN");m.out.setSourceH(outputIn,x);m.out.setSourceH(dryIn,dry);
    if(!m.out.step())++failures;
    const double ref=m.out.nodeV(p)-m.out.nodeV(nn);const auto taps=eq.process(vin);const double fitted=output.process(taps.wet);
    if(!std::isfinite(fitted)||!std::isfinite(ref))++failures;
    if(t>=.1){signal+=ref*ref;error+=(fitted-ref)*(fitted-ref);eqSignal+=x*x;eqError+=(taps.wet-x)*(taps.wet-x);++count;}
   }
   const double edb=10.*std::log10(eqError/std::max(eqSignal,1e-30)+1e-30),odb=10.*std::log10(error/std::max(signal,1e-30)+1e-30);
   std::printf("%.0f Hz %.2f V: EQ error %.3f dB relative RMS, EQ+output %.3f dB %s\n",frequencies[k],amplitude,edb,odb,k==4?"(overload diagnostic; linear profile not accepted here)":"");
   if(k<4&&(edb>-35||odb>-35))++failures;
  }
  std::printf("linear holdout failures %d\n",failures);return failures?1:0;
 }
 catch(const std::exception&e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
