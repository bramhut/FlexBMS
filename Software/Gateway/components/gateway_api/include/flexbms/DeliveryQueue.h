#pragma once
#include <array>
#include <cstdlib>
#include <cstddef>

namespace FlexBms::GatewayApi {
// Owns queued text; callers serialize access. Replies/events are FIFO, state
// is replaceable only within its own topic. One send may be active separately.
class DeliveryQueue {
public:
    enum class Topic { Reliable, Hello, GatewayStatus, BmsStatus, Snapshot };
    struct Message { char *text = nullptr; size_t length = 0; };
    ~DeliveryQueue() { clear(); }
    DeliveryQueue() = default;
    DeliveryQueue(const DeliveryQueue &) = delete;
    DeliveryQueue &operator=(const DeliveryQueue &) = delete;
    bool push(Message message, Topic topic) {
        if (topic == Topic::Reliable) {
            if (count == replies.size()) return false;
            replies[(head + count++) % replies.size()] = message;
        } else {
            auto &slot = state[static_cast<size_t>(topic) - 1];
            std::free(slot.text);
            slot = message;
        }
        return true;
    }
    Message pop() {
        if (count) {
            auto message = replies[head];
            replies[head] = {};
            head = (head + 1) % replies.size();
            --count;
            return message;
        }
        for (size_t offset = 0; offset < state.size(); ++offset) {
            const auto index = (nextState + offset) % state.size();
            auto &slot = state[index];
            if (!slot.text) continue;
            auto message = slot; slot = {};
            nextState = (index + 1) % state.size();
            return message;
        }
        return {};
    }
    bool empty() const {
        if (count) return false;
        for (const auto &slot : state) if (slot.text) return false;
        return true;
    }
    void clear() {
        for (auto &slot : replies) { std::free(slot.text); slot = {}; }
        for (auto &slot : state) { std::free(slot.text); slot = {}; }
        head = count = nextState = 0;
    }
private:
    std::array<Message, 16> replies{};
    std::array<Message, 4> state{};
    size_t head = 0, count = 0, nextState = 0;
};
}
