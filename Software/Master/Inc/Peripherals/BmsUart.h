#pragma once

#include <cstddef>
#include <cstdint>

namespace BmsUart
{
    void setup();

    // True after the initial 1.5 s grace period if CRC-valid Gateway traffic
    // has not been received recently. Safety ownership remains separate.
    bool isGatewayLinkLost();

    // HAL callback entry points. They do bounded ISR work only; framing and
    // services run in the BmsUart task.
    void onRxEvent(const uint8_t *data, size_t length);
    void onTxComplete();
    void onError();
}
