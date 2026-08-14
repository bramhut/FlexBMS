#pragma once

// Copy this file to FallbackNetworks.local.h and replace the placeholders.
// The entries are tried in this exact order, but only when their SSIDs were
// visible in the latest scan. Leave the macro empty to disable fallbacks.
//
// This file is compiled into the Gateway firmware. A firmware image can be
// extracted from a device, so use only local networks and credentials whose
// disclosure risk is acceptable. Never commit FallbackNetworks.local.h.
#define FLEXBMS_WIFI_FALLBACK_NETWORKS \
    {"fallback-ssid-1", "fallback-password-1"}, \
    {"fallback-ssid-2", "fallback-password-2"}
