#ifdef PLATFORM_SDL2
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#include <SDL2/SDL.h>

#ifdef __vita__
#include <psp2/ctrl.h>
#include <psp2/power.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

// Rogue's code can use a fair amount of stack; the default is 256KB
int sceUserMainThreadStackSize = 4 * 1024 * 1024;
unsigned int _newlib_heap_size_user = 128 * 1024 * 1024;

#define DATA_DIR "ux0:data/pokeemerald_rogue/"
#else
#define DATA_DIR ""
#endif
#define SAVE_PATH DATA_DIR "pokeemerald.sav"

#include "global.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/system.h"

extern void (*const gIntrTable[])(void);

SDL_Thread *mainLoopThread;
SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
SDL_sem *vBlankSemaphore;
SDL_atomic_t isFrameAvailable;
bool speedUp = false;
unsigned int videoScale = 1;
bool videoScaleChanged = false;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
struct SiiRtcInfo internalClock;

static FILE *sSaveFile = NULL;
#ifdef __vita__
static SDL_AudioStream *sAudioStream = NULL;
#endif

extern void AgbMain(void);
extern void MainLoop(void);
extern void DoSoftReset(void);

int DoMain(void *param);
void ProcessEvents(void);
void VDraw(SDL_Texture *texture);

static void ReadSaveFile(char *path);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

static void UpdateInternalClock(void);

// Log file (DATA_DIR/log.txt) - also echoed to stdout on desktop
static void PlatformLog(const char *fmt, ...)
{
    va_list args;
#ifdef __vita__
    static bool sStarted = false;
    // Reopened per message so every line is committed even if the app is killed
    FILE *log = fopen(DATA_DIR "log.txt", sStarted ? "a" : "w");

    sStarted = true;
    va_start(args, fmt);
    if (log != NULL)
    {
        vfprintf(log, fmt, args);
        fclose(log);
    }
    va_end(args);
#else
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#endif
}

// Frame timing statistics, logged every 600 frames when ROGUE_PERFLOG=1
static bool sPerfLog = false;
static double sPerfLogic, sPerfDraw, sPerfAudio, sPerfWorst;
static unsigned sPerfFrames;

static double NowMs(void)
{
    return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

// ---------------------------------------------------------------------------
// Test harness, driven by environment variables:
//   ROGUE_HEADLESS=1          no window/audio device, run frames as fast as possible
//   ROGUE_MAXFRAMES=N         exit after N frames
//   ROGUE_DUMP=a,b,c          write frame_<n>.bmp for these frames
//   ROGUE_DUMPEVERY=N         write a frame every N frames
//   ROGUE_INPUT=f:KEYS:len;.. hold KEYS (A+B+START+SELECT+L+R+UP+DOWN+LEFT+RIGHT)
//                             from frame f for len frames
// ---------------------------------------------------------------------------
static bool sHeadless = false;
static unsigned long sFrameCount = 0;
static unsigned long sMaxFrames = 0;
static unsigned long sDumpEvery = 0;
static unsigned long sBattleFrame = 0;
extern void Harness_StartTestBattle(void);
static const char *sDumpList = NULL;
static const char *sInputScript = NULL;
static uint16_t sFrameImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];

static u16 ParseKeyName(const char *name, int len)
{
    static const struct { const char *name; u16 key; } sKeys[] = {
        {"A", A_BUTTON}, {"B", B_BUTTON}, {"START", START_BUTTON}, {"SELECT", SELECT_BUTTON},
        {"L", L_BUTTON}, {"R", R_BUTTON}, {"UP", DPAD_UP}, {"DOWN", DPAD_DOWN},
        {"LEFT", DPAD_LEFT}, {"RIGHT", DPAD_RIGHT},
    };
    for (unsigned i = 0; i < sizeof(sKeys) / sizeof(sKeys[0]); i++)
        if ((int)strlen(sKeys[i].name) == len && strncmp(sKeys[i].name, name, len) == 0)
            return sKeys[i].key;
    return 0;
}

static u16 GetScriptedKeys(unsigned long frame)
{
    u16 result = 0;
    const char *p = sInputScript;

    while (p != NULL && *p)
    {
        char *end;
        unsigned long start = strtoul(p, &end, 10);
        unsigned long len = 1;
        u16 keys = 0;

        if (*end != ':')
            break;
        p = end + 1;
        while (*p && *p != ':' && *p != ';')
        {
            const char *k = p;
            while (*p && *p != '+' && *p != ':' && *p != ';')
                p++;
            keys |= ParseKeyName(k, p - k);
            if (*p == '+')
                p++;
        }
        if (*p == ':')
        {
            len = strtoul(p + 1, &end, 10);
            p = end;
        }
        if (frame >= start && frame < start + len)
            result |= keys;
        while (*p && *p != ';')
            p++;
        if (*p == ';')
            p++;
    }
    return result;
}

static bool ShouldDumpFrame(unsigned long frame)
{
    const char *p = sDumpList;

    if (sDumpEvery != 0 && frame % sDumpEvery == 0)
        return true;
    while (p != NULL && *p)
    {
        char *end;
        unsigned long n = strtoul(p, &end, 10);
        if (end == p)
            break;
        if (n == frame)
            return true;
        p = (*end == ',') ? end + 1 : end;
    }
    return false;
}

static void WriteLE(FILE *f, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++)
        fputc((v >> (8 * i)) & 0xFF, f);
}

static void DumpFrame(unsigned long frame)
{
    char path[64];
    FILE *f;
    int rowSize = DISPLAY_WIDTH * 3;

    snprintf(path, sizeof(path), DATA_DIR "frame_%06lu.bmp", frame);
    f = fopen(path, "wb");
    if (f == NULL)
        return;
    fputc('B', f); fputc('M', f);
    WriteLE(f, 54 + rowSize * DISPLAY_HEIGHT, 4);
    WriteLE(f, 0, 4);
    WriteLE(f, 54, 4);
    WriteLE(f, 40, 4);
    WriteLE(f, DISPLAY_WIDTH, 4);
    WriteLE(f, DISPLAY_HEIGHT, 4);
    WriteLE(f, 1, 2);
    WriteLE(f, 24, 2);
    WriteLE(f, 0, 4);
    WriteLE(f, rowSize * DISPLAY_HEIGHT, 4);
    WriteLE(f, 2835, 4);
    WriteLE(f, 2835, 4);
    WriteLE(f, 0, 4);
    WriteLE(f, 0, 4);
    for (int y = DISPLAY_HEIGHT - 1; y >= 0; y--)
    {
        for (int x = 0; x < DISPLAY_WIDTH; x++)
        {
            uint16_t c = sFrameImage[y * DISPLAY_WIDTH + x];
            uint8_t r = (c & 0x1F) << 3, g = ((c >> 5) & 0x1F) << 3, b = ((c >> 10) & 0x1F) << 3;
            fputc(b, f); fputc(g, f); fputc(r, f);
        }
    }
    fclose(f);
}

#ifdef __vita__
// The Vita has no environment; read KEY=VALUE lines from DATA_DIR/harness.txt
static char sHarnessFile[65536];

static const char *HarnessGetenv(const char *name)
{
    static bool sLoaded = false;
    static char sValue[65536];
    size_t nameLen = strlen(name);
    const char *p;

    if (!sLoaded)
    {
        FILE *f = fopen(DATA_DIR "harness.txt", "rb");
        sLoaded = true;
        if (f != NULL)
        {
            size_t n = fread(sHarnessFile, 1, sizeof(sHarnessFile) - 1, f);
            sHarnessFile[n] = 0;
            fclose(f);
        }
    }
    for (p = sHarnessFile; *p; )
    {
        const char *eol = strchr(p, '\n');
        size_t lineLen = eol ? (size_t)(eol - p) : strlen(p);
        if (lineLen > nameLen && strncmp(p, name, nameLen) == 0 && p[nameLen] == '=')
        {
            size_t valLen = lineLen - nameLen - 1;
            while (valLen > 0 && (p[nameLen + 1 + valLen - 1] == '\r'))
                valLen--;
            if (valLen >= sizeof(sValue))
                valLen = sizeof(sValue) - 1;
            memcpy(sValue, p + nameLen + 1, valLen);
            sValue[valLen] = 0;
            return strdup(sValue);
        }
        if (!eol)
            break;
        p = eol + 1;
    }
    return NULL;
}
#define getenv HarnessGetenv
#endif

static void InitTestHarness(void)
{
    const char *v;

    sHeadless = (v = getenv("ROGUE_HEADLESS")) != NULL && *v == '1';
    if ((v = getenv("ROGUE_MAXFRAMES")) != NULL)
        sMaxFrames = strtoul(v, NULL, 10);
    if ((v = getenv("ROGUE_DUMPEVERY")) != NULL)
        sDumpEvery = strtoul(v, NULL, 10);
    sDumpList = getenv("ROGUE_DUMP");
    if ((v = getenv("ROGUE_BATTLE")) != NULL)
        sBattleFrame = strtoul(v, NULL, 10);
    sPerfLog = (v = getenv("ROGUE_PERFLOG")) != NULL && *v == '1';
#ifdef __vita__
    sPerfLog = true; // cheap, and the only way to see real hardware performance
#endif
    sInputScript = getenv("ROGUE_INPUT");
    if (sHeadless)
    {
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
        SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    }
}

// Runs one emulated frame; returns false when the harness wants to quit
static bool RunGameFrame(bool draw)
{
    double t0 = sPerfLog ? NowMs() : 0, t1 = 0, t2 = 0;

    //run game logic, draw frame and process DMAs and vblank
    ENTER_VBLANK(); //you must be in VBlank before running a game tick
    if (sBattleFrame != 0 && sFrameCount == sBattleFrame)
        Harness_StartTestBattle();
    MainLoop();
    if (sPerfLog)
        t1 = NowMs();
    if (draw || sHeadless)
        VDraw(sdlTexture);
    RunDMAsAndVBlank();
    if (sPerfLog)
    {
        t2 = NowMs();
        sPerfLogic += t1 - t0;
        sPerfDraw += t2 - t1;
        if (t2 - t0 > sPerfWorst)
            sPerfWorst = t2 - t0;
        if (++sPerfFrames == 600)
        {
            PlatformLog("frame %lu: avg logic %.2f ms, avg draw %.2f ms, avg audio %.2f ms, worst frame %.2f ms\n",
                        sFrameCount, sPerfLogic / sPerfFrames, sPerfDraw / sPerfFrames, sPerfAudio / sPerfFrames, sPerfWorst);
            sPerfLogic = sPerfDraw = sPerfAudio = sPerfWorst = 0;
            sPerfFrames = 0;
        }
    }

    sFrameCount++;
    if (ShouldDumpFrame(sFrameCount))
        DumpFrame(sFrameCount);
    if (sMaxFrames != 0 && sFrameCount >= sMaxFrames)
        return false;
    return true;
}

int main(int argc, char **argv)
{
    // Open an output console on Windows
#ifdef _WIN32
    AllocConsole() ;
    AttachConsole( GetCurrentProcessId() ) ;
    freopen( "CON", "w", stdout ) ;
#endif

#ifdef __vita__
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);
    sceIoMkdir("ux0:data", 0777);
    sceIoMkdir(DATA_DIR, 0777);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    PlatformLog("pokeemerald_rogue starting\n");
#endif

    InitTestHarness();
    ReadSaveFile(SAVE_PATH);

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        PlatformLog("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __vita__
    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 544, SDL_WINDOW_SHOWN);
#else
    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
    if (sdlWindow == NULL)
    {
        PlatformLog("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
    if (sdlRenderer == NULL)
    {
        PlatformLog("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#ifndef __vita__
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
#endif

    sdlTexture = SDL_CreateTexture(sdlRenderer,
                                   SDL_PIXELFORMAT_ABGR1555,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (sdlTexture == NULL)
    {
        PlatformLog("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    simTime = curGameTime = lastGameTime = SDL_GetPerformanceCounter();

    isFrameAvailable.value = 0;
    vBlankSemaphore = SDL_CreateSemaphore(0);

    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 42048;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    cgb_audio_init(want.freq);
#ifdef __vita__
    // Vita audio ports only accept standard rates: mix at 42048 Hz, resample to 48 kHz
    want.freq = 48000;
    sAudioStream = SDL_NewAudioStream(AUDIO_F32, 2, 42048, AUDIO_F32, 2, 48000);
#endif


    if (SDL_OpenAudio(&want, 0) < 0)
        PlatformLog("Failed to open audio: %s\n", SDL_GetError());
    else
    {
        if (want.format != AUDIO_F32) /* we let this one thing change. */
            SDL_Log("We didn't get Float32 audio format.");
        SDL_PauseAudio(0);
    }

    PlatformLog("SDL ready, starting game\n");
    AgbMain();
    PlatformLog("AgbMain done\n");

    double accumulator = 0.0;

    memset(&internalClock, 0, sizeof(internalClock));
    internalClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();

    bool isGameStepDrawn = false;
    while (isRunning)
    {
        double deltaTime;

        ProcessEvents();

        curGameTime = SDL_GetPerformanceCounter();
        deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
        deltaTime *= timeScale; //apply speedup

        if (!paused)
        {
            accumulator += deltaTime;

            isGameStepDrawn = false;

            if (sHeadless)
            {
                if (!RunGameFrame(true))
                    isRunning = false;
                AudioUpdate();
                SDL_ClearQueuedAudio(1);
                lastGameTime = curGameTime;
                continue;
            }

            while (accumulator >= fixedTimestep)
            {
                if (!RunGameFrame(!isGameStepDrawn))
                    isRunning = false;
                isGameStepDrawn = true;

                accumulator -= fixedTimestep;
            }

            //samples per frame is 701, that gets multipled by two when being queued and then multipled by four because samples are float32 which are 4 bytes long hence the divide by 8
            //this number is then checked against samples per frame multipled by three rounded down to 2000 to give it enough margin of error while not desyncing
            //this is all done to sync audio to gameplay
            if (SDL_GetQueuedAudioSize(1)/8 < 2000)
            {
                double ta = sPerfLog ? NowMs() : 0;
                AudioUpdate();
                if (sPerfLog)
                    sPerfAudio += NowMs() - ta;
            }

            if (videoScaleChanged)
            {
                SDL_SetWindowSize(sdlWindow, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale);
                videoScaleChanged = false;
            }
        }

        lastGameTime = curGameTime;

#ifdef __vita__
        {
            // 3x integer scale, centred on the 960x544 screen
            SDL_Rect dst = { (960 - DISPLAY_WIDTH * 3) / 2, (544 - DISPLAY_HEIGHT * 3) / 2, DISPLAY_WIDTH * 3, DISPLAY_HEIGHT * 3 };
            SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
            SDL_RenderClear(sdlRenderer);
            SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &dst);
        }
#else
        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
#endif
        SDL_RenderPresent(sdlRenderer);
    }

    CloseSaveFile();

    SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
#ifdef __vita__
    sceKernelExitProcess(0);
#endif
    return 0;
}

static void ReadSaveFile(char *path)
{
    // Check whether the saveFile exists, and create it if not
    sSaveFile = fopen(path, "r+b");
    if (sSaveFile == NULL)
    {
        sSaveFile = fopen(path, "w+b");
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++)
    {
        FLASH_BASE[i] = 0xFF;
    }
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL)
    {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
        fflush(sSaveFile);
    }
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    u32 start = (sectorNum << gFlash->sector.shift) + offset;

    DBGPRINTF("ReadFlash(sectorNum=0x%04X,offset=0x%08X,size=0x%02X)\n",sectorNum,offset,size);
    if (start >= sizeof(FLASH_BASE))
        return;
    if (start + size > sizeof(FLASH_BASE))
        size = sizeof(FLASH_BASE) - start;
    memcpy(dest, &FLASH_BASE[start], size);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    static FILE *sRawAudio = NULL;
    static bool sRawAudioChecked = false;
    static float sPrevIn[2], sPrevOut[2];
    int count = samplesPerFrame / sizeof(float);

    // DC blocking high-pass filter, like the capacitor on the GBA's audio output.
    // Without it, CGB channels left at a non-zero level cause pops.
    for (int i = 0; i < count; i++)
    {
        int ch = i & 1;
        float in = audioBuffer[i];
        float out = in - sPrevIn[ch] + 0.9995f * sPrevOut[ch];
        sPrevIn[ch] = in;
        sPrevOut[ch] = out;
        audioBuffer[i] = out;
    }

    // ROGUE_RAWAUDIO=path dumps the mixer output (float32 stereo) for testing
    if (!sRawAudioChecked)
    {
        const char *path = getenv("ROGUE_RAWAUDIO");
        sRawAudioChecked = true;
        if (path != NULL)
            sRawAudio = fopen(path, "wb");
    }
    if (sRawAudio != NULL)
        fwrite(audioBuffer, 1, samplesPerFrame, sRawAudio);

#ifdef __vita__
    if (sAudioStream != NULL)
    {
        static float sResampled[8192];
        int got;

        SDL_AudioStreamPut(sAudioStream, audioBuffer, samplesPerFrame);
        while ((got = SDL_AudioStreamGet(sAudioStream, sResampled, sizeof(sResampled))) > 0)
            SDL_QueueAudio(1, sResampled, got);
        return;
    }
#endif
    SDL_QueueAudio(1, audioBuffer, samplesPerFrame);
}


static void CloseSaveFile()
{
    if (sSaveFile != NULL)
    {
        fclose(sSaveFile);
    }
}

// Key mappings
#define KEY_A_BUTTON      SDLK_z
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_a
#define KEY_R_BUTTON      SDLK_s
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key) \
case KEY_##key:  keys &= ~key; break;

#define HANDLE_KEYDOWN(key) \
case KEY_##key:  keys |= key; break;

static u16 keys;

void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYUP(A_BUTTON)
            HANDLE_KEYUP(B_BUTTON)
            HANDLE_KEYUP(START_BUTTON)
            HANDLE_KEYUP(SELECT_BUTTON)
            HANDLE_KEYUP(L_BUTTON)
            HANDLE_KEYUP(R_BUTTON)
            HANDLE_KEYUP(DPAD_UP)
            HANDLE_KEYUP(DPAD_DOWN)
            HANDLE_KEYUP(DPAD_LEFT)
            HANDLE_KEYUP(DPAD_RIGHT)
            case SDLK_SPACE:
                if (speedUp)
                {
                    speedUp = false;
                    timeScale = 1.0;
                    //SDL_ClearQueuedAudio(1);
                    //SDL_PauseAudio(0);
                }
                break;
            }
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYDOWN(A_BUTTON)
            HANDLE_KEYDOWN(B_BUTTON)
            HANDLE_KEYDOWN(START_BUTTON)
            HANDLE_KEYDOWN(SELECT_BUTTON)
            HANDLE_KEYDOWN(L_BUTTON)
            HANDLE_KEYDOWN(R_BUTTON)
            HANDLE_KEYDOWN(DPAD_UP)
            HANDLE_KEYDOWN(DPAD_DOWN)
            HANDLE_KEYDOWN(DPAD_LEFT)
            HANDLE_KEYDOWN(DPAD_RIGHT)
            case SDLK_r:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    DoSoftReset();
                }
                break;
            case SDLK_p:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;
                }
                break;
            case SDLK_SPACE:
                if (!speedUp)
                {
                    speedUp = true;
                    timeScale = 5.0;
                    //SDL_PauseAudio(1);
                }
                break;
            }
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            {
                unsigned int w = event.window.data1;
                unsigned int h = event.window.data2;
                
                videoScale = 0;
                if (w / DISPLAY_WIDTH > videoScale)
                    videoScale = w / DISPLAY_WIDTH;
                if (h / DISPLAY_HEIGHT > videoScale)
                    videoScale = h / DISPLAY_HEIGHT;
                if (videoScale < 1)
                    videoScale = 1;

                videoScaleChanged = true;
            }
            break;
        }
    }
}

#ifdef _WIN32
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;

        if (oldTimeScale != timeScale)
        {
            if (timeScale > 1.0)
            {
                SDL_PauseAudio(1);
            }
            else
            {
                SDL_ClearQueuedAudio(1);
                SDL_PauseAudio(0);
            }
        }
    }

    return xinputKeys;
}
#endif // _WIN32

#ifdef __vita__
// Cross=A, Circle=B, L/R triggers, Start/Select, d-pad or left stick; hold Triangle to fast-forward
static u16 GetVitaKeys(void)
{
    SceCtrlData pad;
    u16 k = 0;

    if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
        return 0;
    if (pad.buttons & SCE_CTRL_CROSS)    k |= A_BUTTON;
    if (pad.buttons & SCE_CTRL_CIRCLE)   k |= B_BUTTON;
    if (pad.buttons & SCE_CTRL_START)    k |= START_BUTTON;
    if (pad.buttons & SCE_CTRL_SELECT)   k |= SELECT_BUTTON;
    if (pad.buttons & SCE_CTRL_LTRIGGER) k |= L_BUTTON;
    if (pad.buttons & SCE_CTRL_RTRIGGER) k |= R_BUTTON;
    if ((pad.buttons & SCE_CTRL_UP)    || pad.ly < 64)  k |= DPAD_UP;
    if ((pad.buttons & SCE_CTRL_DOWN)  || pad.ly > 192) k |= DPAD_DOWN;
    if ((pad.buttons & SCE_CTRL_LEFT)  || pad.lx < 64)  k |= DPAD_LEFT;
    if ((pad.buttons & SCE_CTRL_RIGHT) || pad.lx > 192) k |= DPAD_RIGHT;
    timeScale = (pad.buttons & SCE_CTRL_TRIANGLE) ? 3.0 : 1.0;
    return k;
}
#endif

u16 Platform_GetKeyInput(void)
{
    u16 scripted = (sInputScript != NULL) ? GetScriptedKeys(sFrameCount) : 0;
#ifdef __vita__
    return GetVitaKeys() | scripted;
#endif
#ifdef _WIN32
    u16 gamepadKeys = GetXInputKeys();
    return ((gamepadKeys != 0) ? gamepadKeys : keys) | scripted;
#endif

    return keys | scripted;
}

void VDraw(SDL_Texture *texture)
{
    uint16_t *image = sFrameImage;

    memset(sFrameImage, 0, sizeof(sFrameImage));
    DrawFrame(image);
    SDL_UpdateTexture(texture, NULL, image, DISPLAY_WIDTH * sizeof (Uint16));
    REG_VCOUNT = 161; // prep for being in VBlank period
}

int DoMain(void *data)
{
    AgbMain();
}

void VBlankIntrWait(void)
{
    return;
}

u8 BinToBcd(u8 bin)
{
    int placeCounter = 1;
    u8 out = 0;
    do
    {
        out |= (bin % 10) * placeCounter;
        placeCounter *= 16;
    }
    while ((bin /= 10) > 0);

    return out;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = internalClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    internalClock.status = rtc->status;
}

static void UpdateInternalClock(void)
{
    time_t rawTime = time(NULL);
    struct tm *time = localtime(&rawTime);

    internalClock.year = BinToBcd(time->tm_year - 100);
    internalClock.month = BinToBcd(time->tm_mon + 1);
    internalClock.day = BinToBcd(time->tm_mday);
    internalClock.dayOfWeek = BinToBcd(time->tm_wday);
    internalClock.hour = BinToBcd(time->tm_hour);
    internalClock.minute = BinToBcd(time->tm_min);
    internalClock.second = BinToBcd(time->tm_sec);
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->year = internalClock.year;
    rtc->month = internalClock.month;
    rtc->day = internalClock.day;
    rtc->dayOfWeek = internalClock.dayOfWeek;
    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetDateTime: %d-%02d-%02d %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->year),
                                                         ConvertBcdToBinary(rtc->month),
                                                         ConvertBcdToBinary(rtc->day),
                                                         ConvertBcdToBinary(rtc->hour),
                                                         ConvertBcdToBinary(rtc->minute),
                                                         ConvertBcdToBinary(rtc->second));
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    internalClock.month = rtc->month;
    internalClock.day = rtc->day;
    internalClock.dayOfWeek = rtc->dayOfWeek;
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetTime: %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->hour),
                                        ConvertBcdToBinary(rtc->minute),
                                        ConvertBcdToBinary(rtc->second));
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    // TODO
}

void SoftReset(u32 resetFlags)
{
    puts("Soft Reset called. Exiting.");
    exit(0);
}

#endif