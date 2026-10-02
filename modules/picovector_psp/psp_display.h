// C side of the PSP screen for picovector (pspdisplay.c): the calls the port
// uses to share the screen with the console and the launcher.
#ifndef PSP_DISPLAY_H
#define PSP_DISPLAY_H

#include <stdbool.h>

// True while a script has pspdisplay's screen. The console stays off
// the screen meanwhile.
bool psp_display_active(void);

// Hands the screen back to the console, keeping the last frame showing.
// The launcher calls it when a script ends; it's harmless if nothing's active.
void psp_display_release(void);

#endif // PSP_DISPLAY_H
