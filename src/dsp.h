#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dsp {
constexpr int Rate = 48000;
// Timestamped stereo ring: absent packets are silence, never stale samples.
class Timeline {
public:
    static constexpr size_t Capacity = Rate * 4;
    Timeline() : samples_(Capacity), tags_(Capacity, -1) {}
    void put(int64_t frame, float left, float right) {
        if (frame < 0) return;
        auto slot = static_cast<size_t>(frame) % Capacity;
        tags_[slot] = frame; samples_[slot] = {left, right};
    }
    std::array<float, 2> at(int64_t frame) const {
        if (frame < 0) return {};
        auto slot = static_cast<size_t>(frame) % Capacity;
        return tags_[slot] == frame ? samples_[slot] : std::array<float, 2>{};
    }
private:
    std::vector<std::array<float, 2>> samples_;
    std::vector<int64_t> tags_;
};
// A gentle microphone expander plus 80 Hz high-pass. This is not neural denoising.
class NoiseSuppressor {
public:
    std::array<float, 2> process(std::array<float, 2> value, float strength = 1.f) {
        strength = std::clamp(strength, 0.f, 1.f);
        auto dry = value;
        float level = (value[0] * value[0] + value[1] * value[1]) * .5f;
        envelope_ += (level > envelope_ ? .02f : .001f) * (level - envelope_);
        float rms = std::sqrt(envelope_);
        float threshold = .004f + .012f * strength;
        float floor = .2f - .18f * strength;
        float desired = std::clamp((rms - threshold * .25f) / (threshold * .75f), floor, 1.f);
        gain_ += (desired > gain_ ? .025f : .002f) * (desired - gain_);
        for (int c = 0; c < 2; ++c) {
            float out = .98958f * (previousOut_[c] + value[c] - previousIn_[c]);
            previousIn_[c] = value[c]; previousOut_[c] = out;
            value[c] = dry[c] * (1 - strength) + out * gain_ * strength;
        }
        return value;
    }
private:
    float envelope_{}, gain_{1};
    std::array<float, 2> previousIn_{}, previousOut_{};
};
inline int16_t pcm16(float value) {
    return static_cast<int16_t>(std::lround(std::clamp(value, -1.f, 1.f) * 32767.f));
}
inline void mix(Timeline const* system, Timeline const* mic, int64_t start, int frames,
    std::vector<int16_t>& output, NoiseSuppressor* suppressor = nullptr,
    float systemGain = 1.f, float micGain = 1.f, float noiseStrength = 1.f) {
    output.resize(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        auto a = system ? system->at(start + i) : std::array<float, 2>{};
        auto b = mic ? mic->at(start + i) : std::array<float, 2>{};
        if (mic && suppressor) b = suppressor->process(b, noiseStrength);
        for (int c = 0; c < 2; ++c) output[static_cast<size_t>(i) * 2 + c] = pcm16(a[c] * systemGain + b[c] * micGain);
    }
}
}
