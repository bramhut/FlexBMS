#pragma once

#include <cstddef>
#include <cstdint>

namespace BmsUart
{
    void setup();

    // HAL callback entry points. They do bounded ISR work only; framing and
    // services run in the BmsUart task.
    void onRxEvent(const uint8_t *data, size_t length);
    void onTxComplete();
    void onError();
}
