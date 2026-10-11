#pragma once
#include <algorithm>
#include <cmath>

struct Point { float x{}, y{}; };
class SmoothMotion {
public:
    explicit SmoothMotion(Point initial) : position_(initial) {}
    Point step(Point target, float seconds) {
        seconds = std::clamp(seconds, 0.f, .1f);
        advance(position_.x, velocity_.x, std::clamp(target.x, 0.f, 1.f), seconds);
        advance(position_.y, velocity_.y, std::clamp(target.y, 0.f, 1.f), seconds);
        return position_;
    }
private:
    Point position_{}, velocity_{};
    static void advance(float& position, float& velocity, float target, float dt) {
        // Exact critically damped spring: no frame-rate dependent lerp and no overshoot.
        constexpr float omega = 22.f;
        float error = position - target, c = velocity + omega * error;
        float decay = std::exp(-omega * dt);
        position = target + (error + c * dt) * decay;
        velocity = (velocity - omega * c * dt) * decay;
        position = std::clamp(position, 0.f, 1.f);
    }
};
struct CirclePlacement { int left{}, top{}, diameter{}, margin{}; };
inline CirclePlacement circlePlacement(int width, int height, Point point) {
    int diameter = std::min(width, height) / 4;
    int margin = std::max(2, diameter / 12);
    return {margin + static_cast<int>(std::lround(std::max(0, width - diameter - 2 * margin) * std::clamp(point.x, 0.f, 1.f))),
        margin + static_cast<int>(std::lround(std::max(0, height - diameter - 2 * margin) * std::clamp(point.y, 0.f, 1.f))), diameter, margin};
}
