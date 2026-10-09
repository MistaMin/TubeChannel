// Reproducible synthetic drums/bass/chords for listening; no private recordings.
#include "Plugin/Chain.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
static std::string read(const std::string&p){std::ifstream f(p);if(!f)throw std::runtime_error(p);std::stringstream s;s<<f.rdbuf();return s.str();}
static void u16(std::ostream& s,unsigned v){char b[]={char(v),char(v>>8)};s.write(b,2);}
static void u32(std::ostream& s,unsigned v){char b[]={char(v),char(v>>8),char(v>>16),char(v>>24)};s.write(b,4);}
static double noise(unsigned n){n=n*747796405u+2891336453u;n=(n^(n>>13))*277803737u;n^=n>>16;return double(n)/2147483648.-1.;}
int main(int argc,char**argv){
 if(argc!=3)return 2;using namespace tubechannel;channel::NetlistSet nets;std::string dir=argv[1];
#define LOAD(s) nets.s=read(dir+"/" #s ".net")
 LOAD(mic);LOAD(inst);LOAD(tube);LOAD(preout);LOAD(comp);LOAD(det);LOAD(eq1);LOAD(eq2);LOAD(eq3);LOAD(eq4);LOAD(out);LOAD(psu);
#undef LOAD
 std::filesystem::create_directories(argv[2]);constexpr int rate=48000,frames=9*rate;
 for(auto quality:{dsp::Quality::Default,dsp::Quality::Eco}){
  tc::ChainConfig cfg;cfg.quality=quality;cfg.sampleRate=rate;cfg.maxBlock=256;cfg.tolerances=false;
  tc::Chain chain;chain.prepare(cfg,nets);tc::ChainGlobals g;g.noise=false;g.psuModel=false;g.outputTrimDb=-12;
  channel::ChannelControls c;c.preOut=.75;c.release=.8;
  channel::Telemetry meters[2];channel::Telemetry* tel[]={&meters[0],&meters[1]};float l[256],r[256];float* data[]={l,r};std::vector<float> audio;audio.reserve(frames*2);double peak=0;
  for(int start=0;start<frames;start+=256){int n=std::min(256,frames-start);c.gain=start>=3*rate&&start<6*rate?.22:0;c.gainPlus20=start>=6*rate;c.plateHigh=start<6*rate;
   for(int i=0;i<n;++i){const int sample=start+i;const double t=double(sample)/rate,beat=std::fmod(t,.5),snare=std::fmod(t+.5,1.),hat=std::fmod(t,.25),note=std::fmod(t,.5);
    const double kick=.45*std::exp(-beat*18)*std::sin(6.283185307179586*(55*beat+5*(1-std::exp(-beat*20))));
    const double percussion=.10*std::exp(-snare*25)*noise(unsigned(sample))+.018*std::exp(-hat*100)*(noise(unsigned(sample))-noise(unsigned(sample-1)));
    const double notes[]={82.4069,110.,73.4162,98.};const double f=notes[(sample/(rate/2))%4];double bass=0;for(int h=1;h<=6;++h)bass+=std::sin(6.283185307179586*f*h*t)/h;
    const double bed=.045*std::min(1.,note/.008)*std::exp(-note*2)*bass;
    const double envelope=.025*std::min(1.,std::fmod(t,2.)/.02)*std::exp(-std::fmod(t,2.)*.8);
    const double chord=std::sin(6.283185307179586*146.8324*t)+std::sin(6.283185307179586*184.9972*t)+std::sin(6.283185307179586*220*t);
    l[i]=float(.5*(kick+percussion+bed+envelope*chord));r[i]=float(.5*(kick+percussion+bed+envelope*(chord+.3*std::sin(6.283185307179586*293.6648*t))));
   }
   chain.process(data,n,nullptr,0,c,c,g,tel);
   for(int i=0;i<n;++i)for(int ch=0;ch<2;++ch){if(!std::isfinite(data[ch][i]))return 1;peak=std::max(peak,std::fabs(double(data[ch][i])));audio.push_back(data[ch][i]);}
  }
  const std::string name=quality==dsp::Quality::Eco?"eco.wav":"reference.wav";std::ofstream wav(std::string(argv[2])+"/"+name,std::ios::binary);unsigned bytes=unsigned(audio.size()*4);
  wav.write("RIFF",4);u32(wav,36+bytes);wav.write("WAVEfmt ",8);u32(wav,16);u16(wav,3);u16(wav,2);u32(wav,rate);u32(wav,rate*8);u16(wav,8);u16(wav,32);wav.write("data",4);u32(wav,bytes);
  for(float value:audio)u32(wav,std::bit_cast<unsigned>(value));
  std::printf("%s: 9 s synthetic material, matched -12 dB output trim, peak %.6f\n",name.c_str(),peak);
  if(!wav||peak>1.)return 1;
 }
 return 0;
}
