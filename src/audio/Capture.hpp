#pragma once

#include "model/Document.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace brick {

struct CaptureDevice { int index = -1; std::string name; };
struct CaptureStatus {
    bool recording = false;
    bool limitReached = false;
    std::size_t frames = 0;
    std::size_t clippedFrames = 0;
    double seconds = 0;
    float peak = 0;
};

// No device is opened by construction or device enumeration. start() is the
// only operation which enables the microphone; call it on explicit user input.
class Capture {
public:
    Capture();
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;
    static std::vector<CaptureDevice> devices(); // includes system default (-1)
    void start(int deviceIndex = -1, double maxSeconds = 60.0); // 1..300 seconds
    CaptureStatus status(); // closes a completed/disconnected device on UI thread
    Sample finish(); // stops capture, returns mono PCM with XM pitch tuning
    void cancel(); // closes device and discards this recording
    static constexpr int sampleRate = 48000;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace brick
