#pragma once

#include <cstdint>
#include <limits>

namespace SelfDischarge
{
    // Capacity is stored in mAh, and rate in tenths of one percent per 30 days.
    // The unit conversions cancel, leaving the exact denominator below for µAh.
    constexpr uint64_t kDenominator = 2'592'000'000ULL;
    constexpr uint32_t kMaximumCapacityMilliAh = 10'000'000U;
    constexpr uint8_t kMaximumRateTenthPercent = 50U;

    static_assert(static_cast<uint64_t>(kMaximumCapacityMilliAh) *
                      kMaximumRateTenthPercent * UINT32_MAX + (kDenominator - 1U) <=
                  std::numeric_limits<uint64_t>::max());

    class Calculator
    {
    public:
        // Returns whole µAh and carries the exact fractional numerator forward.
        // Unsigned elapsed subtraction at the caller supports one millis() wrap.
        uint64_t advance(uint32_t capacityMilliAh, uint8_t rateTenthPercent,
                         uint32_t elapsedMs)
        {
            if (capacityMilliAh == 0U || capacityMilliAh > kMaximumCapacityMilliAh ||
                rateTenthPercent > kMaximumRateTenthPercent)
            {
                return 0U;
            }

            const uint64_t numerator = static_cast<uint64_t>(capacityMilliAh) *
                                           rateTenthPercent * elapsedMs +
                                       remainder_;
            const uint64_t lossMicroAh = numerator / kDenominator;
            remainder_ = numerator % kDenominator;
            return lossMicroAh;
        }

        uint64_t remainder() const { return remainder_; }
        void reset() { remainder_ = 0U; }

    private:
        uint64_t remainder_{};
    };

    class CalibrationInterval
    {
    public:
        uint64_t advance(uint32_t capacityMilliAh, uint8_t rateTenthPercent,
                         uint32_t elapsedMs, bool socValid)
        {
            if (!socValid)
            {
                complete_ = false;
                return 0U;
            }

            const uint64_t loss = calculator_.advance(capacityMilliAh,
                                                       rateTenthPercent,
                                                       elapsedMs);
            accumulatedMicroAh_ = std::numeric_limits<uint64_t>::max() - accumulatedMicroAh_ < loss
                                      ? std::numeric_limits<uint64_t>::max()
                                      : accumulatedMicroAh_ + loss;
            return loss;
        }

        void beginAfterCalibration(bool socValid)
        {
            calculator_.reset();
            accumulatedMicroAh_ = 0U;
            complete_ = socValid;
        }

        void markIncomplete() { complete_ = false; }

        uint64_t accumulatedMicroAh() const { return accumulatedMicroAh_; }
        bool complete() const { return complete_; }

    private:
        Calculator calculator_{};
        uint64_t accumulatedMicroAh_{};
        bool complete_{};
    };

    // Equivalent current rounded to the nearest µA.
    constexpr uint64_t equivalentCurrentMicroAmps(uint32_t capacityMilliAh,
                                                   uint8_t rateTenthPercent)
    {
        if (rateTenthPercent > kMaximumRateTenthPercent) return 0U;
        return (static_cast<uint64_t>(capacityMilliAh) * rateTenthPercent + 360U) / 720U;
    }

    constexpr uint64_t roundedMilliAmpHours(uint64_t microAmpHours)
    {
        return microAmpHours / 1'000U + (microAmpHours % 1'000U >= 500U ? 1U : 0U);
    }
}
