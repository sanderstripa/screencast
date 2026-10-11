#pragma once
#include "common.h"
#include "motion.h"
std::vector<Device> cameraDevices();
Image loadImage(std::wstring const& path);
Image resizeImage(Image const& input, int width, int height);
void compositeCamera(Image& screen, Image const& camera, int corner);
void compositeCamera(Image& screen, Image const& camera, Point position, bool mirror=false);
Image blurImage(Image const& image, int radius);
Image backgroundImage(BackgroundMode mode, int preset, int width, int height);
class Camera {
public:
    explicit Camera(Settings const& settings);
    ~Camera();
    Image latest();
    std::shared_ptr<Image const> latestFrame();
    bool waitForFirstFrame(std::atomic_bool const& stop, int timeoutMs = 10000);
    std::wstring diagnostics();
    static void testModel();
private:
    struct State;
    class Callback;
    std::shared_ptr<State> state_;
    ComPtr<IMFMediaSource> source_;
    ComPtr<IMFSourceReader> reader_;
};
