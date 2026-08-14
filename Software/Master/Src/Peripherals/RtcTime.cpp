#include "RtcTime.h"

#include "main.h"
#include "rtc.h"

namespace RtcTime
{
    namespace
    {
        constexpr uint32_t kValidMarker = 0x4652'5443UL; // "FRTC"
        constexpr uint32_t kSecondsPerDay = 86'400U;
        constexpr uint32_t kFirstSupportedUnixTime = 946'684'800U;  // 2000-01-01T00:00:00Z
        constexpr uint32_t kEndSupportedUnixTime = 4'102'444'800U; // 2100-01-01T00:00:00Z

        // Gregorian conversion algorithms operate on days since 1970-01-01
        // and deliberately do not use the C library timezone state.
        int64_t daysFromCivil(int32_t year, uint32_t month, uint32_t day)
        {
            year -= month <= 2U ? 1 : 0;
            const int32_t era = (year >= 0 ? year : year - 399) / 400;
            const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
            const uint32_t monthPrime = month > 2U ? month - 3U : month + 9U;
            const uint32_t doy = (153U * monthPrime + 2U) / 5U + day - 1U;
            const uint32_t doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
            return static_cast<int64_t>(era) * 146097LL + static_cast<int64_t>(doe) - 719468LL;
        }

        void civilFromDays(int64_t days, int32_t &year, uint32_t &month, uint32_t &day)
        {
            days += 719468LL;
            const int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
            const uint32_t doe = static_cast<uint32_t>(days - era * 146097LL);
            const uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
            year = static_cast<int32_t>(yoe) + static_cast<int32_t>(era * 400LL);
            const uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
            const uint32_t monthPrime = (5U * doy + 2U) / 153U;
            day = doy - (153U * monthPrime + 2U) / 5U + 1U;
            month = monthPrime < 10U ? monthPrime + 3U : monthPrime - 9U;
            year += month <= 2U ? 1 : 0;
        }

        bool calendarIsValid(const RTC_TimeTypeDef &time, const RTC_DateTypeDef &date)
        {
            if (time.Hours > 23U || time.Minutes > 59U || time.Seconds > 59U ||
                date.Month < RTC_MONTH_JANUARY || date.Month > RTC_MONTH_DECEMBER || date.Date == 0U)
            {
                return false;
            }
            const int32_t year = 2000 + date.Year;
            const int64_t days = daysFromCivil(year, date.Month, date.Date);
            int32_t checkYear = 0;
            uint32_t checkMonth = 0U;
            uint32_t checkDay = 0U;
            civilFromDays(days, checkYear, checkMonth, checkDay);
            return checkYear == year && checkMonth == date.Month && checkDay == date.Date;
        }
    }

    bool isSupportedUnixTime(uint32_t unixTime)
    {
        return unixTime >= kFirstSupportedUnixTime && unixTime < kEndSupportedUnixTime;
    }

    bool setUnixTime(uint32_t unixTime)
    {
        if (!isSupportedUnixTime(unixTime)) return false;

        const uint32_t secondsOfDay = unixTime % kSecondsPerDay;
        const int64_t days = static_cast<int64_t>(unixTime / kSecondsPerDay);
        int32_t year = 0;
        uint32_t month = 0U;
        uint32_t day = 0U;
        civilFromDays(days, year, month, day);
        if (year < 2000 || year > 2099) return false;

        RTC_TimeTypeDef time{};
        time.Hours = secondsOfDay / 3600U;
        time.Minutes = (secondsOfDay / 60U) % 60U;
        time.Seconds = secondsOfDay % 60U;
        time.TimeFormat = RTC_HOURFORMAT_24;
        time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        time.StoreOperation = RTC_STOREOPERATION_RESET;

        RTC_DateTypeDef date{};
        date.WeekDay = static_cast<uint8_t>((days + 3LL) % 7LL + 1LL); // Monday = 1
        date.Month = month;
        date.Date = day;
        date.Year = static_cast<uint8_t>(year - 2000);

        HAL_PWR_EnableBkUpAccess();
        TAMP->BKP31R = 0U;
        if (HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK) return false;
        if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK) return false;
        TAMP->BKP31R = kValidMarker;
        return true;
    }

    bool getUnixTime(uint32_t &unixTime)
    {
        HAL_PWR_EnableBkUpAccess();
        if (TAMP->BKP31R != kValidMarker) return false;

        RTC_TimeTypeDef time{};
        RTC_DateTypeDef date{};
        if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK || !calendarIsValid(time, date))
        {
            return false;
        }

        const int64_t days = daysFromCivil(2000 + date.Year, date.Month, date.Date);
        const int64_t seconds = days * static_cast<int64_t>(kSecondsPerDay) +
                                static_cast<int64_t>(time.Hours) * 3600LL +
                                static_cast<int64_t>(time.Minutes) * 60LL + time.Seconds;
        if (seconds < 0LL || seconds > static_cast<int64_t>(UINT32_MAX)) return false;
        const uint32_t value = static_cast<uint32_t>(seconds);
        if (!isSupportedUnixTime(value)) return false;
        unixTime = value;
        return true;
    }
}
