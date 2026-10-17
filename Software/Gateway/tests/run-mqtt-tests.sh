#!/usr/bin/env bash
set -euo pipefail

# Run under Linux/WSL with GCC. Use the cJSON source shipped with the configured
# ESP-IDF framework; do not vendor another library or require connected hardware.
gateway_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
json_dir=${1:?Pass the ESP-IDF components/json/cJSON directory}
test_dir=$(mktemp -d /tmp/flexbms-mqtt-tests.XXXXXX)
printf 'Native test artifacts: %s\n' "$test_dir"
common=(-Wall -Wextra -Werror -I "$gateway_dir/components/mqtt_client/include"
        -I "$gateway_dir/components/uart_v1/include"
        -I "$gateway_dir/components/energy_meters/include")
g++ -std=c++17 "${common[@]}" "$gateway_dir/tests/acquired_publication_test.cpp" \
    -o "$test_dir/acquired_publication_test"
"$test_dir/acquired_publication_test"
gcc -c "$json_dir/cJSON.c" -o "$test_dir/cJSON.o"
g++ -std=c++20 "${common[@]}" -Wno-unused-parameter \
    -I "$gateway_dir/tests/mqtt_stubs" -I "$json_dir" \
    "$gateway_dir/tests/mqtt_publication_test.cpp" \
    "$gateway_dir/components/mqtt_client/src/MqttClient.cpp" \
    "$gateway_dir/components/energy_meters/src/MeterPublisher.cpp" \
    "$gateway_dir/components/energy_meters/src/MeterJson.cpp" \
    "$gateway_dir/components/uart_v1/src/Protocol.cpp" "$test_dir/cJSON.o" \
    -o "$test_dir/mqtt_publication_test"
"$test_dir/mqtt_publication_test"
g++ -std=c++17 "${common[@]}" "$gateway_dir/tests/pack_schema_test.cpp" \
    "$gateway_dir/components/uart_v1/src/Protocol.cpp" -o "$test_dir/pack_schema_test"
"$test_dir/pack_schema_test"
g++ -std=c++17 "${common[@]}" -I "$json_dir" \
    "$gateway_dir/tests/energy_meters_test.cpp" \
    "$gateway_dir/components/energy_meters/src/MeterJson.cpp" \
    "$gateway_dir/components/energy_meters/src/MeterPublisher.cpp" "$test_dir/cJSON.o" \
    -o "$test_dir/energy_meters_test"
"$test_dir/energy_meters_test"
printf 'PASS: acquisition gate, actual MQTT publisher, UART pack/schema, RS485 meter regression\n'
