#pragma once
#include <cstdlib>
#include <new>

// Observe the real processor callback, including prepared-state adoption/return from bounce.
static thread_local bool countRealtimeAllocations = false;
static thread_local size_t callbackAllocations = 0, callbackFrees = 0;
void* operator new(std::size_t size)
{
    if (countRealtimeAllocations) ++callbackAllocations;
    if (void* p = std::malloc(std::max(size, size_t(1)))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { if (countRealtimeAllocations && p) ++callbackFrees; std::free(p); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (countRealtimeAllocations) ++callbackAllocations;
    void* p = nullptr;
    if (posix_memalign(&p, std::max(size_t(alignment), sizeof(void*)), std::max(size, size_t(1))) == 0) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { ::operator delete(p); }

static int offlineBounceTest()
{
    namespace id = tc::params::id;
    auto set = [](TubeChannelProcessor& p, const char* name, float value) {
        auto* parameter = p.apvts.getParameter(name);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    auto prepare = [](TubeChannelProcessor& p, double rate) {
        p.setRateAndBufferSizeDetails(rate, 64); p.prepareToPlay(rate, 64);
    };
    auto fill = [](juce::AudioBuffer<float>& b, int start, double rate) {
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample(ch, i, float(.03 + (.08 + .02 * ch) * std::sin(6.283185307179586 * (997 + 173 * ch) * (start+i) / rate)));
    };
    auto realtime = [](TubeChannelProcessor& p, juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) {
        callbackAllocations = callbackFrees = 0;
        countRealtimeAllocations = true; p.processBlock(b, midi); countRealtimeAllocations = false;
        check(callbackAllocations == 0 && callbackFrees == 0, "playback callback does not allocate/free on render transitions", double(callbackAllocations + callbackFrees));
    };
    {
        TubeChannelProcessor p;
        auto* choice = p.apvts.getParameter(id::offlineBounce);
        check(choice->getAllValueStrings().size() == 9, "exactly nine offline combinations");
        check(choice->getVersionHint() == 3, "offline choice appended with a new AU version hint");
        check(p.apvts.getRawParameterValue(id::offlineBounce)->load() == 8, "default offline profile HQ/Derp8");
        check(p.apvts.getRawParameterValue(id::offlineBounceEnabled)->load() == 1, "new instances enable offline profile");
        auto legacy = p.apvts.copyState();
        legacy.removeChild(legacy.getChildWithProperty("id", id::offlineBounce), nullptr);
        legacy.removeChild(legacy.getChildWithProperty("id", id::offlineBounceEnabled), nullptr);
        juce::MemoryBlock data;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), data);
        p.setStateInformation(data.getData(), int(data.getSize()));
        check(p.apvts.getRawParameterValue(id::offlineBounceEnabled)->load() == 0, "old state retains playback behavior offline");
        p.setNonRealtime(true);prepare(p, 48000);
        juce::AudioBuffer<float> b(2,64);b.clear();juce::MidiBuffer midi;p.processBlock(b,midi);
        check(p.currentFactor()==1 && p.currentQuality()==3 && !p.isOfflineBounceActive(), "legacy Eco render stays Eco/1x");
    }

    // Hosts can announce offline mode before prepare, and can use mono/sidechain/Dual Mono layouts.
    for (int mainChannels : {1,2}) for (int sideChannels : {0,1,2})
    {
        TubeChannelProcessor automatic, manual;
        auto layout = [&](TubeChannelProcessor& p) {
            auto buses=p.getBusesLayout();
            buses.inputBuses.getReference(0)=mainChannels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo();
            buses.outputBuses.getReference(0)=buses.inputBuses[0];
            buses.inputBuses.getReference(1)=sideChannels==0?juce::AudioChannelSet::disabled():sideChannels==1?juce::AudioChannelSet::mono():juce::AudioChannelSet::stereo();
            check(p.setBusesLayout(buses),"offline test layout accepted");
            set(p,id::dualMono,1);set(p,id::extSc,float(sideChannels>0));
            set(p,"extsc_b",float(sideChannels>0));set(p,"gain_b",.1f);
        };
        layout(automatic);layout(manual);
        set(manual,id::offlineBounceEnabled,0);set(manual,id::eco,0);set(manual,id::quality,1);set(manual,id::oversampling,4);
        automatic.setNonRealtime(true);manual.setNonRealtime(true);prepare(automatic,48000);prepare(manual,48000);
        check(automatic.currentQuality()==1 && automatic.currentFactor()==8,"offline-before-prepare selects HQ/8x immediately");
        juce::MidiBuffer midi;
        for(int block=0;block<12;++block){
            juce::AudioBuffer<float> b(mainChannels+sideChannels,64),ref(mainChannels+sideChannels,64);
            for(int ch=0;ch<b.getNumChannels();++ch)for(int i=0;i<64;++i)b.setSample(ch,i,float((ch>=mainChannels?.5:.05)*std::cos(.13*(block*64+i)*(ch+1))));
            ref.makeCopyOf(b);automatic.processBlock(b,midi);manual.processBlock(ref,midi);
            double error=0;
            for(int ch=0;ch<mainChannels;++ch)for(int i=0;i<64;++i)error=std::max(error,std::fabs(double(b.getSample(ch,i)-ref.getSample(ch,i))));
            check(error==0,"offline mono/stereo/sidechain/Dual Mono output matches manual engine",error);
        }
    }

    for (double rate : {48000., 96000.}) for (bool linear : {false, true})
    {
        TubeChannelProcessor automatic, playbackReference;
        set(automatic,id::linearPhase,float(linear));
        set(playbackReference,id::linearPhase,float(linear));
        set(playbackReference,id::offlineBounceEnabled,0);
        prepare(automatic,rate);prepare(playbackReference,rate);
        const int fixedLatency=automatic.getLatencySamples();
        juce::MidiBuffer midi;
        std::vector<float> liveReference[2];int liveStart=0;
        auto checkPlayback = [&] {
            automatic.setNonRealtime(false);
            juce::AudioBuffer<float> b(2,64), ref(2,64);fill(b,liveStart,rate);ref.makeCopyOf(b);
            realtime(automatic,b,midi);realtime(playbackReference,ref,midi);
            for(int i=0;i<64;++i)for(int ch=0;ch<2;++ch){
                liveReference[ch].push_back(ref.getSample(ch,i));const int at=liveStart+i-fixedLatency;
                const float expected=at<0?0.f:liveReference[ch][size_t(at)];
                check(b.getSample(ch,i)==expected,"realtime export/return matches playback history exactly");
            }
            liveStart+=64;
            check(!automatic.isOfflineBounceActive() && automatic.currentQuality()==3 && automatic.currentFactor()==1,"realtime export remains Eco/1x");
            check(automatic.getLatencySamples()==fixedLatency,"profile switching keeps host latency constant");
        };
        checkPlayback();
        for(int choice=0;choice<9;++choice)
        {
            // No message dispatch/reprepare: emulate hosts reporting kOffline at the first block.
            set(automatic,id::offlineBounce,float(choice));automatic.setNonRealtime(true);
            TubeChannelProcessor manual;
            set(manual,id::offlineBounceEnabled,0);set(manual,id::linearPhase,float(linear));
            const int modes[]={3,0,1};const int mode=modes[choice/3];
            set(manual,id::eco,mode==3?1.f:0.f);set(manual,id::quality,float(mode==3?0:mode));
            set(manual,id::oversampling,float(choice%3+2));manual.setNonRealtime(true);prepare(manual,rate);
            std::vector<float> reference[2];int start=0;
            for(int frames : {1,17,64,129,8,64,129,64})
            {
                juce::AudioBuffer<float> b(2,frames),ref(2,frames);if(start>=219)b.clear();else fill(b,start,rate);ref.makeCopyOf(b);
                automatic.processBlock(b,midi);manual.processBlock(ref,midi);
                const int delay=fixedLatency-manual.getLatencySamples();
                for(int i=0;i<frames;++i)for(int ch=0;ch<2;++ch){
                    reference[ch].push_back(ref.getSample(ch,i));const int at=start+i-delay;
                    const float expected=at<0?0.f:reference[ch][size_t(at)];
                    check(b.getSample(ch,i)==expected,"offline output including first sample matches manual engine plus reported padding");
                    check(std::isfinite(b.getSample(ch,i)),"offline output finite");
                }
                start+=frames;
                check(automatic.currentQuality()==mode && automatic.currentFactor()==(1<<(choice%3+1)),"offline dropdown selects requested engine and factor");
                check(automatic.isOfflineBounceActive(),"host-reported offline flag activates bounce profile");
                check(automatic.getLatencySamples()==fixedLatency && automatic.currentLatency()==fixedLatency,"render PDC remains unchanged");
            }
            check(automatic.apvts.getRawParameterValue(id::eco)->load()==1 && automatic.apvts.getRawParameterValue(id::oversampling)->load()==1,"offline rendering leaves playback parameters untouched");
            juce::MemoryBlock saved;automatic.getStateInformation(saved);
            set(automatic,id::offlineBounce,0);set(automatic,id::offlineBounceEnabled,0);
            automatic.setStateInformation(saved.getData(),int(saved.getSize()));
            check(automatic.apvts.getRawParameterValue(id::offlineBounce)->load()==choice && automatic.apvts.getRawParameterValue(id::offlineBounceEnabled)->load()==1,"offline profile state round trips");
            checkPlayback();
        }
        // Identical repeated bounces must restart from a deterministic prepared state.
        set(automatic,id::offlineBounce,8);
        std::vector<float> first;
        for(int bounce=0;bounce<3;++bounce){
            if(bounce==2)automatic.reset();
            else { automatic.setNonRealtime(false);automatic.setNonRealtime(true); }
            juce::AudioBuffer<float> b(2,64);fill(b,0,rate);automatic.processBlock(b,midi);
            for(int ch=0;ch<2;++ch)for(int i=0;i<64;++i){if(bounce==0)first.push_back(b.getSample(ch,i));else check(first[size_t(ch*64+i)]==b.getSample(ch,i),"repeat bounces deterministic without prepare or realtime blocks");}
        }
        checkPlayback();
        // Message-thread preparation of a bounce-only edit preserves the running playback engine.
        set(automatic,id::offlineBounce,0);pump(30);checkPlayback();
        std::printf("  %.0f Hz%s: all 9 profiles, first samples/tails, stable latency %d, playback allocation checks passed\n",rate,linear?" linear":" minimum",fixedLatency);
    }
    std::printf("offline bounce %s (%d failures)\n",failures?"FAILED":"PASSED",failures);
    return failures?1:0;
}
