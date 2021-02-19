/**
 * @file laird_battery.c
 * @brief
 *
 * Copyright (c) 2021 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
LOG_MODULE_REGISTER(lc_battery, CONFIG_LAIRD_CONNECT_BATTERY_LOG_LEVEL);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <devicetree.h>
#include <drivers/gpio.h>
#include <drivers/sensor.h>

#include "lairdconnect_battery.h"
#include "ble_battery_service.h"
#include "nv.h"
#include "laird_power.h"
#ifdef CONFIG_LAIRD_CONNECT_BATTERY_LOGGING
#include "sdcard_log.h"
#endif

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

/* These values are specific to the MG100 design, determined through testing
 * over the supported temperature range. Threshold 4 is the maximum voltage,
 * and threshold 0 is the minimum operating voltage.
 */

/* clang-format off */
#define BATTERY_THRESH_POWER_OFF 2750
#define BATTERY_THRESH_4         4200
#define BATTERY_THRESH_3         3800
#define BATTERY_THRESH_2         3400
#define BATTERY_THRESH_1         3000
#define BATTERY_THRESH_0         BATTERY_THRESH_POWER_OFF
/* clang-format on */

#define BATTERY_THRESH_LOW BATTERY_THRESH_2
#define BATTERY_THRESH_ALARM BATTERY_THRESH_1

#define BATTERY_VOLT_OFFSET 150
#define BASE_TEMP 20

#define BATTERY_NUM_READINGS 50

/* values used to indicate the charger state */
/* clang-format off */
#define BATTERY_EXT_POWER_STATE     BIT(0)
#define BATTERY_CHARGING_STATE      BIT(1)
#define BATTERY_NOT_CHARGING_STATE  BIT(2)
#define BATTERY_DISCHARGING_STATE   BIT(3)
/* clang-format on */

/* battery charging related GPIO settings */
/* clang-format off */
#define CHG_STATE_PORT          DT_PROP(DT_NODELABEL(gpio0), label)
#define CHG_STATE_PIN           30
#define PWR_STATE_PORT          DT_PROP(DT_NODELABEL(gpio1), label)
#define PWR_STATE_PIN           4
#define CHG_PIN_CHARGING        0
#define CHG_PIN_NOT_CHARGING    1
#define PWR_PIN_PWR_PRESENT     0
#define PWR_PIN_PWR_NOT_PRESENT 1
/* clang-format on */

#define INVALID_TEMPERATURE -127

#define MAX_LOG_STR_SIZE 30

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static enum battery_status batteryCapacity = BATTERY_STATUS_0;

/* clang-format off */
static uint16_t batteryThresholds[BATTERY_IDX_MAX] = {
	BATTERY_THRESH_0,
	BATTERY_THRESH_1,
	BATTERY_THRESH_2,
	BATTERY_THRESH_3,
	BATTERY_THRESH_4,
	BATTERY_THRESH_LOW,
	BATTERY_THRESH_ALARM
};
/* clang-format on */

static uint16_t previousVoltageReadings[BATTERY_NUM_READINGS];
static struct battery_data batteryStatus;
static uint8_t lastVoltageReadingIdx = 0;
static uint8_t batteryAlarmState = BATTERY_ALARM_INACTIVE;

static struct k_work chgStateWork;
static const struct device *batteryChgStateDev;
static struct gpio_callback batteryChgStateCb;
static struct gpio_callback batteryPwrStateCb;
static const struct device *batteryPwrStateDev;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int ReadTempSensor();
static enum battery_status CalculateRemainingCapacity(int16_t Voltage);
static int16_t DetermineTempOffset(int32_t Temperature);
static void ChgStateHandler(struct k_work *Item);
static void BatteryGpioInit();
#ifdef CONFIG_LAIRD_CONNECT_BATTERY_LOGGING
static void BatteryLogData(int16_t voltage, int32_t temp);
#endif

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
bool UpdateBatteryThreshold0(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Threshold 0 = %d", Value);
	BatterySetThresholds(BATTERY_IDX_0, Value);

	return (ret);
}

bool UpdateBatteryThreshold1(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Threshold 1 = %d", Value);
	BatterySetThresholds(BATTERY_IDX_1, Value);

	return (ret);
}

bool UpdateBatteryThreshold2(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Threshold 2 = %d", Value);
	BatterySetThresholds(BATTERY_IDX_2, Value);

	return (ret);
}

bool UpdateBatteryThreshold3(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Threshold 3 = %d", Value);
	BatterySetThresholds(BATTERY_IDX_3, Value);

	return (ret);
}

bool UpdateBatteryThreshold4(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Threshold 4 = %d", Value);
	BatterySetThresholds(BATTERY_IDX_4, Value);

	return (ret);
}

bool UpdateBatteryLowThreshold(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Low Threshold = %d", Value);
	BatterySetThresholds(BATTERY_IDX_LOW, Value);

	return (ret);
}

bool UpdateBatteryBadThreshold(int Value)
{
	bool ret = true;
	LOG_DBG("Battery Bad Threshold = %d", Value);
	BatterySetThresholds(BATTERY_IDX_ALARM, Value);

	return (ret);
}

int GetBatteryThreshold0()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_0));
}

int GetBatteryThreshold1()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_1));
}

int GetBatteryThreshold2()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_2));
}

int GetBatteryThreshold3()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_3));
}

int GetBatteryThreshold4()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_4));
}

int GetBatteryLowThreshold()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_LOW));
}

int GetBatteryBadThreshold()
{
	return ((int)BatteryGetThresholds(BATTERY_IDX_ALARM));
}

struct battery_data *batteryGetStatus()
{
	/* update the thresholds and charge state in case they changed */
	batteryStatus.batteryThreshold0 = BatteryGetThresholds(BATTERY_IDX_0);
	batteryStatus.batteryThreshold1 = BatteryGetThresholds(BATTERY_IDX_1);
	batteryStatus.batteryThreshold2 = BatteryGetThresholds(BATTERY_IDX_2);
	batteryStatus.batteryThreshold3 = BatteryGetThresholds(BATTERY_IDX_3);
	batteryStatus.batteryThreshold4 = BatteryGetThresholds(BATTERY_IDX_4);
	batteryStatus.batteryThresholdGood =
		BatteryGetThresholds(BATTERY_IDX_4);
	batteryStatus.batteryThresholdBad =
		BatteryGetThresholds(BATTERY_IDX_ALARM);
	batteryStatus.batteryThresholdLow =
		BatteryGetThresholds(BATTERY_IDX_LOW);
	batteryStatus.batteryChgState = BatteryGetChgState();
	return (&batteryStatus);
}

uint8_t BatteryGetChgState()
{
	int pinState = 0;
	uint8_t pwrState = 0;

	pinState = gpio_pin_get(batteryPwrStateDev, PWR_STATE_PIN);
	if (pinState == PWR_PIN_PWR_PRESENT) {
		pwrState = BATTERY_EXT_POWER_STATE;
	} else {
		pwrState = BATTERY_DISCHARGING_STATE;
	}

	pinState = gpio_pin_get(batteryChgStateDev, CHG_STATE_PIN);
	if (pinState == CHG_PIN_CHARGING) {
		pwrState |= BATTERY_CHARGING_STATE;
	} else {
		pwrState |= BATTERY_NOT_CHARGING_STATE;
	}

	return (pwrState);
}

void BatteryInit()
{
	uint16_t batteryData = 0;

	BatteryGpioInit();

	/* zero out the array of previous voltage readings */
	lastVoltageReadingIdx = 0;
	memset(previousVoltageReadings, 0,
	       sizeof(uint16_t) * BATTERY_NUM_READINGS);

	/* initialize the battery thresholds from NVM */
	nvReadBatteryLow(&batteryData);
	BatterySetThresholds(BATTERY_IDX_LOW, batteryData);
	nvReadBatteryAlarm(&batteryData);
	BatterySetThresholds(BATTERY_IDX_ALARM, batteryData);
	nvReadBattery4(&batteryData);
	BatterySetThresholds(BATTERY_IDX_4, batteryData);
	nvReadBattery3(&batteryData);
	BatterySetThresholds(BATTERY_IDX_3, batteryData);
	nvReadBattery2(&batteryData);
	BatterySetThresholds(BATTERY_IDX_2, batteryData);
	nvReadBattery1(&batteryData);
	BatterySetThresholds(BATTERY_IDX_1, batteryData);
	nvReadBattery0(&batteryData);
	BatterySetThresholds(BATTERY_IDX_0, batteryData);

	/* update values in the ble battery service */
	battery_svc_update_data();

	/* start periodic ADC conversions */
	power_mode_set(true);

	return;
}

uint8_t BatterySetThresholds(enum battery_thresh_idx Thresh, uint16_t Value)
{
	uint8_t status = BATTERY_FAIL;

	if (Thresh < BATTERY_IDX_MAX) {
		batteryThresholds[Thresh] = Value;
		status = BATTERY_SUCCESS;
		switch (Thresh) {
		case BATTERY_IDX_LOW:
			nvStoreBatteryLow(&Value);
			break;
		case BATTERY_IDX_ALARM:
			nvStoreBatteryAlarm(&Value);
			break;
		case BATTERY_IDX_4:
			nvStoreBattery4(&Value);
			break;
		case BATTERY_IDX_3:
			nvStoreBattery3(&Value);
			break;
		case BATTERY_IDX_2:
			nvStoreBattery2(&Value);
			break;
		case BATTERY_IDX_1:
			nvStoreBattery1(&Value);
			break;
		case BATTERY_IDX_0:
			nvStoreBattery0(&Value);
			break;
		default:
			break;
		}
	}

	return (status);
}

uint16_t BatteryGetThresholds(enum battery_thresh_idx Thresh)
{
	uint16_t threshValue = 0;

	if (Thresh < BATTERY_IDX_MAX) {
		threshValue = batteryThresholds[Thresh];
	}

	return (threshValue);
}

uint16_t BatteryCalculateRunningAvg(uint16_t Voltage)
{
	uint32_t total = 0;
	uint16_t ret = 0;
	uint8_t idx = 0;

	/* store the latest voltage reading */
	previousVoltageReadings[lastVoltageReadingIdx] = Voltage;

	/* increment the index, but reset to zero if we are passed
	   the max number of saved readings.
	*/
	if (++lastVoltageReadingIdx >= BATTERY_NUM_READINGS) {
		lastVoltageReadingIdx = 0;
	}

	/* calculate the average voltage of the last
	   BATTERY_NUM_READINGS number of samples.
	*/
	for (idx = 0; idx < BATTERY_NUM_READINGS; idx++) {
		if (previousVoltageReadings[idx] != 0.0) {
			total += previousVoltageReadings[idx];
		} else {
			break;
		}
	}

	if (idx != 0) {
		ret = total / idx;
	}

	return (ret);
}

static int ReadTempSensor()
{
	int status = 0;
	int temp = INVALID_TEMPERATURE;
	struct sensor_value val;
	const struct device *sensor =
		device_get_binding(DT_LABEL(DT_INST(0, st_lis2dh)));

	status = sensor_sample_fetch_chan(sensor, SENSOR_CHAN_AMBIENT_TEMP);

	if (status >= 0) {
		status = sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP,
					    &val);
	}

	if ((status < 0) || (val.val1 == INVALID_TEMPERATURE)) {
		LOG_WRN("Failed to retrieve temperature\n");
	} else {
		temp = val.val1;
	}

	return (temp);
}

enum battery_status BatteryCalculateRemainingCapacity(uint16_t Volts)
{
	int32_t Temperature = 0;
	int16_t vOffset = 0;
	int16_t Voltage = 0;

	Voltage = BatteryCalculateRunningAvg(Volts);

	/* get the ambient temperature from the LIS3DHTR sensor */
	Temperature = ReadTempSensor();

	/* if the temperature can't be read, then just use the
	 * BASE_TEMP value as a safe default.
	 */
	if (Temperature <= INVALID_TEMPERATURE) {
		Temperature = BASE_TEMP;
	}
#ifdef CONFIG_LAIRD_CONNECT_BATTERY_LOGGING
	BatteryLogData(Voltage, Temperature);
#endif

	/* adjust the voltage based on the ambient temperature */
	vOffset = DetermineTempOffset(Temperature);

	Voltage -= vOffset;

	/* convert the raw voltage to segment value */
	batteryCapacity = CalculateRemainingCapacity(Voltage);

	/* send battery data notifications */
	battery_svc_set_battery(Voltage, batteryCapacity);

	/* send up a warning for low battery if the battery is below
	 * the alarm threshold and not externally powered.
	 */
	if ((batteryCapacity <= batteryThresholds[BATTERY_IDX_ALARM]) &&
	    ((BatteryGetChgState() & BATTERY_EXT_POWER_STATE) == 0)) {
		batteryAlarmState = BATTERY_ALARM_ACTIVE;
		battery_svc_set_alarm_state(batteryAlarmState);
	} else if ((batteryCapacity > batteryThresholds[BATTERY_IDX_ALARM]) &&
		   (batteryAlarmState == BATTERY_ALARM_ACTIVE)) {
		batteryAlarmState = BATTERY_ALARM_INACTIVE;
		battery_svc_set_alarm_state(batteryAlarmState);
	}

	batteryStatus.batteryVoltage = Voltage;
	batteryStatus.batteryCapacity = batteryCapacity;
	batteryStatus.ambientTemperature = Temperature;

	return (batteryCapacity);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
#ifdef CONFIG_LAIRD_CONNECT_BATTERY_LOGGING
static void BatteryLogData(int16_t voltage, int32_t temp)
{
	char *logStr = k_malloc(MAX_LOG_STR_SIZE);
	logStr = k_malloc(MAX_LOG_STR_SIZE);
	snprintk(logStr, MAX_LOG_STR_SIZE, "%d,%d", voltage, temp);
	if (logStr > 0) {
		sdCardLogBatteryData(logStr, strlen(logStr));
		k_free(logStr);
		logStr = 0;
	}
}
#endif

static void BatteryStateChanged(const struct device *Dev,
				struct gpio_callback *Cb, uint32_t Pins)
{
	k_work_submit(&chgStateWork);
}

static void BatteryGpioInit()
{
	/* configure the charging state gpio  */
	k_work_init(&chgStateWork, ChgStateHandler);
	batteryChgStateDev = device_get_binding(CHG_STATE_PORT);
	gpio_pin_configure(batteryChgStateDev, CHG_STATE_PIN,
			   (GPIO_INPUT | GPIO_INT_ENABLE | GPIO_INT_EDGE |
			    GPIO_INT_EDGE_BOTH | GPIO_ACTIVE_HIGH));
	gpio_init_callback(&batteryChgStateCb, BatteryStateChanged,
			   BIT(CHG_STATE_PIN));
	gpio_add_callback(batteryChgStateDev, &batteryChgStateCb);

	/* configure the power state gpio */
	batteryPwrStateDev = device_get_binding(PWR_STATE_PORT);
	gpio_pin_configure(batteryPwrStateDev, PWR_STATE_PIN,
			   (GPIO_INPUT | GPIO_INT_ENABLE | GPIO_INT_EDGE |
			    GPIO_INT_EDGE_BOTH | GPIO_ACTIVE_HIGH));
	gpio_init_callback(&batteryPwrStateCb, BatteryStateChanged,
			   BIT(PWR_STATE_PIN));
	gpio_add_callback(batteryPwrStateDev, &batteryPwrStateCb);
}

static int16_t DetermineTempOffset(int32_t Temperature)
{
	int16_t tempOffset;
	int16_t offsetPerDegree = BATTERY_VOLT_OFFSET / BASE_TEMP;
	int16_t voltageOffset;

	tempOffset = BASE_TEMP - Temperature;
	voltageOffset = offsetPerDegree * tempOffset;

	return (voltageOffset);
}

static void ChgStateHandler(struct k_work *Item)
{
	uint8_t state = BatteryGetChgState();
	battery_svc_set_chg_state(state);

	return;
}

static enum battery_status CalculateRemainingCapacity(int16_t Voltage)
{
	enum battery_status battStat;

	if (Voltage > batteryThresholds[BATTERY_IDX_3]) {
		battStat = BATTERY_STATUS_4;
	} else if ((Voltage <= batteryThresholds[BATTERY_IDX_3]) &&
		   (Voltage > batteryThresholds[BATTERY_IDX_2])) {
		battStat = BATTERY_STATUS_3;
	} else if ((Voltage <= batteryThresholds[BATTERY_IDX_2]) &&
		   (Voltage > batteryThresholds[BATTERY_IDX_1])) {
		battStat = BATTERY_STATUS_2;
	} else if (Voltage <= batteryThresholds[BATTERY_IDX_1]) {
		battStat = BATTERY_STATUS_1;
	} else {
		battStat = BATTERY_STATUS_0;
	}

	return (battStat);
}
