#ifndef GUARD_PLATFORM_ROM_ASSETS_H
#define GUARD_PLATFORM_ROM_ASSETS_H

#include <stdbool.h>
#include <stddef.h>

// One asset file: its placeholder id and where its bytes are in the ROM
struct RomAsset
{
    unsigned int id;
    unsigned int offset;
    unsigned int size;
};

extern const unsigned char gRomAssetsSha1[20];
extern const unsigned int gRomAssetsRomSize;
extern const unsigned int gRomAssetsCount;
extern const struct RomAsset gRomAssets[];

// Fills the asset placeholders from the player's ROM in dataDir. On failure
// returns false with a short explanation in err.
bool RomAssets_Load(const char *dataDir, char *err, size_t errSize);

#endif // GUARD_PLATFORM_ROM_ASSETS_H
