// Holdout complete-channel diagnostics. Fidelity failures remain visible; they
// are separate from engineering safety and CPU acceptance.
#include "Plugin/Chain.h"
#include <fstream>
#include <sstream>
#include <cstdio>
static std::string read(const std::string&p){std::ifstream f(p);std::stringstream s;s<<f.rdbuf();return s.str();}
int main(int argc,char**argv){
 if(argc!=3)return 2;using namespace tubechannel;channel::NetlistSet nets;std::string dir=argv[1];
#define LOAD(s) nets.s=read(dir+"/" #s ".net")
 LOAD(mic);LOAD(inst);LOAD(tube);LOAD(preout);LOAD(comp);LOAD(det);LOAD(eq1);LOAD(eq2);LOAD(eq3);LOAD(eq4);LOAD(out);LOAD(psu);
#undef LOAD
 std::ofstream csv(argv[2]);csv<<"rate,fixture,mode,frequency,amplitude,input_gain,plate,boost,rms,fundamental,h2,h3,h4,h5,imd_lower,imd_upper,gr_db,relative_error_db\n";
 int nonfinite=0;
 for(double rate:{48000.,96000.})for(int fixture=0;fixture<20;++fixture){
  const double freq=fixture==0?37.:fixture==1?4001.:fixture==2||fixture==18||fixture==19?14003.:997.;
  const double amp=fixture==19?.02:fixture==17?.25:fixture<3?.01:fixture==3?.126:fixture==4?.8:fixture==5?.126:fixture==6?.126:fixture==7?.126:fixture==8?.02:.8;
  channel::ChannelControls c;c.gain=fixture==5?.35:0;c.plateHigh=fixture!=6;c.gainPlus20=fixture==7;
  c.instrument=fixture==8;c.attack=c.release=fixture==9?1.:.5;c.autoDetect=fixture==10;c.externalSidechain=fixture==11;
  if(fixture==12){c.low=1;c.high=0;c.lmLevel=.75;c.hmLevel=.25;c.lmX10=c.hmX10=true;}
  c.limiter=fixture==13;
  if(fixture==18)c.gainPlus20=true;
  if(fixture==19){c.instrument=true;c.instSourceOhms=100000.;c.compActive=false;}
  if(fixture==14){c.low=.73;c.high=.21;c.hmLevel=.83;c.hmFreq=.41;c.lmLevel=.19;c.lmFreq=.67;c.hmX10=c.lmX10=true;}
  if(fixture==15){c.compActive=false;c.lowCut=.75;c.preOut=.7;c.master=.6;}
  if(fixture==16){c.compActive=false;c.instrument=true;c.gain=.5;c.impedance=.9;}

  std::vector<float> reference;double rmsReference=0;
  for(auto quality:{dsp::Quality::Default,dsp::Quality::Eco}){
   tc::ChainConfig cfg;cfg.sampleRate=rate;cfg.maxBlock=256;cfg.channels=1;cfg.tolerances=false;cfg.quality=quality;
   tc::Chain chain;chain.prepare(cfg,nets);tc::ChainGlobals g;g.noise=false;g.psuModel=false;
   channel::Telemetry t;channel::Telemetry* tel[]={&t};float data[256],sc[256];float* audio[]={data};const float* side[]={sc};
   std::vector<float> y;const int frames=int(rate*1.5),first=int(rate*.5);y.reserve(size_t(frames-first));
   for(int start=0;start<frames;start+=256){int n=std::min(256,frames-start);
    for(int i=0;i<n;++i){double time=(start+i)/rate;double env=fixture>=9&&fixture<=11?(std::fmod(time,.6)<.3?1.:.05):1.;data[i]=float(amp*env*std::sin(6.283185307179586*freq*time));if(fixture==17)data[i]+=float(amp*std::sin(6.283185307179586*3371*time));sc[i]=float(.8*std::sin(6.283185307179586*337*time));}
    chain.process(audio,n,side,1,c,c,g,tel);for(int i=0;i<n;++i)if(start+i>=first)y.push_back(data[i]);
   }
   double sum=0,error=0,cosines[7]{},sines[7]{};
   for(size_t i=0;i<y.size();++i){if(!std::isfinite(y[i]))++nonfinite;sum+=y[i]*y[i];if(quality==dsp::Quality::Eco)error+=(y[i]-reference[i])*(y[i]-reference[i]);for(int h=1;h<=7;++h){double hz=h<=5?h*freq:h==6?1377.:5745.;double ph=6.283185307179586*hz*(double(i)+first)/rate;cosines[h-1]+=y[i]*std::cos(ph);sines[h-1]+=y[i]*std::sin(ph);}}
   if(quality==dsp::Quality::Default){reference=y;rmsReference=sum;}
   csv<<rate<<','<<fixture<<','<<int(quality)<<','<<freq<<','<<amp<<','<<c.gain<<','<<c.plateHigh<<','<<c.gainPlus20<<','<<std::sqrt(sum/y.size());
   for(int h=0;h<7;++h)csv<<','<<2*std::hypot(cosines[h],sines[h])/y.size();
   double residual=quality==dsp::Quality::Eco?10*std::log10(std::max(error/std::max(rmsReference,1e-30),1e-30)):-300;
   csv<<','<<t.grDb.load()<<','<<residual<<'\n';
   std::printf("%.0f Hz fixture %d mode %d RMS %.6f residual %.2f dB\n",rate,fixture,int(quality),std::sqrt(sum/y.size()),residual);std::fflush(stdout);
  }
 }
 return nonfinite?1:0;
}
