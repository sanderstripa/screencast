#include "capture.h"
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <dwmapi.h>
#include <dxgi.h>
using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

std::vector<Target> monitors() {
    std::vector<Target> result;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL {
        auto& list = *reinterpret_cast<std::vector<Target>*>(param);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            auto r = info.rcMonitor;
            auto name = L"Экран " + std::to_wstring(list.size() + 1) + L" · " +
                std::to_wstring(r.right - r.left) + L" × " + std::to_wstring(r.bottom - r.top);
            if (info.dwFlags & MONITORINFOF_PRIMARY) name += L" · основной";
            list.push_back({name, monitor});
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
std::vector<Target> windows() {
    std::vector<Target> result;
    EnumWindows([](HWND hwnd, LPARAM param) -> BOOL {
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId()) return TRUE;
        DWORD cloaked{}; DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked) return TRUE;
        wchar_t title[512]{}; if (!GetWindowTextW(hwnd, title, 512)) return TRUE;
        reinterpret_cast<std::vector<Target>*>(param)->push_back({title, nullptr, hwnd, pid});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
Capture::Capture(Target const& target) {
    if (!GraphicsCaptureSession::IsSupported()) throw hresult_error(E_NOTIMPL, L"Захват экрана недоступен в этой системе");
    auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(hr)) check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device_, nullptr, &context_));
    ComPtr<IDXGIDevice> dxgi; check(device_.As(&dxgi));
    com_ptr<IInspectable> inspectable;
    check(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
    rtDevice_ = inspectable.as<IDirect3DDevice>();
    auto factory = get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    if (target.window) {
        if (!IsWindow(target.window)) throw hresult_error(E_INVALIDARG, L"Выбранное окно уже закрыто");
        DWORD pid{}; GetWindowThreadProcessId(target.window, &pid);
        if (pid != target.pid) throw hresult_error(E_INVALIDARG, L"Источник окна изменился. Выберите его заново");
        check(factory->CreateForWindow(target.window, guid_of<GraphicsCaptureItem>(), put_abi(item_)));
    } else {
        if (!target.monitor) throw hresult_error(E_INVALIDARG, L"Выберите экран");
        check(factory->CreateForMonitor(target.monitor, guid_of<GraphicsCaptureItem>(), put_abi(item_)));
    }
    size_ = item_.Size();
    auto ended = ended_;
    closed_ = item_.Closed([ended](auto&&, auto&&) { ended->store(true); });
    pool_ = Direct3D11CaptureFramePool::CreateFreeThreaded(rtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size_);
    session_ = pool_.CreateCaptureSession(item_);
    session_.IsCursorCaptureEnabled(true);
    session_.StartCapture();
}
Capture::~Capture() {
    try { if (item_) item_.Closed(closed_); if (session_) session_.Close(); if (pool_) pool_.Close(); } catch (...) {}
}
bool Capture::next(Image& image) {
    if (*ended_) throw hresult_error(E_ABORT, L"Источник захвата закрыт или отключен");
    auto frame = pool_.TryGetNextFrame();
    if (!frame) return false;
    auto newSize = frame.ContentSize();
    if (newSize.Width <= 0 || newSize.Height <= 0) return false;
    auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> texture;
    check(access->GetInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(texture.GetAddressOf())));
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC old{}; if (staging_) staging_->GetDesc(&old);
    if (!staging_ || old.Width != desc.Width || old.Height != desc.Height) {
        staging_.Reset(); desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
        check(device_->CreateTexture2D(&desc, nullptr, &staging_));
    }
    context_->CopyResource(staging_.Get(), texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    image.width = std::min(newSize.Width, static_cast<int>(desc.Width));
    image.height = std::min(newSize.Height, static_cast<int>(desc.Height));
    image.time100ns = frame.SystemRelativeTime().count();
    image.bgra.resize(static_cast<size_t>(image.width) * image.height * 4);
    for (int y = 0; y < image.height; ++y) memcpy(image.bgra.data() + static_cast<size_t>(y) * image.width * 4,
        static_cast<uint8_t*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch, image.width * 4);
    context_->Unmap(staging_.Get(), 0);
    frame.Close();
    if (newSize.Width != size_.Width || newSize.Height != size_.Height) {
        size_ = newSize; pool_.Recreate(rtDevice_, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size_);
    }
    return true;
}
