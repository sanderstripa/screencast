#include "audio.h"
#include <audioclientactivationparams.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>

std::vector<Device> audioDevices(bool microphone) {
    ComPtr<IMMDeviceEnumerator> enumerator;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
    ComPtr<IMMDeviceCollection> collection;
    check(enumerator->EnumAudioEndpoints(microphone ? eCapture : eRender, DEVICE_STATE_ACTIVE, &collection));
    UINT count{}; check(collection->GetCount(&count));
    std::vector<Device> devices{{L"По умолчанию", L""}};
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device; check(collection->Item(i, &device));
        LPWSTR id{}; check(device->GetId(&id));
        std::wstring saved(id); CoTaskMemFree(id);
        ComPtr<IPropertyStore> properties; check(device->OpenPropertyStore(STGM_READ, &properties));
        PROPVARIANT value{}; check(properties->GetValue(PKEY_Device_FriendlyName, &value));
        std::wstring name = value.vt == VT_LPWSTR ? value.pwszVal : saved;
        PropVariantClear(&value); devices.push_back({name, saved});
    }
    return devices;
}
class Activation final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
    IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
public:
    Activation() : event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Activation() { if (event) CloseHandle(event); }
    HANDLE event{};
    HRESULT result{E_PENDING};
    ComPtr<IAudioClient> client;
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
        ComPtr<IUnknown> unknown;
        HRESULT activationResult{};
        result = operation->GetActivateResult(&activationResult, &unknown);
        if (SUCCEEDED(result)) result = activationResult;
        if (SUCCEEDED(result)) result = unknown.As(&client);
        SetEvent(event); return S_OK;
    }
};
AudioCapture::AudioCapture(bool mic, AudioMode mode, std::wstring const& endpoint, DWORD pid) {
    bool process = !mic && mode == AudioMode::Process;
    if (process) {
        if (!pid) throw winrt::hresult_error(E_INVALIDARG, L"Выберите приложение для записи звука");
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams.TargetProcessId = pid;
        params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
        PROPVARIANT activation{};
        activation.vt = VT_BLOB; activation.blob.cbSize = sizeof(params);
        activation.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
        auto callback = Microsoft::WRL::Make<Activation>();
        if (!callback->event) check(HRESULT_FROM_WIN32(GetLastError()));
        ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
        check(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient),
            &activation, callback.Get(), &operation));
        if (WaitForSingleObject(callback->event, 10000) != WAIT_OBJECT_0)
            throw winrt::hresult_error(E_FAIL, L"Не удалось подключить звук выбранного процесса");
        check(callback->result); client_ = callback->client;
        formatBytes_.resize(sizeof(WAVEFORMATEX));
        format_ = reinterpret_cast<WAVEFORMATEX*>(formatBytes_.data());
        *format_ = {WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
    } else {
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)));
        ComPtr<IMMDevice> device;
        if (endpoint.empty()) check(enumerator->GetDefaultAudioEndpoint(mic ? eCapture : eRender, eConsole, &device));
        else check(enumerator->GetDevice(endpoint.c_str(), &device));
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client_.GetAddressOf())));
        WAVEFORMATEX* raw{}; check(client_->GetMixFormat(&raw));
        formatBytes_.assign(reinterpret_cast<uint8_t*>(raw), reinterpret_cast<uint8_t*>(raw) + sizeof(WAVEFORMATEX) + raw->cbSize);
        CoTaskMemFree(raw); format_ = reinterpret_cast<WAVEFORMATEX*>(formatBytes_.data());
    }
    WORD tag = format_->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (format_->cbSize < 22) throw winrt::hresult_error(E_NOTIMPL, L"Некорректный аудиоформат устройства");
        auto ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format_);
        channelMask_ = ext->dwChannelMask;
        if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (ext->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
        else tag = 0;
    }
    floating_ = tag == WAVE_FORMAT_IEEE_FLOAT;
    if ((!floating_ && tag != WAVE_FORMAT_PCM) || (floating_ && format_->wBitsPerSample != 32) ||
        (!floating_ && format_->wBitsPerSample != 16 && format_->wBitsPerSample != 24 && format_->wBitsPerSample != 32))
        throw winrt::hresult_error(E_NOTIMPL, L"Формат аудиоустройства не поддерживается");
    if (!format_->nSamplesPerSec || !format_->nChannels) throw winrt::hresult_error(E_INVALIDARG);
    check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, mic ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK,
        1000000, 0, format_, nullptr));
    check(client_->GetService(IID_PPV_ARGS(&capture_)));
}
AudioCapture::~AudioCapture() { if (started_) client_->Stop(); }
void AudioCapture::start(int64_t epoch) { epoch_ = epoch; check(client_->Start()); started_ = true; }
void AudioCapture::poll() {
    UINT32 frames{}; check(capture_->GetNextPacketSize(&frames));
    while (frames) {
        BYTE* data{}; DWORD flags{}; UINT64 position{}, qpc{};
        check(capture_->GetBuffer(&data, &frames, &flags, &position, &qpc));
        // Packet timestamps use the same QPC clock as video. Silence has a valid duration.
        int64_t base = (static_cast<int64_t>(qpc) - epoch_) * dsp::Rate / 10000000;
        if (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) {
            capture_->ReleaseBuffer(frames);
            throw winrt::hresult_error(E_FAIL, L"Аудиоустройство вернуло некорректную временную метку");
        }
        int count = static_cast<int>((static_cast<uint64_t>(frames) * dsp::Rate) / format_->nSamplesPerSec);
        auto sample = [&](UINT32 frame, int channel) -> float {
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) return 0.f;
            auto offset = static_cast<size_t>(frame) * format_->nBlockAlign +
                static_cast<size_t>(std::min(channel, static_cast<int>(format_->nChannels) - 1)) * (format_->wBitsPerSample / 8);
            if (floating_) { float v{}; memcpy(&v, data + offset, 4); return std::isfinite(v) ? v : 0.f; }
            if (format_->wBitsPerSample == 16) { int16_t v{}; memcpy(&v, data + offset, 2); return v / 32768.f; }
            if (format_->wBitsPerSample == 32) { int32_t v{}; memcpy(&v, data + offset, 4); return static_cast<float>(v / 2147483648.0); }
            int32_t v = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16);
            if (v & 0x800000) v |= static_cast<int32_t>(0xff000000); return v / 8388608.f;
        };
        auto stereo = [&](UINT32 frame) -> std::array<float, 2> {
            if (format_->nChannels == 1) { auto value = sample(frame, 0); return {value, value}; }
            if (format_->nChannels == 2) return {sample(frame, 0), sample(frame, 1)};
            std::array<float, 2> value{};
            DWORD remaining = channelMask_;
            for (int channel = 0; channel < format_->nChannels; ++channel) {
                DWORD speaker{};
                if (remaining) { speaker = remaining & (~remaining + 1); remaining &= ~speaker; }
                else speaker = channel < 2 ? (channel == 0 ? SPEAKER_FRONT_LEFT : SPEAKER_FRONT_RIGHT) : SPEAKER_FRONT_CENTER;
                float v = sample(frame, channel);
                switch (speaker) {
                case SPEAKER_FRONT_LEFT: value[0] += v; break;
                case SPEAKER_FRONT_RIGHT: value[1] += v; break;
                case SPEAKER_BACK_LEFT: case SPEAKER_SIDE_LEFT: case SPEAKER_FRONT_LEFT_OF_CENTER: value[0] += .5f * v; break;
                case SPEAKER_BACK_RIGHT: case SPEAKER_SIDE_RIGHT: case SPEAKER_FRONT_RIGHT_OF_CENTER: value[1] += .5f * v; break;
                case SPEAKER_LOW_FREQUENCY: value[0] += .25f * v; value[1] += .25f * v; break;
                default: value[0] += .7071f * v; value[1] += .7071f * v; break;
                }
            }
            return value;
        };
        for (int i = 0; i < count; ++i) {
            double source = static_cast<double>(i) * format_->nSamplesPerSec / dsp::Rate;
            auto a = std::min(static_cast<UINT32>(source), frames - 1);
            auto b = std::min(a + 1, frames - 1); float blend = static_cast<float>(source - a);
            auto first = stereo(a), second = stereo(b);
            float left = first[0] * (1 - blend) + second[0] * blend;
            float right = first[1] * (1 - blend) + second[1] * blend;
            timeline_.put(base + i, left, right);
        }
        check(capture_->ReleaseBuffer(frames));
        check(capture_->GetNextPacketSize(&frames));
    }
}
