#include "flexbms/Protocol.h"
#include <cassert>
#include <cstdint>
using namespace FlexBms::UartV1;
int main() {
    assert(verifyCodec());
    Frame frame{};
    frame.type = MessageType::Pack;
    frame.length = 27;
    frame.payload[0] = 2;
    Pack pack{};
    const int32_t values[] = {INT32_MIN, -9000, -1, 0, 1, 5000, INT32_MAX};
    for (auto value : values) {
        const uint32_t bits = static_cast<uint32_t>(value);
        for (int i = 0; i < 4; ++i) frame.payload[5+i] = static_cast<uint8_t>(bits >> (8*i));
        assert(decodePack(frame, pack));
        assert(pack.packCurrentMicroAmps == value);
    }
    frame.payload[0] = 1; assert(!decodePack(frame, pack));
    frame.payload[0] = 2; frame.length = 24; assert(!decodePack(frame, pack));
    frame = {};
    frame.type = MessageType::GoodweCanDiagnostics;
    frame.length = 167; frame.payload[0] = 2;
    frame.payload[156] = 2;
    const auto put32 = [&](size_t offset, uint32_t value) {
        for (size_t i=0; i<4; ++i) frame.payload[offset+i] = static_cast<uint8_t>(value >> (8*i));
    };
    put32(157, static_cast<uint32_t>(-45123));
    frame.payload[161] = 0x90; frame.payload[162] = 1;
    put32(163, 123456);
    GoodweCanDiagnostics diagnostics{};
    assert(decodeGoodweCanDiagnostics(frame, diagnostics));
    assert(diagnostics.violationDirection == 2 && diagnostics.violationCurrentMilliA == -45123);
    assert(diagnostics.violationLimitDeciA == 400 && diagnostics.violationUptimeMs == 123456);
    frame.payload[156] = 3; assert(!decodeGoodweCanDiagnostics(frame, diagnostics));
    frame.payload[156] = 0; frame.length = 166; assert(!decodeGoodweCanDiagnostics(frame, diagnostics));
    frame.length = 156; frame.payload[0] = 1;
    assert(decodeGoodweCanDiagnostics(frame, diagnostics));
    assert(diagnostics.violationDirection == 0 && diagnostics.violationCurrentMilliA == 0);
}
