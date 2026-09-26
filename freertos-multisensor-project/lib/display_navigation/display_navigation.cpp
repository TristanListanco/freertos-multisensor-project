#include "display_navigation.h"

DisplayMode nextDisplayMode(DisplayMode mode)
{
    return static_cast<DisplayMode>((static_cast<int>(mode) + 1) % DISPLAY_MODE_COUNT);
}

DisplayMode previousDisplayMode(DisplayMode mode)
{
    // Adding COUNT - 1 instead of subtracting 1 keeps the value non-negative.
    return static_cast<DisplayMode>((static_cast<int>(mode) + DISPLAY_MODE_COUNT - 1) %
                                    DISPLAY_MODE_COUNT);
}

const char *displayModeName(DisplayMode mode)
{
    switch (mode)
    {
    case DisplayMode::TEMPERATURE:
        return "Temperature";
    case DisplayMode::HUMIDITY:
        return "Humidity";
    case DisplayMode::LIGHT:
        return "Light";
    default:
        return "Motion";
    }
}
