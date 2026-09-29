#ifndef GUARD_PLATFORM_SAVESTATE_H
#define GUARD_PLATFORM_SAVESTATE_H

#include <stdbool.h>
#include <stdint.h>

// Save states need the linker defined __start_/__stop_ section symbols (ELF)
#if defined(__vita__) || defined(__linux__)
#define SAVESTATES_SUPPORTED
#endif

bool Savestate_Supported(void);
void Savestate_TakeBootSnapshot(void);
bool Savestate_RestoreBootSnapshot(void);
bool Savestate_Save(const char *path);
bool Savestate_Load(const char *path);
bool Savestate_Exists(const char *path);

#endif // GUARD_PLATFORM_SAVESTATE_H
