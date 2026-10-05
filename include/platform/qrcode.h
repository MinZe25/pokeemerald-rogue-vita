#ifndef GUARD_PLATFORM_QRCODE_H
#define GUARD_PLATFORM_QRCODE_H

#include <stdbool.h>
#include <stdint.h>

#define QR_MAX_SIZE 177  // version 40
#define QR_MAX_TEXT 2954 // bytes at version 40, level L (+ NUL)

// Encodes text (byte mode, error correction L) and returns the symbol's size in
// modules, or 0 if it doesn't fit (more than 2953 bytes).
int Qr_Encode(const uint8_t *text, int len);
// Module of the last encoded symbol (true = dark)
bool Qr_Module(int x, int y);

#endif // GUARD_PLATFORM_QRCODE_H
