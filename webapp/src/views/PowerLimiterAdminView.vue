<template>
    <BasePage :title="$t('powerlimiteradmin.PowerLimiterSettings')" :isLoading="dataLoading">
        <BootstrapAlert v-model="showAlert" dismissible :variant="alertType">
            {{ alertMessage }}
        </BootstrapAlert>

        <BootstrapAlert v-model="configAlert" variant="warning">
            {{ $t('powerlimiteradmin.ConfigAlertMessage') }}
        </BootstrapAlert>

        <CardElement
            :text="$t('powerlimiteradmin.ConfigHints')"
            textVariant="text-bg-primary"
            v-if="getConfigHints().length"
        >
            <div class="row">
                <div class="col-sm-12">
                    {{ $t('powerlimiteradmin.ConfigHintsIntro') }}
                    <ul class="mb-0">
                        <li v-for="(hint, idx) in getConfigHints()" :key="idx">
                            <b v-if="hint.severity === 'requirement'"
                                >{{ $t('powerlimiteradmin.ConfigHintRequirement') }}:</b
                            >
                            <b v-if="hint.severity === 'optional'">{{ $t('powerlimiteradmin.ConfigHintOptional') }}:</b>
                            {{ $t('powerlimiteradmin.ConfigHint' + hint.subject) }}
                        </li>
                    </ul>
                </div>
            </div>
        </CardElement>

        <form @submit="savePowerLimiterConfig" v-if="!configAlert">
            <CardElement
                :text="$t('powerlimiteradmin.General')"
                textVariant="text-bg-primary"
                :add-space="getConfigHints().length > 0"
            >
                <InputElement
                    :label="$t('powerlimiteradmin.Enable')"
                    v-model="powerLimiterConfigList.enabled"
                    type="checkbox"
                    wide
                />

                <template v-if="powerLimiterConfigList.enabled">
                    <InputElement
                        v-for="(inv, idx) in powerLimiterConfigList.inverters"
                        :key="idx"
                        :label="$t('powerlimiteradmin.GovernInverter', { name: inverterName(inv.serial) })"
                        v-model="powerLimiterConfigList.inverters[idx].is_governed"
                        type="checkbox"
                        wide
                    />
                </template>

                <template v-if="isEnabled()">
                    <InputElement
                        :label="$t('powerlimiteradmin.VerboseLogging')"
                        v-model="powerLimiterConfigList.verbose_logging"
                        type="checkbox"
                        wide
                    />

                    <InputElement
                        v-if="hasPowerMeter()"
                        :label="$t('powerlimiteradmin.TargetPowerConsumption')"
                        :tooltip="$t('powerlimiteradmin.TargetPowerConsumptionHint')"
                        v-model="powerLimiterConfigList.target_power_consumption"
                        postfix="W"
                        type="number"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.BaseLoadLimit')"
                        :tooltip="$t('powerlimiteradmin.BaseLoadLimitHint')"
                        v-model="powerLimiterConfigList.base_load_limit"
                        placeholder="200"
                        postfix="W"
                        type="number"
                        min="0"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.TargetPowerConsumptionHysteresis')"
                        :tooltip="$t('powerlimiteradmin.TargetPowerConsumptionHysteresisHint')"
                        v-model="powerLimiterConfigList.target_power_consumption_hysteresis"
                        postfix="W"
                        type="number"
                        min="1"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.TotalUpperPowerLimit')"
                        :tooltip="$t('powerlimiteradmin.TotalUpperPowerLimitHint')"
                        v-model="powerLimiterConfigList.total_upper_power_limit"
                        postfix="W"
                        type="number"
                        min="1"
                        wide
                    />
                </template>
            </CardElement>

            <template v-if="isEnabled()">
                <template v-for="(inv, idx) in powerLimiterConfigList.inverters" :key="idx">
                    <CardElement
                        v-if="inv.is_governed"
                        :text="inverterLabel(inv.serial)"
                        textVariant="text-bg-primary"
                        add-space
                    >
                        <InputElement
                            v-if="hasPowerMeter()"
                            :label="$t('powerlimiteradmin.InverterIsBehindPowerMeter')"
                            v-model="powerLimiterConfigList.inverters[idx].is_behind_power_meter"
                            :tooltip="$t('powerlimiteradmin.InverterIsBehindPowerMeterHint')"
                            type="checkbox"
                            wide
                        />

                        <InputElement
                            :label="$t('powerlimiteradmin.InverterIsSolarPowered')"
                            v-model="powerLimiterConfigList.inverters[idx].is_solar_powered"
                            type="checkbox"
                            wide
                        />

                        <InputElement
                            v-if="powerLimiterConfigList.inverters[idx].is_solar_powered"
                            :label="$t('powerlimiteradmin.UseOverscalingToCompensateShading')"
                            :tooltip="$t('powerlimiteradmin.UseOverscalingToCompensateShadingHint')"
                            v-model="powerLimiterConfigList.inverters[idx].use_overscaling_to_compensate_shading"
                            type="checkbox"
                            wide
                        />

                        <InputElement
                            :label="$t('powerlimiteradmin.LowerPowerLimit')"
                            :tooltip="$t('powerlimiteradmin.LowerPowerLimitHint')"
                            v-model="powerLimiterConfigList.inverters[idx].lower_power_limit"
                            postfix="W"
                            type="number"
                            wide
                        />

                        <InputElement
                            :label="$t('powerlimiteradmin.UpperPowerLimit')"
                            v-model="powerLimiterConfigList.inverters[idx].upper_power_limit"
                            :tooltip="$t('powerlimiteradmin.UpperPowerLimitHint')"
                            postfix="W"
                            type="number"
                            wide
                        />
                    </CardElement>
                </template>

                <CardElement
                    :text="$t('powerlimiteradmin.InverterSettings')"
                    textVariant="text-bg-primary"
                    add-space
                    v-if="batteryPoweredInverterConfigured()"
                >
                    <InputElement
                        :label="$t('powerlimiteradmin.BatteryDischargeAtNight')"
                        v-model="powerLimiterConfigList.battery_always_use_at_night"
                        type="checkbox"
                        wide
                    />

                    <div class="row mb-3">
                        <label for="inverter_serial_for_dc_voltage" class="col-sm-4 col-form-label">
                            {{ $t('powerlimiteradmin.InverterForDcVoltage') }}
                        </label>
                        <div class="col-sm-8">
                            <select
                                id="inverter_serial_for_dc_voltage"
                                class="form-select"
                                v-model="powerLimiterConfigList.inverter_serial_for_dc_voltage"
                                required
                            >
                                <option value="" disabled hidden selected>
                                    {{ $t('powerlimiteradmin.SelectInverter') }}
                                </option>
                                <option v-for="inv in batteryPoweredInverters" :key="inv.serial" :value="inv.serial">
                                    {{ inverterLabel(inv.serial) }}
                                </option>
                            </select>
                        </div>
                    </div>

                    <div class="row mb-3" v-if="needsChannelSelection()">
                        <label for="inverter_channel" class="col-sm-4 col-form-label">
                            {{ $t('powerlimiteradmin.InverterChannelId') }}
                        </label>
                        <div class="col-sm-8">
                            <select
                                id="inverter_channel"
                                class="form-select"
                                v-model="powerLimiterConfigList.inverter_channel_id_for_dc_voltage"
                            >
                                <option
                                    v-for="channel in range(
                                        getInverterInfo(powerLimiterConfigList.inverter_serial_for_dc_voltage).channels
                                    )"
                                    :key="channel"
                                    :value="channel"
                                >
                                    {{ channel + 1 }}
                                </option>
                            </select>
                        </div>
                    </div>

                    <div class="row mb-3">
                        <label for="inverter_restart" class="col-sm-4 col-form-label">
                            {{ $t('powerlimiteradmin.InverterRestartHour') }}
                            <BIconInfoCircle v-tooltip :title="$t('powerlimiteradmin.InverterRestartHint')" />
                        </label>
                        <div class="col-sm-8">
                            <select
                                id="inverter_restart"
                                class="form-select"
                                v-model="powerLimiterConfigList.inverter_restart_hour"
                            >
                                <option value="-1">
                                    {{ $t('powerlimiteradmin.InverterRestartDisabled') }}
                                </option>
                                <option v-for="hour in range(24)" :key="hour" :value="hour">
                                    {{ hour > 9 ? hour : '0' + hour }}:00
                                </option>
                            </select>
                        </div>
                    </div>
                </CardElement>

                <CardElement
                    :text="$t('powerlimiteradmin.SolarPassthrough')"
                    textVariant="text-bg-primary"
                    add-space
                    v-if="canUseSolarPassthrough()"
                >
                    <div
                        class="alert alert-secondary"
                        role="alert"
                        v-html="$t('powerlimiteradmin.SolarpassthroughInfo')"
                    ></div>

                    <InputElement
                        :label="$t('powerlimiteradmin.EnableSolarPassthrough')"
                        v-model="powerLimiterConfigList.solar_passthrough_enabled"
                        type="checkbox"
                        wide
                    />

                    <template v-if="powerLimiterConfigList.solar_passthrough_enabled">
                        <InputElement
                            :label="$t('powerlimiteradmin.SolarPassthroughLosses')"
                            v-model="powerLimiterConfigList.solar_passthrough_losses"
                            placeholder="3"
                            min="0"
                            max="10"
                            postfix="%"
                            type="number"
                            wide
                        />

                        <div
                            class="alert alert-secondary"
                            role="alert"
                            v-html="$t('powerlimiteradmin.SolarPassthroughLossesInfo')"
                        ></div>
                    </template>
                </CardElement>

                <CardElement
                    :text="$t('powerlimiteradmin.VoltageThresholds')"
                    textVariant="text-bg-primary"
                    add-space
                    v-if="canUseVoltageThresholds()"
                >
                    <InputElement
                        :label="$t('powerlimiteradmin.IgnoreSoc')"
                        :tooltip="$t('powerlimiteradmin.IgnoreSocHint')"
                        v-model="powerLimiterConfigList.ignore_soc"
                        type="checkbox"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.StartThreshold')"
                        v-model="powerLimiterConfigList.voltage_start_threshold"
                        placeholder="50"
                        min="16"
                        max="66"
                        postfix="V"
                        type="number"
                        step="0.01"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.StopThreshold')"
                        v-model="powerLimiterConfigList.voltage_stop_threshold"
                        placeholder="49"
                        min="16"
                        max="66"
                        postfix="V"
                        type="number"
                        step="0.01"
                        wide
                    />

                    <template v-if="isSolarPassthroughEnabled()">
                        <InputElement
                            :label="$t('powerlimiteradmin.FullSolarPassthroughStartThreshold')"
                            :tooltip="$t('powerlimiteradmin.FullSolarPassthroughStartThresholdHint')"
                            v-model="powerLimiterConfigList.full_solar_passthrough_start_voltage"
                            placeholder="49"
                            min="16"
                            max="66"
                            postfix="V"
                            type="number"
                            step="0.01"
                            wide
                        />

                        <InputElement
                            :label="$t('powerlimiteradmin.VoltageSolarPassthroughStopThreshold')"
                            v-model="powerLimiterConfigList.full_solar_passthrough_stop_voltage"
                            placeholder="49"
                            min="16"
                            max="66"
                            postfix="V"
                            type="number"
                            step="0.01"
                            wide
                        />
                    </template>

                    <InputElement
                        :label="$t('powerlimiteradmin.VoltageLoadCorrectionFactor')"
                        v-model="powerLimiterConfigList.voltage_load_correction_factor"
                        placeholder="0.0001"
                        postfix="1/A"
                        type="number"
                        step="0.0001"
                        wide
                    />

                    <div
                        class="alert alert-secondary"
                        role="alert"
                        v-html="$t('powerlimiteradmin.VoltageLoadCorrectionInfo')"
                    ></div>
                </CardElement>

                <CardElement
                    :text="$t('powerlimiteradmin.SocThresholds')"
                    textVariant="text-bg-primary"
                    add-space
                    v-if="canUseSoCThresholds()"
                >
                    <InputElement
                        :label="$t('powerlimiteradmin.StartThreshold')"
                        v-model="powerLimiterConfigList.battery_soc_start_threshold"
                        placeholder="80"
                        min="0"
                        max="100"
                        postfix="%"
                        type="number"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.StopThreshold')"
                        v-model="powerLimiterConfigList.battery_soc_stop_threshold"
                        placeholder="20"
                        min="0"
                        max="100"
                        postfix="%"
                        type="number"
                        wide
                    />

                    <InputElement
                        :label="$t('powerlimiteradmin.FullSolarPassthroughStartThreshold')"
                        :tooltip="$t('powerlimiteradmin.FullSolarPassthroughStartThresholdHint')"
                        v-model="powerLimiterConfigList.full_solar_passthrough_soc"
                        v-if="isSolarPassthroughEnabled()"
                        placeholder="80"
                        min="0"
                        max="100"
                        postfix="%"
                        type="number"
                        wide
                    />

                    <div
                        class="alert alert-secondary"
                        role="alert"
                        v-html="$t('powerlimiteradmin.BatterySocInfo')"
                    ></div>
                </CardElement>
            </template>

            <FormFooter @reload="getAllData" />
        </form>
    </BasePage>
</template>

<script lang="ts">
import { defineComponent } from 'vue';
import BasePage from '@/components/BasePage.vue';
import BootstrapAlert from '@/components/BootstrapAlert.vue';
import { handleResponse, authHeader } from '@/utils/authentication';
import CardElement from '@/components/CardElement.vue';
import FormFooter from '@/components/FormFooter.vue';
import InputElement from '@/components/InputElement.vue';
import { BIconInfoCircle } from 'bootstrap-icons-vue';
import type {
    PowerLimiterConfig,
    PowerLimiterInverterConfig,
    PowerLimiterMetaData,
    PowerLimiterInverterInfo,
} from '@/types/PowerLimiterConfig';

export default defineComponent({
    components: {
        BasePage,
        BootstrapAlert,
        CardElement,
        FormFooter,
        InputElement,
        BIconInfoCircle,
    },
    data() {
        return {
            dataLoading: true,
            powerLimiterConfigList: {} as PowerLimiterConfig,
            powerLimiterMetaData: {} as PowerLimiterMetaData,
            alertMessage: '',
            alertType: 'info',
            showAlert: false,
            configAlert: false,
        };
    },
    created() {
        this.getAllData();
    },
    watch: {
        governedInverters() {
            console.log("new governed inverters");
            if (!this.governedInverters.some((inv: PowerLimiterInverterConfig) => inv.serial == this.powerLimiterConfigList.inverter_serial_for_dc_voltage)) {
                // marks serial as invalid, selects placeholder option
                console.log("resetting serial for voltage measurement");
                this.powerLimiterConfigList.inverter_serial_for_dc_voltage = '';
            }
        },
    },
    computed: {
        governedInverters(): [PowerLimiterInverterConfig] {
            return this.powerLimiterConfigList?.inverters?.filter((inv: PowerLimiterInverterConfig) => inv.is_governed) || [];
        },
        batteryPoweredInverters(): [PowerLimiterInverterConfig] {
            return this.governedInverters.filter((inv: PowerLimiterInverterConfig) => !inv.is_solar_powered);
        },
    },
    methods: {
        getInverterInfo(serial: string): PowerLimiterInverterInfo {
            return (
                this.powerLimiterMetaData.inverters?.find((inv: PowerLimiterInverterInfo) => inv.serial === serial) ||
                ({} as PowerLimiterInverterInfo)
            );
        },
        getConfigHints(): [object] {
            const meta = this.powerLimiterMetaData;
            const hints = [];

            if (meta.power_meter_enabled !== true) {
                hints.push({ severity: 'optional', subject: 'PowerMeterDisabled' });
            }

            if (typeof meta.inverters === 'undefined' || meta.inverters.length == 0) {
                hints.push({ severity: 'requirement', subject: 'NoInverter' });
                this.configAlert = true;
            } else {
                const managedInverters = this.powerLimiterConfigList.inverters;
                for (const managedInv of Object.values(managedInverters)) {
                    const inv = this.getInverterInfo(managedInv.serial);
                    if (!inv) {
                        continue;
                    }
                    if (
                        inv !== undefined &&
                        !(inv.poll_enable && inv.command_enable && inv.poll_enable_night && inv.command_enable_night)
                    ) {
                        hints.push({ severity: 'requirement', subject: 'InverterCommunication' });
                        break;
                    }
                }
            }

            if (this.batteryPoweredInverterConfigured()) {
                if (!meta.charge_controller_enabled) {
                    hints.push({ severity: 'optional', subject: 'NoChargeController' });
                }

                if (!meta.battery_enabled) {
                    hints.push({ severity: 'optional', subject: 'NoBatteryInterface' });
                }
            }

            return hints;
        },
        isEnabled() {
            const cfg = this.powerLimiterConfigList;
            return cfg.enabled && this.governedInverters.length > 0;
        },
        hasPowerMeter() {
            return this.powerLimiterMetaData.power_meter_enabled;
        },
        canUseSolarPassthrough() {
            const meta = this.powerLimiterMetaData;
            return meta.charge_controller_enabled && this.batteryPoweredInverterConfigured();
        },
        canUseSoCThresholds() {
            const cfg = this.powerLimiterConfigList;
            const meta = this.powerLimiterMetaData;
            return meta.battery_enabled && this.batteryPoweredInverterConfigured() && !cfg.ignore_soc;
        },
        canUseVoltageThresholds() {
            return this.batteryPoweredInverterConfigured();
        },
        isSolarPassthroughEnabled() {
            return this.powerLimiterConfigList.solar_passthrough_enabled;
        },
        range(end: number) {
            return Array.from(Array(end).keys());
        },
        inverterName(serial: string) {
            if (serial === undefined) {
                return 'undefined';
            }
            const meta = this.powerLimiterMetaData;
            if (meta === undefined) {
                return 'metadata pending';
            }
            const inv = this.getInverterInfo(serial);
            if (inv === undefined) {
                return 'not found';
            }
            return inv.name;
        },
        inverterLabel(serial: string) {
            if (serial === undefined) {
                return 'undefined';
            }
            const meta = this.powerLimiterMetaData;
            if (meta === undefined) {
                return 'metadata pending';
            }
            const inv = this.getInverterInfo(serial);
            if (inv === undefined) {
                return 'not found';
            }
            return inv.name + ' (' + inv.type + ')';
        },
        batteryPoweredInverterConfigured() {
            return this.batteryPoweredInverters.length > 0;
        },
        needsChannelSelection() {
            const cfg = this.powerLimiterConfigList;

            const reset = function () {
                cfg.inverter_channel_id_for_dc_voltage = 0;
                return false;
            };

            if (!this.batteryPoweredInverterConfigured()) {
                return reset();
            }

            if (cfg.inverter_serial_for_dc_voltage === '') {
                return reset();
            }

            const inverter = this.getInverterInfo(cfg.inverter_serial_for_dc_voltage);
            if (inverter === undefined) {
                return reset();
            }

            if (cfg.inverter_channel_id_for_dc_voltage >= inverter.channels) {
                cfg.inverter_channel_id_for_dc_voltage = 0;
            }

            return inverter.channels > 1;
        },
        getAllData() {
            this.dataLoading = true;
            fetch('/api/powerlimiter/metadata', { headers: authHeader() })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((data) => {
                    this.powerLimiterMetaData = data;
                    fetch('/api/powerlimiter/config', { headers: authHeader() })
                        .then((response) => handleResponse(response, this.$emitter, this.$router))
                        .then((data) => {
                            console.log(data);
                            data.inverters.filter((cfgInv: PowerLimiterInverterConfig) =>
                                this.powerLimiterMetaData.inverters.some(metaInv => metaInv.serial === cfgInv.serial)
                            );
                            this.powerLimiterMetaData.inverters.map((metaInv: PowerLimiterInverterInfo) => {
                                if (data.inverters.some((cfgInv: PowerLimiterInverterConfig) => cfgInv.serial === metaInv.serial)) { return; }
                                const newInv = {} as PowerLimiterInverterConfig;
                                newInv.serial = metaInv.serial;
                                newInv.is_governed = false;
                                newInv.is_behind_power_meter = true;
                                newInv.lower_power_limit = 10 * metaInv.channels;
                                newInv.upper_power_limit = 123; // TODO use inverter physical max output
                                data.inverters.push(newInv);
                            });
                            console.log(data);
                            this.powerLimiterConfigList = data;
                            this.dataLoading = false;
                        });
                });
        },
        savePowerLimiterConfig(e: Event) {
            e.preventDefault();

            const formData = new FormData();
            formData.append('data', JSON.stringify(this.powerLimiterConfigList));

            fetch('/api/powerlimiter/config', {
                method: 'POST',
                headers: authHeader(),
                body: formData,
            })
                .then((response) => handleResponse(response, this.$emitter, this.$router))
                .then((response) => {
                    this.alertMessage = response.message;
                    this.alertType = response.type;
                    this.showAlert = true;
                    window.scrollTo(0, 0);
                });
        },
    },
});
</script>
