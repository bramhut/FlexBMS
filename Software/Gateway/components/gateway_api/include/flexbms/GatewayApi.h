#pragma once

#include "flexbms/Protocol.h"

#include <cstdint>

namespace FlexBms::GatewayApi
{
    enum class Service : uint8_t { SetRunRequest = 0x02U, AcknowledgeFaults = 0x03U, ReadRegister = 0x04U, SetRtc = 0x05U, GetDeviceInfo = 0x06U, GetRtc = 0x08U, SetBalancingEnabled = 0x09U, GetConfig = 0x0BU, SetConfig = 0x0CU, GetDiagnosticReport = 0x0DU };
    enum class ServiceResult : uint8_t { Ok = 0U, Denied = 1U, Invalid = 2U, TransportError = 3U, Busy = 4U, UsbHostActive = 5U };

    // The callback only serialises a validated request to UART. The API owns no
    // safety decision and never queues multiple browser-originated requests.
    using ServiceSender = bool (*)(Service service, const uint8_t *arguments, uint8_t argumentLength, uint8_t sequence);
    using InternalServiceCompletion = void (*)(ServiceResult result);

    bool start(ServiceSender sender);
    void poll();
    void setUartHealthy(bool healthy);
    void publishFrame(const UartV1::Frame &frame, uint32_t gatewayUptimeMs);
    void publishGatewayStatus();
    void completeService(uint8_t sequence, ServiceResult result, const uint8_t *data, uint8_t dataLength);
    bool serviceBusy();
    // Internal Gateway work uses the same exclusive UART service slot as the
    // browser facade.  It never emits a browser service_result.
    bool beginInternalService(Service service, const uint8_t *arguments, uint8_t argumentLength, InternalServiceCompletion completion);
    bool verifyBrowserApi();
}
