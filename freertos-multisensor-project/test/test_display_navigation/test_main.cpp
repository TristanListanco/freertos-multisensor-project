// Host-side unit tests for encoder navigation between OLED pages (lab steps 29
// and 43). Run with: pio test -e native

#include <unity.h>
#include "display_navigation.h"

// Compares the enum values themselves; on failure the message names the page
// actually returned.
#define ASSERT_MODE(expected, actual)                                                   \
    do                                                                                  \
    {                                                                                   \
        DisplayMode result = (actual);                                                  \
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(result), \
                                      displayModeName(result));                         \
    } while (0)

void setUp(void) {}
void tearDown(void) {}

static void test_next_moves_forward_one_page(void)
{
    ASSERT_MODE(DisplayMode::HUMIDITY, nextDisplayMode(DisplayMode::TEMPERATURE));
    ASSERT_MODE(DisplayMode::LIGHT, nextDisplayMode(DisplayMode::HUMIDITY));
    ASSERT_MODE(DisplayMode::MOTION, nextDisplayMode(DisplayMode::LIGHT));
}

static void test_next_wraps_from_motion_to_temperature(void)
{
    ASSERT_MODE(DisplayMode::TEMPERATURE, nextDisplayMode(DisplayMode::MOTION));
}

static void test_previous_moves_back_one_page(void)
{
    ASSERT_MODE(DisplayMode::LIGHT, previousDisplayMode(DisplayMode::MOTION));
    ASSERT_MODE(DisplayMode::HUMIDITY, previousDisplayMode(DisplayMode::LIGHT));
    ASSERT_MODE(DisplayMode::TEMPERATURE, previousDisplayMode(DisplayMode::HUMIDITY));
}

static void test_previous_wraps_from_temperature_to_motion(void)
{
    ASSERT_MODE(DisplayMode::MOTION, previousDisplayMode(DisplayMode::TEMPERATURE));
}

// A clockwise step followed by a counterclockwise one, or the other way round,
// must land back on the starting page, from every page.
static void test_next_and_previous_undo_each_other(void)
{
    const DisplayMode all[] = {DisplayMode::TEMPERATURE, DisplayMode::HUMIDITY, DisplayMode::LIGHT,
                               DisplayMode::MOTION};
    for (DisplayMode mode : all)
    {
        ASSERT_MODE(mode, previousDisplayMode(nextDisplayMode(mode)));
        ASSERT_MODE(mode, nextDisplayMode(previousDisplayMode(mode)));
    }
}

// The lab's clockwise sequence, from Temperature all the way round.
static void test_clockwise_cycle_visits_every_page_in_order(void)
{
    const DisplayMode expected[] = {DisplayMode::HUMIDITY, DisplayMode::LIGHT, DisplayMode::MOTION,
                                    DisplayMode::TEMPERATURE};
    DisplayMode mode = DisplayMode::TEMPERATURE;
    for (DisplayMode page : expected)
    {
        mode = nextDisplayMode(mode);
        ASSERT_MODE(page, mode);
    }
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_next_moves_forward_one_page);
    RUN_TEST(test_next_wraps_from_motion_to_temperature);
    RUN_TEST(test_previous_moves_back_one_page);
    RUN_TEST(test_previous_wraps_from_temperature_to_motion);
    RUN_TEST(test_next_and_previous_undo_each_other);
    RUN_TEST(test_clockwise_cycle_visits_every_page_in_order);
    return UNITY_END();
}
