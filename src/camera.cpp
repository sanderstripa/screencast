#include "camera.h"
#include "resample.h"
#include <wincodec.h>
#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Foundation.Collections.h>
using namespace winrt::Windows::AI::MachineLearning;

std::vector<Device> cameraDevices() {
    ComPtr<IMFAttributes> attributes; check(MFCreateAttributes(&attributes, 1));
    check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    IMFActivate** devices{}; UINT count{};
    check(MFEnumDeviceSources(attributes.Get(), &devices, &count));
    std::vector<Device> result;
    for (UINT i = 0; i < count; ++i) {
        wchar_t *name{}, *id{}; UINT length{};
        if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length)) &&
            SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &id, &length)))
            result.push_back({name, id});
        CoTaskMemFree(name); CoTaskMemFree(id); devices[i]->Release();
    }
    CoTaskMemFree(devices); return result;
}
Image loadImage(std::wstring const& path) {
    ComPtr<IWICImagingFactory> factory;
    check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> frame; check(decoder->GetFrame(0, &frame));
    UINT w{}, h{}; check(frame->GetSize(&w, &h));
    if (!w || !h || w > 16384 || h > 16384 || static_cast<uint64_t>(w) * h > 50000000)
        throw winrt::hresult_error(E_INVALIDARG, L"Фоновое изображение слишком большое");
    ComPtr<IWICFormatConverter> converter; check(factory->CreateFormatConverter(&converter));
    check(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    Image image{static_cast<int>(w), static_cast<int>(h), std::vector<uint8_t>(static_cast<size_t>(w) * h * 4)};
    check(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(image.bgra.size()), image.bgra.data()));
    return image;
}
Image resizeImage(Image const& input, int width, int height) {
    if (input.empty() || width <= 0 || height <= 0) return {};
    Image out{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
    out.time100ns = input.time100ns;
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        auto value=sampleBgra(input.bgra.data(),input.width,input.height,(x+.5f)*input.width/width-.5f,(y+.5f)*input.height/height-.5f);
        auto dest=out.bgra.data()+(static_cast<size_t>(y)*width+x)*4;for(int c=0;c<4;++c)dest[c]=static_cast<uint8_t>(value[c]+.5f);
    }
    return out;
}
static Image composeMatte(Image const& source,Image const& background,std::vector<float> const& alpha,int maskWidth,int maskHeight) {
    Image output=source;
    float scale=std::max(float(source.width)/background.width,float(source.height)/background.height);
    float cropW=source.width/scale,cropH=source.height/scale;
    auto renderRows=[&](int first,int last) {for(int y=first;y<last;++y)for(int x=0;x<source.width;++x) {
        BilinearPoint mask(maskWidth,maskHeight,(x+.5f)*maskWidth/source.width-.5f,(y+.5f)*maskHeight/source.height-.5f);
        float a=std::clamp(mask.sample([&](int sx,int sy){return alpha[static_cast<size_t>(sy)*maskWidth+sx];}),0.f,1.f);
        auto bg=sampleBgra(background.bgra.data(),background.width,background.height,(background.width-cropW)/2+(x+.5f)/scale-.5f,(background.height-cropH)/2+(y+.5f)/scale-.5f);
        auto pixel=output.bgra.data()+(static_cast<size_t>(y)*source.width+x)*4;
        for(int c=0;c<3;++c)pixel[c]=static_cast<uint8_t>(pixel[c]*a+bg[c]*(1-a)+.5f);
        pixel[3]=255;
    }};
    unsigned count=source.width*source.height>=300000?std::clamp(std::thread::hardware_concurrency()/2,1u,4u):1u;
    std::vector<std::jthread> workers;workers.reserve(count-1);
    for(unsigned i=1;i<count;++i)workers.emplace_back(renderRows,source.height*i/count,source.height*(i+1)/count);
    renderRows(0,source.height/count);workers.clear();
    return output;
}
void compositeCamera(Image& screen, Image const& camera, int corner) {
    compositeCamera(screen, camera, Point{float(corner % 2), float(corner / 2)});
}
void compositeCamera(Image& screen, Image const& camera, Point position, bool mirror) {
    if (camera.empty() || screen.empty()) return;
    int diameter = std::min(screen.width, screen.height) / 4;
    if (diameter < 8) return;
    auto placement = circlePlacement(screen.width, screen.height, position);
    int left = placement.left, top = placement.top;
    // Cache coverage: movement must not recompute a square root for every pixel.
    thread_local int cachedDiameter{};
    thread_local std::vector<float> coverage;
    if (cachedDiameter != diameter) {
        cachedDiameter = diameter; coverage.resize(static_cast<size_t>(diameter)*diameter);
        for (int y=0;y<diameter;++y) for(int x=0;x<diameter;++x) {
            float dx=x+.5f-diameter*.5f, dy=y+.5f-diameter*.5f;
            coverage[static_cast<size_t>(y)*diameter+x]=std::clamp(diameter*.5f-std::sqrt(dx*dx+dy*dy),0.f,1.f);
        }
    }
    // Crop camera to a square before fitting into the circular mask.
    int crop = std::min(camera.width, camera.height);
    int cropX = (camera.width - crop) / 2, cropY = (camera.height - crop) / 2;
    for (int y = 0; y < diameter; ++y) for (int x = 0; x < diameter; ++x) {
        float edge = coverage[static_cast<size_t>(y)*diameter+x];
        if (edge == 0) continue;
        float sx=cropX+(x+.5f)*crop/diameter-.5f,sy=cropY+(y+.5f)*crop/diameter-.5f;
        if(mirror)sx=camera.width-1-sx;
        auto src=sampleBgra(camera.bgra.data(),camera.width,camera.height,sx,sy);
        auto dest = screen.bgra.data() + (static_cast<size_t>(top + y) * screen.width + left + x) * 4;
        for (int c = 0; c < 3; ++c) dest[c] = static_cast<uint8_t>(src[c] * edge + dest[c] * (1 - edge));
        dest[3] = 255;
    }
}
Image backgroundImage(BackgroundMode mode, int preset, int width, int height) {
    const std::array<std::array<int,3>,4> colors{{{48,49,52},{244,241,234},{221,224,226},{220,205,184}}};
    auto a=colors[std::clamp(preset,0,3)], b=colors[(std::clamp(preset,0,3)+1)%4];
    Image image{width,height,std::vector<uint8_t>(static_cast<size_t>(width)*height*4)};
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        float t=mode==BackgroundMode::Gradient ? float(x+y)/std::max(1,width+height-2) : 0.f;
        auto p=image.bgra.data()+(static_cast<size_t>(y)*width+x)*4;
        for(int c=0;c<3;++c) p[c]=static_cast<uint8_t>(a[2-c]*(1-t)+b[2-c]*t); p[3]=255;
    }
    return image;
}
Image blurImage(Image const& image,int radius) {
    if(image.empty()) return {};
    Image out=image,temp=image; radius=std::clamp(radius,1,40);
    // Three separable box passes approximate Gaussian blur in O(pixels), independent of radius.
    for(int pass=0;pass<3;++pass) for(int axis=0;axis<2;++axis) {
        int length=axis?image.height:image.width, rows=axis?image.width:image.height;
        for(int row=0;row<rows;++row) for(int c=0;c<3;++c) {
            auto index=[&](int value) { int x=axis?row:std::clamp(value,0,length-1),y=axis?std::clamp(value,0,length-1):row; return (static_cast<size_t>(y)*image.width+x)*4+c; };
            int sum{}; for(int i=-radius;i<=radius;++i) sum+=out.bgra[index(i)];
            for(int i=0;i<length;++i) { temp.bgra[index(i)]=static_cast<uint8_t>(sum/(radius*2+1)); sum+=out.bgra[index(i+radius+1)]-out.bgra[index(i-radius)]; }
        }
        std::swap(out.bgra,temp.bgra);
    }
    return out;
}
struct Camera::State {
    std::mutex mutex;
    std::condition_variable changed;
    std::thread processor;
    std::shared_ptr<Image const> raw, published;
    std::shared_ptr<LiveSettings> live;
    std::wstring customPath;
    Image image, background;
    std::atomic_bool stopped{};
    int width{}, height{}, stride{};
    int64_t timestamp{}, processedAt{};
    std::wstring error;
    uint64_t captured{},effectFrames{};
    int64_t effectTime{},maximumEffectTime{};
    LearningModelSession session{nullptr};
    std::array<TensorFloat, 4> recurrent{nullptr, nullptr, nullptr, nullptr};
    bool replace{};
    ComPtr<IMFSourceReader> reader;

    void format() {
        ComPtr<IMFSourceReader> active;
        {std::lock_guard lock(mutex);active=reader;}
        if(!active || stopped) return;
        ComPtr<IMFMediaType> type; check(active->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,&type));
        UINT32 w{},h{},pitch{}; check(MFGetAttributeSize(type.Get(),MF_MT_FRAME_SIZE,&w,&h));
        if(!w || !h || w>4096 || h>4096) throw winrt::hresult_error(E_INVALIDARG,L"Размер кадра камеры не поддерживается");
        width=w; height=h;
        if(FAILED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&pitch))) {
            LONG value{}; check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1,w,&value)); pitch=static_cast<UINT32>(value);
        }
        stride=static_cast<LONG>(pitch);
    }
    void process() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            int64_t last{};
            BackgroundMode previous=BackgroundMode::Original; int preset=-1;
            while(!stopped) {
                std::shared_ptr<Image const> input;
                { std::unique_lock lock(mutex); changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return stopped || (raw && raw->time100ns!=last);});
                  if(stopped) break; input=raw; }
                if(!input || input->time100ns==last) continue; last=input->time100ns;
                auto mode=live->background.load(); auto selected=live->backgroundPreset.load();
                if(mode==BackgroundMode::Original) { std::lock_guard lock(mutex); published=input; changed.notify_all(); continue; }
                if(!session) {
                    auto path=executableDir()+L"\\models\\rvm_mobilenetv3_fp32.onnx";
                    if(!std::filesystem::exists(path)) throw winrt::hresult_error(E_FAIL,L"Для обработки фона установите модель: scripts/setup-background.ps1");
                    auto model=LearningModel::LoadFromFilePath(path);
                    try { session=LearningModelSession(model,LearningModelDevice(LearningModelDeviceKind::DirectXHighPerformance)); }
                    catch(...) { session=LearningModelSession(model,LearningModelDevice(LearningModelDeviceKind::Cpu)); }
                    for(auto& value:recurrent) value=TensorFloat::CreateFromArray({1,1,1,1},std::array<float,1>{0.f});
                }
                if(mode!=previous || selected!=preset || background.empty()) {
                    if(mode==BackgroundMode::Custom) background=loadImage(customPath);
                    else if(mode==BackgroundMode::Interior) {
                        const wchar_t* names[]{L"milk-room.jpg",L"beige-room.jpg",L"grey-room.jpg"};
                        background=loadImage(executableDir()+L"\\assets\\backgrounds\\"+names[std::clamp(selected,0,2)]);
                    } else background=backgroundImage(mode,selected,256,std::max(32,input->height*256/input->width));
                    previous=mode; preset=selected;
                }
                if(mode==BackgroundMode::Blur) background=blurImage(resizeImage(*input,256,std::max(32,input->height*256/input->width)),live->blurStrength);
                auto started=clock100ns();
                auto output=std::make_shared<Image>(matting(*input)); output->time100ns=input->time100ns;
                auto elapsed=clock100ns()-started;
                { std::lock_guard lock(mutex); published=std::move(output);++effectFrames;effectTime+=elapsed;maximumEffectTime=std::max(maximumEffectTime,elapsed);changed.notify_all(); }
            }
        } catch(...) { std::lock_guard lock(mutex); error=errorText(); changed.notify_all(); }
        session=nullptr; for(auto& value:recurrent) value=nullptr;
        winrt::uninit_apartment();
    }

    Image matting(Image const& source) {
        int h = std::max(32, source.height * 256 / source.width);
        auto imageSmall = resizeImage(source, 256, h);
        size_t plane = static_cast<size_t>(256) * h;
        std::vector<float> rgb(plane * 3);
        for (size_t i = 0; i < plane; ++i) for (int c = 0; c < 3; ++c)
            rgb[static_cast<size_t>(c) * plane + i] = imageSmall.bgra[i * 4 + 2 - c] / 255.f;
        auto tensor = TensorFloat::CreateFromArray({1, 3, h, 256}, rgb);
        LearningModelBinding binding(session);
        binding.Bind(L"src", tensor);
        const wchar_t* names[]{L"r1i", L"r2i", L"r3i", L"r4i"};
        for (int i = 0; i < 4; ++i) binding.Bind(names[i], recurrent[i]);
        binding.Bind(L"downsample_ratio", TensorFloat::CreateFromArray({1}, std::array<float, 1>{1.f}));
        auto result = session.Evaluate(binding, L"camera");
        auto alpha = result.Outputs().Lookup(L"pha").as<TensorFloat>().GetAsVectorView();
        const wchar_t* outputs[]{L"r1o", L"r2o", L"r3o", L"r4o"};
        for (int i = 0; i < 4; ++i) recurrent[i] = result.Outputs().Lookup(outputs[i]).as<TensorFloat>();
        if (alpha.Size() != plane)
            throw winrt::hresult_error(E_FAIL, L"Модель фона вернула неожиданный размер изображения");
        std::vector<float> alphaValues(plane);alpha.GetMany(0,alphaValues);
        // Upsample only the silhouette; preserve the camera's original color/detail.
        return composeMatte(source,background,alphaValues,256,h);
    }
};
class Camera::Callback final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IMFSourceReaderCallback, Microsoft::WRL::FtmBase> {
public:
    explicit Callback(std::shared_ptr<State> const& state) : state_(state) {}
    STDMETHOD(OnReadSample)(HRESULT status, DWORD, DWORD flags, LONGLONG, IMFSample* sample) override {
        auto state = state_.lock(); if (!state || state->stopped) return S_OK;
        try {
            check(status);
            if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))
                throw winrt::hresult_error(E_FAIL, L"Веб-камера отключена");
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) state->format();
            int64_t now = clock100ns();
            if (sample && now - state->processedAt >= 300000) {
                ComPtr<IMFMediaBuffer> buffer; check(sample->ConvertToContiguousBuffer(&buffer));
                Image image{state->width, state->height, std::vector<uint8_t>(static_cast<size_t>(state->width) * state->height * 4)};
                ComPtr<IMF2DBuffer> twoD;
                if (SUCCEEDED(buffer.As(&twoD))) {
                    BYTE* top{}; LONG pitch{}; check(twoD->Lock2D(&top, &pitch));
                    for (int y = 0; y < state->height; ++y)
                        memcpy(image.bgra.data() + static_cast<size_t>(y) * state->width * 4,
                            top + static_cast<ptrdiff_t>(y) * pitch, state->width * 4);
                    check(twoD->Unlock2D());
                } else {
                    BYTE* bytes{}; DWORD length{}; check(buffer->Lock(&bytes, nullptr, &length));
                    if (length < static_cast<uint64_t>(std::abs(state->stride)) * state->height) {
                        buffer->Unlock(); throw winrt::hresult_error(E_FAIL, L"Неполный кадр камеры");
                    }
                    BYTE* top = state->stride < 0 ? bytes + static_cast<size_t>(state->height - 1) * -state->stride : bytes;
                    for (int y = 0; y < state->height; ++y)
                        memcpy(image.bgra.data() + static_cast<size_t>(y) * state->width * 4,
                            top + static_cast<ptrdiff_t>(y) * state->stride, state->width * 4);
                    check(buffer->Unlock());
                }
                image.time100ns = now;
                {
                    std::lock_guard lock(state->mutex);
                    state->raw = std::make_shared<Image>(std::move(image)); state->timestamp = clock100ns();++state->captured;
                    state->changed.notify_all();
                }
                state->processedAt = now;
            }
            ComPtr<IMFSourceReader> reader;
            { std::lock_guard lock(state->mutex); reader = state->reader; }
            if (!state->stopped && reader) check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr));
        } catch (...) { std::lock_guard lock(state->mutex); state->error = errorText(); }
        return S_OK;
    }
    STDMETHOD(OnFlush)(DWORD) override { return S_OK; }
    STDMETHOD(OnEvent)(DWORD, IMFMediaEvent*) override { return S_OK; }
private:
    std::weak_ptr<State> state_;
};
Camera::Camera(Settings const& settings) : state_(std::make_shared<State>()) {
    state_->live = settings.live;
    state_->customPath = settings.backgroundPath;
    ComPtr<IMFAttributes> sourceAttributes; check(MFCreateAttributes(&sourceAttributes, 2));
    check(sourceAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    check(sourceAttributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, settings.cameraId.c_str()));
    check(MFCreateDeviceSource(sourceAttributes.Get(), &source_));
    auto callback = Microsoft::WRL::Make<Callback>(state_);
    ComPtr<IMFAttributes> readerAttributes; check(MFCreateAttributes(&readerAttributes, 3));
    check(readerAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, static_cast<IMFSourceReaderCallback*>(callback.Get())));
    check(readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
    check(MFCreateSourceReaderFromMediaSource(source_.Get(), readerAttributes.Get(), &reader_));
    check(reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE));
    check(reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));
    // Prefer 720p/30; select an actual supported mode rather than forcing a format.
    ComPtr<IMFMediaType> chosen;
    uint64_t score = UINT64_MAX;
    for (DWORD i = 0; ; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type))) break;
        UINT32 w{}, h{}; if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h))) continue;
        UINT32 numerator{}, denominator{}; MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
        double fps = denominator ? double(numerator)/denominator : 0;
        auto candidate = static_cast<uint64_t>(std::abs(static_cast<int>(w)-1280)+std::abs(static_cast<int>(h)-720) + std::abs(fps-30)*200);
        if (candidate < score) { chosen = type; score = candidate; }
    }
    if (chosen) check(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, chosen.Get()));
    ComPtr<IMFMediaType> output; check(MFCreateMediaType(&output));
    check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    check(reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.Get()));
    check(reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &output));
    UINT32 w{}, h{}, stride{}; check(MFGetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, &w, &h));
    if (!w || !h || w > 4096 || h > 4096) throw winrt::hresult_error(E_INVALIDARG, L"Размер кадра камеры не поддерживается");
    state_->width = w; state_->height = h;
    if (FAILED(output->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
        LONG pitch{}; check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, w, &pitch)); stride = static_cast<UINT32>(pitch);
    }
    state_->stride = static_cast<LONG>(stride);
    state_->timestamp = clock100ns();
    state_->reader = reader_;
    check(reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr));
    state_->processor = std::thread([state=state_]{state->process();});
}
Camera::~Camera() {
    state_->stopped = true; state_->changed.notify_all();
    // The callback holds only a weak state pointer; there is no reader/callback ownership cycle.
    { std::lock_guard lock(state_->mutex); state_->reader.Reset(); }
    if (reader_) reader_->Flush(MF_SOURCE_READER_ALL_STREAMS);
    if (source_) source_->Shutdown();
    if(state_->processor.joinable()) state_->processor.join();
}
Image Camera::latest() { auto value=latestFrame(); return value ? *value : Image{}; }
std::shared_ptr<Image const> Camera::latestFrame() {
    std::lock_guard lock(state_->mutex);
    if (!state_->error.empty()) throw winrt::hresult_error(E_FAIL,state_->error);
    if(clock100ns()-state_->timestamp>100000000) throw winrt::hresult_error(E_FAIL,L"Камера не передает кадры. Проверьте доступ к камере в настройках Windows");
    return state_->published;
}
bool Camera::waitForFirstFrame(std::atomic_bool const& stop,int timeoutMs) {
    auto deadline=clock100ns()+int64_t(timeoutMs)*10000;
    while(!stop) {
        if(auto frame=latestFrame(); frame && !frame->empty()) return true;
        if(clock100ns()>deadline) throw winrt::hresult_error(E_FAIL,L"Камера не передала первый кадр. Проверьте разрешение доступа и закройте другие приложения камеры");
        std::unique_lock lock(state_->mutex); state_->changed.wait_for(lock,std::chrono::milliseconds(20));
    }
    return false;
}
std::wstring Camera::diagnostics() {
    std::lock_guard lock(state_->mutex);
    return std::to_wstring(state_->width)+L"x"+std::to_wstring(state_->height)+L" captured="+std::to_wstring(state_->captured)+
        L" effects="+std::to_wstring(state_->effectFrames)+L" mean_effect_ms="+std::to_wstring(state_->effectFrames?double(state_->effectTime)/state_->effectFrames/10000:0)+
        L" max_effect_ms="+std::to_wstring(double(state_->maximumEffectTime)/10000);
}
void Camera::testModel() {
    State state;
    auto model = LearningModel::LoadFromFilePath(executableDir() + L"\\models\\rvm_mobilenetv3_fp32.onnx");
    state.session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::Cpu));
    for (auto& recurrent : state.recurrent)
        recurrent = TensorFloat::CreateFromArray({1, 1, 1, 1}, std::array<float, 1>{0.f});
    state.background = {64, 64, std::vector<uint8_t>(64 * 64 * 4)};
    for (size_t i = 0; i < state.background.bgra.size(); i += 4) {
        state.background.bgra[i] = 210; state.background.bgra[i+1] = 40;
        state.background.bgra[i+2] = 20; state.background.bgra[i+3] = 255;
    }
    Image source{320, 240, std::vector<uint8_t>(320 * 240 * 4, 110)};
    Image detail{1280,720,std::vector<uint8_t>(1280*720*4)};
    for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto p=detail.bgra.data()+(y*1280+x)*4;p[0]=p[1]=p[2]=uint8_t((x%2)*255);p[3]=255;}
    auto retained=composeMatte(detail,state.background,std::vector<float>(256*144,1.f),256,144);
    if(retained.bgra!=detail.bgra)throw winrt::hresult_error(E_FAIL,L"Обработка фона потеряла исходные детали камеры");
    for (int frame = 0; frame < 2; ++frame) {
        auto result = state.matting(source);
        if (result.width != source.width || result.height != source.height || result.bgra.size() != source.bgra.size())
            throw winrt::hresult_error(E_FAIL, L"Неверный размер кадра после замены фона");
        bool changed{};
        for (size_t i = 0; i < result.bgra.size(); i += 4) {
            changed |= std::abs(result.bgra[i] - 110) > 10;
            if (result.bgra[i+3] != 255) throw winrt::hresult_error(E_FAIL, L"Неверный альфа-канал композиции");
        }
        if (!changed) throw winrt::hresult_error(E_FAIL, L"Замена фона не изменила кадр");
    }
}
