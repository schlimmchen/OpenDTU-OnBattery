// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Configuration.h"
#include <Hoymiles.h>
#include <optional>

class PowerLimiterInverter {
public:
    PowerLimiterInverter(bool verboseLogging, PowerLimiterInverterConfig const& config);

    // NOTE: this class is not prepared to be used if isValid() returns false,
    // i.e., nullptr exceptions will occur if the instance is used despite
    // isValid() being false.
    bool isValid() const;

    // send command(s) to inverter to reach desired target state (limit and
    // production). return true if an update is pending, i.e., if the target
    // state is NOT yet reached, false otherwise.
    bool update();

    // returns the timestamp of the oldest stats received for this inverter
    // *after* its last command completed. return std::nullopt if new stats
    // are pendng after the last command completed.
    std::optional<uint32_t> getLatestStatsMillis() const;

    // the amount of times an update command issues to the inverter timed out
    uint8_t getUpdateTimeouts() const { return _updateTimeouts; }

    // maximum amount of AC power the inverter is able to produce
    // (not regarding the configured upper power limit)
    uint16_t getInverterMaxPowerWatts() const;

    // maximum amount of AC power the inverter is allowed to produce as per
    // upper power limit (additionally restricted by inverter's absolute max)
    uint16_t getConfiguredMaxPowerWatts() const;

    uint16_t getCurrentOutputAcWatts() const;

    // this differs from current output power if new limit was assigned
    uint16_t getExpectedOutputAcWatts() const;

    // the maximum reduction of power output the inverter
    // can achieve with or withouth going into standby.
    uint16_t getMaxReductionWatts(bool allowStandby) const;

    // the maximum increase of power output the inverter can achieve
    // (is expected to achieve), possibly coming out of standby.
    uint16_t getMaxIncreaseWatts() const;

    // change the target limit such that the requested change becomes effective
    // on the expected AC power output. returns the change in the range
    // [0..reduction] that will become effective (once update() returns false).
    uint16_t applyReduction(uint16_t reduction, bool allowStandby);
    uint16_t applyIncrease(uint16_t increase);

    // stop producing AC power. returns the change in power output
    // that will become effective (once update() returns false).
    uint16_t standby();

    // wake the inverter from standby and set it to produce
    // as much power as permissible by its upper power limit.
    void setMaxOutput();

    void restart();

    float getDcVoltage(uint8_t input);
    bool isSendingCommandsEnabled() const { return _spInverter->getEnableCommands(); }
    bool isReachable() const { return _spInverter->isReachable(); }
    bool isProducing() const { return _spInverter->isProducing(); }

    uint64_t getSerial() const { return _config.Serial; }
    char const* getSerialStr() const { return _serialStr; }
    bool isBehindPowerMeter() const { return _config.IsBehindPowerMeter; }
    bool isSolarPowered() const { return _config.IsSolarPowered; }

    void debug() const;

private:
    uint16_t getCurrentLimitWatts() const;
    uint16_t scaleLimit(uint16_t expectedOutputWatts);
    void setAcOutput(uint16_t expectedOutputWatts);

    bool _verboseLogging;
    char _serialStr[16];
    char _logPrefix[32];

    // copied to avoid races with web UI
    PowerLimiterInverterConfig _config;

    // Hoymiles lib inverter instance
    std::shared_ptr<InverterAbstract> _spInverter = nullptr;

    // track (target) state
    uint8_t _updateTimeouts = 0;
    std::optional<uint32_t> _oUpdateStartMillis = std::nullopt;
    std::optional<uint16_t> _oTargetPowerLimitWatts = std::nullopt;
    std::optional<bool> _oTargetPowerState = std::nullopt;
    mutable std::optional<uint32_t> _oStatsMillis = std::nullopt;

    // the expected AC output, which possibly is different
    // from the target limit due to scaling
    uint16_t _targetAcOutputWatts = 0;
};
