#pragma once
#include <algorithm>
#include <cmath>
#include <array>

namespace TemperatureLimits
{
    inline constexpr double ColdStopC = 0, ColdRecoveryC = 2;
    inline constexpr double HotStartC = 50, HotStopC = 55, HotRecoveryC = 53, HotRateC = .50;
    inline constexpr std::array<double, 4> ColdTemperaturesC{0, 5, 10, 15};
    inline constexpr std::array<double, 4> ColdRatesC{.05, .12, .30, .50};
    inline double coldRate(double temperature) {
        for (size_t i=1; i<ColdTemperaturesC.size(); ++i)
            if (temperature < ColdTemperaturesC[i])
                return ColdRatesC[i-1] + (temperature-ColdTemperaturesC[i-1]) *
                    (ColdRatesC[i]-ColdRatesC[i-1]) / (ColdTemperaturesC[i]-ColdTemperaturesC[i-1]);
        return ColdRatesC.back();
    }
    struct Limits { double chargeA{}; double dischargeA{}; bool coldStopped{true}; bool hotStopped{true}; };
    class Controller
    {
        bool coldStopped = true;
        bool hotStopped = true;
    public:
        void reset() { coldStopped = hotStopped = true; }
        Limits update(bool valid, double minimumC, double maximumC, double allowanceC, double capacityAh)
        {
            if (!valid || !std::isfinite(minimumC) || !std::isfinite(maximumC) ||
                !std::isfinite(allowanceC) || allowanceC < 0 || allowanceC > 10 ||
                !std::isfinite(capacityAh) || capacityAh <= 0 || minimumC > maximumC)
            { reset(); return {}; }
            const double coldC = minimumC - allowanceC;
            if (coldC <= ColdStopC) coldStopped = true;
            else if (coldC > ColdRecoveryC) coldStopped = false;
            if (maximumC >= HotStopC) hotStopped = true;
            else if (maximumC < HotRecoveryC) hotStopped = false;
            const double coldAllowance = coldRate(coldC);
            const double hotRate = std::clamp((HotStopC - maximumC) * HotRateC / (HotStopC-HotStartC), 0.0, HotRateC);
            return {coldStopped || hotStopped ? 0 : capacityAh * std::min(coldAllowance, hotRate),
                    hotStopped ? 0 : capacityAh * hotRate, coldStopped, hotStopped};
        }
    };
}
