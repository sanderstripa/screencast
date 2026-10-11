#include "recorder.h"
#include "capture.h"
#include "audio.h"
#include "camera.h"
#include "spectral.h"
#include <avrt.h>
#include <deque>
#include <d3d11.h>
#include <fstream>
#include <mftransform.h>
#include "overlay.h"
#include "live_capture.h"

namespace {
class Writer {
public:
    Writer(std::wstring const& path, int w, int h, int fps, bool audio) : width(w), height(h), fps_(fps) {
        try { open(path, audio, true); }
        catch (...) { sink.Reset(); open(path, audio, false); }
    }
    bool hardware() const {return hardware_;}
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
    ComPtr<ID3D11Device> device;
    ComPtr<IMFDXGIDeviceManager> manager;
    bool hardware_{};
    void open(std::wstring const& path, bool audio, bool hardware) {
        ComPtr<IMFAttributes> attrs; check(MFCreateAttributes(&attrs, 2));
        check(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, hardware));
        check(attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
        if(hardware) {
            // Associate the encoder with the actual adapter; plain CPU buffers remain a supported fallback.
            if(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr,0,D3D11_SDK_VERSION,&device,nullptr,nullptr))) {
                UINT token{};
                if(SUCCEEDED(MFCreateDXGIDeviceManager(&token,&manager)) && SUCCEEDED(manager->ResetDevice(device.Get(),token)))
                    check(attrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER,manager.Get()));
            }
        } else {manager.Reset();device.Reset();}
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
        ComPtr<IMFSinkWriterEx> extended;
        if(SUCCEEDED(sink.As(&extended))) for(DWORD i=0;i<8;++i) {
            GUID category{};ComPtr<IMFTransform> transform;
            if(FAILED(extended->GetTransformForStream(videoStream,i,&category,&transform))) break;
            if(category!=MFT_CATEGORY_VIDEO_ENCODER) continue;
            ComPtr<IMFAttributes> attributes;
            if(SUCCEEDED(transform->GetAttributes(&attributes))) {
                UINT length{};
                if(SUCCEEDED(attributes->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute,&length)) && length) hardware_=true;
            }
        }
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
    join(); stop_ = false; epoch_ = 0; busy_ = true; preview_=nullptr;
    webcam_=nullptr; settings.live->overlayReady=0;
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
            if(camera && !camera->waitForFirstFrame(stop_)) { result->cancelled=true; throw winrt::hresult_error(E_ABORT,L"Запуск отменен"); }
            if(camera) {
                webcam_=camera->latestFrame();
                auto deadline=clock100ns()+50000000;
                while(!stop_ && !settings.live->overlayReady) {
                    if(clock100ns()>deadline) throw winrt::hresult_error(E_FAIL,L"Не удалось показать кружок камеры на экране");
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
            Image image;
            auto deadline = clock100ns() + 50000000;
            while (!stop_) {
                if(capture.next(image) && (!camera || !settings.target.monitor || image.time100ns>=settings.live->overlayReady.load())) break;
                if (clock100ns() > deadline) throw winrt::hresult_error(E_FAIL, L"Нет кадров от источника. Восстановите свернутое окно");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (stop_) { result->cancelled = true; throw winrt::hresult_error(E_ABORT, L"Запуск отменен"); }
            GUID id{}; check(CoCreateGuid(&id)); wchar_t suffix[40]{}; StringFromGUID2(id, suffix, 40);
            temporary = settings.path + L"." + suffix + L".partial.mp4";
            // The MP4 stream layout is fixed at start; reserve AAC for later mic activation.
            bool audioTrack=system || mic || settings.micAvailable;
            writer = std::make_unique<Writer>(temporary, (image.width + 1) & ~1, (image.height + 1) & ~1, settings.fps, audioTrack);
            hardware_=writer->hardware();
            int64_t epoch = clock100ns(); epoch_ = epoch;
            if (system) system->start(epoch); if (mic) mic->start(epoch);
            recording_ = true;
            int64_t videoFrame{}, audioFrame{}, nextDiskCheck{}, stopAt{};
            std::deque<Image> videoQueue;
            std::deque<std::shared_ptr<Image const>> cameraQueue;
            auto cameraImage=camera?camera->latestFrame():nullptr;
            SmoothMotion movement({settings.live->cameraX,settings.live->cameraY});
            int64_t nextCapture{};
            int64_t lastCameraTimestamp{};
            dsp::SpectralDenoiser suppressor;
            dsp::Timeline micTail;
            std::vector<float> cleanMic;
            float systemGain=settings.live->systemGain,micGain=settings.live->micGain;
            std::vector<int16_t> pcm;
            // Delay output by 150 ms so audio packets arrive before their timeline is mixed.
            constexpr int64_t latency = 1500000;
            while (true) {
                auto now = clock100ns() - epoch;
                if (stop_ && !stopAt) stopAt = std::max<int64_t>(now, 1);
                if(!stopAt) {
                    bool wantMic=settings.live->micEnabled && settings.micAvailable;
                    if(mic && !wantMic){mic->poll();micTail=mic->timeline();}
                    if(setCaptureEnabled(mic,wantMic,[&]{auto value=std::make_unique<AudioCapture>(true,AudioMode::Off,settings.micId,0);value->start(epoch);return value;}))suppressor=dsp::SpectralDenoiser{};
                    bool wantCamera=settings.live->cameraEnabled && !settings.cameraId.empty();
                    if(setCaptureEnabled(camera,wantCamera,[&]{return std::make_unique<Camera>(settings);})) {
                        cameraQueue.clear();cameraImage=nullptr;lastCameraTimestamp=0;webcam_=nullptr;
                    }
                }
                if (system) system->poll(); if (mic) mic->poll();
                if (process.value && WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0)
                    throw winrt::hresult_error(E_ABORT, L"Приложение, звук которого записывался, завершилось");
                if(now>=nextCapture) {
                    Image fresh; if (capture.next(fresh)) videoQueue.push_back(std::move(fresh));
                    nextCapture=now+10000000/settings.fps;
                    while(videoQueue.size()>16) videoQueue.pop_front();
                }
                if (camera) {
                    auto value = camera->latestFrame();
                    webcam_=value;
                    if (value && value->time100ns > lastCameraTimestamp) {
                        lastCameraTimestamp = value->time100ns; cameraQueue.push_back(std::move(value));
                        while(cameraQueue.size()>16) cameraQueue.pop_front();
                    }
                }
                int64_t ready = std::min(now - latency, stopAt ? stopAt : now);
                while (videoFrame * 10000000 / settings.fps <= ready) {
                    auto due = epoch + videoFrame * 10000000 / settings.fps;
                    while (!videoQueue.empty() && videoQueue.front().time100ns <= due) {
                        image = std::move(videoQueue.front()); videoQueue.pop_front();
                    }
                    while (!cameraQueue.empty() && cameraQueue.front()->time100ns <= due) {
                        cameraImage = std::move(cameraQueue.front()); cameraQueue.pop_front();
                    }
                    Image output = image;
                    auto position=movement.step({settings.live->cameraX,settings.live->cameraY},1.f/settings.fps);
                    cameraX_=position.x; cameraY_=position.y;
                    // A monitor includes the real desktop overlay, also visible to Print Screen.
                    // Window capture excludes unrelated top-level windows, so composite there only.
                    if (cameraImage && settings.live->cameraEnabled && !settings.target.monitor) compositeCamera(output, *cameraImage, position,settings.live->cameraMirror);
                    preview_=std::make_shared<Image>(resizeImage(output,480,std::max(1,output.height*480/output.width)));
                    writer->video(output, videoFrame++);
                    // Falling behind by >3 s signals overload instead of allocating an unbounded queue.
                    if (clock100ns() - epoch - videoFrame * 10000000 / settings.fps > 30000000)
                        throw winrt::hresult_error(E_FAIL, L"Компьютер не успевает кодировать запись. Уменьшите разрешение источника или выключите замену фона");
                }
                if (audioTrack) {
                    int64_t readyFrames = std::max<int64_t>(0, ready) * dsp::Rate / 10000000;
                    while (audioFrame + 480 <= readyFrames) {
                        float strength=settings.live->denoise?settings.live->noiseStrength.load():0.f;
                        auto const& microphone=mic?mic->timeline():micTail;
                        if(strength>0) suppressor.render(microphone,audioFrame,480,strength,cleanMic);
                        pcm.resize(960);
                        for(int i=0;i<480;++i) {
                            // 20 ms gain ramp prevents clicks when changing a live slider.
                            systemGain+=(settings.live->systemGain.load()-systemGain)*.001f;
                            micGain+=((settings.live->micEnabled?settings.live->micGain.load():0.f)-micGain)*.001f;
                            auto a=system?system->timeline().at(audioFrame+i):std::array<float,2>{};
                            auto b=microphone.at(audioFrame+i);
                            if(strength>0) b={cleanMic[i],cleanMic[i]};
                            pcm[i*2]=dsp::pcm16(a[0]*systemGain+b[0]*micGain);
                            pcm[i*2+1]=dsp::pcm16(a[1]*systemGain+b[1]*micGain);
                        }
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
        webcam_=nullptr; recording_ = false; busy_ = false;
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
    Writer cameraWriter(directory+L"\\synthetic-camera.mp4",640,360,30,false);
    Image camera{128,128,std::vector<uint8_t>(128*128*4)};
    for(size_t i=0;i<camera.bgra.size();i+=4) {camera.bgra[i]=10;camera.bgra[i+1]=20;camera.bgra[i+2]=245;camera.bgra[i+3]=255;}
    SmoothMotion motion({0,0});
    for(int frame=0;frame<90;++frame) {
        Image image{640,360,std::vector<uint8_t>(640*360*4,30)};
        auto p=motion.step(frame<15?Point{0,0}:Point{1,1},1.f/30);
        compositeCamera(image,camera,p);cameraWriter.video(image,frame);
    }
    cameraWriter.finalize();
    Writer liveWriter(directory+L"\\synthetic-live-controls.mp4",640,360,30,true);
    Image asymmetric=camera;
    for(int y=0;y<128;++y)for(int x=64;x<128;++x){auto p=asymmetric.bgra.data()+(y*128+x)*4;p[0]=245;p[1]=20;p[2]=10;}
    auto live=std::make_shared<LiveSettings>();dsp::Timeline testSystem,testMic;std::vector<int16_t> livePcm;
    for(int frame=0;frame<90;++frame) {
        live->cameraEnabled=frame>=15 && frame<75;live->cameraMirror=frame>=45;
        live->micEnabled=frame>=30 && frame<60;
        Image screenFrame{640,360,std::vector<uint8_t>(640*360*4,30)};
        if(live->cameraEnabled)compositeCamera(screenFrame,asymmetric,Point{0,0},live->cameraMirror);
        liveWriter.video(screenFrame,frame);
        for(int i=0;i<1600;++i){auto sample=frame*1600+i;auto phase=sample*6.283185307179586/dsp::Rate;testSystem.put(sample,float(.2*std::sin(phase*440)),float(.2*std::sin(phase*440)));testMic.put(sample,float(.2*std::sin(phase*880)),float(.2*std::sin(phase*880)));}
        dsp::mix(&testSystem,&testMic,frame*1600,1600,livePcm,nullptr,1,live->micEnabled?1.f:0.f);liveWriter.audio(livePcm,frame*1600);
    }
    liveWriter.finalize();
    // Representative 1080p timing, including camera composition and actual encoder writes.
    std::wofstream performance{std::filesystem::path(directory)/L"performance.txt"};
    Writer benchmark(directory+L"\\benchmark-1080p.mp4",1920,1080,30,false);
    Image screen{1920,1080,std::vector<uint8_t>(1920*1080*4,40)};
    int64_t total{},maximum{},benchmarkStart=clock100ns();
    for(int i=0;i<90;++i) {
        auto begin=clock100ns();std::fill(screen.bgra.begin(),screen.bgra.end(),40);
        compositeCamera(screen,camera,Point{float(i)/89,1});benchmark.video(screen,i);
        auto elapsed=clock100ns()-begin;total+=elapsed;maximum=std::max(maximum,elapsed);
    }
    benchmark.finalize();
    auto wall=clock100ns()-benchmarkStart;
    performance<<L"1080p camera composition + encode, 90 frames\nmean_frame_ms="<<double(total)/900000
        <<L"\nmax_frame_ms="<<double(maximum)/10000<<L"\nwall_seconds_including_finalize="<<double(wall)/10000000
        <<L"\nencoded_fps="<<900000000.0/wall<<L"\nhardware_encoder="<<benchmark.hardware()<<L"\n";
    auto blurred=blurImage(camera,12);
    if(blurred.bgra!=camera.bgra) throw winrt::hresult_error(E_FAIL,L"Размытие изменило однотонное изображение");
    return 0;
}
int liveDeviceSelfTest(std::wstring const& directory) {
    std::filesystem::create_directories(directory);std::ofstream log{std::filesystem::path(directory)/L"live-devices.txt"};
    Settings settings;auto cameras=cameraDevices();auto microphones=audioDevices(true);std::atomic_bool stop{};
    std::unique_ptr<Camera> camera;std::unique_ptr<AudioCapture> mic;
    for(int cycle=0;cycle<2;++cycle) {
        if(!cameras.empty()) {
            settings.cameraId=cameras.front().id;setCaptureEnabled(camera,true,[&]{return std::make_unique<Camera>(settings);});
            if(!camera->waitForFirstFrame(stop))throw std::runtime_error("Camera restart did not deliver a frame");
            setCaptureEnabled(camera,false,[&]{return std::make_unique<Camera>(settings);});if(camera)throw std::runtime_error("Camera remained open");
        }
        if(microphones.size()>1) {
            setCaptureEnabled(mic,true,[&]{auto value=std::make_unique<AudioCapture>(true,AudioMode::Off,microphones[1].id,0);value->start(clock100ns());return value;});
            auto until=clock100ns()+3000000;while(clock100ns()<until){mic->poll();std::this_thread::sleep_for(std::chrono::milliseconds(3));}
            setCaptureEnabled(mic,false,[&]{return std::make_unique<AudioCapture>(true,AudioMode::Off,microphones[1].id,0);});if(mic)throw std::runtime_error("Microphone remained open");
        }
    }
    log<<"Camera open/frame/close cycles: "<<(cameras.empty()?0:2)<<"\nMicrophone open/poll/close cycles: "<<(microphones.size()>1?2:0)<<"\nNo camera images or microphone samples saved.\nPASS\n";
    return 0;
}
int cameraSelfTest(std::wstring const& directory) {
    std::wofstream log{std::filesystem::path(directory)/L"camera-diagnostics.txt"};
    auto devices=cameraDevices();
    if(devices.empty()) {log<<L"No physical webcam detected; hardware capture test unavailable.\n";return 0;}
    Settings settings;settings.cameraId=devices.front().id;std::atomic_bool stop{};
    Camera camera(settings);auto started=clock100ns();camera.waitForFirstFrame(stop);
    log<<L"first_frame_ms="<<double(clock100ns()-started)/10000<<L"\n";
    for(auto mode:{BackgroundMode::Original,BackgroundMode::Blur,BackgroundMode::Solid,BackgroundMode::Gradient,BackgroundMode::Interior}) {
        if(mode!=BackgroundMode::Original && !std::filesystem::exists(executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx")) continue;
        settings.live->background=mode;
        // Exclude one-time GPU/model initialization from the steady-state rate.
        auto changedAt=clock100ns(),warmupDeadline=changedAt+100000000;
        while(clock100ns()<warmupDeadline) {
            auto frame=camera.latestFrame();
            if(frame && frame->time100ns>=changedAt) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        log<<L"mode="<<int(mode)<<L" warmup_ms="<<double(clock100ns()-changedAt)/10000<<L"\n";
        auto until=clock100ns()+30000000;int count{};int64_t previous{};
        while(clock100ns()<until) {
            auto frame=camera.latestFrame();if(frame && frame->time100ns!=previous) {previous=frame->time100ns;++count;}
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        log<<L"mode="<<int(mode)<<L" delivered_fps="<<count/3.0<<L" "<<camera.diagnostics()<<L"\n";log.flush();
    }
    return 0;
}
int overlaySelfTest(std::wstring const& directory) {
    Target target;target.monitor=MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};check(GetMonitorInfoW(target.monitor,&info)?S_OK:E_FAIL);
    auto live=std::make_shared<LiveSettings>();live->cameraX=0;live->cameraY=0;
    auto camera=std::make_shared<Image>(Image{128,128,std::vector<uint8_t>(128*128*4)});
    for(size_t i=0;i<camera->bgra.size();i+=4){camera->bgra[i]=10;camera->bgra[i+1]=20;camera->bgra[i+2]=245;camera->bgra[i+3]=255;}
    CameraOverlay overlay;overlay.configure(target,live);overlay.update(camera);
    auto c=circlePlacement(info.rcMonitor.right-info.rcMonitor.left,info.rcMonitor.bottom-info.rcMonitor.top,{0,0});
    int x=c.left+c.diameter/2,y=c.top+c.diameter/2;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    HDC desktop=GetDC(nullptr),memory=CreateCompatibleDC(desktop);auto bitmap=CreateCompatibleBitmap(desktop,1,1);auto old=SelectObject(memory,bitmap);
    check(BitBlt(memory,0,0,1,1,desktop,info.rcMonitor.left+x,info.rcMonitor.top+y,SRCCOPY|CAPTUREBLT)?S_OK:E_FAIL);
    auto color=GetPixel(memory,0,0);SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(nullptr,desktop);
    if(GetRValue(color)<180 || GetGValue(color)>80) throw winrt::hresult_error(E_FAIL,L"Кружок не виден на снимке рабочего стола");
    Capture capture(target);Image screen;auto until=clock100ns()+50000000;
    do {if(capture.next(screen) && screen.time100ns>=live->overlayReady.load())break;std::this_thread::sleep_for(std::chrono::milliseconds(5));}while(clock100ns()<until);
    if(screen.empty()) throw winrt::hresult_error(E_FAIL,L"Нет кадра для проверки кружка");
    auto pixel=screen.bgra.data()+(static_cast<size_t>(y)*screen.width+x)*4;
    if(pixel[2]<180 || pixel[1]>80) throw winrt::hresult_error(E_FAIL,L"Кружок не попал в захват монитора");
    // Store only a synthetic red crop. Personal desktop contents never enter the artifact.
    Image crop{64,64,std::vector<uint8_t>(64*64*4)};
    for(int row=0;row<64;++row)memcpy(crop.bgra.data()+row*64*4,screen.bgra.data()+(static_cast<size_t>(y-32+row)*screen.width+x-32)*4,64*4);
    Writer writer(directory+L"\\desktop-overlay.mp4",64,64,30,false);for(int frame=0;frame<30;++frame)writer.video(crop,frame);writer.finalize();
    std::ofstream log{std::filesystem::path(directory)/L"desktop-overlay.txt"};log<<"Layered circle visible in desktop screenshot and Windows Graphics Capture. Encoded synthetic-only crop.\n";
    overlay.hide();return 0;
}
