#pragma once
#include <array>
#include <cstdint>

namespace td_protocol {
constexpr int kReportBytes = 11;
constexpr int kMaxFrames = 4096;
constexpr int kMaxDelta = 127 * kMaxFrames;
using Report = std::array<uint8_t, kReportBytes>;

inline bool valid_delta(int value) {
    return value >= -kMaxDelta && value <= kMaxDelta;
}
inline int step(int value) {
    return value > 127 ? 127 : (value < -127 ? -127 : value);
}

// Confirmed against ToDesk.exe RVA 0x148B5D0 (relative mouse writer).
// 0x0B is the outer report length, NOT the relative input payload length.
// Driver legacy WriteReport consumes the input report starting at byte 2.
inline Report relative(uint8_t buttons, int dx, int dy, int wheel, int hwheel) {
    return {0x41, 0x0B, 0x05, static_cast<uint8_t>(buttons & 0x1F),
            static_cast<uint8_t>(dx), static_cast<uint8_t>(dy),
            static_cast<uint8_t>(wheel), static_cast<uint8_t>(hwheel), 0, 0, 0};
}
}
