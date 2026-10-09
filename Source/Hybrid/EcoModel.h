#pragma once

#include "EcoCalibration.h"
#include "Detector.h"
#include "Supply.h"
#include "../Channel/ChannelModel.h"
#include <array>
#include <algorithm>
#include <stdexcept>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace tubechannel::hybrid
{
// Fixed-cost engine. Calibration data is generated offline; this path never
// constructs or steps a circuit, including during preparation and automation.
class EcoModel
{
    using Profile = FilterBank<4>::Profile;
    struct Filter
    {
        Profile current, target;
        std::array<double,4> z1 {}, z2 {};
        void set(const Profile& p, bool immediate)
        {
            target=p; if(immediate)current=p;
        }
        void tick()
        {
            if(current.count==0&&target.count==0)return;
            current.count=std::max(current.count,target.count);
            current.gain+=(target.gain-current.gain)*.35;
            for(size_t i=0;i<4;++i) {
                auto& a=current.sections[i];const auto& b=target.sections[i];
                a.b0+=(b.b0-a.b0)*.35;a.b1+=(b.b1-a.b1)*.35;a.b2+=(b.b2-a.b2)*.35;
                a.a1+=(b.a1-a.a1)*.35;a.a2+=(b.a2-a.a2)*.35;
            }
        }
        double process(double x)
        {
            for(size_t i=0;i<current.count;++i) {
                const auto& c=current.sections[i];double y=c.b0*x+z1[i];
#if defined(__aarch64__)
                // The two independent filter histories share a NEON lane pair.
                const float64x2_t feed = vld1q_f64(&c.b1), feedback = vld1q_f64(&c.a1);
                const float64x2_t history { z2[i], 0.0 };
                auto state = vaddq_f64(vsubq_f64(vmulq_n_f64(feed,x),vmulq_n_f64(feedback,y)),history);
                state = vbslq_f64(vcltq_f64(vabsq_f64(state),vdupq_n_f64(1e-25)),vdupq_n_f64(0),state);
                z1[i]=vgetq_lane_f64(state,0);z2[i]=vgetq_lane_f64(state,1);
#else
                z1[i]=c.b1*x-c.a1*y+z2[i];z2[i]=c.b2*x-c.a2*y;
                if(std::fabs(z1[i])<1e-25)z1[i]=0;if(std::fabs(z2[i])<1e-25)z2[i]=0;
#endif
                x=y;
            }
            return x*current.gain;
        }
    };
    struct Shape
    {
        const ecoData::Curve* curve=nullptr;
        std::array<double,1025> integral {};
        double previous=0,step=1,scale=1,lowSlope=0,highSlope=0,lower=0,upper=0;
        void prepare(const ecoData::Curve& c)
        {
            curve=&c;step=2*c.limit/1024;scale=1/step;previous=0;integral.fill(0);
            lower=c.output.front();upper=c.output.back();
            // These captures end before the microphone/output buffers clip.
            // Continue their measured endpoint slope up to the known 13 V
            // rail limit rather than inventing an early clip at the table edge.
            if(c.stage==0||c.stage==9){
                lowSlope=std::max(0.,(c.output[1]-c.output[0])*scale);
                highSlope=std::max(0.,(c.output[1024]-c.output[1023])*scale);
                lower=-13.;upper=13.;
            }
            for(size_t i=1;i<1025;++i) {
                double x=-c.limit+double(i)*step;
                integral[i]=integral[i-1]+.5*step*(c.output[i-1]+c.output[i]-(2*x-step));
            }
        }
        double residual(double x)const
        {
            if(x<=-curve->limit)return std::max(lower,curve->output.front()+lowSlope*(x+curve->limit))-x;
            if(x>=curve->limit)return std::min(upper,curve->output.back()+highSlope*(x-curve->limit))-x;
            double p=(x+curve->limit)*scale;size_t i=std::min(size_t(p),size_t(1023));
            double t=p-double(i);return curve->output[i]+t*(curve->output[i+1]-curve->output[i])-x;
        }
        double antiderivative(double x)const
        {
            if(x<-curve->limit)return tailIntegral(x,-curve->limit,curve->output.front(),lowSlope,lower,0);
            if(x>curve->limit)return tailIntegral(x,curve->limit,curve->output.back(),highSlope,upper,integral.back());
            double p=(x+curve->limit)*scale;size_t i=std::min(size_t(p),size_t(1023));
            double t=p-double(i),dx=t*step;
            double r=curve->output[i]-(-curve->limit+double(i)*step);
            double slope=(curve->output[i+1]-curve->output[i])*scale-1;
            return integral[i]+dx*(r+.5*slope*dx);
        }
        static double tailIntegral(double x,double origin,double output,double slope,double bound,double base)
        {
            const double delta=x-origin;
            const double crossing=slope>0?(bound-output)/slope:0;
            const double line=slope>0?(delta>0?std::min(delta,crossing):std::max(delta,crossing)):0;
            const double remaining=delta-line;
            const double atEnd=slope>0?bound:output;
            return base+(output-origin)*line+.5*(slope-1)*line*line
                +(atEnd-origin-line)*remaining-.5*remaining*remaining;
        }
        double process(double x)
        {
            // Antialias the nonlinear residual so the calibrated linear path
            // retains its response. Bound overload output after the correction.
            x=std::clamp(x,-256.,256.);double d=x-previous;
            double r=std::fabs(d)>1e-6*(1+std::fabs(x))?
                (antiderivative(x)-antiderivative(previous))/d:residual(.5*(x+previous));
            previous=x;
            return std::clamp(x+r,lower,upper);
        }
    };
public:
    void prepare(double sampleRate,unsigned seed,bool tolerances)
    {
        fs=sampleRate;stride=std::max(1,int(std::lround(fs/12000.)));
        supplyStride=std::max(1,int(std::lround(fs/8000.)));
        controlStride=std::max(1,int(std::lround(fs/1500.)));detector.prepare(fs/stride,{});vactrol.prepare(fs/stride);
        limiter.prepare(fs);noise=channel::Noise(seed);noiseSigma=1.8e-9*std::sqrt(.5*fs);
        variance=tolerances?1.+.005*(double((seed*2654435761u)>>16)/65535.-.5):1.;
        for(const auto& row:ecoData::linear) {
            Profile p=resample(row.filter,fs);
            if(row.stage>=6&&row.stage<=8){
                size_t idx=size_t(row.range*(row.stage==6?25:50)+row.narrow*25+int(std::lround(row.level*4))*5+int(std::lround(row.frequency*4)));
                eq[size_t(row.stage-6)][idx]=p;
            } else if(row.stage==2)tubeProfiles[size_t(row.plate*2+row.boost)]=p;
            else fixed[size_t(row.stage)]=p;
        }
        for(size_t i=0;i<ecoData::curves.size();++i){
            shapes[i].prepare(ecoData::curves[i]);const auto& r=ecoData::curves[i];
            if(r.stage==2)tubeShapes[size_t(r.plate*2+r.boost)]=i;else shapeForStage[size_t(r.stage)]=i;
        }
        for(size_t i=0;i<filters.size();++i)filters[i].set(fixed[i],true);

        configure({},true);
        Supply::Snapshot anchor;
        for(size_t i=0;i<7;++i)anchor.volts[i]=ecoData::rails[i];anchor.heaterCurrent=ecoData::rails[7];
        supply.prepare(fs/supplyStride,anchor,true,{});
        meterCoef=1-std::exp(-1/(.3*fs));peakCoef=std::exp(-1/(.2*fs));
        compMix=1;eqMix=1;
    }
    void configure(const channel::ChannelControls& c,bool immediate=false)
    {
        if(!immediate&&c==controls)return;
        controls=c;
        const size_t tubeIndex=size_t(c.plateHigh*2+c.gainPlus20);
        filters[2].set(tubeProfiles[tubeIndex],immediate);
        filters[1].set(c.instrument?fixed[10]:fixed[1],immediate);
        const size_t nextShape=tubeShapes[tubeIndex];
        if(nextShape!=tubeShape){
            oldTubeShape=tubeShape;tubeShape=nextShape;tubeFade=immediate?1.:0.;
            shapes[tubeShape].previous=lastTubeLinear;
        }
        if(c.instrument!=wasInstrument){shapes[shapeForStage[10]].previous=lastBufferLinear;wasInstrument=c.instrument;}
        filters[6].set(interpolate(eq[0],c.low,c.high,int(c.loShift40)*2+int(c.hiShift6k),0,25),immediate);
        filters[7].set(interpolate(eq[1],c.hmLevel,c.hmFreq,int(c.hmX10),int(c.hmNarrow),50),immediate);
        filters[8].set(interpolate(eq[2],c.lmLevel,c.lmFreq,int(c.lmX10),int(c.lmNarrow),50),immediate);
        double gain=std::pow(10.,(c.instrument?30.5:40.6)*c.gain/20.);
        auto loadAt=[](double k){double pot=150.+5000.*(std::pow(10.,2*k)-1)/99.;return 1./(1./pot+1./9980.+1./6686.6666667);};
        const double imp=loadAt(c.impedance),nominal=loadAt(.4);
        constexpr double instLoad=1./(1./1e6+1./5.1e6);
        gain*=c.instrument?(instLoad+10000.)/(instLoad+std::max(1.,c.instSourceOhms)):
            (imp/(imp+std::max(1.,c.micSourceOhms)))/(nominal/(nominal+150.));
        Profile source;
        if(c.instrument&&std::fabs(c.instSourceOhms-10000.)>1e-6){
            // The instrument pickup sees R224 || R225 and C85 through R223.
            // Correct the measured 10 kohm profile for the chosen source, at
            // the reference integrator cadence before converting session rate.
            const double nominalTau=100e-12*(4990.+10000.*instLoad/(10000.+instLoad));
            const double actualTau=100e-12*(4990.+c.instSourceOhms*instLoad/(c.instSourceOhms+instLoad));
            const double dt=1./96000.,angle=3.141592653589793*14000.*dt,kappa=angle/std::tan(angle);
            const double norm=.55*dt+kappa*actualTau;
            Section a{(.55*dt+kappa*nominalTau)/norm,(.45*dt-kappa*nominalTau)/norm,0,(.45*dt-kappa*actualTau)/norm,0};
            const double u=(96000.-fs)/(96000.+fs),den=1+a.a1*u;
            source.count=1;source.sections[0]={(a.b0+a.b1*u)/den,(2*a.b0*u+a.b1*(1+u*u))/den,(a.b0*u*u+a.b1*u)/den,(2*u+a.a1*(1+u*u))/den,(u*u+a.a1*u)/den};
        }
        sourceCompensation.set(source,immediate);
        gainTarget=gain*variance;
        preTarget=loadedPot(c.preOut,20000.,8200.);
        compTarget=loadedPot((std::pow(10.,2*c.compOut)-1)/99.,20000.,20000.)/loadedPot((std::pow(10.,1.76)-1)/99.,20000.,20000.);
        masterTarget=loadedPot(c.master,20000.,8200.)/loadedPot(.8,20000.,8200.);
        // Ratio of the new and measured low-cut responses; the measured buffer
        // already includes the default 11.2 Hz pole.
        double k=2*fs,w=6.283185307179586*11.2*std::pow(250./11.2,c.lowCut),base=6.283185307179586*11.2;
        Profile cut;cut.count=1;cut.sections[0]={(k+base)/(k+w),(base-k)/(k+w),0,(w-k)/(w+k),0};
        lowCut.set(cut,immediate);
        detector.configure(c);limiter.setCeiling(1.096*std::pow(10.,(-20.+40.*c.threshold)/20.));limiter.setRelease(c.limiterRelease);
        if(c.limiter&&!wasLimiter){limiter.reset();vactrol.reset();gr=1;grStep=0;}wasLimiter=c.limiter;
        if(immediate){gainValue=gainTarget;preValue=preTarget;compValue=compTarget;masterValue=masterTarget;}
    }
    void globals(bool n,bool p,double hz,double scale){noiseOn=n;psuOn=p;mainsHz=hz;mainsScale=scale;}
    void attach(channel::Telemetry* t){telemetry=t;}
    double detectorLevel()const{return lastDetector;}
    double process(double x,double sidechain,bool linked,double other)
    {
        if(++controlCount>=controlStride){
            controlCount=0;for(auto& f:filters)f.tick();lowCut.tick();sourceCompensation.tick();
            gainValue+=(gainTarget-gainValue)*.35;preValue+=(preTarget-preValue)*.35;
            compValue+=(compTarget-compValue)*.35;masterValue+=(masterTarget-masterValue)*.35;
        }
        if(noiseOn)x+=noiseSigma*noise.next();
        x*=gainValue;
        if(!controls.instrument)x=shapes[shapeForStage[0]].process(filters[0].process(x));
        x=sourceCompensation.process(x);x=filters[1].process(x);lastBufferLinear=x;
        if(controls.instrument)x=shapes[shapeForStage[10]].process(x);
        x=lowCut.process(x);x=filters[2].process(x);lastTubeLinear=x;
        const double shaped=shapes[tubeShape].process(x);
        if(tubeFade<1.){
            const double old=shapes[oldTubeShape].process(x);tubeFade=std::min(1.,tubeFade+1./(.003*fs));
            x=old+(shaped-old)*tubeFade;
        }else x=shaped;
        if(controls.invert)x=-x;
        double pre=filters[3].process(x*preValue);
        double detectorInput=controls.externalSidechain?channel::ChannelModel::kSidechainMatch*sidechain:pre*gr*(1e6/(1e6+25000.*controls.vactrolCal+2210.));
        lastDetector=std::fabs(detectorInput);if(linked)detectorInput=std::max(std::fabs(detectorInput),other);
        detSum+=std::fabs(detectorInput);
        if(++detCount>=stride) {
            detCount=0;double led=detector.process(detSum/stride);detSum=0;
            double r=vactrol.process(controls.compActive&&!controls.limiter?led:0);
            double series=25000.*controls.vactrolCal+2210.;
            double target=r/(r+series)/(1e6/(1e6+series));grStep=(target-gr)/stride;

        }
        if(psuOn&&++supplyCount>=supplyStride){
            supplyCount=0;supply.step({.075+.0001*std::fabs(pre),.075,.012,.006+.00003*std::fabs(x)},controls.plateHigh,mainsHz,mainsScale);
            if(telemetry){const auto& rails=supply.snapshot().volts;int w=telemetry->scopeWrite.load(std::memory_order_relaxed);
                telemetry->scope25[w].store(float(rails[2]),std::memory_order_relaxed);telemetry->scope150[w].store(float(rails[6]),std::memory_order_relaxed);telemetry->scopeLoad[w].store(.075f,std::memory_order_relaxed);
                telemetry->scopeWrite.store((w+1)%channel::Telemetry::kScope,std::memory_order_release);}
        }
        gr+=grStep;
        double active=shapes[shapeForStage[4]].process(filters[4].process(pre*gr))*compValue;
        compMix+=((controls.compActive?1.:0.)-compMix)*.02;
        double compressed=pre+(active-pre)*compMix;
        if(controls.limiter)compressed=limiter.process(compressed);
        double equalized=compressed;for(size_t band=5;band<9;++band)equalized=filters[band].process(equalized);
        eqMix+=((controls.eqActive?1.:0.)-eqMix)*.02;
        double eqOut=compressed+(equalized-compressed)*eqMix;
        double y=shapes[shapeForStage[9]].process(filters[9].process(eqOut*masterValue));
        // The reference rails modulate headroom; reduced supply preserves this
        // interaction without a runtime circuit solve. Freeze state when off.
        y*=std::clamp(supply.snapshot().volts[Supply::Positive]/ecoData::rails[0],.75,1.1);
        const double taps[]={pre,compressed,eqOut,y};for(size_t i=0;i<4;++i)tapMs[i]+=(taps[i]*taps[i]-tapMs[i])*meterCoef;
        peak=std::max(peak*peakCoef,std::fabs(y));drive=std::max(drive*peakCoef,std::fabs(x));return y;
    }
    void publish(channel::Telemetry& t)const
    {
        t.outPeak.store(float(peak));t.tubeDrive.store(float(drive));t.clip.store(peak>12.5?1.f:0.f);
        t.grDb.store(float(controls.limiter?limiter.gainReductionDb():vactrol.gainReductionDb(12500.*controls.vactrolCal+2210.)));
        for(size_t i=0;i<4;++i)t.vuTap[i].store(float(std::sqrt(tapMs[i])));
        const auto& s=supply.snapshot();t.p15.store(float(s.volts[0]));t.n15.store(float(s.volts[1]));t.p25.store(float(s.volts[2]));t.p45.store(float(s.volts[3]));t.p75.store(float(s.volts[4]));t.p150.store(float(s.volts[5]));t.tubeP.store(float(s.volts[6]));
        t.mainsPhase.store(float(s.phase));t.heaterCurrent.store(float(s.heaterCurrent));t.loadP15.store(.075f);t.loadTube.store(.006f);

    }
private:
    static double loadedPot(double p,double track,double load){p=std::clamp(p,0.,1.);return p*load/(load+track*p*(1-p));}
    static Profile resample(Profile p,double rate)
    {
        double u=(96000.-rate)/(96000.+rate);
        for(size_t i=0;i<p.count;++i){auto a=p.sections[i];double den=1+a.a1*u+a.a2*u*u;
            p.sections[i]={(a.b0+a.b1*u+a.b2*u*u)/den,(2*a.b0*u+a.b1*(1+u*u)+2*a.b2*u)/den,
                (a.b0*u*u+a.b1*u+a.b2)/den,(2*u+a.a1*(1+u*u)+2*a.a2*u)/den,(u*u+a.a1*u+a.a2)/den};}
        if(!p.valid())throw std::runtime_error("unstable Eco calibration");return p;
    }
    static Profile interpolate(const std::array<Profile,100>& rows,double l,double f,int range,int narrow,int rangeStride)
    {
        l=std::clamp(l,0.,1.)*4;f=std::clamp(f,0.,1.)*4;int li=std::min(int(l),3),fi=std::min(int(f),3);double lt=l-li,ft=f-fi;
        Profile p;p.gain=0;for(auto& s:p.sections)s={0,0,0,0,0};
        for(int a=0;a<2;++a)for(int b=0;b<2;++b){auto r=rows[size_t(range*rangeStride+narrow*25+(li+a)*5+fi+b)];
            if(rangeStride==50){double scale=std::pow(108200./8200.,(f-double(fi+b))*.25);double u=(scale-1)/(scale+1);auto q=r.sections[1];double den=1+q.a1*u+q.a2*u*u;r.sections[1]={(q.b0+q.b1*u+q.b2*u*u)/den,(2*q.b0*u+q.b1*(1+u*u)+2*q.b2*u)/den,(q.b0*u*u+q.b1*u+q.b2)/den,(2*u+q.a1*(1+u*u)+2*q.a2*u)/den,(u*u+q.a1*u+q.a2)/den};}
            double w=(a?lt:1-lt)*(b?ft:1-ft);p.count=std::max(p.count,r.count);p.gain+=w*r.gain;
            for(size_t i=0;i<4;++i){auto& s=p.sections[i];const auto& q=r.sections[i];s.b0+=w*q.b0;s.b1+=w*q.b1;s.b2+=w*q.b2;s.a1+=w*q.a1;s.a2+=w*q.a2;}}
        return p;
    }
    double fs=48000.,noiseSigma=0,variance=1,meterCoef=0,peakCoef=0;
    std::array<Profile,11> fixed;std::array<Profile,4> tubeProfiles;
    std::array<std::array<Profile,100>,4> eq;
    std::array<Filter,11> filters;Filter lowCut,sourceCompensation;
    std::array<Shape,ecoData::curves.size()> shapes;
    std::array<size_t,11> shapeForStage{};std::array<size_t,4> tubeShapes{};size_t tubeShape=0,oldTubeShape=0;
    double tubeFade=1,lastTubeLinear=0,lastBufferLinear=0;
    Detector detector;Supply supply;channel::Vactrol vactrol;channel::BrickwallLimiter limiter;channel::Noise noise;
    channel::ChannelControls controls;
    double gainTarget=1,gainValue=1,preTarget=1,preValue=1,compTarget=1,compValue=1,masterTarget=1,masterValue=1;
    double detSum=0,lastDetector=0,gr=1,grStep=0,compMix=1,eqMix=1,peak=0,drive=0,tapMs[4]{};
    int stride=4,detCount=0,controlStride=64,controlCount=0,supplyStride=6,supplyCount=0;
    channel::Telemetry* telemetry=nullptr;
    bool noiseOn=false,psuOn=false,wasLimiter=false,wasInstrument=false;double mainsHz=60,mainsScale=1;
};
} // namespace tubechannel::hybrid
