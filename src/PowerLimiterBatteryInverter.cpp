#include "PowerLimiterBatteryInverter.h"

PowerLimiterBatteryInverter::PowerLimiterBatteryInverter(bool verboseLogging, PowerLimiterInverterConfig const& config)
    : PowerLimiterInverter(verboseLogging, config) { }

uint16_t PowerLimiterBatteryInverter::getMaxReductionWatts(bool allowStandby) const
{
    if (!isProducing()) { return 0; }

    if (getCurrentOutputAcWatts() <= _config.LowerPowerLimit) { return 0; }

    if (allowStandby) {
        return getCurrentOutputAcWatts();
    }

    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterBatteryInverter::getMaxIncreaseWatts() const
{
    if (!isProducing()) {
        // if a solar-powered inverter is in standby, we cannot know how much
        // it could produce if its limit was raised, so we assume it can
        // deliver at full power.
        return getConfiguredMaxPowerWatts();
    }

    if (!isSolarPowered()) {
        // this should not happen for battery-powered inverters, but we want to
        // be robust in case something else set a limit on the inverter (or in
        // case we did something wrong...).
        if (getCurrentLimitWatts() > getConfiguredMaxPowerWatts()) { return 0; }

        // we must not substract the current AC output here, but the current
        // limit value, so we avoid trying to produce even more even if the
        // inverter is already at the maximum limit value (the actual AC
        // output may be less than the inverter's current power limit).
        return getConfiguredMaxPowerWatts() - getCurrentLimitWatts();
    }

    // TODO(schlimmchen): left for the author of the scaling method: @AndreasBoehm
    return std::min(getConfiguredMaxPowerWatts() - getCurrentOutputAcWatts(), 100);
}

uint16_t PowerLimiterBatteryInverter::applyReduction(uint16_t reduction, bool allowStandby)
{
    if (reduction == 0) { return 0; }

    if ((getCurrentOutputAcWatts() - _config.LowerPowerLimit) >= reduction) {
        auto baseline = isSolarPowered() ? getCurrentOutputAcWatts() : getCurrentLimitWatts();
        setAcOutput(baseline - reduction);
        return reduction;
    }

    if (allowStandby) {
        standby();
        return std::min(reduction, getCurrentOutputAcWatts());
    }

    setAcOutput(_config.LowerPowerLimit);
    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterBatteryInverter::applyIncrease(uint16_t increase)
{
    if (increase == 0) { return 0; }

    // do not wake inverter up if it would produce too much power
    if (!isProducing() && _config.LowerPowerLimit > increase) { return 0; }

    // the limit for solar-powered inverters might be scaled,
    // so we use the current output as the baseline for those.
    auto baseline = isSolarPowered() ? getCurrentOutputAcWatts() : getCurrentLimitWatts();

    // battery-powered inverters in standby can have an arbitrary limit, yet
    // the baseline is 0 in case we are about to wake it up from standby.
    // for solar-powered inverters, this statement is expected to be redundant,
    // as the output of inverters in standby should be 0 in any case.
    if (!isProducing()) { baseline = 0; }

    auto actualIncrease = std::min(increase, getMaxIncreaseWatts());
    setAcOutput(baseline + actualIncrease);
    return actualIncrease;
}

uint16_t PowerLimiterBatteryInverter::standby()
{
    if (isSolarPowered()) {
        setAcOutput(_config.LowerPowerLimit);
        return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
    }

    setTargetPowerState(false);
    setExpectedOutputAcWatts(0);
    return getCurrentOutputAcWatts();
}

void PowerLimiterBatteryInverter::setAcOutput(uint16_t expectedOutputWatts)
{
    setExpectedOutputAcWatts(expectedOutputWatts);
    setTargetPowerLimitWatts(expectedOutputWatts);
    setTargetPowerState(true);
}
