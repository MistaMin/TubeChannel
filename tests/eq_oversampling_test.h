// Regression coverage for EQ response across oversampling, engine selection and offline rendering.
static int eqOversamplingTest()
{
    namespace id=tc::params::id; int eqFailures=0;
    for (int mode : {3,0,1}) for(int stages : {1,2,3}) {
        TubeChannelProcessor p; setChoice(p,id::quality,mode);setChoice(p,id::oversampling,stages+1);
        setBool(p,id::offlineBounceEnabled,false);setBool(p,id::noise,false);setBool(p,id::psuModel,false);setBool(p,id::comp,false);setBool(p,id::eq,true);
        p.setRateAndBufferSizeDetails(48000,128);p.prepareToPlay(48000,128);
        juce::AudioBuffer<float> b(2,128);juce::MidiBuffer midi;double energy[2][2]={};
        for(int phase=0;phase<2;++phase){auto* prm=p.apvts.getParameter(id::hmLevel);prm->setValueNotifyingHost(prm->convertTo0to1(float(phase)));
            for(int block=0;block<90;++block){for(int i=0;i<128;++i){const float v=float(.03*std::sin(6.283185307179586*1000*(block*128+i)/48000.));b.setSample(0,i,v);b.setSample(1,i,v);}p.processBlock(b,midi);if(block>=70)for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i)energy[phase][ch]+=b.getSample(ch,i)*b.getSample(ch,i);}}
        for(int ch=0;ch<2;++ch){double db=10*std::log10(energy[1][ch]/energy[0][ch]);std::printf("processor mode=%d factor=%d ch=%d eq cut-to-boost %.3f dB\n",mode,p.currentFactor(),ch,db);if(!std::isfinite(db)||db<3.0||p.currentFactor()!=(1<<stages))++eqFailures;}
    }
    for(int mode : {3,0,1}) {
        TubeChannelProcessor p;setChoice(p,id::quality,mode);setChoice(p,id::oversampling,2);setBool(p,id::offlineBounceEnabled,false);setBool(p,id::noise,false);setBool(p,id::psuModel,false);setBool(p,id::comp,false);setBool(p,id::eq,true);
        p.setRateAndBufferSizeDetails(48000,128);p.prepareToPlay(48000,128);juce::AudioBuffer<float> b(2,128);juce::MidiBuffer midi;
        for(int stages : {2,3}) {
            setChoice(p,id::oversampling,stages+1);const auto deadline=juce::Time::getMillisecondCounterHiRes()+12000.;
            do {pump(10);b.clear();p.processBlock(b,midi);}while(p.currentFactor()!=(1<<stages) && juce::Time::getMillisecondCounterHiRes()<deadline);
            if(p.currentFactor()!=(1<<stages)) {std::printf("switch failed mode=%d factor=%d\n",mode,1<<stages);++eqFailures;continue;}
            struct Band{const char* name;const char* id;double frequency;};
            for(auto band : {Band{"low",id::low,100},Band{"high",id::high,8000},Band{"low-mid",id::lmLevel,200},Band{"high-mid",id::hmLevel,1000}}) {
                for(const char* param : {id::low,id::high,id::lmLevel,id::hmLevel})p.apvts.getParameter(param)->setValueNotifyingHost(.5f);
                double energy[2][2]={};
                for(int phase=0;phase<2;++phase){p.apvts.getParameter(band.id)->setValueNotifyingHost(float(phase));
                    for(int block=0;block<120;++block){for(int i=0;i<128;++i){float v=float(.03*std::sin(6.283185307179586*band.frequency*(block*128+i)/48000.));b.setSample(0,i,v);b.setSample(1,i,v);}p.processBlock(b,midi);if(block>=100)for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i)energy[phase][ch]+=b.getSample(ch,i)*b.getSample(ch,i);}}
                for(int ch=0;ch<2;++ch){double db=10*std::log10(energy[1][ch]/energy[0][ch]);std::printf("live mode=%d factor=%d band=%s ch=%d cut-to-boost %.3f dB\n",mode,p.currentFactor(),band.name,ch,db);if(!std::isfinite(db)||db<1.)++eqFailures;}
            }
        }
    }
    // Set the EQ first, then switch oversampling while the processor is live.
    for(int mode : {0,1}) {
        TubeChannelProcessor p;setChoice(p,id::quality,mode);setChoice(p,id::oversampling,2);setBool(p,id::offlineBounceEnabled,false);setBool(p,id::noise,false);setBool(p,id::psuModel,false);setBool(p,id::comp,false);
        p.apvts.getParameter(id::hmLevel)->setValueNotifyingHost(1.f);
        p.setRateAndBufferSizeDetails(48000,128);p.prepareToPlay(48000,128);juce::AudioBuffer<float> b(2,128);juce::MidiBuffer midi;
        for(int stages : {2,3}) {
            setBool(p,id::eq,true);setChoice(p,id::oversampling,stages+1);const auto deadline=juce::Time::getMillisecondCounterHiRes()+12000.;
            do {pump(10);b.clear();p.processBlock(b,midi);} while(p.currentFactor()!=(1<<stages) && juce::Time::getMillisecondCounterHiRes()<deadline);
            if(p.currentFactor()!=(1<<stages)){std::printf("switch failed mode=%d factor=%d\n",mode,1<<stages);++eqFailures;continue;}
            double energy[2]={};
            for(int phase=0;phase<2;++phase) { if(phase) setBool(p,id::eq,false);
                for(int block=0;block<100;++block){for(int i=0;i<128;++i){float v=float(.03*std::sin(6.283185307179586*1000*(block*128+i)/48000.));b.setSample(0,i,v);b.setSample(1,i,v);}p.processBlock(b,midi);if(block>=80)for(int i=0;i<128;++i)energy[phase]+=b.getSample(0,i)*b.getSample(0,i);}}
            double db=10*std::log10(energy[0]/energy[1]);std::printf("preserved EQ mode=%d factor=%d in-vs-out %.3f dB\n",mode,1<<stages,db);
            if(!std::isfinite(db)||std::fabs(db)<1.)++eqFailures;
        }
    }
    for(int mode : {0,1}) for(int stages : {2,3}) for(bool linear : {false,true}) {
        TubeChannelProcessor p;setChoice(p,id::quality,mode);setChoice(p,id::oversampling,stages+1);setBool(p,id::linearPhase,linear);
        setBool(p,id::noise,false);setBool(p,id::psuModel,false);setBool(p,id::comp,false);setBool(p,id::offlineBounceEnabled,true);
        setChoice(p,id::offlineBounce,(mode==0?3:6)+(stages-1));
        p.setRateAndBufferSizeDetails(48000,128);p.setNonRealtime(true);p.prepareToPlay(48000,128);
        p.apvts.getParameter(id::hmLevel)->setValueNotifyingHost(1.f);double energy[2]={};juce::AudioBuffer<float> b(2,128);juce::MidiBuffer midi;
        for(int phase=0;phase<2;++phase){setBool(p,id::eq,phase==1);for(int block=0;block<100;++block){for(int i=0;i<128;++i){float v=float(.03*std::sin(6.283185307179586*1000*(block*128+i)/48000.));b.setSample(0,i,v);b.setSample(1,i,v);}p.processBlock(b,midi);if(block>=80)for(int i=0;i<128;++i)energy[phase]+=b.getSample(0,i)*b.getSample(0,i);}}
        double db=10*std::log10(energy[1]/energy[0]);std::printf("offline mode=%d factor=%d linear=%d EQ in-vs-out %.3f dB\n",mode,p.currentFactor(),linear,db);
        if(!std::isfinite(db)||std::fabs(db)<1.0||p.currentFactor()!=(1<<stages))++eqFailures;
    }
    return eqFailures ? 1 : 0;
}
