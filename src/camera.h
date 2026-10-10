#pragma once
#include "common.h"
std::vector<Device> cameraDevices();
Image loadImage(std::wstring const& path);
Image resizeImage(Image const& input, int width, int height);
void compositeCamera(Image& screen, Image const& camera, int corner);
class Camera {
public:
    explicit Camera(Settings const& settings);
    ~Camera();
    Image latest();
    static void testModel();
private:
    struct State;
    class Callback;
    std::shared_ptr<State> state_;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
};
