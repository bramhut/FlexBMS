// Exercise the actual record parser, slot selection, CRC and writer against RAM.
#include "../Src/Peripherals/RuntimeConfiguration.cpp"
#include <sys/mman.h>
#include <cassert>
int main() {
    using namespace RuntimeConfiguration;
    void *memory=mmap(reinterpret_cast<void *>(0x08000000U), 512*1024,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
    assert(memory != MAP_FAILED);
    for (uint16_t version=2; version<=5; ++version) {
        std::memset(memory,0xff,512*1024);
        auto values=defaults(); values.slaveCount=8; values.selfDischargeRateTenthPercentPer30Days=17;
        values.startupDiagnostics=false; values.coldAllowanceDeciC=45;
        auto record=recordFromValues(values,7); record.version=version;
        record.crc=crc32(reinterpret_cast<const uint8_t *>(&record),offsetof(Record,crc));
        std::memcpy(reinterpret_cast<void *>(CONFIG_BASE_ADDRESS),&record,sizeof(record));
        auto result=load(); assert(result.status==LoadStatus::Valid && result.values.slaveCount==8);
        assert(result.values.coldAllowanceDeciC==(version==5 ? 45 : 30));
        assert(result.values.selfDischargeRateTenthPercentPer30Days==(version>=4 ? 17 : 0));
        assert(result.values.startupDiagnostics==(version==2));
        assert(save(result.values)); assert(load().storedVersion==5);
        assert(updateBalanceEnabled(false)); assert(load().values.coldAllowanceDeciC==result.values.coldAllowanceDeciC);
    }
    std::memset(memory,0xff,512*1024); assert(load().status==LoadStatus::Blank);
    auto v=defaults(); v.coldAllowanceDeciC=101; assert(!save(v));
    v.coldAllowanceDeciC=100; assert(save(v)); assert(load().values.coldAllowanceDeciC==100);
    v.coldAllowanceDeciC=0; assert(save(v)); assert(load().values.coldAllowanceDeciC==0);
    // Corrupt newest slot; older valid slot must still load.
    *reinterpret_cast<uint8_t *>(CONFIG_BASE_ADDRESS+SLOT_BYTES+8)^=1;
    assert(load().status==LoadStatus::Valid && load().values.coldAllowanceDeciC==100);
    *reinterpret_cast<uint8_t *>(CONFIG_BASE_ADDRESS+8)^=1;
    assert(load().status==LoadStatus::Corrupt);
    std::memset(memory,0xff,512*1024);
    auto unsupported=recordFromValues(defaults(),1); unsupported.version=99;
    unsupported.crc=crc32(reinterpret_cast<const uint8_t *>(&unsupported),offsetof(Record,crc));
    std::memcpy(reinterpret_cast<void *>(CONFIG_BASE_ADDRESS),&unsupported,sizeof(unsupported));
    assert(load().status==LoadStatus::VersionMismatch);
    assert(defaults().coldAllowanceDeciC==30);
    munmap(memory,512*1024);
}
