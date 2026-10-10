#pragma once
#include "common.h"
#include <d3d11.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

std::vector<Target> monitors();
std::vector<Target> windows();
class Capture {
public:
    explicit Capture(Target const& target);
    ~Capture();
    bool next(Image& image);
private:
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11Texture2D> staging_;
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item_{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool pool_{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice rtDevice_{nullptr};
    winrt::Windows::Graphics::SizeInt32 size_{};
    winrt::event_token closed_{};
    std::shared_ptr<std::atomic_bool> ended_{std::make_shared<std::atomic_bool>(false)};
};
