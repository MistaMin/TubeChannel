// Offline circuit measurements. This executable is never linked into the plugin.
#include "Channel/ChannelModel.h"
#include <fstream>
#include <sstream>
#include <cstdio>
#include <filesystem>
#include <iomanip>
static std::string read(const std::string& p) {std::ifstream f(p); if(!f)throw std::runtime_error(p); std::stringstream s;s<<f.rdbuf();return s.str();}
int main(int argc,char**argv) {
 if(argc!=3)return 2;
 using namespace tubechannel;channel::NetlistSet n;std::string d=argv[1],dest=argv[2];
#define LOAD(s) n.s=read(d+"/" #s ".net")
 LOAD(mic);LOAD(inst);LOAD(tube);LOAD(preout);LOAD(comp);LOAD(det);LOAD(eq1);LOAD(eq2);LOAD(eq3);LOAD(eq4);LOAD(out);LOAD(psu);
#undef LOAD
 std::filesystem::create_directories(dest);
 channel::ChannelModel m;m.prepare(96000,n,1,false);m.setNoiseEnabled(false);m.setPsuEnabled(false);
 circuit::Block* bs[]={&m.mic,&m.inst,&m.tube,&m.preout,&m.comp,&m.eq1,&m.eq2,&m.eq3,&m.eq4,&m.out,&m.inst};
 const char* ins[]={"SP","O1","O13B","V1OUT","J6T","COUT","X","O10A","O8A","O8C","ISRC"};
 const char* outs[]={"A1A","A13B","V1OUT","J6T","COUT","A10B","A10A","A8A","A8C","OUTP","A13B"};
 std::ofstream rails(dest+"/rails.txt");rails<<std::setprecision(17)<<m.psu.p15()<<' '<<m.psu.n15()<<' '<<m.psu.p25()<<' '<<m.psu.p45()<<' '<<m.psu.p75()<<' '<<m.psu.p150()<<' '<<m.psu.tubeP()<<' '<<m.psu.heaterCurrent()<<'\n';
 std::ofstream meta(dest+"/fixtures.csv");meta<<"id,stage,level,freq,range,narrow,plate,boost,amplitude\n";int id=0;
 auto capture=[&](int stage,double level,double freq,int range,int narrow,int plate,int boost,double amp,bool curve){
  auto& b=*bs[stage];
  b.setParam("PREOUT",1);b.setParam("MASTER",.8);b.setParam("CMPOUT",(std::pow(10.,1.76)-1)/99.);
  b.setParam("INST",stage==10?1:0);if(stage==10)b.setParam("GAINPOT",4990./(std::pow(10.,3.5/20)-1)-100);b.setParam("LOW",stage==6?level:.5);b.setParam("HIGH",stage==6?freq:.5);
  b.setParam("HMID",stage==7?level:.5);b.setParam("LMID",stage==8?level:.5);
  b.setParam("LOSH",stage==6?(range>>1):1);b.setParam("HISH",stage==6?(range&1):0);b.setParam("HMLO",1-range);b.setParam("LMLO",1-range);
  b.setParam("HMNAR",narrow);b.setParam("LMNAR",narrow);
  const double ff=(108200*std::pow(8200./108200.,freq)-8200)/100000;
  b.setParam("HMF",ff);b.setParam("LMF",ff);b.setParam("HV",plate);b.setParam("TUBE20",boost);b.applyParams();
  if(curve){b.c.maxIter=64;b.c.guardJunctionUpdates=true;}
  b.setRails(m.psu.p15(),m.psu.n15(),m.psu.p45(),plate?m.psu.p150():m.psu.p45());b.setSupply(m.psu.p15(),m.psu.n15());
  int input=b.inputHandle(ins[stage]);int negative=stage==0?b.inputHandle("SN"):0;
  int out=(stage==0||stage==1||stage==5||stage==6||stage==7||stage==8||stage==10)?b.opampHandle(outs[stage]):b.nodeId(outs[stage]);
  auto output=[&](){return (stage==0||stage==1||stage==5||stage==6||stage==7||stage==8||stage==10)?b.opampOutH(out):b.nodeV(out)-(stage==9?b.node("OUTN"):0);};
  auto set=[&](double x){b.setSourceH(input,stage==0?.5*x:x);if(stage==0)b.setSourceH(negative,-.5*x);};
  if(stage==9)b.setInput("EQIN",0);
  set(0);const bool dcOk=b.dc();int failedSteps=0;
  const double offset=output();
  std::ofstream f(dest+"/"+std::to_string(id)+(curve?"-curve.bin":"-impulse.bin"),std::ios::binary);
  std::ofstream valid(dest+"/"+std::to_string(id)+"-valid.bin",std::ios::binary);
  const int count=curve?24576:65536;
  for(int i=0;i<count;++i){
   double x=curve?amp*std::sin(6.283185307179586*1000*i/96000.):(i==0?.001:0);
   set(x);const bool converged=b.step();if(!converged)++failedSteps;const char ok=converged?1:0;valid.write(&ok,1);const double y=output()-offset;
   if(!std::isfinite(y))throw std::runtime_error("nonfinite reference capture");
   f.write(reinterpret_cast<const char*>(&x),sizeof(double));f.write(reinterpret_cast<const char*>(&y),sizeof(double));
  }
  meta<<id<<','<<stage<<','<<level<<','<<freq<<','<<range<<','<<narrow<<','<<plate<<','<<boost<<','<<(curve?amp:0)<<'\n';meta.flush();
  std::printf("%d stage %d %s: DC %s, unconverged steps %d\n",id,stage,curve?"curve":"impulse",dcOk?"ok":"FAILED",failedSteps);std::fflush(stdout);++id;
 };
 for(int s=0;s<11;++s){
  if(s==2){for(int p=0;p<2;++p)for(int boost=0;boost<2;++boost){capture(s,.5,.5,1,1,p,boost,0,false);for(double amp:{.25,.5,1.,2.,4.,8.,16.})capture(s,.5,.5,1,1,p,boost,boost?amp/8.:amp,true);}}
  else if(s==6||s==7||s==8){
   for(int range=0;range<(s==6?4:2);++range)for(int narrow=0;narrow<(s==6?1:2);++narrow)
    for(double lev:{0.,.25,.5,.75,1.})for(double fr:{0.,.25,.5,.75,1.})capture(s,lev,fr,range,narrow,1,0,0,false);
  } else {capture(s,.5,.5,1,1,1,0,0,false);if(s==0||s==4||s==9||s==10)for(double amp:{.25,.5,1.,2.,4.,8.,16.})capture(s,.5,.5,1,1,1,0,amp,true);}
 }
 return meta?0:1;
}
