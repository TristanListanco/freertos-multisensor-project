// Host-side unit tests for the temperature alarm logic (lab steps 30 and 43).
// Run with: pio test -e native
//
// The normal range is ALARM_LOW_TEMPERATURE_C (18.0) to ALARM_HIGH_TEMPERATURE_C
// (30.0), inclusive. 0.1 C is the DHT22's resolution, so 17.9 and 30.1 are the
// nearest readings outside it.

#include <unity.h>
#include "alarm_logic.h"

// Compares the enum values themselves; on failure the message names the state
// actually returned.
#define ASSERT_STATE(expected, actual)                                                  \
    do                                                                                  \
    {                                                                                   \
        AlarmState result = (actual);                                                   \
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(result), \
                                      alarmStateName(result));                          \
    } while (0)

void setUp(void) {}
void tearDown(void) {}

static void test_below_lower_threshold_is_low(void)
{
    ASSERT_STATE(AlarmState::LOW_TEMPERATURE, evaluateTemperature(17.9f));
}

static void test_exactly_lower_threshold_is_normal(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(ALARM_LOW_TEMPERATURE_C));
}

static void test_normal_value_is_normal(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(25.4f));
}

static void test_exactly_upper_threshold_is_normal(void)
{
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(ALARM_HIGH_TEMPERATURE_C));
}

static void test_above_upper_threshold_is_high(void)
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
    ASSERT_STATE(AlarmState::LOW_TEMPERATURE, evaluateTemperature(179 / 10.0f));
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(180 / 10.0f));
    ASSERT_STATE(AlarmState::NORMAL, evaluateTemperature(300 / 10.0f));
    ASSERT_STATE(AlarmState::HIGH_TEMPERATURE, evaluateTemperature(301 / 10.0f));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_below_lower_threshold_is_low);
    RUN_TEST(test_exactly_lower_threshold_is_normal);
    RUN_TEST(test_normal_value_is_normal);
    RUN_TEST(test_exactly_upper_threshold_is_normal);
    RUN_TEST(test_above_upper_threshold_is_high);
    RUN_TEST(test_sensor_range_extremes);
    RUN_TEST(test_boundaries_as_built_from_dht22_tenths);
    return UNITY_END();
}
