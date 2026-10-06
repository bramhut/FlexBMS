#include "flexbms/DeliveryQueue.h"
#include <cassert>
#include <cstring>
using FlexBms::GatewayApi::DeliveryQueue;
using Topic = DeliveryQueue::Topic;
static DeliveryQueue::Message message(const char *text) {
    const auto length = std::strlen(text);
    auto *copy = static_cast<char *>(std::malloc(length + 1));
    std::memcpy(copy, text, length + 1);
    return {copy, length};
}
static void expect(DeliveryQueue &queue, const char *text) {
    auto item = queue.pop();
    assert(item.text && std::strcmp(item.text, text) == 0);
    std::free(item.text);
}
int main() {
    DeliveryQueue queue;
    assert(queue.push(message("old snapshot"), Topic::Snapshot));
    assert(queue.push(message("reply1"), Topic::Reliable));
    assert(queue.push(message("status"), Topic::BmsStatus));
    assert(queue.push(message("reply2"), Topic::Reliable));
    for (int i = 0; i < 100; ++i) assert(queue.push(message("new snapshot"), Topic::Snapshot));
    expect(queue, "reply1"); expect(queue, "reply2");
    expect(queue, "status"); expect(queue, "new snapshot");
    assert(queue.empty());
    for (int i = 0; i < 16; ++i) assert(queue.push(message("reply"), Topic::Reliable));
    auto overflow = message("overflow");
    assert(!queue.push(overflow, Topic::Reliable));
    std::free(overflow.text);
    queue.clear(); assert(queue.empty());
    assert(queue.push(message("new connection"), Topic::Reliable));
    expect(queue, "new connection");
}
