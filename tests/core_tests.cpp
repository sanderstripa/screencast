#include "../src/dsp.h"
#include "../src/spectral.h"
#include "../src/motion.h"
#include <iostream>
#include <stdexcept>

void require(bool condition, char const* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        dsp::Timeline system, microphone;
        // Sources begin at different device packet times; the mixer must preserve alignment.
        system.put(480, .2f, -.2f); microphone.put(481, .3f, .3f);
        std::vector<int16_t> pcm;
        dsp::mix(&system, &microphone, 480, 3, pcm);
        require(pcm[0] == dsp::pcm16(.2f) && pcm[1] == dsp::pcm16(-.2f), "system packet shifted");
        require(pcm[2] == dsp::pcm16(.3f) && pcm[3] == dsp::pcm16(.3f), "microphone packet shifted");
        require(pcm[4] == 0 && pcm[5] == 0, "missing packet must be silence");
        system.put(480 + dsp::Timeline::Capacity, .7f, .7f);
        dsp::mix(&system, nullptr, 480, 1, pcm);
        require(pcm[0] == 0, "wrapped ring leaked stale audio");
        system.put(900, .8f, -.8f); microphone.put(900, .8f, -.8f);
        dsp::mix(&system, &microphone, 900, 1, pcm);
        require(pcm[0] == 32767 && pcm[1] == -32767, "mixed peaks must saturate, not wrap");
        dsp::mix(nullptr, &microphone, 900, 1, pcm);
        require(pcm[0] == dsp::pcm16(.8f), "disabled output leaked into microphone recording");
        dsp::mix(nullptr, nullptr, 900, 2, pcm);
        require(pcm == std::vector<int16_t>(4, 0), "disabled sources must be silent");
        dsp::NoiseSuppressor suppression;
        double inputPower{}, outputPower{};
        for (int i = 0; i < 48000; ++i) {
            float noise = .001f * std::sin(i * .61f);
            auto out = suppression.process({noise, noise});
            if (i > 24000) { inputPower += noise * noise; outputPower += out[0] * out[0]; }
        }
        require(outputPower < inputPower * .1, "low-level noise was not attenuated");
        dsp::mix(&system,&microphone,900,1,pcm,nullptr,.25f,0.f);
        require(pcm[0]==dsp::pcm16(.2f),"system gain or microphone mute is wrong");
        dsp::mix(&system,&microphone,900,1,pcm,nullptr,0.f,.5f);
        require(pcm[0]==dsp::pcm16(.4f),"independent microphone gain is wrong");
        SmoothMotion movement({0,0}); Point previous{};
        for(int i=0;i<120;++i) {
            auto p=movement.step({1,1},1.f/30);
            require(p.x>=previous.x && p.x<=1 && p.x-previous.x<.28f,"camera movement jumps or overshoots"); previous=p;
        }
        require(previous.x>.999f,"camera movement did not reach its destination");
        dsp::Timeline noise;
        unsigned seed=1234567;
        for(int i=0;i<96000;++i) {seed=seed*1664525+1013904223;float value=(int(seed>>16)-32768)/32768.f*.008f;noise.put(i,value,value);}
        dsp::SpectralDenoiser wet,dry,medium; std::vector<float> a,b,c;
        double original{},filtered{},moderate{};
        for(int start=0;start<95040;start+=480) {
            wet.render(noise,start,480,1,a);dry.render(noise,start,480,0,b);medium.render(noise,start,480,.5f,c);
            for(int i=0;i<480;++i) {
                require(b[i]==noise.at(start+i)[0],"zero noise intensity must be exact bypass");
                if(start>24000) {original+=b[i]*b[i];filtered+=a[i]*a[i];moderate+=c[i]*c[i];}
            }
        }
        require(filtered<original*.65,"spectral suppression failed to attenuate broadband noise");
        require(filtered<moderate && moderate<original,"noise intensity does not affect actual audio");
        std::cout << "Timestamp alignment, silence, independent sources, clipping and noise attenuation passed\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
