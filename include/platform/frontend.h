#ifndef GUARD_PLATFORM_FRONTEND_H
#define GUARD_PLATFORM_FRONTEND_H

#include <stdbool.h>
#include <stdint.h>

// Physical buttons, as reported by the platform layer
enum
{
    PHYS_UP       = 1 << 0,
    PHYS_DOWN     = 1 << 1,
    PHYS_LEFT     = 1 << 2,
    PHYS_RIGHT    = 1 << 3,
    PHYS_CROSS    = 1 << 4,
    PHYS_CIRCLE   = 1 << 5,
    PHYS_SQUARE   = 1 << 6,
    PHYS_TRIANGLE = 1 << 7,
    PHYS_L        = 1 << 8,
    PHYS_R        = 1 << 9,
    PHYS_START    = 1 << 10,
    PHYS_SELECT   = 1 << 11,
    PHYS_COUNT    = 12,
};

// Remappable actions
enum
{
    ACTION_A,
    ACTION_B,
    ACTION_L,
    ACTION_R,
    ACTION_START,
    ACTION_SELECT,
    ACTION_FAST_FORWARD,
    ACTION_MENU,
    ACTION_COUNT,
};

enum
{
    SCALE_1X,
    SCALE_2X,
    SCALE_3X,
    SCALE_FIT,     // keep aspect ratio, full height
    SCALE_STRETCH, // full screen
    SCALE_COUNT,
};

// What the frontend wants the platform layer to do after a menu update
enum
{
    FE_REQUEST_NONE,
    FE_REQUEST_SAVE_STATE,
    FE_REQUEST_LOAD_STATE,
    FE_REQUEST_RESET,
    FE_REQUEST_SWITCH_SAVE_FILE,
    FE_REQUEST_QUIT,
};

struct FrontendConfig
{
    int scale;
    int smoothFilter;
    int fastForwardSpeed; // 2..5
    int saveFile;         // 1..3
    int touchOpensMenu;
    int saveAnywhere;     // real in-game Save during runs / where saving is disabled
    uint32_t actionButton[ACTION_COUNT]; // PHYS_* bit per action
};

extern struct FrontendConfig gFrontendConfig;

void Frontend_Init(const char *dataDir);
void Frontend_SaveConfig(void);

// Game input from physical buttons (d-pad is never remapped)
uint16_t Frontend_MapButtons(uint32_t phys);
bool Frontend_FastForwardHeld(uint32_t phys);
// True on the frame the menu button goes down (outside the menu)
bool Frontend_MenuButtonPressed(uint32_t phys);

bool Frontend_MenuIsOpen(void);
void Frontend_OpenMenu(const uint16_t *currentFrame);
// Feeds held physical buttons; returns an FE_REQUEST_*
int Frontend_UpdateMenu(uint32_t phys);
// Composes the menu over the frozen game frame (240x160 ABGR1555)
void Frontend_DrawMenu(uint16_t *frame);
void Frontend_ShowMessage(const char *msg);
// Draws a pending short message (e.g. "State saved") over the game frame
void Frontend_DrawMessage(uint16_t *frame);

int Frontend_StateSlot(void);
const char *Frontend_StatePath(void);
const char *Frontend_SavePath(void);

// Destination rectangle for the game image on a screen of the given size
void Frontend_GetDestRect(int screenW, int screenH, int *x, int *y, int *w, int *h);

#endif // GUARD_PLATFORM_FRONTEND_H
