// Host-side unit tests for the ACTIVE/INACTIVE state machine (lab steps 32 and
// 43). Run with: pio test -e native

#include <unity.h>
#include "system_state.h"

// Compares the enum values themselves; on failure the message names the state
// actually returned.
#define ASSERT_STATE(expected, actual)                                                  \
    do                                                                                  \
    {                                                                                   \
        SystemState result = (actual);                                                  \
        TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(result), \
                                      systemStateName(result));                         \
    } while (0)

static const uint32_t TIMEOUT_MS = 15000;

void setUp(void) {}
void tearDown(void) {}

// ACTIVE, no timeout yet: 1 ms short of it.
static void test_active_without_timeout_stays_active(void)
{
    ASSERT_STATE(SystemState::ACTIVE,
                 evaluateSystemState(SystemState::ACTIVE, false, TIMEOUT_MS - 1, TIMEOUT_MS));
}

// ACTIVE, timeout reached: exactly TIMEOUT_MS without motion.
static void test_active_at_timeout_goes_inactive(void)
{
    ASSERT_STATE(SystemState::INACTIVE,
                 evaluateSystemState(SystemState::ACTIVE, false, TIMEOUT_MS, TIMEOUT_MS));
}

// INACTIVE, no motion: stays INACTIVE however long it has been.
static void test_inactive_without_motion_stays_inactive(void)
{
    ASSERT_STATE(SystemState::INACTIVE,
                 evaluateSystemState(SystemState::INACTIVE, false, TIMEOUT_MS, TIMEOUT_MS));
    ASSERT_STATE(SystemState::INACTIVE,
                 evaluateSystemState(SystemState::INACTIVE, false, 3 * TIMEOUT_MS, TIMEOUT_MS));
}

// INACTIVE, motion: back to ACTIVE.
static void test_inactive_with_motion_goes_active(void)
{
    ASSERT_STATE(SystemState::ACTIVE,
                 evaluateSystemState(SystemState::INACTIVE, true, 0, TIMEOUT_MS));
}

// ACTIVE, motion: stays ACTIVE even if the caller's elapsed time says the
// timeout has passed, because motion now wins.
static void test_active_with_motion_stays_active(void)
{
    ASSERT_STATE(SystemState::ACTIVE,
                 evaluateSystemState(SystemState::ACTIVE, true, 2 * TIMEOUT_MS, TIMEOUT_MS));
}

// Replays what MotionTask does: poll every 100 ms, PIR high from 0 to 5 s (the
// Wokwi PIR's default hold time), then quiet. The system must stay ACTIVE until
// 15 s after the PIR last read high, then go INACTIVE and stay there.
static void test_timeline_goes_inactive_15_s_after_last_motion(void)
{
    const uint32_t POLL_MS = 100;
    SystemState state = SystemState::ACTIVE;
    uint32_t lastMotionMs = 0;
    uint32_t inactiveAtMs = 0;

    for (uint32_t now = 0; now <= 30000; now += POLL_MS)
    {
        bool motion = now < 5000;
        if (motion)
        {
            lastMotionMs = now;
        }
        SystemState next = evaluateSystemState(state, motion, now - lastMotionMs, TIMEOUT_MS);
        if (next == SystemState::INACTIVE && state == SystemState::ACTIVE)
        {
            inactiveAtMs = now;
        }
        state = next;
    }

    ASSERT_STATE(SystemState::INACTIVE, state);
    TEST_ASSERT_EQUAL_UINT32(4900 + TIMEOUT_MS, inactiveAtMs); // last high poll was at 4.9 s
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_active_without_timeout_stays_active);
    RUN_TEST(test_active_at_timeout_goes_inactive);
    RUN_TEST(test_inactive_without_motion_stays_inactive);
    RUN_TEST(test_inactive_with_motion_goes_active);
    RUN_TEST(test_active_with_motion_stays_active);
    RUN_TEST(test_timeline_goes_inactive_15_s_after_last_motion);
    return UNITY_END();
}
