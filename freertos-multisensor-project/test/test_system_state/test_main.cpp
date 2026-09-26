// Host-side unit tests for the ACTIVE/INACTIVE state machine (lab step 32).
// Run with: pio test -e native

#include <unity.h>
#include "system_state.h"

#define ASSERT_STATE(expected, actual) \
    TEST_ASSERT_EQUAL_STRING(systemStateName(expected), systemStateName(actual))

static const uint32_t TIMEOUT_MS = 15000;

void setUp(void) {}
void tearDown(void) {}

static void test_active_stays_active_just_before_timeout(void)
{
    ASSERT_STATE(SystemState::ACTIVE,
                 nextSystemState(SystemState::ACTIVE, false, TIMEOUT_MS - 1, TIMEOUT_MS));
}

static void test_active_goes_inactive_at_timeout(void)
{
    ASSERT_STATE(SystemState::INACTIVE,
                 nextSystemState(SystemState::ACTIVE, false, TIMEOUT_MS, TIMEOUT_MS));
}

static void test_motion_keeps_active(void)
{
    ASSERT_STATE(SystemState::ACTIVE, nextSystemState(SystemState::ACTIVE, true, 0, TIMEOUT_MS));
}

static void test_inactive_stays_inactive_without_motion(void)
{
    ASSERT_STATE(SystemState::INACTIVE,
                 nextSystemState(SystemState::INACTIVE, false, 3 * TIMEOUT_MS, TIMEOUT_MS));
}

static void test_motion_restores_active(void)
{
    ASSERT_STATE(SystemState::ACTIVE, nextSystemState(SystemState::INACTIVE, true, 0, TIMEOUT_MS));
}

// Replays what MotionTask does: poll every 100 ms, PIR high from 0 to 5 s (the
// Wokwi PIR's default hold time), then quiet. The system must stay ACTIVE until
// 15 s after the PIR last read high, then go INACTIVE.
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
        SystemState next = nextSystemState(state, motion, now - lastMotionMs, TIMEOUT_MS);
        if (next == SystemState::INACTIVE && state == SystemState::ACTIVE)
        {
            inactiveAtMs = now;
        }
        state = next;
    }

    ASSERT_STATE(SystemState::INACTIVE, state);
    TEST_ASSERT_EQUAL_UINT32(4900 + TIMEOUT_MS, inactiveAtMs); // last high poll was at 4.9 s
}

static void test_state_names(void)
{
    TEST_ASSERT_EQUAL_STRING("ACTIVE", systemStateName(SystemState::ACTIVE));
    TEST_ASSERT_EQUAL_STRING("INACTIVE", systemStateName(SystemState::INACTIVE));
}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_active_stays_active_just_before_timeout);
    RUN_TEST(test_active_goes_inactive_at_timeout);
    RUN_TEST(test_motion_keeps_active);
    RUN_TEST(test_inactive_stays_inactive_without_motion);
    RUN_TEST(test_motion_restores_active);
    RUN_TEST(test_timeline_goes_inactive_15_s_after_last_motion);
    RUN_TEST(test_state_names);
    return UNITY_END();
}
