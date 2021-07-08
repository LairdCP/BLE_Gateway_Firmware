/**
 * @file laird_battery.c
 * @brief
 *
 * Copyright (c) 2020-2021 Laird Connectivity
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
#include "attr.h"
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
int UpdateBatteryThreshold0(int Value)
{
	return attr_set_uint32(ATTR_ID_battery0, Value);
}

int UpdateBatteryThreshold1(int Value)
{
	return attr_set_uint32(ATTR_ID_battery1, Value);
}

int UpdateBatteryThreshold2(int Value)
{
	return attr_set_uint32(ATTR_ID_battery2, Value);
}

int UpdateBatteryThreshold3(int Value)
{
	return attr_set_uint32(ATTR_ID_battery3, Value);
}

int UpdateBatteryThreshold4(int Value)
{
	return attr_set_uint32(ATTR_ID_battery4, Value);
}

int UpdateBatteryLowThreshold(int Value)
{
	return attr_set_uint32(ATTR_ID_batteryLowThreshold, Value);
}

int UpdateBatteryBadThreshold(int Value)
{
	return attr_set_uint32(ATTR_ID_batteryAlarmThreshold, Value);
}

int GetBatteryThreshold0(void)
{
	return attr_get_uint32(ATTR_ID_battery0, BATTERY_THRESH_0);
}

int GetBatteryThreshold1(void)
{
	return attr_get_uint32(ATTR_ID_battery1, BATTERY_THRESH_1);
}

int GetBatteryThreshold2(void)
{
	return attr_get_uint32(ATTR_ID_battery2, BATTERY_THRESH_2);
}

int GetBatteryThreshold3(void)
{
	return attr_get_uint32(ATTR_ID_battery3, BATTERY_THRESH_3);
}

int GetBatteryThreshold4(void)
{
	return attr_get_uint32(ATTR_ID_battery4, BATTERY_THRESH_4);
}

int GetBatteryLowThreshold(void)
{
	return attr_get_uint32(ATTR_ID_batteryLowThreshold, BATTERY_THRESH_LOW);
}

int GetBatteryBadThreshold(void)
{
	return attr_get_uint32(ATTR_ID_batteryAlarmThreshold,
			       BATTERY_THRESH_ALARM);
}

int GetBatteryAlarmThreshold(void)
{
	return GetBatteryBadThreshold();
}

struct battery_data *batteryGetStatus()
{
	/* update the thresholds and charge state in case they changed */
	batteryStatus.batteryThreshold0 = GetBatteryThreshold0();
	batteryStatus.batteryThreshold1 = GetBatteryThreshold1();
	batteryStatus.batteryThreshold2 = GetBatteryThreshold2();
	batteryStatus.batteryThreshold3 = GetBatteryThreshold3();
	batteryStatus.batteryThreshold4 = GetBatteryThreshold4();
	batteryStatus.batteryThresholdGood = GetBatteryThreshold4();
	batteryStatus.batteryThresholdBad = GetBatteryLowThreshold();
	batteryStatus.batteryThresholdLow = GetBatteryLowThreshold();
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

	attr_set_uint32(ATTR_ID_batteryChargingState, pwrState);

	return (pwrState);
}

void BatteryInit()
{
	BatteryGpioInit();

	/* zero out the array of previous voltage readings */
	lastVoltageReadingIdx = 0;
	memset(previousVoltageReadings, 0,
	       sizeof(uint16_t) * BATTERY_NUM_READINGS);

	return;
}

uint16_t BatteryCalculateRunningAvg(uint16_t Voltage)
{
	uint32_t total = 0;
	uint16_t ret = 0;
	uint8_t idx = 0;

	/* store the latest voltage reading */
	previousVoltageReadings[lastVoltageReadingIdx++] = Voltage;

	/* Mod operation to insure index is between 0 and BATTERY_NUM_READINGS */
	lastVoltageReadingIdx %= BATTERY_NUM_READINGS;

	/* calculate the average voltage of the last
	   BATTERY_NUM_READINGS number of samples.
	*/
	for (idx = 0; idx < BATTERY_NUM_READINGS; idx++) {
		if (previousVoltageReadings[idx]) {
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
		LOG_WRN("Failed to retrieve temperature");
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
	int16_t alarmThreshold;

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

	/* send up a warning for low battery if the battery is below
	 * the alarm threshold and not externally powered.
	 */
	alarmThreshold = GetBatteryAlarmThreshold();
	if ((batteryCapacity <= alarmThreshold) &&
	    ((BatteryGetChgState() & BATTERY_EXT_POWER_STATE) == 0)) {
		batteryAlarmState = BATTERY_ALARM_ACTIVE;
	} else if ((batteryCapacity > alarmThreshold) &&
		   (batteryAlarmState == BATTERY_ALARM_ACTIVE)) {
		batteryAlarmState = BATTERY_ALARM_INACTIVE;
	}

	batteryStatus.batteryVoltage = Voltage;
	batteryStatus.batteryCapacity = batteryCapacity;
	batteryStatus.ambientTemperature = Temperature;

	attr_set_uint32(ATTR_ID_batteryAlarm, batteryAlarmState);
	attr_set_uint32(ATTR_ID_batteryVoltageMv, Voltage);
	attr_set_uint32(ATTR_ID_batteryCapacity, batteryCapacity);
	attr_set_signed32(ATTR_ID_batteryTemperature, Temperature);

	return (batteryCapacity);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
#ifdef CONFIG_LAIRD_CONNECT_BATTERY_LOGGING
static void BatteryLogData(int16_t voltage, int32_t temp)
{
	char log_str[MAX_LOG_STR_SIZE];
	snprintk(log_str, MAX_LOG_STR_SIZE, "%d,%d", voltage, temp);
	sdCardLogBatteryData(log_str, strlen(log_str));
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
	BatteryGetChgState();
	return;
}

static enum battery_status CalculateRemainingCapacity(int16_t Voltage)
{
	enum battery_status battStat;
	int thresh3 = GetBatteryThreshold3();
	int thresh2 = GetBatteryThreshold2();
	int thresh1 = GetBatteryThreshold1();

	if (Voltage > thresh3) {
		battStat = BATTERY_STATUS_4;
	} else if ((Voltage <= thresh3) && (Voltage > thresh2)) {
		battStat = BATTERY_STATUS_3;
	} else if ((Voltage <= thresh2) && (Voltage > thresh1)) {
		battStat = BATTERY_STATUS_2;
	} else if (Voltage <= thresh1) {
		battStat = BATTERY_STATUS_1;
	} else {
		battStat = BATTERY_STATUS_0;
	}

	return (battStat);
}
