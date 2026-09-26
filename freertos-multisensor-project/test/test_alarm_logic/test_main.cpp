// Host-side unit tests for the alarm decision logic (lab step 30).
// Run with: pio test -e native

#include <unity.h>
#include "alarm_logic.h"

#define ASSERT_STATE(expected, actual) \
    TEST_ASSERT_EQUAL_STRING(alarmStateName(expected), alarmStateName(actual))

void setUp(void) {}
void tearDown(void) {}

static void test_typical_room_temperature_is_normal(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(25.4f));
}

static void test_thresholds_themselves_are_normal(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(ALARM_LOW_TEMPERATURE_C));
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(ALARM_HIGH_TEMPERATURE_C));
}

// 0.1 C is the DHT22's resolution, so these are the nearest readings outside
// the normal range.
static void test_one_step_below_low_threshold_is_low(void)
{
    ASSERT_STATE(AlarmState::LOW_TEMPERATURE, evaluateTemperature(17.9f));
}

static void test_one_step_above_high_threshold_is_high(void)
{
    ASSERT_STATE(AlarmState::HIGH_TEMPERATURE, evaluateTemperature(30.1f));
}

// The DHT22's full range is -40 to 80 C.
static void test_sensor_range_extremes(void)
{
    ASSERT_STATE(AlarmState::LOW_TEMPERATURE, evaluateTemperature(-40.0f));
    ASSERT_STATE(AlarmState::HIGH_TEMPERATURE, evaluateTemperature(80.0f));
}

// The firmware builds temperatures as tenths / 10.0f, exactly as below; make
// sure that arithmetic doesn't push a boundary reading across the threshold.
static void test_boundaries_as_built_from_dht22_tenths(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(180 / 10.0f));
    ASSERT_STATE(AlarmState::LOW_TEMPERATURE, evaluateTemperature(179 / 10.0f));
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(300 / 10.0f));
    ASSERT_STATE(AlarmState::HIGH_TEMPERATURE, evaluateTemperature(301 / 10.0f));
}

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("NORMAL", alarmStateName(AlarmState::NORMAL));
    TEST_ASSERT_EQUAL_STRING("LOW_TEMPERATURE", alarmStateName(AlarmState::LOW_TEMPERATURE));
    TEST_ASSERT_EQUAL_STRING("HIGH_TEMPERATURE", alarmStateName(AlarmState::HIGH_TEMPERATURE));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_typical_room_temperature_is_normal);
    RUN_TEST(test_thresholds_themselves_are_normal);
    RUN_TEST(test_one_step_below_low_threshold_is_low);
    RUN_TEST(test_one_step_above_high_threshold_is_high);
    RUN_TEST(test_sensor_range_extremes);
    RUN_TEST(test_boundaries_as_built_from_dht22_tenths);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
