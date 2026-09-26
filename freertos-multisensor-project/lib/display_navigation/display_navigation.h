#ifndef DISPLAY_NAVIGATION_H
#define DISPLAY_NAVIGATION_H

// OLED pages and encoder navigation between them (lab steps 28-29). Pure
// logic, unit tested on the host (test/test_display_navigation,
// `pio test -e native`).

// The page the OLED shows (lab step 28).
enum class DisplayMode
{
    TEMPERATURE,
    HUMIDITY,
    LIGHT,
    MOTION
};
constexpr int DISPLAY_MODE_COUNT = static_cast<int>(DisplayMode::MOTION) + 1;

// One clockwise step: Temperature -> Humidity -> Light -> Motion -> Temperature.
DisplayMode nextDisplayMode(DisplayMode mode);

// One counterclockwise step: the reverse, wrapping from Temperature to Motion.
DisplayMode previousDisplayMode(DisplayMode mode);

// The page's name, as shown on the OLED.
const char *displayModeName(DisplayMode mode);

#endif // DISPLAY_NAVIGATION_H
