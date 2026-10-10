#include "recorder.h"
#include "capture.h"
#include "audio.h"
#include "camera.h"
#include <avrt.h>
#include <deque>

namespace {
class Writer {
public:
    Writer(std::wstring const& path, int w, int h, int fps, bool audio) : width(w), height(h), fps_(fps) {
        try { open(path, audio, true); }
        catch (...) { sink.Reset(); open(path, audio, false); }
    }
    void sample(DWORD stream, BYTE const* data, DWORD bytes, int64_t timestamp, int64_t duration) {
        ComPtr<IMFMediaBuffer> buffer; check(MFCreateMemoryBuffer(bytes, &buffer));
        BYTE* dest{}; check(buffer->Lock(&dest, nullptr, nullptr));
        memcpy(dest, data, bytes); check(buffer->Unlock()); check(buffer->SetCurrentLength(bytes));
        ComPtr<IMFSample> value; check(MFCreateSample(&value));
        check(value->AddBuffer(buffer.Get())); check(value->SetSampleTime(timestamp));
        check(value->SetSampleDuration(duration)); check(sink->WriteSample(stream, value.Get()));
    }
    void video(Image const& image, int64_t frame) {
        // Keep MP4 dimensions fixed. Resized source windows are letterboxed, never stretched.
        Image canvas;
        Image const* source = &image;
        if (image.width != width || image.height != height) {
            canvas = {width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4, 0)};
            double scale = std::min(static_cast<double>(width) / image.width, static_cast<double>(height) / image.height);
            int w = std::max(1, static_cast<int>(image.width * scale)), h = std::max(1, static_cast<int>(image.height * scale));
            auto scaled = resizeImage(image, w, h);
            int x = (width - w) / 2, y = (height - h) / 2;
            for (int row = 0; row < h; ++row) memcpy(canvas.bgra.data() + (static_cast<size_t>(y + row) * width + x) * 4,
                scaled.bgra.data() + static_cast<size_t>(row) * w * 4, w * 4);
            source = &canvas;
        }
        nv12.resize(static_cast<size_t>(width) * height * 3 / 2);
        auto byte = [](int v) { return static_cast<uint8_t>(std::clamp(v, 0, 255)); };
        size_t plane = static_cast<size_t>(width) * height;
        for (int y = 0; y < height; y += 2) for (int x = 0; x < width; x += 2) {
            int r{}, g{}, b{};
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                size_t i = static_cast<size_t>(y + dy) * width + x + dx;
                auto p = source->bgra.data() + i * 4;
                nv12[i] = byte(((66 * p[2] + 129 * p[1] + 25 * p[0] + 128) >> 8) + 16);
                r += p[2]; g += p[1]; b += p[0];
            }
            r /= 4; g /= 4; b /= 4;
            size_t uv = plane + static_cast<size_t>(y / 2) * width + x;
            nv12[uv] = byte(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            nv12[uv + 1] = byte(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
        auto time = frame * 10000000 / fps_;
        sample(videoStream, nv12.data(), static_cast<DWORD>(nv12.size()), time, (frame + 1) * 10000000 / fps_ - time);
    }
    void audio(std::vector<int16_t> const& pcm, int64_t frame) {
        auto count = static_cast<int64_t>(pcm.size() / 2);
        auto time = frame * 10000000 / dsp::Rate;
        sample(audioStream, reinterpret_cast<BYTE const*>(pcm.data()), static_cast<DWORD>(pcm.size() * sizeof(int16_t)),
            time, (frame + count) * 10000000 / dsp::Rate - time);
    }
    void finalize() { check(sink->Finalize()); sink.Reset(); }
    int width{}, height{};
private:
    ComPtr<IMFSinkWriter> sink;
    DWORD videoStream{}, audioStream{};
    int fps_{};
    std::vector<uint8_t> nv12;
    void open(std::wstring const& path, bool audio, bool hardware) {
        ComPtr<IMFAttributes> attrs; check(MFCreateAttributes(&attrs, 2));
        check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hardware));
        check(attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
        check(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &sink));
        ComPtr<IMFMediaType> out; check(MFCreateMediaType(&out));
        check(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        check(out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264));
        check(out->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::clamp(static_cast<int64_t>(width) * height * 4, 2000000LL, 40000000LL))));
        check(out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
        check(MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, width, height));
        check(MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, fps_, 1));
        check(MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
        check(sink->AddStream(out.Get(), &videoStream));
        ComPtr<IMFMediaType> in; check(MFCreateMediaType(&in));
        check(out->CopyAllItems(in.Get()));
        check(in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
        in->DeleteItem(MF_MT_AVG_BITRATE);
        check(in->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
        check(in->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235));
        check(sink->SetInputMediaType(videoStream, in.Get(), nullptr));
        if (audio) {
            check(MFCreateMediaType(&out));
            check(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
            check(out->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
            check(out->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
            check(out->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, dsp::Rate));
            check(out->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
            check(out->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000));
            check(out->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0));
            check(sink->AddStream(out.Get(), &audioStream));
            check(MFCreateMediaType(&in));
            check(in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
            check(in->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
            check(in->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2));
            check(in->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, dsp::Rate));
            check(in->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
            check(in->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4));
            check(in->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, dsp::Rate * 4));
            check(sink->SetInputMediaType(audioStream, in.Get(), nullptr));
        }
        check(sink->BeginWriting());
    }
};
struct ProcessHandle {
    HANDLE value{};
    ~ProcessHandle() { if (value) CloseHandle(value); }
};
}
Recorder::~Recorder() { stop(); join(); }
void Recorder::stop() { stop_ = true; }
void Recorder::join() { if (worker_.joinable()) worker_.join(); }
void Recorder::start(Settings settings, HWND notify) {
    if (busy_) return;
    join(); stop_ = false; epoch_ = 0; busy_ = true;
    worker_ = std::thread([this, settings = std::move(settings), notify] {
        auto result = std::make_unique<RecordingResult>();
        std::unique_ptr<Writer> writer;
        std::wstring temporary;
        HANDLE mmcss{};
        bool apartment{};
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true;
            if (std::filesystem::exists(settings.path))
                throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_EXISTS), L"Файл уже существует. Выберите новое имя");
            DWORD taskIndex{}; mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
            Capture capture(settings.target);
            std::unique_ptr<AudioCapture> system, mic;
            ProcessHandle process;
            if (settings.audio == AudioMode::Process) {
                process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, settings.audioPid);
                if (!process.value) throw winrt::hresult_error(HRESULT_FROM_WIN32(GetLastError()), L"Не удалось открыть выбранный процесс");
                FILETIME created{}, exited{}, kernel{}, user{};
                check(GetProcessTimes(process.value, &created, &exited, &kernel, &user) ? S_OK : HRESULT_FROM_WIN32(GetLastError()));
                auto creation = (static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
                if (!settings.audioCreationTime || creation != settings.audioCreationTime)
                    throw winrt::hresult_error(E_ABORT, L"Выбранный процесс изменился. Обновите список приложений");
            }
            if (settings.audio != AudioMode::Off) system = std::make_unique<AudioCapture>(false, settings.audio, settings.outputId, settings.audioPid);
            if (settings.mic) mic = std::make_unique<AudioCapture>(true, AudioMode::Off, settings.micId, 0);
            std::unique_ptr<Camera> camera;
            if (settings.camera) camera = std::make_unique<Camera>(settings);
            Image image;
            auto deadline = clock100ns() + 50000000;
            while (!stop_ && !capture.next(image)) {
                if (clock100ns() > deadline) throw winrt::hresult_error(E_FAIL, L"Нет кадров от источника. Восстановите свернутое окно");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (stop_) { result->cancelled = true; throw winrt::hresult_error(E_ABORT, L"Запуск отменен"); }
            GUID id{}; check(CoCreateGuid(&id)); wchar_t suffix[40]{}; StringFromGUID2(id, suffix, 40);
            temporary = settings.path + L"." + suffix + L".partial.mp4";
            writer = std::make_unique<Writer>(temporary, (image.width + 1) & ~1, (image.height + 1) & ~1, settings.fps, system || mic);
            int64_t epoch = clock100ns(); epoch_ = epoch;
            if (system) system->start(epoch); if (mic) mic->start(epoch);
            recording_ = true;
            int64_t videoFrame{}, audioFrame{}, nextDiskCheck{}, stopAt{};
            std::deque<Image> videoQueue, cameraQueue;
            Image cameraImage;
            int64_t lastCameraTimestamp{};
            dsp::NoiseSuppressor suppressor;
            std::vector<int16_t> pcm;
            // Delay output by 150 ms so audio packets arrive before their timeline is mixed.
            constexpr int64_t latency = 1500000;
            while (true) {
                auto now = clock100ns() - epoch;
                if (stop_ && !stopAt) stopAt = std::max<int64_t>(now, 1);
                if (system) system->poll(); if (mic) mic->poll();
                if (process.value && WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0)
                    throw winrt::hresult_error(E_ABORT, L"Приложение, звук которого записывался, завершилось");
                Image fresh; if (capture.next(fresh)) videoQueue.push_back(std::move(fresh));
                if (camera) {
                    auto value = camera->latest();
                    if (!value.empty() && value.time100ns > lastCameraTimestamp) {
                        lastCameraTimestamp = value.time100ns; cameraQueue.push_back(std::move(value));
                    }
                }
                int64_t ready = std::min(now - latency, stopAt ? stopAt : now);
                while (videoFrame * 10000000 / settings.fps <= ready) {
                    auto due = epoch + videoFrame * 10000000 / settings.fps;
                    while (!videoQueue.empty() && videoQueue.front().time100ns <= due) {
                        image = std::move(videoQueue.front()); videoQueue.pop_front();
                    }
                    while (!cameraQueue.empty() && cameraQueue.front().time100ns <= due) {
                        cameraImage = std::move(cameraQueue.front()); cameraQueue.pop_front();
                    }
                    Image output = image;
                    if (camera) compositeCamera(output, cameraImage, settings.corner);
                    writer->video(output, videoFrame++);
                    // Falling behind by >3 s signals overload instead of allocating an unbounded queue.
                    if (clock100ns() - epoch - videoFrame * 10000000 / settings.fps > 30000000)
                        throw winrt::hresult_error(E_FAIL, L"Компьютер не успевает кодировать запись. Уменьшите разрешение источника или выключите замену фона");
                }
                if (system || mic) {
                    int64_t readyFrames = std::max<int64_t>(0, ready) * dsp::Rate / 10000000;
                    while (audioFrame + 480 <= readyFrames) {
                        dsp::mix(system ? &system->timeline() : nullptr, mic ? &mic->timeline() : nullptr,
                            audioFrame, 480, pcm, settings.denoise && mic ? &suppressor : nullptr);
                        writer->audio(pcm, audioFrame); audioFrame += 480;
                    }
                }
                if (stopAt && now >= stopAt + latency) break;
                if (now >= nextDiskCheck) {
                    ULARGE_INTEGER available{};
                    auto directory = std::filesystem::path(settings.path).parent_path().wstring();
                    if (!GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr)) check(HRESULT_FROM_WIN32(GetLastError()));
                    if (available.QuadPart < 32 * 1024 * 1024)
                        throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_DISK_FULL), L"Недостаточно места для продолжения записи");
                    nextDiskCheck = now + 10000000;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            writer->finalize(); writer.reset();
            if (!MoveFileExW(temporary.c_str(), settings.path.c_str(), MOVEFILE_WRITE_THROUGH)) check(HRESULT_FROM_WIN32(GetLastError()));
            result->success = true; result->path = settings.path;
            result->message = L"Запись сохранена";
        } catch (...) {
            result->message = errorText();
            if (writer) {
                try { writer->finalize(); result->path = temporary; result->message += L"\nДоступная часть сохранена во временный MP4."; }
                catch (...) { result->message += L"\nНе удалось завершить временный MP4; файл может быть поврежден."; result->path = temporary; }
                writer.reset();
            }
        }
        recording_ = false; busy_ = false;
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        if (apartment) winrt::uninit_apartment();
        if (PostMessageW(notify, WM_RECORD_STATUS, 0, reinterpret_cast<LPARAM>(result.get()))) result.release();
    });
}

int encoderSelfTest(std::wstring const& directory) {
    std::filesystem::create_directories(directory);
    for (bool audio : {false, true}) {
        Writer writer(directory + (audio ? L"\\synthetic-audio.mp4" : L"\\synthetic-silent.mp4"), 640, 360, 30, audio);
        std::vector<int16_t> pcm(1600 * 2);
        for (int frame = 0; frame < 90; ++frame) {
            Image image{640, 360, std::vector<uint8_t>(640 * 360 * 4)};
            for (int y = 0; y < 360; ++y) for (int x = 0; x < 640; ++x) {
                auto i = static_cast<size_t>(y * 640 + x) * 4;
                image.bgra[i] = static_cast<uint8_t>(x * 255 / 640);
                image.bgra[i+1] = static_cast<uint8_t>(y * 255 / 360);
                image.bgra[i+2] = static_cast<uint8_t>(frame * 2);
                image.bgra[i+3] = 255;
            }
            // Exercise odd source size and resized-window letterboxing as well.
            if (frame >= 30 && frame < 60) image = resizeImage(image, 411, 273);
            writer.video(image, frame);
            if (audio) {
                for (int i = 0; i < 1600; ++i) {
                    double phase = (frame * 1600 + i) * 6.283185307179586 / 48000;
                    pcm[i*2] = dsp::pcm16(static_cast<float>(.25 * std::sin(phase * 440)));
                    pcm[i*2+1] = dsp::pcm16(static_cast<float>(.25 * std::sin(phase * 660)));
                }
                writer.audio(pcm, static_cast<int64_t>(frame) * 1600);
            }
        }
        writer.finalize();
    }
    return 0;
}
