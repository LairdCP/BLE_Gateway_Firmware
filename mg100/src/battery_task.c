/**
 * @file battery_task.c
 * @brief
 *
 * Copyright (c) 2020 Laird Connectivity
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(battery_task);

/******************************************************************************/
/* Includes                                                                   */
/******************************************************************************/
#include <gpio.h>
#include <sensor.h>

#include "battery.h"
#include "ble_battery_service.h"
#include "nv.h"
#include "power.h"

/******************************************************************************/
/* Local Constant, Macro and Type Definitions                                 */
/******************************************************************************/

/* These values are specific to the MG100 design, determined through testing
    over the supported temperature range. Threshold 4 is the maximum voltage, and
    threshold 0 is the minimum operating voltage.
*/
#define BATTERY_THRESH_POWER_OFF 2750
#define BATTERY_THRESH_4 4200
#define BATTERY_THRESH_3 3800
#define BATTERY_THRESH_2 3400
#define BATTERY_THRESH_1 3000
#define BATTERY_THRESH_0 BATTERY_THRESH_POWER_OFF

#define BATTERY_THRESH_LOW  BATTERY_THRESH_2
#define BATTERY_THRESH_ALARM BATTERY_THRESH_1

#define BATTERY_VOLT_OFFSET 150
#define BASE_TEMP 20

#define BATTERY_NUM_READINGS 5

/* values used to indicate the charger state */
#define BATTERY_EXT_POWER_STATE     BIT(0)
#define BATTERY_CHARGING_STATE      BIT(1)
#define BATTERY_NOT_CHARGING_STATE  BIT(2)
#define BATTERY_DISCHARGING_STATE   BIT(3)

/* battery charging related GPIO settings */
#define CHG_STATE_PORT		        DT_NORDIC_NRF_GPIO_0_LABEL
#define CHG_STATE_PIN		        30
#define PWR_STATE_PORT		        DT_NORDIC_NRF_GPIO_1_LABEL
#define PWR_STATE_PIN		        4
#define CHG_PIN_CHARGING            0
#define CHG_PIN_NOT_CHARGING        1
#define PWR_PIN_PWR_PRESENT         0
#define PWR_PIN_PWR_NOT_PRESENT     1

#define INVALID_TEMPERATURE         -127

/******************************************************************************/
/* Global Data Definitions                                                    */
/******************************************************************************/

/******************************************************************************/
/* Local Data Definitions                                                     */
/******************************************************************************/
static enum battery_status batteryCapacity = BATTERY_STATUS_0;
static u16_t batteryThresholds[BATTERY_IDX_MAX] = { BATTERY_THRESH_0, 
                                                BATTERY_THRESH_1,
                                                BATTERY_THRESH_2,
                                                BATTERY_THRESH_3,
                                                BATTERY_THRESH_4,
                                                BATTERY_THRESH_LOW,
                                                BATTERY_THRESH_ALARM };
static u16_t previousVoltageReadings[BATTERY_NUM_READINGS];
static u8_t lastVoltageReadingIdx = 0;
static u8_t batteryAlarmState = BATTERY_ALARM_INACTIVE;

static struct k_work chgStateWork;
static struct device * batteryChgStateDev;
static struct gpio_callback batteryChgStateCb;
static struct device * batteryPwrStateDev;

/******************************************************************************/
/* Local Function Prototypes                                                  */
/******************************************************************************/
static int ReadTempSensor();
static enum battery_status CalculateRemainingCapacity(s16_t Voltage);
static s16_t DetermineTempOffset(s32_t Temperature);
static void ChgStateHandler(struct k_work *Item);
static void BatteryGpioInit();

/******************************************************************************/
/* Global Function Definitions                                                */
/******************************************************************************/
u8_t BatteryGetChgState()
{
    int ret = 0;
    u32_t pinState = 0;
    u8_t pwrState = 0;

    ret = gpio_pin_read(batteryPwrStateDev, PWR_STATE_PIN, &pinState);
    if (!ret)
    {
        if (pinState == PWR_PIN_PWR_PRESENT)
        {
            pwrState = BATTERY_EXT_POWER_STATE;
        }
        else
        {
            pwrState = BATTERY_DISCHARGING_STATE;
        }
    }

    ret = gpio_pin_read(batteryChgStateDev, CHG_STATE_PIN, &pinState);
    if(!ret)
    {
        if (pinState == CHG_PIN_CHARGING)
        {
            pwrState |= BATTERY_CHARGING_STATE;
        }
        else
        {
            pwrState |= BATTERY_NOT_CHARGING_STATE;
        }
    }

    return (pwrState);
}

void BatteryInit()
{
    u16_t batteryData = 0;

    BatteryGpioInit();

    /* zero out the array of previous voltage readings */
    lastVoltageReadingIdx = 0;
    memset(previousVoltageReadings, 0, sizeof(u16_t) * BATTERY_NUM_READINGS);

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

u8_t BatterySetThresholds(enum battery_thresh_idx Thresh, u16_t Value)
{
    u8_t status = BATTERY_FAIL;

    if (Thresh < BATTERY_IDX_MAX)
    {
        batteryThresholds[Thresh] = Value;
        status = BATTERY_SUCCESS;
        switch(Thresh) {
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

u16_t BatteryGetThresholds(enum battery_thresh_idx Thresh)
{
    u16_t threshValue = 0;

    if (Thresh < BATTERY_IDX_MAX)
    {
        threshValue = batteryThresholds[Thresh];
    }

    return (threshValue);
}

u16_t BatteryCalculateRunningAvg(u16_t Voltage)
{
    u32_t total = 0;
    u8_t idx = 0;

    /* store the latest voltage reading */
    previousVoltageReadings[lastVoltageReadingIdx] = Voltage;

    /* increment the index, but reset to zero if we are passed
       the max number of saved readings.
    */
    if (++lastVoltageReadingIdx >= BATTERY_NUM_READINGS)
    {
        lastVoltageReadingIdx = 0;
    }

    /* calculate the average voltage of the last
       BATTERY_NUM_READINGS number of samples.
    */
    for (idx = 0; idx < BATTERY_NUM_READINGS; idx++)
    {
        if (previousVoltageReadings[idx] != 0.0)
        {
            total += previousVoltageReadings[idx];
        }
        else
        {
            break;
        }
    }

    return ( total / idx );
}

static int ReadTempSensor()
{
	int status = 0;
    int temp = INVALID_TEMPERATURE;
	struct sensor_value val[3];
    struct device *sensor = device_get_binding(DT_ST_LIS2DH_0_LABEL);

	status = sensor_sample_fetch(sensor);

    if (status >= 0) {
        status = sensor_channel_get(sensor, SENSOR_CHAN_AMBIENT_TEMP, val);
    }

    if ((status < 0) || (val->val1 == INVALID_TEMPERATURE)) {
        LOG_WRN("Failed to retrieve temperature\n");
    }
    else if ((status >= 0) && (val->val1 != INVALID_TEMPERATURE))
    {
        temp = val->val1;
    }

	return (temp);
}

enum battery_status BatteryCalculateRemainingCapacity(u16_t Volts)
{
    s32_t Temperature = 0;
    s16_t vOffset = 0;
    s16_t Voltage = 0;

    Voltage = BatteryCalculateRunningAvg(Volts);

    /* get the ambient temperature from the LIS3DHTR sensor */
    Temperature = ReadTempSensor();

    /* if the temperature can't be read, then just use the
     *  BASE_TEMP value as a safe default.
     */
    if (Temperature < INVALID_TEMPERATURE)
    {
        Temperature = BASE_TEMP;
    }

    /* adjust the voltage based on the ambient temperature */
    vOffset = DetermineTempOffset(Temperature);

	Voltage -= vOffset;

    /* convert the raw voltage to segment value */
    batteryCapacity = CalculateRemainingCapacity(Voltage);

    /* send battery data notifications */
    battery_svc_set_battery(Voltage, batteryCapacity);

    /* send up a warning for low battery if the battery is below
     *    the alarm threshold and not externally powered.
     */
    if ((batteryCapacity <= batteryThresholds[BATTERY_IDX_ALARM]) &&
           ((BatteryGetChgState() & BATTERY_EXT_POWER_STATE) == 0))
    {
        batteryAlarmState = BATTERY_ALARM_ACTIVE;
        battery_svc_set_alarm_state(batteryAlarmState);
    }
    else if ((batteryCapacity > batteryThresholds[BATTERY_IDX_ALARM]) &&
                (batteryAlarmState == BATTERY_ALARM_ACTIVE))
    {
        batteryAlarmState = BATTERY_ALARM_INACTIVE;
        battery_svc_set_alarm_state(batteryAlarmState);
    }

    return (batteryCapacity);
}

/******************************************************************************/
/* Local Function Definitions                                                 */
/******************************************************************************/
static void BatteryChgStateChanged(struct device *Dev,
			   struct gpio_callback *Cb, u32_t Pins)
{
	k_work_submit(&chgStateWork);
}

static void BatteryGpioInit()
{
    /* configure the charging state gpio  */
    k_work_init(&chgStateWork, ChgStateHandler);
	batteryChgStateDev = device_get_binding(CHG_STATE_PORT);
	gpio_pin_configure(batteryChgStateDev, CHG_STATE_PIN,
			   (GPIO_DIR_IN | GPIO_INT | GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
			    GPIO_INT_ACTIVE_LOW));
	gpio_init_callback(&batteryChgStateCb, BatteryChgStateChanged, BIT(CHG_STATE_PIN));
	gpio_add_callback(batteryChgStateDev, &batteryChgStateCb);
	gpio_pin_enable_callback(batteryChgStateDev, CHG_STATE_PIN);

    /* configure the power state gpio */
	batteryPwrStateDev = device_get_binding(PWR_STATE_PORT);
	gpio_pin_configure(batteryPwrStateDev, PWR_STATE_PIN,
			   (GPIO_DIR_IN | GPIO_INT | GPIO_INT_EDGE | GPIO_INT_DOUBLE_EDGE |
			    GPIO_INT_ACTIVE_LOW));
	gpio_init_callback(&batteryChgStateCb, BatteryChgStateChanged, BIT(PWR_STATE_PIN));
	gpio_add_callback(batteryPwrStateDev, &batteryChgStateCb);
	gpio_pin_enable_callback(batteryPwrStateDev, PWR_STATE_PIN);
}

static s16_t DetermineTempOffset(s32_t Temperature)
{
	s16_t tempOffset;
	s16_t offsetPerDegree = BATTERY_VOLT_OFFSET / BASE_TEMP;
	s16_t voltageOffset;

    tempOffset = BASE_TEMP - Temperature;
    voltageOffset = offsetPerDegree * tempOffset;

	return (voltageOffset);
}

static void ChgStateHandler(struct k_work *Item)
{
    u8_t state = BatteryGetChgState();
    battery_svc_set_chg_state(state);

    return;
}

static enum battery_status CalculateRemainingCapacity(s16_t Voltage)
{
    enum battery_status battStat;

	if (Voltage > batteryThresholds[BATTERY_IDX_3])
	{
		battStat = BATTERY_STATUS_4;
	}
	else if ((Voltage <= batteryThresholds[BATTERY_IDX_3]) && (Voltage > batteryThresholds[BATTERY_IDX_2]))
	{
		battStat = BATTERY_STATUS_3;
	}
	else if ((Voltage <= batteryThresholds[BATTERY_IDX_2]) && (Voltage > batteryThresholds[BATTERY_IDX_1]))
	{
		battStat = BATTERY_STATUS_2;
	}
	else if (Voltage <= batteryThresholds[BATTERY_IDX_1])
	{
		battStat = BATTERY_STATUS_1;
	}
    else
    {
        battStat = BATTERY_STATUS_0;
    }

    return (battStat);
}
/******************************************************************************/
/* Interrupt Service Routines                                                 */
/******************************************************************************/
