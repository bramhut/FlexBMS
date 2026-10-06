#include "flexbms/EnergyMeters.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbcontroller.h"
#include "nvs.h"

#include <algorithm>

namespace FlexBms::EnergyMeters
{
    namespace
    {
        constexpr const char *kTag = "energy_meters";
        constexpr const char *kNamespace = "energy_meters";
        constexpr uart_port_t kUart = UART_NUM_0;
        constexpr gpio_num_t kEnable = GPIO_NUM_0;
        constexpr uint32_t kStorageVersion = 1;
        // Persist flags as bytes, not native bool objects. Validate the envelope
        // before converting it, including after upgrades or NVS damage.
        struct StoredMeter { uint8_t enabled = 0, address = 0, reverse = 0; std::array<char, kNameBytes + 1> name{}; };
        struct StoredConfiguration
        {
            uint32_t version = kStorageVersion, baudRate = 9600;
            uint8_t parity = 0, stopBits = 1;
            std::array<StoredMeter, kMeterCount> meters{};
        };
        bool restore(const StoredConfiguration &stored, Configuration &config)
        {
            if (stored.version != kStorageVersion) return false;
            config.baudRate = stored.baudRate; config.parity = static_cast<Parity>(stored.parity); config.stopBits = stored.stopBits;
            for (size_t slot = 0; slot < kMeterCount; ++slot)
            {
                const auto &source = stored.meters[slot];
                if (source.enabled > 1 || source.reverse > 1) return false;
                auto &target = config.meters[slot];
                target.enabled = source.enabled == 1; target.reversePowerDirection = source.reverse == 1;
                target.address = source.address; target.name = source.name;
            }
            return valid(config);
        }
        SemaphoreHandle_t mutex = nullptr;
        TaskHandle_t worker = nullptr;
        State shared{};
        void *controller = nullptr; // exclusively owned by the worker

        void lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
        void unlock() { xSemaphoreGive(mutex); }
        void closeBus()
        {
            if (controller != nullptr) { (void)mbc_master_delete(controller); controller = nullptr; }
            gpio_reset_pin(kEnable);
            gpio_set_direction(kEnable, GPIO_MODE_OUTPUT);
            gpio_set_level(kEnable, 0); // receiver enabled, driver disabled
        }
        esp_err_t openBus(const Configuration &config)
        {
            mb_communication_info_t communication{};
            communication.ser_opts.port = kUart;
            communication.ser_opts.mode = MB_RTU;
            communication.ser_opts.baudrate = config.baudRate;
            communication.ser_opts.parity = config.parity == Parity::Even ? UART_PARITY_EVEN : config.parity == Parity::Odd ? UART_PARITY_ODD : UART_PARITY_DISABLE;
            communication.ser_opts.response_tout_ms = 500;
            communication.ser_opts.data_bits = UART_DATA_8_BITS;
            communication.ser_opts.stop_bits = config.stopBits == 2 ? UART_STOP_BITS_2 : UART_STOP_BITS_1;
            esp_err_t result = mbc_master_create_serial(&communication, &controller);
            // The library requires a descriptor even when using raw requests.
            static const mb_parameter_descriptor_t descriptor = {
                .cid = 0, .param_key = "phase_current", .param_units = "A", .mb_slave_addr = 1,
                .mb_param_type = MB_PARAM_INPUT, .mb_reg_start = 6, .mb_size = 2,
                .param_offset = 0, .param_type = PARAM_TYPE_FLOAT, .param_size = 4,
                .param_opts = {}, .access = PAR_PERMS_READ
            };
            if (result == ESP_OK) result = uart_set_pin(kUart, GPIO_NUM_7, GPIO_NUM_6, kEnable, UART_PIN_NO_CHANGE);
            if (result == ESP_OK) result = mbc_master_set_descriptor(controller, &descriptor, 1);
            if (result == ESP_OK) result = uart_set_mode(kUart, UART_MODE_RS485_HALF_DUPLEX);
            if (result == ESP_OK) result = mbc_master_start(controller);
            if (result != ESP_OK) closeBus();
            return result;
        }
        esp_err_t read(uint8_t address, uint8_t function, uint16_t offset, uint16_t count, uint16_t *values)
        {
            mb_param_request_t request{};
            request.slave_addr = address;
            request.command = function;
            request.reg_start = offset;
            request.reg_size = count;
            return mbc_master_send_request(controller, &request, values);
        }
        void commitReading(size_t slot, uint32_t generation, const Reading &reading)
        {
            lock();
            if (shared.generation == generation) shared.readings[slot] = reading;
            unlock();
        }
        void failed(Reading &reading, esp_err_t error)
        {
            if (reading.failedReads != UINT32_MAX) ++reading.failedReads;
            reading.lastError = error;
        }
        void run(void *)
        {
            uint32_t generation = 0;
            std::array<int64_t, kMeterCount> nextElectrical{}, nextEnergy{}, nextIdentity{};
            std::array<uint8_t, kMeterCount> failures{};
            int64_t nextBusAttempt = 0;
            while (true)
            {
                State state = getState();
                if (generation != state.generation)
                {
                    closeBus();
                    generation = state.generation;
                    nextElectrical = {}; nextEnergy = {}; nextIdentity = {}; failures = {};
                    nextBusAttempt = 0;
                }
                if (!anyEnabled(state.configuration))
                {
                    // After first use the dormant worker sleeps here; neither
                    // the Modbus stack nor UART driver remains allocated.
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    continue;
                }
                if (controller == nullptr)
                {
                    if (esp_timer_get_time() < nextBusAttempt) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)); continue; }
                    const esp_err_t error = openBus(state.configuration);
                    lock();
                    if (shared.generation == generation) { shared.busReady = error == ESP_OK; shared.busError = error; }
                    unlock();
                    if (error != ESP_OK)
                    {
                        ESP_LOGW(kTag, "RS485 setup failed: %s", esp_err_to_name(error));
                        nextBusAttempt = esp_timer_get_time() + 5'000'000;
                        continue;
                    }
                }
                for (size_t slot = 0; slot < kMeterCount; ++slot)
                {
                    state = getState();
                    if (state.generation != generation) break;
                    const auto &config = state.configuration.meters[slot];
                    int64_t now = esp_timer_get_time();
                    if (!config.enabled || now < nextElectrical[slot]) continue;
                    Reading reading = state.readings[slot];
                    std::array<uint16_t, 12> phases{};
                    std::array<uint16_t, 2> total{};
                    esp_err_t error = read(config.address, 4, 0x0006, phases.size(), phases.data());
                    if (error == ESP_OK) error = read(config.address, 4, 0x0034, total.size(), total.data());
                    if (error == ESP_OK && !decodeElectrical(phases.data(), total.data(), reading)) error = ESP_ERR_INVALID_RESPONSE;
                    reading.electricalValid = error == ESP_OK;
                    if (error != ESP_OK)
                    {
                        failed(reading, error);
                        failures[slot] = std::min<uint8_t>(failures[slot] + 1U, 5U);
                        nextElectrical[slot] = esp_timer_get_time() + failures[slot] * kElectricalPeriodUs;
                        commitReading(slot, generation, reading);
                        continue;
                    }
                    reading.electricalUs = esp_timer_get_time();
                    reading.lastError = ESP_OK;
                    failures[slot] = 0;
                    nextElectrical[slot] = now + kElectricalPeriodUs;
                    commitReading(slot, generation, reading); // publish fast data before auxiliary requests
                    if (esp_timer_get_time() >= nextEnergy[slot])
                    {
                        std::array<uint16_t, 4> energy{};
                        error = read(config.address, 4, 0x0048, energy.size(), energy.data());
                        if (error == ESP_OK && !decodeEnergy(energy.data(), reading)) error = ESP_ERR_INVALID_RESPONSE;
                        reading.energyValid = error == ESP_OK;
                        if (error == ESP_OK) reading.energyUs = esp_timer_get_time();
                        else failed(reading, error);
                        nextEnergy[slot] = esp_timer_get_time() + (error == ESP_OK ? kEnergyPeriodUs : 5'000'000);
                        commitReading(slot, generation, reading);
                    }
                    // Optional identity reads never gate electrical reporting.
                    // Read one identity request per cycle, retry failures slowly.
                    if (esp_timer_get_time() >= nextIdentity[slot] && (!reading.identityValid || !reading.firmwareValid))
                    {
                        std::array<uint16_t, 3> identity{};
                        if (!reading.identityValid)
                        {
                            error = read(config.address, 3, 0xFC00, identity.size(), identity.data());
                            if (error == ESP_OK)
                            {
                                reading.serialNumber = (static_cast<uint32_t>(identity[0]) << 16U) | identity[1];
                                reading.meterCode = identity[2];
                                reading.identityValid = true;
                            }
                        }
                        else
                        {
                            error = read(config.address, 3, 0xFC84, 1, identity.data());
                            if (error == ESP_OK) { reading.firmwareVersion = identity[0]; reading.firmwareValid = true; }
                        }
                        if (error != ESP_OK) failed(reading, error);
                        nextIdentity[slot] = esp_timer_get_time() + (error == ESP_OK ? kElectricalPeriodUs : kEnergyPeriodUs);
                        commitReading(slot, generation, reading);
                    }
                }
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
            }
        }
        bool ensureWorker()
        {
            return worker != nullptr || xTaskCreate(run, "energy_meters", 4096, nullptr, 4, &worker) == pdPASS;
        }
    }
    bool start()
    {
        if (mutex != nullptr) return true;
        closeBus();
        mutex = xSemaphoreCreateMutex();
        if (mutex == nullptr) return false;
        shared.available = true;
        nvs_handle_t handle = 0;
        if (nvs_open(kNamespace, NVS_READONLY, &handle) == ESP_OK)
        {
            StoredConfiguration stored{};
            Configuration configuration{};
            size_t size = sizeof(stored);
            const esp_err_t error = nvs_get_blob(handle, "config", &stored, &size);
            if (error == ESP_OK && size == sizeof(stored) && restore(stored, configuration)) shared.configuration = configuration;
            else if (error != ESP_ERR_NVS_NOT_FOUND) shared.storedConfigurationValid = false;
            nvs_close(handle);
        }
        if (anyEnabled(shared.configuration) && !ensureWorker()) { shared.busError = ESP_ERR_NO_MEM; return false; }
        return true;
    }
    State getState()
    {
        if (mutex == nullptr) return {};
        lock(); const State result = shared; unlock(); return result;
    }
    bool configure(const Configuration &configuration)
    {
        if (mutex == nullptr || !valid(configuration)) return false;
        lock();
        if (anyEnabled(configuration) && !ensureWorker()) { unlock(); return false; }
        StoredConfiguration stored{};
        stored.baudRate = configuration.baudRate; stored.parity = static_cast<uint8_t>(configuration.parity); stored.stopBits = configuration.stopBits;
        for (size_t slot = 0; slot < kMeterCount; ++slot)
        {
            const auto &source = configuration.meters[slot]; auto &target = stored.meters[slot];
            target.enabled = source.enabled; target.reverse = source.reversePowerDirection; target.address = source.address; target.name = source.name;
        }
        nvs_handle_t handle = 0;
        esp_err_t error = nvs_open(kNamespace, NVS_READWRITE, &handle);
        if (error == ESP_OK) error = nvs_set_blob(handle, "config", &stored, sizeof(stored));
        if (error == ESP_OK) error = nvs_commit(handle);
        if (handle != 0) nvs_close(handle);
        if (error == ESP_OK)
        {
            shared.configuration = configuration;
            ++shared.generation;
            shared.readings = {};
            shared.busReady = false;
            shared.busError = 0;
            shared.storedConfigurationValid = true;
        }
        if (worker != nullptr) xTaskNotifyGive(worker);
        unlock();
        return error == ESP_OK;
    }
}
