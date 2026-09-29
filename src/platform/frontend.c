#ifdef PORTABLE
// In-game frontend menu for the PC/Vita port: scaling, filtering, button
// remapping, save states, save files. Drawn in GBA resolution over a dimmed
// copy of the frozen game frame. Platform independent: the platform layer
// feeds it physical buttons and does the actual work for the requests.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "platform/frontend.h"
#include "platform/savestate.h"
#include "platform/frontend_font.h"

#define W 240
#define H 160
#define STATE_SLOTS 5
#define SAVE_FILES 3

// GBA key bits (include/gba/io_reg.h) - kept local so this file needs no game headers
#define GBA_A      0x0001
#define GBA_B      0x0002
#define GBA_SELECT 0x0004
#define GBA_START  0x0008
#define GBA_RIGHT  0x0010
#define GBA_LEFT   0x0020
#define GBA_UP     0x0040
#define GBA_DOWN   0x0080
#define GBA_R      0x0100
#define GBA_L      0x0200

struct FrontendConfig gFrontendConfig;

static char sDataDir[256];
static bool sMenuOpen;
static uint16_t sMenuBackground[W * H];
static uint32_t sPrevPhys;
static int sHoldFrames;
static int sCursor;
static int sMenuPage; // 0 main, 1 remap
static int sRemapWaiting = -1;
static int sStateSlot = 1;
static int sPendingSaveFile;
static char sMessage[40];
static int sMessageTimer;
static char sPathBuffer[300];

static const char *const sActionNames[ACTION_COUNT] = { "A", "B", "L", "R", "Start", "Select", "Fast fwd", "Menu" };
static const uint16_t sActionGbaKey[ACTION_COUNT] = { GBA_A, GBA_B, GBA_L, GBA_R, GBA_START, GBA_SELECT, 0, 0 };
static const char *const sScaleNames[SCALE_COUNT] = { "1x", "2x", "3x", "Fit", "Stretch" };

static const char *PhysName(uint32_t bit)
{
    static const char *const names[PHYS_COUNT] = {
        "Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle", "L", "R", "Start", "Select"
    };
    for (int i = 0; i < PHYS_COUNT; i++)
        if (bit == (1u << i))
            return names[i];
    return "-";
}

static void SetDefaults(void)
{
    gFrontendConfig.scale = SCALE_3X;
    gFrontendConfig.smoothFilter = 0;
    gFrontendConfig.fastForwardSpeed = 3;
    gFrontendConfig.saveFile = 1;
    gFrontendConfig.actionButton[ACTION_A] = PHYS_CROSS;
    gFrontendConfig.actionButton[ACTION_B] = PHYS_CIRCLE;
    gFrontendConfig.actionButton[ACTION_L] = PHYS_L;
    gFrontendConfig.actionButton[ACTION_R] = PHYS_R;
    gFrontendConfig.actionButton[ACTION_START] = PHYS_START;
    gFrontendConfig.actionButton[ACTION_SELECT] = PHYS_SELECT;
    gFrontendConfig.actionButton[ACTION_FAST_FORWARD] = PHYS_TRIANGLE;
    gFrontendConfig.actionButton[ACTION_MENU] = PHYS_SQUARE;
    gFrontendConfig.touchOpensMenu = 1;
}

static const char *DataPath(const char *name)
{
    snprintf(sPathBuffer, sizeof(sPathBuffer), "%s%s", sDataDir, name);
    return sPathBuffer;
}

void Frontend_Init(const char *dataDir)
{
    FILE *f;
    char line[128];

    snprintf(sDataDir, sizeof(sDataDir), "%s", dataDir);
    SetDefaults();

    f = fopen(DataPath("config.txt"), "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f))
    {
        char key[64];
        int value;
        if (sscanf(line, "%63[^=]=%d", key, &value) != 2)
            continue;
        if (!strcmp(key, "scale") && value >= 0 && value < SCALE_COUNT)
            gFrontendConfig.scale = value;
        else if (!strcmp(key, "smooth"))
            gFrontendConfig.smoothFilter = value != 0;
        else if (!strcmp(key, "ff_speed") && value >= 2 && value <= 5)
            gFrontendConfig.fastForwardSpeed = value;
        else if (!strcmp(key, "save_file") && value >= 1 && value <= SAVE_FILES)
            gFrontendConfig.saveFile = value;
        else if (!strcmp(key, "touch_menu"))
            gFrontendConfig.touchOpensMenu = value != 0;
        else if (!strcmp(key, "save_anywhere"))
            gFrontendConfig.saveAnywhere = value != 0;
        else
        {
            for (int i = 0; i < ACTION_COUNT; i++)
            {
                char name[32];
                snprintf(name, sizeof(name), "button_%d", i);
                if (!strcmp(key, name) && value >= 0 && value < PHYS_COUNT)
                    gFrontendConfig.actionButton[i] = 1u << value;
            }
        }
    }
    fclose(f);
}

void Frontend_SaveConfig(void)
{
    FILE *f = fopen(DataPath("config.txt"), "w");

    if (f == NULL)
        return;
    fprintf(f, "scale=%d\nsmooth=%d\nff_speed=%d\nsave_file=%d\ntouch_menu=%d\nsave_anywhere=%d\n", gFrontendConfig.scale,
            gFrontendConfig.smoothFilter, gFrontendConfig.fastForwardSpeed, gFrontendConfig.saveFile,
            gFrontendConfig.touchOpensMenu, gFrontendConfig.saveAnywhere);
    for (int i = 0; i < ACTION_COUNT; i++)
    {
        int bit = 0;
        while (bit < PHYS_COUNT && gFrontendConfig.actionButton[i] != (1u << bit))
            bit++;
        fprintf(f, "button_%d=%d\n", i, bit);
    }
    fclose(f);
}

uint16_t Frontend_MapButtons(uint32_t phys)
{
    uint16_t keys = 0;

    if (phys & PHYS_UP)    keys |= GBA_UP;
    if (phys & PHYS_DOWN)  keys |= GBA_DOWN;
    if (phys & PHYS_LEFT)  keys |= GBA_LEFT;
    if (phys & PHYS_RIGHT) keys |= GBA_RIGHT;
    for (int i = 0; i < ACTION_COUNT; i++)
        if (sActionGbaKey[i] && (phys & gFrontendConfig.actionButton[i]))
            keys |= sActionGbaKey[i];
    return keys;
}

bool Frontend_FastForwardHeld(uint32_t phys)
{
    return (phys & gFrontendConfig.actionButton[ACTION_FAST_FORWARD]) != 0;
}

// Called by the game's start menu (src/start_menu.c)
int Platform_SaveAnywhere(void)
{
    return gFrontendConfig.saveAnywhere;
}

bool Frontend_MenuButtonPressed(uint32_t phys)
{
    static uint32_t sPrevGamePhys;
    uint32_t menuButton = gFrontendConfig.actionButton[ACTION_MENU];
    bool pressed = (phys & menuButton) && !(sPrevGamePhys & menuButton);

    sPrevGamePhys = phys;
    return pressed && !sMenuOpen;
}

const char *Frontend_SavePath(void)
{
    // save file 1 keeps the original name so existing saves are picked up
    if (gFrontendConfig.saveFile <= 1)
        return DataPath("pokeemerald.sav");
    snprintf(sPathBuffer, sizeof(sPathBuffer), "%spokeemerald_%d.sav", sDataDir, gFrontendConfig.saveFile);
    return sPathBuffer;
}

int Frontend_StateSlot(void)
{
    return sStateSlot;
}

const char *Frontend_StatePath(void)
{
    snprintf(sPathBuffer, sizeof(sPathBuffer), "%sstate_save%d_slot%d.bin", sDataDir, gFrontendConfig.saveFile, sStateSlot);
    return sPathBuffer;
}

void Frontend_ShowMessage(const char *msg)
{
    snprintf(sMessage, sizeof(sMessage), "%s", msg);
    sMessageTimer = 120;
}

bool Frontend_MenuIsOpen(void)
{
    return sMenuOpen;
}

void Frontend_OpenMenu(const uint16_t *currentFrame)
{
    for (int i = 0; i < W * H; i++)
        sMenuBackground[i] = ((currentFrame[i] & 0x7BDE) >> 1) | 0x8000; // half brightness
    sMenuOpen = true;
    sMenuPage = 0;
    sCursor = 0;
    sRemapWaiting = -1;
    sPendingSaveFile = gFrontendConfig.saveFile;
    sPrevPhys = ~0u; // ignore buttons still held from the game
}

// ---- menu ------------------------------------------------------------------

enum
{
    ITEM_RESUME,
    ITEM_SAVE_STATE,
    ITEM_LOAD_STATE,
    ITEM_SCALE,
    ITEM_FILTER,
    ITEM_FF_SPEED,
    ITEM_SAVE_ANYWHERE,
    ITEM_REMAP,
    ITEM_SAVE_FILE,
    ITEM_RESET,
    ITEM_QUIT,
    ITEM_COUNT,
};

#define REMAP_ITEMS (ACTION_COUNT + 3) // actions, touch, defaults, back
#define REMAP_TOUCH (ACTION_COUNT)
#define REMAP_DEFAULTS (ACTION_COUNT + 1)

static int WrapAdd(int value, int delta, int count)
{
    return ((value + delta) % count + count) % count;
}

static void CloseMenu(void)
{
    sMenuOpen = false;
    Frontend_SaveConfig();
}

int Frontend_UpdateMenu(uint32_t phys)
{
    uint32_t pressed = phys & ~sPrevPhys;
    uint32_t held = phys & sPrevPhys;
    int items = (sMenuPage == 0) ? ITEM_COUNT : REMAP_ITEMS;
    int request = FE_REQUEST_NONE;

    if (sPrevPhys == ~0u)
    {
        // wait until everything is released before accepting input
        sPrevPhys = phys ? ~0u : 0;
        return FE_REQUEST_NONE;
    }
    sPrevPhys = phys;

    // key repeat for up/down/left/right
    if (held & (PHYS_UP | PHYS_DOWN | PHYS_LEFT | PHYS_RIGHT))
    {
        if (++sHoldFrames > 20 && (sHoldFrames % 5) == 0)
            pressed |= held & (PHYS_UP | PHYS_DOWN | PHYS_LEFT | PHYS_RIGHT);
    }
    else
    {
        sHoldFrames = 0;
    }

    if (sRemapWaiting >= 0)
    {
        uint32_t newButton = pressed & ~(PHYS_UP | PHYS_DOWN | PHYS_LEFT | PHYS_RIGHT);
        if (newButton)
        {
            uint32_t bit = newButton & -newButton; // lowest set bit
            // swap with any action already using that button
            for (int i = 0; i < ACTION_COUNT; i++)
                if (gFrontendConfig.actionButton[i] == bit)
                    gFrontendConfig.actionButton[i] = gFrontendConfig.actionButton[sRemapWaiting];
            gFrontendConfig.actionButton[sRemapWaiting] = bit;
            sRemapWaiting = -1;
        }
        return FE_REQUEST_NONE;
    }

    if (pressed & PHYS_UP)
        sCursor = WrapAdd(sCursor, -1, items);
    if (pressed & PHYS_DOWN)
        sCursor = WrapAdd(sCursor, 1, items);

    if (sMenuPage == 1)
    {
        if (pressed & PHYS_CIRCLE)
        {
            sMenuPage = 0;
            sCursor = ITEM_REMAP;
        }
        else if (pressed & PHYS_CROSS)
        {
            if (sCursor < ACTION_COUNT)
            {
                sRemapWaiting = sCursor;
            }
            else if (sCursor == REMAP_TOUCH)
            {
                gFrontendConfig.touchOpensMenu = !gFrontendConfig.touchOpensMenu;
            }
            else if (sCursor == REMAP_DEFAULTS)
            {
                int scale = gFrontendConfig.scale, smooth = gFrontendConfig.smoothFilter;
                int ff = gFrontendConfig.fastForwardSpeed, saveFile = gFrontendConfig.saveFile;
                int touch = gFrontendConfig.touchOpensMenu;
                SetDefaults();
                gFrontendConfig.touchOpensMenu = touch;
                gFrontendConfig.scale = scale;
                gFrontendConfig.smoothFilter = smooth;
                gFrontendConfig.fastForwardSpeed = ff;
                gFrontendConfig.saveFile = saveFile;
            }
            else
            {
                sMenuPage = 0;
                sCursor = ITEM_REMAP;
            }
        }
        return FE_REQUEST_NONE;
    }

    if (pressed & (PHYS_LEFT | PHYS_RIGHT))
    {
        int delta = (pressed & PHYS_LEFT) ? -1 : 1;
        switch (sCursor)
        {
        case ITEM_SAVE_STATE:
        case ITEM_LOAD_STATE:
            sStateSlot = WrapAdd(sStateSlot - 1, delta, STATE_SLOTS) + 1;
            break;
        case ITEM_SCALE:
            gFrontendConfig.scale = WrapAdd(gFrontendConfig.scale, delta, SCALE_COUNT);
            break;
        case ITEM_FILTER:
            gFrontendConfig.smoothFilter = !gFrontendConfig.smoothFilter;
            break;
        case ITEM_FF_SPEED:
            gFrontendConfig.fastForwardSpeed = WrapAdd(gFrontendConfig.fastForwardSpeed - 2, delta, 4) + 2;
            break;
        case ITEM_SAVE_ANYWHERE:
            gFrontendConfig.saveAnywhere = !gFrontendConfig.saveAnywhere;
            break;
        case ITEM_SAVE_FILE:
            sPendingSaveFile = WrapAdd(sPendingSaveFile - 1, delta, SAVE_FILES) + 1;
            break;
        }
    }

    if (pressed & (PHYS_CIRCLE | gFrontendConfig.actionButton[ACTION_MENU]))
    {
        CloseMenu();
        return FE_REQUEST_NONE;
    }

    if (pressed & PHYS_CROSS)
    {
        switch (sCursor)
        {
        case ITEM_RESUME:
            CloseMenu();
            break;
        case ITEM_SAVE_STATE:
            CloseMenu();
            request = FE_REQUEST_SAVE_STATE;
            break;
        case ITEM_LOAD_STATE:
            CloseMenu();
            request = FE_REQUEST_LOAD_STATE;
            break;
        case ITEM_SCALE:
            gFrontendConfig.scale = WrapAdd(gFrontendConfig.scale, 1, SCALE_COUNT);
            break;
        case ITEM_FILTER:
            gFrontendConfig.smoothFilter = !gFrontendConfig.smoothFilter;
            break;
        case ITEM_FF_SPEED:
            gFrontendConfig.fastForwardSpeed = WrapAdd(gFrontendConfig.fastForwardSpeed - 2, 1, 4) + 2;
            break;
        case ITEM_SAVE_ANYWHERE:
            gFrontendConfig.saveAnywhere = !gFrontendConfig.saveAnywhere;
            break;
        case ITEM_REMAP:
            sMenuPage = 1;
            sCursor = 0;
            break;
        case ITEM_SAVE_FILE:
            if (sPendingSaveFile != gFrontendConfig.saveFile)
            {
                gFrontendConfig.saveFile = sPendingSaveFile;
                CloseMenu();
                request = FE_REQUEST_SWITCH_SAVE_FILE;
            }
            break;
        case ITEM_RESET:
            CloseMenu();
            request = FE_REQUEST_RESET;
            break;
        case ITEM_QUIT:
            CloseMenu();
            request = FE_REQUEST_QUIT;
            break;
        }
    }
    return request;
}

// ---- drawing -------------------------------------------------------------------

#define COLOR_TEXT     (0x7FFF | 0x8000)
#define COLOR_SELECTED (0x03FF | 0x8000) // yellow
#define COLOR_DISABLED (0x4210 | 0x8000) // grey
#define COLOR_TITLE    (0x7EE0 | 0x8000) // light blue
#define COLOR_PANEL    (0x1084 | 0x8000)

static void DrawChar(uint16_t *frame, int x, int y, char c, uint16_t color)
{
    if (c < 32 || c > 126)
        c = '?';
    for (int row = 0; row < 8; row++)
    {
        unsigned char bits = sFrontendFont[c - 32][row];
        if (y + row < 0 || y + row >= H)
            continue;
        for (int col = 0; col < 8; col++)
            if ((bits & (0x80 >> col)) && x + col >= 0 && x + col < W)
                frame[(y + row) * W + x + col] = color;
    }
}

static void DrawText(uint16_t *frame, int x, int y, const char *text, uint16_t color)
{
    for (; *text; text++, x += 8)
        DrawChar(frame, x, y, *text, color);
}

static void FillRect(uint16_t *frame, int x, int y, int w, int h, uint16_t color)
{
    for (int j = y; j < y + h && j < H; j++)
        for (int i = x; i < x + w && i < W; i++)
            if (i >= 0 && j >= 0)
                frame[j * W + i] = color;
}

static void DrawRow(uint16_t *frame, int row, bool selected, const char *label, const char *value, bool enabled)
{
    int y = 20 + row * 10;
    uint16_t color = !enabled ? COLOR_DISABLED : (selected ? COLOR_SELECTED : COLOR_TEXT);

    if (selected)
        DrawChar(frame, 12, y, '>', COLOR_SELECTED);
    DrawText(frame, 24, y, label, color);
    if (value != NULL)
        DrawText(frame, 136, y, value, color);
}

void Frontend_DrawMenu(uint16_t *frame)
{
    char value[32];

    memcpy(frame, sMenuBackground, sizeof(sMenuBackground));
    FillRect(frame, 6, 4, W - 12, H - 8, COLOR_PANEL);

    if (sMenuPage == 0)
    {
        bool states = Savestate_Supported();
        DrawText(frame, 12, 8, "EMERALD ROGUE - MENU", COLOR_TITLE);
        DrawRow(frame, ITEM_RESUME, sCursor == ITEM_RESUME, "Resume", NULL, true);
        snprintf(value, sizeof(value), "< %d >%s", sStateSlot, Savestate_Exists(Frontend_StatePath()) ? " *" : "");
        DrawRow(frame, ITEM_SAVE_STATE, sCursor == ITEM_SAVE_STATE, "Save state", value, states);
        DrawRow(frame, ITEM_LOAD_STATE, sCursor == ITEM_LOAD_STATE, "Load state", value, states);
        snprintf(value, sizeof(value), "< %s >", sScaleNames[gFrontendConfig.scale]);
        DrawRow(frame, ITEM_SCALE, sCursor == ITEM_SCALE, "Scale", value, true);
        snprintf(value, sizeof(value), "< %s >", gFrontendConfig.smoothFilter ? "Smooth" : "Sharp");
        DrawRow(frame, ITEM_FILTER, sCursor == ITEM_FILTER, "Filter", value, true);
        snprintf(value, sizeof(value), "< %dx >", gFrontendConfig.fastForwardSpeed);
        DrawRow(frame, ITEM_FF_SPEED, sCursor == ITEM_FF_SPEED, "Fast fwd", value, true);
        DrawRow(frame, ITEM_SAVE_ANYWHERE, sCursor == ITEM_SAVE_ANYWHERE, "Save anywhere", gFrontendConfig.saveAnywhere ? "< On >" : "< Off >", true);
        DrawRow(frame, ITEM_REMAP, sCursor == ITEM_REMAP, "Buttons...", NULL, true);
        snprintf(value, sizeof(value), "< %d >%s", sPendingSaveFile, sPendingSaveFile != gFrontendConfig.saveFile ? " X=load" : "");
        DrawRow(frame, ITEM_SAVE_FILE, sCursor == ITEM_SAVE_FILE, "Save file", value, states);
        DrawRow(frame, ITEM_RESET, sCursor == ITEM_RESET, "Reset game", NULL, states);
        DrawRow(frame, ITEM_QUIT, sCursor == ITEM_QUIT, "Quit", NULL, true);
        DrawText(frame, 12, H - 14, "X:select  O:close", COLOR_DISABLED);
    }
    else
    {
        DrawText(frame, 12, 8, "BUTTONS", COLOR_TITLE);
        for (int i = 0; i < ACTION_COUNT; i++)
        {
            const char *name = (sRemapWaiting == i) ? "press..." : PhysName(gFrontendConfig.actionButton[i]);
            DrawRow(frame, i, sCursor == i, sActionNames[i], name, true);
        }
        DrawRow(frame, REMAP_TOUCH, sCursor == REMAP_TOUCH, "Touch=menu", gFrontendConfig.touchOpensMenu ? "On" : "Off", true);
        DrawRow(frame, REMAP_DEFAULTS, sCursor == REMAP_DEFAULTS, "Defaults", NULL, true);
        DrawRow(frame, REMAP_DEFAULTS + 1, sCursor == REMAP_DEFAULTS + 1, "Back", NULL, true);
        DrawText(frame, 12, H - 14, "X:change  O:back", COLOR_DISABLED);
    }
}

void Frontend_DrawMessage(uint16_t *frame)
{
    if (sMessageTimer <= 0)
        return;
    sMessageTimer--;
    int w = (int)strlen(sMessage) * 8 + 8;
    FillRect(frame, 4, H - 16, w, 12, COLOR_PANEL);
    DrawText(frame, 8, H - 14, sMessage, COLOR_TEXT);
}

void Frontend_GetDestRect(int screenW, int screenH, int *x, int *y, int *w, int *h)
{
    switch (gFrontendConfig.scale)
    {
    case SCALE_1X:
    case SCALE_2X:
    case SCALE_3X:
        *w = W * (gFrontendConfig.scale + 1);
        *h = H * (gFrontendConfig.scale + 1);
        if (*w > screenW || *h > screenH) // doesn't fit: fall back to fit
        {
            *h = screenH;
            *w = screenH * W / H;
        }
        break;
    case SCALE_FIT:
        *h = screenH;
        *w = screenH * W / H;
        if (*w > screenW)
        {
            *w = screenW;
            *h = screenW * H / W;
        }
        break;
    default:
        *w = screenW;
        *h = screenH;
        break;
    }
    *x = (screenW - *w) / 2;
    *y = (screenH - *h) / 2;
}
#endif // PORTABLE
