#ifdef PORTABLE
// Save states and soft reset for the PC/Vita port.
//
// At link time every game object is merged and its .data/.bss renamed to
// "gamedata"/"gamebss" (see tools/pc/link_game.sh), so all of the game's
// state - RAM, emulated VRAM/OAM/palette/IO registers, flash and sound - lives
// in two contiguous ranges that the linker marks with __start_/__stop_
// symbols. A state is simply a copy of those ranges. The platform layer
// (SDL, menu, config) is linked separately and is not touched by a restore.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "platform/savestate.h"

#ifdef SAVESTATES_SUPPORTED
extern char __start_gamedata[], __stop_gamedata[];
extern char __start_gamebss[], __stop_gamebss[];

#define STATE_MAGIC 0x52534D50 // "PMSR"

struct StateHeader
{
    unsigned int magic;
    unsigned int dataSize;
    unsigned int bssSize;
    unsigned int buildId;
};

static char *sBootSnapshot;

static unsigned int DataSize(void) { return (unsigned int)(__stop_gamedata - __start_gamedata); }
static unsigned int BssSize(void)  { return (unsigned int)(__stop_gamebss - __start_gamebss); }

// States only make sense for the exact build that made them
static unsigned int BuildId(void)
{
    return (unsigned int)(uintptr_t)__start_gamedata ^ (DataSize() << 8) ^ (BssSize() << 1) ^ (unsigned int)(uintptr_t)__start_gamebss;
}

bool Savestate_Supported(void)
{
    return true;
}

void Savestate_TakeBootSnapshot(void)
{
    if (sBootSnapshot == NULL)
        sBootSnapshot = malloc(DataSize() + BssSize());
    if (sBootSnapshot != NULL)
    {
        memcpy(sBootSnapshot, __start_gamedata, DataSize());
        memcpy(sBootSnapshot + DataSize(), __start_gamebss, BssSize());
    }
}

bool Savestate_RestoreBootSnapshot(void)
{
    if (sBootSnapshot == NULL)
        return false;
    memcpy(__start_gamedata, sBootSnapshot, DataSize());
    memcpy(__start_gamebss, sBootSnapshot + DataSize(), BssSize());
    return true;
}

bool Savestate_Save(const char *path)
{
    struct StateHeader header = { STATE_MAGIC, DataSize(), BssSize(), BuildId() };
    FILE *f = fopen(path, "wb");
    bool ok;

    if (f == NULL)
        return false;
    ok = fwrite(&header, sizeof(header), 1, f) == 1
      && fwrite(__start_gamedata, 1, header.dataSize, f) == header.dataSize
      && fwrite(__start_gamebss, 1, header.bssSize, f) == header.bssSize;
    fclose(f);
    return ok;
}

bool Savestate_Load(const char *path)
{
    struct StateHeader header;
    char *buffer;
    FILE *f = fopen(path, "rb");
    bool ok;

    if (f == NULL)
        return false;
    if (fread(&header, sizeof(header), 1, f) != 1
     || header.magic != STATE_MAGIC || header.dataSize != DataSize()
     || header.bssSize != BssSize() || header.buildId != BuildId())
    {
        fclose(f);
        return false;
    }
    // read everything first so a truncated file can't leave a half restored game
    buffer = malloc(header.dataSize + header.bssSize);
    ok = buffer != NULL && fread(buffer, 1, header.dataSize + header.bssSize, f) == header.dataSize + header.bssSize;
    fclose(f);
    if (ok)
    {
        memcpy(__start_gamedata, buffer, header.dataSize);
        memcpy(__start_gamebss, buffer + header.dataSize, header.bssSize);
    }
    free(buffer);
    return ok;
}

bool Savestate_Exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return false;
    fclose(f);
    return true;
}

#else // !SAVESTATES_SUPPORTED

bool Savestate_Supported(void) { return false; }
void Savestate_TakeBootSnapshot(void) {}
bool Savestate_RestoreBootSnapshot(void) { return false; }
bool Savestate_Save(const char *path) { return false; }
bool Savestate_Load(const char *path) { return false; }
bool Savestate_Exists(const char *path) { return false; }

#endif // SAVESTATES_SUPPORTED
#endif // PORTABLE
