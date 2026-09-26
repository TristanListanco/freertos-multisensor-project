#ifndef DISPLAY_H
#define DISPLAY_H

// DisplayMode and page navigation are pure logic in lib/display_navigation.
#include "display_navigation.h"

// Owns the OLED (lab step 26): shows the latest reading on the selected page
// and turns the panel off while the system is INACTIVE.
void DisplayTask(void *pvParameters);

#endif // DISPLAY_H
