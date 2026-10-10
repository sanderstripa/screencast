#include "../src/dsp.h"
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
        std::cout << "Timestamp alignment, silence, independent sources, clipping and noise attenuation passed\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
