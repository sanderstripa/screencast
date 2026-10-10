#include "camera.h"
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
        int sy = y * input.height / height, sx = x * input.width / width;
        memcpy(out.bgra.data() + (static_cast<size_t>(y) * width + x) * 4,
            input.bgra.data() + (static_cast<size_t>(sy) * input.width + sx) * 4, 4);
    }
    return out;
}
void compositeCamera(Image& screen, Image const& camera, int corner) {
    if (camera.empty() || screen.empty()) return;
    int diameter = std::min(screen.width, screen.height) / 4;
    if (diameter < 8) return;
    int margin = std::max(4, diameter / 12);
    int left = (corner % 2) ? screen.width - diameter - margin : margin;
    int top = (corner / 2) ? screen.height - diameter - margin : margin;
    float radius = diameter * .5f;
    // Crop camera to a square before fitting into the circular mask.
    int crop = std::min(camera.width, camera.height);
    int cropX = (camera.width - crop) / 2, cropY = (camera.height - crop) / 2;
    for (int y = 0; y < diameter; ++y) for (int x = 0; x < diameter; ++x) {
        float dx = x + .5f - radius, dy = y + .5f - radius;
        float edge = std::clamp(radius - std::sqrt(dx * dx + dy * dy), 0.f, 1.f);
        if (edge == 0) continue;
        int sx = cropX + x * crop / diameter, sy = cropY + y * crop / diameter;
        auto src = camera.bgra.data() + (static_cast<size_t>(sy) * camera.width + sx) * 4;
        auto dest = screen.bgra.data() + (static_cast<size_t>(top + y) * screen.width + left + x) * 4;
        for (int c = 0; c < 3; ++c) dest[c] = static_cast<uint8_t>(src[c] * edge + dest[c] * (1 - edge));
        dest[3] = 255;
    }
}
struct Camera::State {
    std::mutex mutex;
    Image image, background;
    std::atomic_bool stopped{};
    int width{}, height{}, stride{};
    int64_t timestamp{}, processedAt{};
    std::wstring error;
    LearningModelSession session{nullptr};
    std::array<TensorFloat, 4> recurrent{nullptr, nullptr, nullptr, nullptr};
    bool replace{};
    ComPtr<IMFSourceReader> reader;

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
        auto fg = result.Outputs().Lookup(L"fgr").as<TensorFloat>().GetAsVectorView();
        const wchar_t* outputs[]{L"r1o", L"r2o", L"r3o", L"r4o"};
        for (int i = 0; i < 4; ++i) recurrent[i] = result.Outputs().Lookup(outputs[i]).as<TensorFloat>();
        if (alpha.Size() != plane || fg.Size() != plane * 3)
            throw winrt::hresult_error(E_FAIL, L"Модель фона вернула неожиданный размер изображения");
        // Center-crop background, retaining its aspect ratio.
        float scale = std::max(256.f / background.width, static_cast<float>(h) / background.height);
        float cropW = 256 / scale, cropH = h / scale;
        for (int y = 0; y < h; ++y) for (int x = 0; x < 256; ++x) {
            size_t i = static_cast<size_t>(y) * 256 + x;
            float a = std::clamp(alpha.GetAt(static_cast<uint32_t>(i)), 0.f, 1.f);
            int bx = std::clamp(static_cast<int>((background.width - cropW) / 2 + x / scale), 0, background.width - 1);
            int by = std::clamp(static_cast<int>((background.height - cropH) / 2 + y / scale), 0, background.height - 1);
            auto bg = background.bgra.data() + (static_cast<size_t>(by) * background.width + bx) * 4;
            for (int c = 0; c < 3; ++c) {
                float foreground = std::clamp(fg.GetAt(static_cast<uint32_t>((2 - c) * plane + i)), 0.f, 1.f) * 255;
                imageSmall.bgra[i * 4 + c] = static_cast<uint8_t>(foreground * a + bg[c] * (1 - a));
            }
            imageSmall.bgra[i * 4 + 3] = 255;
        }
        return imageSmall;
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
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                throw winrt::hresult_error(E_FAIL, L"Формат камеры изменился. Начните запись заново");
            int64_t now = clock100ns();
            if (sample && now - state->processedAt >= 1000000) {
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
                if (state->replace) image = state->matting(image);
                image.time100ns = now;
                {
                    std::lock_guard lock(state->mutex);
                    state->image = std::move(image); state->timestamp = clock100ns();
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
    state_->replace = settings.replaceBackground;
    if (state_->replace) {
        state_->background = loadImage(settings.backgroundPath);
        auto path = executableDir() + L"\\models\\rvm_mobilenetv3_fp32.onnx";
        if (!std::filesystem::exists(path))
            throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"Модель замены фона не установлена. Запустите scripts/setup-background.ps1");
        auto model = LearningModel::LoadFromFilePath(path);
        // CPU mode keeps the optional model usable on machines without a supported GPU.
        state_->session = LearningModelSession(model, LearningModelDevice(LearningModelDeviceKind::Cpu));
        for (auto& recurrent : state_->recurrent)
            recurrent = TensorFloat::CreateFromArray({1, 1, 1, 1}, std::array<float, 1>{0.f});
    }
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
    // Choose a modest native frame size; do not force a format unsupported by the camera.
    ComPtr<IMFMediaType> chosen;
    uint64_t score = UINT64_MAX;
    for (DWORD i = 0; ; ++i) {
        ComPtr<IMFMediaType> type;
        if (FAILED(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type))) break;
        UINT32 w{}, h{}; if (FAILED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h))) continue;
        auto candidate = static_cast<uint64_t>(std::abs(static_cast<int>(w) - 640)) + std::abs(static_cast<int>(h) - 480);
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
}
Camera::~Camera() {
    state_->stopped = true;
    // The callback holds only a weak state pointer; there is no reader/callback ownership cycle.
    { std::lock_guard lock(state_->mutex); state_->reader.Reset(); }
    if (reader_) reader_->Flush(MF_SOURCE_READER_ALL_STREAMS);
    if (source_) source_->Shutdown();
}
Image Camera::latest() {
    std::lock_guard lock(state_->mutex);
    if (!state_->error.empty()) throw winrt::hresult_error(E_FAIL, state_->error);
    if (clock100ns() - state_->timestamp > 50000000)
        throw winrt::hresult_error(E_FAIL, L"Камера не передает кадры более пяти секунд");
    return state_->image;
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
    for (int frame = 0; frame < 2; ++frame) {
        auto result = state.matting(source);
        if (result.width != 256 || result.height != 192 || result.bgra.size() != 256 * 192 * 4)
            throw winrt::hresult_error(E_FAIL, L"Неверный размер кадра после замены фона");
        bool changed{};
        for (size_t i = 0; i < result.bgra.size(); i += 4) {
            changed |= std::abs(result.bgra[i] - 110) > 10;
            if (result.bgra[i+3] != 255) throw winrt::hresult_error(E_FAIL, L"Неверный альфа-канал композиции");
        }
        if (!changed) throw winrt::hresult_error(E_FAIL, L"Замена фона не изменила кадр");
    }
}
