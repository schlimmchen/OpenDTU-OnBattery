#include "Utils.h"
#include "MessageOutput.h"
#include "PowerLimiterInverter.h"
#include "inverters/HMS_4CH.h"

PowerLimiterInverter::PowerLimiterInverter(bool verboseLogging, PowerLimiterInverterConfig const& config)
    : _verboseLogging(verboseLogging)
    , _config(config)
{
    _spInverter = Hoymiles.getInverterBySerial(config.Serial);
    if (!_spInverter) { return; }

    snprintf(_serialStr, sizeof(_serialStr), "%0x%08x",
            ((uint32_t)((config.Serial >> 32) & 0xFFFFFFFF)),
            ((uint32_t)(config.Serial & 0xFFFFFFFF)));

    snprintf(_logPrefix, sizeof(_logPrefix), "[DPL inverter %s]:", _serialStr);
}

bool PowerLimiterInverter::isValid() const
{
    if (!_spInverter) { return false; }

    // the model-dependent maximum AC power output is only known after the
    // first DevInfoSimpleCommand succeeded. we desperately need this info, so
    // the inverter is deemed invalid until we have this info.
    if (getInverterMaxPowerWatts() == 0) { return false; }

    return true;
}

bool PowerLimiterInverter::update()
{
    auto reset = [this]() -> bool {
        _oTargetPowerState = std::nullopt;
        _oTargetPowerLimitWatts = std::nullopt;
        _oUpdateStartMillis = std::nullopt;
        return false;
    };

    // do not reset _updateTimeouts below if no state change requested
    if (!_oTargetPowerState.has_value() && !_oTargetPowerLimitWatts.has_value()) {
        return reset();
    }

    if (!_oUpdateStartMillis.has_value()) {
        _oUpdateStartMillis = millis();
    }

    if ((millis() - *_oUpdateStartMillis) > 30 * 1000) {
        ++_updateTimeouts;
        MessageOutput.printf("%s timeout (%d in succession), "
                "state transition pending: %s, limit pending: %s\r\n",
                _logPrefix, _updateTimeouts,
                (_oTargetPowerState.has_value()?"yes":"no"),
                (_oTargetPowerLimitWatts.has_value()?"yes":"no"));

        // NOTE that this is not always 5 minutes, since this counts timeouts,
        // not absolute time. after any timeout, an update cycle ends. a new
        // timeout can only happen after starting a new update cycle, which in
        // turn is only started if the DPL did calculate a new limit, which in
        // turn does not happen while the inverter is unreachable, no matter
        // how long (a whole night) that might be.
        if (_updateTimeouts >= 10) {
            MessageOutput.printf("%s issuing restart command after update "
                    "timed out repeatedly\r\n", _logPrefix);
            _spInverter->sendRestartControlRequest();
        }

        if (_updateTimeouts >= 20) {
            MessageOutput.printf("%s restarting system since inverter is "
                    "unresponsive\r\n", _logPrefix);
            Utils::restartDtu();
        }

        return reset();
    }

    auto constexpr halfOfAllMillis = std::numeric_limits<uint32_t>::max() / 2;

    auto switchPowerState = [this](bool transitionOn) -> bool {
        // no power state transition requested at all
        if (!_oTargetPowerState.has_value()) { return false; }

        // the transition that may be started is not the one which is requested
        if (transitionOn != *_oTargetPowerState) { return false; }

        // wait for pending power command(s) to complete
        auto lastPowerCommandState = _spInverter->PowerCommand()->getLastPowerCommandSuccess();
        if (CMD_PENDING == lastPowerCommandState) {
            return true;
        }

        // we need to wait for statistics that are more recent than
        // the last power update command to reliably use isProducing()
        auto lastPowerCommandMillis = _spInverter->PowerCommand()->getLastUpdateCommand();
        auto lastStatisticsMillis = _spInverter->Statistics()->getLastUpdate();
        if ((lastStatisticsMillis - lastPowerCommandMillis) > halfOfAllMillis) { return true; }

        if (isProducing() != *_oTargetPowerState) {
            MessageOutput.printf("%s %s inverter...\r\n", _logPrefix,
                    ((*_oTargetPowerState)?"Starting":"Stopping"));
            _spInverter->sendPowerControlRequest(*_oTargetPowerState);
            return true;
        }

        _oTargetPowerState = std::nullopt; // target power state reached
        return false;
    };

    // we use a lambda function here to be able to use return statements,
    // which allows to avoid if-else-indentions and improves code readability
    auto updateLimit = [this]() -> bool {
        // no limit update requested at all
        if (!_oTargetPowerLimitWatts.has_value()) { return false; }

        // wait for pending limit command(s) to complete
        auto lastLimitCommandState = _spInverter->SystemConfigPara()->getLastLimitCommandSuccess();
        if (CMD_PENDING == lastLimitCommandState) {
            return true;
        }

        float newRelativeLimit = static_cast<float>(*_oTargetPowerLimitWatts * 100) / getInverterMaxPowerWatts();

        // if no limit command is pending, the SystemConfigPara does report the
        // current limit, as the answer by the inverter to a limit command is
        // the canonical source that updates the known current limit.
        auto currentRelativeLimit = _spInverter->SystemConfigPara()->getLimitPercent();

        // we assume having exclusive control over the inverter. if the last
        // limit command was successful and sent after we started the last
        // update cycle, we should assume *our* requested limit was set.
        uint32_t lastLimitCommandMillis = _spInverter->SystemConfigPara()->getLastUpdateCommand();
        if ((lastLimitCommandMillis - *_oUpdateStartMillis) < halfOfAllMillis &&
                CMD_OK == lastLimitCommandState) {
            MessageOutput.printf("%s actual limit is %.1f %% (%.0f W "
                    "respectively), effective %d ms after update started, "
                    "requested were %.1f %%\r\n",
                    _logPrefix, currentRelativeLimit,
                    (currentRelativeLimit * getInverterMaxPowerWatts() / 100),
                    (lastLimitCommandMillis - *_oUpdateStartMillis),
                    newRelativeLimit);

            if (std::abs(newRelativeLimit - currentRelativeLimit) > 2.0) {
                MessageOutput.printf("%s NOTE: expected limit of %.1f %% "
                        "and actual limit of %.1f %% mismatch by more than 2 %%, "
                        "is the DPL in exclusive control over the inverter?\r\n",
                        _logPrefix, newRelativeLimit, currentRelativeLimit);
            }

            _oTargetPowerLimitWatts = std::nullopt;
            return false;
        }

        MessageOutput.printf("%s sending limit of %.1f %% (%.0f W "
                "respectively), max output is %d W\r\n", _logPrefix,
                newRelativeLimit, (newRelativeLimit * getInverterMaxPowerWatts() / 100),
                getInverterMaxPowerWatts());

        _spInverter->sendActivePowerControlRequest(newRelativeLimit,
                PowerLimitControlType::RelativNonPersistent);

        return true;
    };

    // disable power production as soon as possible.
    // setting the power limit is less important once the inverter is off.
    if (switchPowerState(false)) { return true; }

    if (updateLimit()) { return true; }

    // enable power production only after setting the desired limit
    if (switchPowerState(true)) { return true; }

    _updateTimeouts = 0;

    return reset();
}

std::optional<uint32_t> PowerLimiterInverter::getLatestStatsMillis() const
{
    // concerns both power limits and start/stop/restart commands and is
    // only updated if a respective response was received from the inverter
    auto lastUpdateCmd = std::max(
            _spInverter->SystemConfigPara()->getLastUpdateCommand(),
            _spInverter->PowerCommand()->getLastUpdateCommand());

    // TODO(schlimmchen): comparisons break when millis() wraps around.
    // we are looking for *one* inverter stats younger than the last update command
    if (_oStatsMillis.has_value() && lastUpdateCmd > *_oStatsMillis) {
        _oStatsMillis = std::nullopt;
    }

    if (!_oStatsMillis.has_value()) {
        auto lastStats = _spInverter->Statistics()->getLastUpdate();
        if (lastStats <= lastUpdateCmd) {
            return std::nullopt;
        }

        _oStatsMillis = lastStats;
    }

    return _oStatsMillis;
}

uint16_t PowerLimiterInverter::getInverterMaxPowerWatts() const
{
    return _spInverter->DevInfo()->getMaxPower();
}

uint16_t PowerLimiterInverter::getConfiguredMaxPowerWatts() const
{
    return std::min(getInverterMaxPowerWatts(), _config.UpperPowerLimit);
}

uint16_t PowerLimiterInverter::getCurrentOutputAcWatts() const
{
    return _spInverter->Statistics()->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);
}

uint16_t PowerLimiterInverter::getExpectedOutputAcWatts() const
{
    if (!_oTargetPowerLimitWatts && !_oTargetPowerState) {
        // the inverter's output will not change due to commands being sent
        return getCurrentOutputAcWatts();
    }

    return _targetAcOutputWatts;
}

uint16_t PowerLimiterInverter::getMaxReductionWatts(bool allowStandby) const
{
    if (!isProducing()) { return 0; }

    if (getCurrentOutputAcWatts() <= _config.LowerPowerLimit) { return 0; }

    if (allowStandby) {
        return getCurrentOutputAcWatts();
    }

    return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
}

uint16_t PowerLimiterInverter::getMaxIncreaseWatts() const
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

uint16_t PowerLimiterInverter::applyReduction(uint16_t reduction, bool allowStandby)
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

uint16_t PowerLimiterInverter::applyIncrease(uint16_t increase)
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

uint16_t PowerLimiterInverter::standby()
{
    if (isSolarPowered()) {
        setAcOutput(_config.LowerPowerLimit);
        return getCurrentOutputAcWatts() - _config.LowerPowerLimit;
    }

    _oTargetPowerState = false;
    _targetAcOutputWatts = 0;
    return getCurrentOutputAcWatts();
}

void PowerLimiterInverter::setMaxOutput()
{
    _oTargetPowerState = true;
    setAcOutput(getConfiguredMaxPowerWatts());
}

void PowerLimiterInverter::restart()
{
    _spInverter->sendRestartControlRequest();
}

float PowerLimiterInverter::getDcVoltage(uint8_t input)
{
    return _spInverter->Statistics()->getChannelFieldValue(TYPE_DC,
            static_cast<ChannelNum_t>(input), FLD_UDC);
}

uint16_t PowerLimiterInverter::getCurrentLimitWatts() const
{
    auto currentLimitPercent = _spInverter->SystemConfigPara()->getLimitPercent();
    return static_cast<uint16_t>(currentLimitPercent * getInverterMaxPowerWatts() / 100);
}

uint16_t PowerLimiterInverter::scaleLimit(uint16_t expectedOutputWatts)
{
    if (!isSolarPowered()) { return expectedOutputWatts; }

    // prevent scaling if inverter is not producing, as input channels are not
    // producing energy and hence are detected as not-producing, causing
    // unreasonable scaling.
    if (!isProducing()) { return expectedOutputWatts; }

    auto pStats = _spInverter->Statistics();
    std::list<ChannelNum_t> dcChnls = pStats->getChannelsByType(TYPE_DC);
    size_t dcTotalChnls = dcChnls.size();

    // according to the upstream projects README (table with supported devs),
    // every 2 channel inverter has 2 MPPTs. then there are the HM*S* 4 channel
    // models which have 4 MPPTs. all others have a different number of MPPTs
    // than inputs. those are not supported by the current scaling mechanism.
    bool supported = dcTotalChnls == 2;
    supported |= dcTotalChnls == 4 && HMS_4CH::isValidSerial(getSerial());
    if (!supported) { return expectedOutputWatts; }

    // test for a reasonable power limit that allows us to assume that an input
    // channel with little energy is actually not producing, rather than
    // producing very little due to the very low limit.
    if (getCurrentLimitWatts() < dcTotalChnls * 10) { return expectedOutputWatts; }

    // overscalling allows us to compensate for shaded panels by increasing the
    // total power limit, if the inverter is solar powered.
    if (_config.UseOverscalingToCompensateShading && isSolarPowered()) {
        auto inverterOutputAC = pStats->getChannelFieldValue(TYPE_AC, CH0, FLD_PAC);

        float inverterEfficiencyFactor = pStats->getChannelFieldValue(TYPE_INV, CH0, FLD_EFF);

        // fall back to hoymiles peak efficiency as per datasheet if inverter
        // is currently not producing (efficiency is zero in that case)
        inverterEfficiencyFactor = (inverterEfficiencyFactor > 0) ? inverterEfficiencyFactor/100 : 0.967;

        // 98% of the expected power is good enough
        auto expectedAcPowerPerChannel = (getCurrentLimitWatts() / dcTotalChnls) * 0.98;

        if (_verboseLogging) {
            MessageOutput.printf("%s expected AC power per channel %f W\r\n",
                    _logPrefix, expectedAcPowerPerChannel);
        }

        size_t dcShadedChnls = 0;
        auto shadedChannelACPowerSum = 0.0;

        for (auto& c : dcChnls) {
            auto channelPowerAC = pStats->getChannelFieldValue(TYPE_DC, c, FLD_PDC) * inverterEfficiencyFactor;

            if (channelPowerAC < expectedAcPowerPerChannel) {
                dcShadedChnls++;
                shadedChannelACPowerSum += channelPowerAC;
            }

            if (_verboseLogging) {
                MessageOutput.printf("%s ch %d AC power %f W\r\n",
                        _logPrefix, c, channelPowerAC);
            }
        }

        // no shading or the shaded channels provide more power than what
        // we currently need.
        if (dcShadedChnls == 0 || shadedChannelACPowerSum >= expectedOutputWatts) {
            return expectedOutputWatts;
        }

        if (dcShadedChnls == dcTotalChnls) {
            // keep the currentLimit when:
            // - all channels are shaded
            // - currentLimit >= expectedOutputWatts
            // - we get the expected AC power or less and
            if (getCurrentLimitWatts() >= expectedOutputWatts &&
                    inverterOutputAC <= expectedOutputWatts) {
                if (_verboseLogging) {
                    MessageOutput.printf("%s all channels are shaded, "
                            "keeping the current limit of %d W\r\n",
                            _logPrefix, getCurrentLimitWatts());
                }

                return getCurrentLimitWatts();

            } else {
                return expectedOutputWatts;
            }
        }

        size_t dcNonShadedChnls = dcTotalChnls - dcShadedChnls;
        uint16_t overScaledLimit = (expectedOutputWatts - shadedChannelACPowerSum) / dcNonShadedChnls * dcTotalChnls;

        if (overScaledLimit <= expectedOutputWatts) { return expectedOutputWatts; }

        if (_verboseLogging) {
            MessageOutput.printf("%s %d/%d channels are shaded, scaling %d W\r\n",
                    _logPrefix, dcShadedChnls, dcTotalChnls, overScaledLimit);
        }

        return overScaledLimit;
    }

    size_t dcProdChnls = 0;
    for (auto& c : dcChnls) {
        if (pStats->getChannelFieldValue(TYPE_DC, c, FLD_PDC) > 2.0) {
            dcProdChnls++;
        }
    }

    if (dcProdChnls == 0 || dcProdChnls == dcTotalChnls) { return expectedOutputWatts; }

    uint16_t scaled = expectedOutputWatts / dcProdChnls * dcTotalChnls;
    MessageOutput.printf("%s %d/%d channels are producing, scaling from %d to "
            "%d W\r\n", _logPrefix, dcProdChnls, dcTotalChnls,
            expectedOutputWatts, scaled);

    return scaled;
}

void PowerLimiterInverter::setAcOutput(uint16_t expectedOutputWatts)
{
    _targetAcOutputWatts = expectedOutputWatts;
    _oTargetPowerLimitWatts = scaleLimit(expectedOutputWatts);
    _oTargetPowerState = true;
}

void PowerLimiterInverter::debug() const
{
    if (!_verboseLogging) { return; }

    MessageOutput.printf("%s debug info:\r\n", _logPrefix);
    MessageOutput.printf("    solar powered: %s\r\n", (isSolarPowered()?"yes":"no"));
    MessageOutput.printf("    output capability: %d W\r\n", getInverterMaxPowerWatts());
    MessageOutput.printf("    upper power limit: %d W\r\n", _config.UpperPowerLimit);
    MessageOutput.printf("    lower power limit: %d W\r\n", _config.LowerPowerLimit);
    MessageOutput.printf("    producing: %s\r\n", (isProducing()?"yes":"no"));
    MessageOutput.printf("    current output: %d W\r\n", getCurrentOutputAcWatts());
    MessageOutput.printf("    current limit: %d W\r\n", getCurrentLimitWatts());
    MessageOutput.printf("    max reduction: %d W (online), %d W (standby)\r\n", getMaxReductionWatts(false), getMaxReductionWatts(true));
    MessageOutput.printf("    max increase: %d W\r\n", getMaxIncreaseWatts());
    if (_oTargetPowerLimitWatts) {
        MessageOutput.printf("    target limit: %d\r\n", *_oTargetPowerLimitWatts);
    }
    if (_oTargetPowerState) {
        MessageOutput.printf("    target state: %s\r\n", (*_oTargetPowerState?"producing":"standby"));
    }
    MessageOutput.printf("    expected (new) output: %d\r\n", getExpectedOutputAcWatts());
    MessageOutput.printf("    update timeouts: %d\r\n", getUpdateTimeouts());
}
