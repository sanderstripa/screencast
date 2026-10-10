#pragma once
#include "common.h"
#include "dsp.h"
#include <audioclient.h>
#include <mmdeviceapi.h>

std::vector<Device> audioDevices(bool microphone);
class AudioCapture {
public:
    AudioCapture(bool mic, AudioMode mode, std::wstring const& endpoint, DWORD pid);
    ~AudioCapture();
    void start(int64_t epoch);
    void poll();
    dsp::Timeline const& timeline() const { return timeline_; }
private:
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> capture_;
    std::vector<uint8_t> formatBytes_;
    WAVEFORMATEX* format_{};
    bool floating_{}, started_{};
    int64_t epoch_{};
    dsp::Timeline timeline_;
};
