#include "Plugin/Chain.h"
#include <cstdio>
#include <vector>
#include <cmath>
using namespace tubechannel;
static std::vector<float> capture(int block,double rate,int channels,bool linked,bool dual,bool side,bool supply,
                                  channel::Telemetry* meters)
{
    tc::ChainConfig cfg;cfg.quality=dsp::Quality::Eco;cfg.maxBlock=block;cfg.sampleRate=rate;
    cfg.channels=channels;cfg.tolerances=false;
    tc::Chain chain;chain.prepare(cfg,{}); // No circuit data permitted in this engine.
    tc::ChainGlobals g;g.noise=false;g.psuModel=supply;g.stereoLink=linked;g.dualMono=dual;
    channel::ChannelControls a,b;a.externalSidechain=b.externalSidechain=side;
    if(dual){b.instrument=true;b.gain=.2;b.low=.75;b.hmLevel=.25;}
    std::vector<float> left(size_t(block),0),right(size_t(block),0),sc(size_t(block),0),out;
    float* audio[]={left.data(),right.data()};const float* external[]={sc.data()};
    channel::Telemetry* tel[]={&meters[0],&meters[1]};
    const int frames=int(rate*2.);out.reserve(size_t(frames*channels));
    for(int start=0;start<frames;start+=block){int n=std::min(block,frames-start);
        // Changes happen at fixed sample times, independent of partition size.
        for(int i=0;i<n;++i){double t=double(start+i)/rate;
            left[size_t(i)]=float(t<1.2?.8*std::sin(6.283185307179586*997*t):0);
            right[size_t(i)]=float(t<1.2?.03*std::sin(6.283185307179586*337*t):0);
            sc[size_t(i)]=float(t<1.2?.9*std::sin(6.283185307179586*211*t):0);}
        chain.process(audio,n,side?external:nullptr,side?1:0,a,b,g,tel);
        for(int i=0;i<n;++i)for(int ch=0;ch<channels;++ch)out.push_back(audio[ch][i]);
    }
    return out;
}
int main(){
    int failures=0;
    for(double fs:{44100.,48000.,96000.,192000.}){
        channel::Telemetry a[2],b[2];auto x=capture(32,fs,2,false,false,false,false,a);
        auto y=capture(257,fs,2,false,false,false,false,b);
        if(x!=y)++failures;
        double tail=0;for(size_t i=0;i<x.size();++i){if(!std::isfinite(x[i])||std::fabs(x[i])>16)++failures;if(i>x.size()-size_t(fs*.2))tail=std::max(tail,std::fabs(double(x[i])));}
        if(tail>1e-3)++failures;
        std::printf("%.0f Hz block partition %s, silence tail %.9f\n",fs,x==y?"exact":"FAILED",tail);
    }
    channel::Telemetry linked[2],free[2],side[2],dual[2];
    const auto link=capture(64,48000,2,true,false,false,false,linked);
    const auto unlinked=capture(64,48000,2,false,false,false,false,free);
    const auto external=capture(64,48000,2,false,false,true,false,side);
    const auto dm=capture(64,48000,2,false,true,false,false,dual);
    if(link==unlinked||external==unlinked||dm==unlinked)++failures;
    std::printf("link, sidechain and dual mono produce distinct responses\n");
    // Every continuous control and switch, violent drive, limiter changes,
    // rail transitions, noise and non-divisible block lengths must stay finite.
    for(double fs:{44100.,48000.,96000.,192000.})for(int chs:{1,2}){
        tc::ChainConfig cfg;cfg.quality=dsp::Quality::Eco;cfg.sampleRate=fs;cfg.channels=chs;cfg.maxBlock=127;
        tc::Chain chain;chain.prepare(cfg,{});tc::ChainGlobals g;g.psuModel=true;g.dualMono=true;
        channel::Telemetry m[2];channel::Telemetry* tel[]={&m[0],&m[1]};channel::ChannelControls c,d;
        float l[127],r[127],sc[127];float* audio[]={l,r};const float* ext[]={sc};
        for(int block=0;block<600;++block){double k=.5+.5*std::sin(block*.1);
            c.gain=k;c.impedance=1-k;c.lowCut=k;c.preOut=k;c.compOut=k;c.master=k;
            c.threshold=k;c.ratio=1-k;c.attack=c.release=k;c.low=c.hmLevel=k;c.high=c.lmLevel=1-k;c.hmFreq=c.lmFreq=k;
            c.gainPlus20=block%50<25;c.plateHigh=block<300;c.instrument=block%80<40;c.invert=block%70<35;
            c.eqActive=block%60<30;c.compActive=block%40<20;c.limiter=block%90<45;c.externalSidechain=true;
            c.hmX10=c.lmX10=block%100<50;c.hmNarrow=c.lmNarrow=block%110<55;
            c.loShift40=c.hiShift6k=block%120<60;c.autoDetect=block%150<75;d=c;d.gain=1-k;
            for(int i=0;i<127;++i){l[i]=float(4*std::sin((block*127+i)*.17));r[i]=float(3*std::sin((block*127+i)*.23));sc[i]=float(std::sin((block*127+i)*.29));}
            chain.process(audio,127,ext,1,c,d,g,tel);
            for(int ch=0;ch<chs;++ch)for(int i=0;i<127;++i)if(!std::isfinite(audio[ch][i])||std::fabs(audio[ch][i])>16)++failures;
            if(!std::isfinite(m[0].grDb.load())||!std::isfinite(m[0].tubeP.load()))++failures;
        }
    }
    std::printf("Eco engineering failures %d\n",failures);return failures?1:0;
}
