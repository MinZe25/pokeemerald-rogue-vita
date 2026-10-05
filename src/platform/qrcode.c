#ifdef PORTABLE
// Minimal QR Code encoder (ISO/IEC 18004): byte mode, error correction level L,
// versions 1-40, best of the 8 masks. Follows the structure of Project Nayuki's
// QR Code generator (MIT).
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "platform/qrcode.h"

static const int8_t sEccPerBlockL[41] = {
    -1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
    28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30,
};
static const int8_t sBlocksL[41] = {
    -1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8,
    8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25,
};

static uint8_t sModules[QR_MAX_SIZE][QR_MAX_SIZE];
static uint8_t sFunction[QR_MAX_SIZE][QR_MAX_SIZE];
static int sSize;

static int RawDataModules(int ver)
{
    int result = (16 * ver + 128) * ver + 64;
    if (ver >= 2)
    {
        int numAlign = ver / 7 + 2;
        result -= (25 * numAlign - 10) * numAlign - 55;
        if (ver >= 7)
            result -= 36;
    }
    return result;
}

static int DataCodewords(int ver)
{
    return RawDataModules(ver) / 8 - sEccPerBlockL[ver] * sBlocksL[ver];
}

static void SetFunction(int x, int y, bool dark)
{
    sModules[y][x] = dark;
    sFunction[y][x] = 1;
}

static void DrawFinder(int cx, int cy)
{
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++)
        {
            int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            int x = cx + dx, y = cy + dy;
            if (x >= 0 && x < sSize && y >= 0 && y < sSize)
                SetFunction(x, y, dist != 2 && dist != 4);
        }
}

static void DrawAlignment(int cx, int cy)
{
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
        {
            int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
            SetFunction(cx + dx, cy + dy, dist != 1);
        }
}

static void DrawFormatBits(int mask)
{
    int data = (1 << 3) | mask; // level L = 01
    int rem = data;
    for (int i = 0; i < 10; i++)
        rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    int bits = ((data << 10) | rem) ^ 0x5412;

    for (int i = 0; i <= 5; i++)
        SetFunction(8, i, (bits >> i) & 1);
    SetFunction(8, 7, (bits >> 6) & 1);
    SetFunction(8, 8, (bits >> 7) & 1);
    SetFunction(7, 8, (bits >> 8) & 1);
    for (int i = 9; i < 15; i++)
        SetFunction(14 - i, 8, (bits >> i) & 1);
    for (int i = 0; i < 8; i++)
        SetFunction(sSize - 1 - i, 8, (bits >> i) & 1);
    for (int i = 8; i < 15; i++)
        SetFunction(8, sSize - 15 + i, (bits >> i) & 1);
    SetFunction(8, sSize - 8, true);
}

static void DrawFunctionPatterns(int ver)
{
    for (int i = 0; i < sSize; i++)
    {
        SetFunction(6, i, i % 2 == 0);
        SetFunction(i, 6, i % 2 == 0);
    }
    DrawFinder(3, 3);
    DrawFinder(sSize - 4, 3);
    DrawFinder(3, sSize - 4);

    if (ver >= 2)
    {
        int numAlign = ver / 7 + 2;
        int step = (ver == 32) ? 26 : (ver * 4 + numAlign * 2 + 1) / (numAlign * 2 - 2) * 2;
        int pos[7];
        pos[0] = 6;
        for (int i = numAlign - 1, p = sSize - 7; i >= 1; i--, p -= step)
            pos[i] = p;
        for (int i = 0; i < numAlign; i++)
            for (int j = 0; j < numAlign; j++)
                if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
                    DrawAlignment(pos[i], pos[j]);
    }

    DrawFormatBits(0); // placeholder, redrawn with the chosen mask

    if (ver >= 7)
    {
        int rem = ver;
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = ((long)ver << 12) | rem;
        for (int i = 0; i < 18; i++)
        {
            bool bit = (bits >> i) & 1;
            int a = sSize - 11 + i % 3, b = i / 3;
            SetFunction(a, b, bit);
            SetFunction(b, a, bit);
        }
    }
}

// ---- Reed-Solomon over GF(2^8), polynomial 0x11D ----
static uint8_t GfMul(uint8_t x, uint8_t y)
{
    int z = 0;
    for (int i = 7; i >= 0; i--)
    {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return (uint8_t)z;
}

static void RsDivisor(int degree, uint8_t *result)
{
    uint8_t root = 1;
    memset(result, 0, degree);
    result[degree - 1] = 1;
    for (int i = 0; i < degree; i++)
    {
        for (int j = 0; j < degree; j++)
        {
            result[j] = GfMul(result[j], root);
            if (j + 1 < degree)
                result[j] ^= result[j + 1];
        }
        root = GfMul(root, 0x02);
    }
}

static void RsRemainder(const uint8_t *data, int len, const uint8_t *divisor, int degree, uint8_t *result)
{
    memset(result, 0, degree);
    for (int i = 0; i < len; i++)
    {
        uint8_t factor = data[i] ^ result[0];
        memmove(result, result + 1, degree - 1);
        result[degree - 1] = 0;
        for (int j = 0; j < degree; j++)
            result[j] ^= GfMul(divisor[j], factor);
    }
}

// ---- masks and penalty ----
static bool MaskBit(int mask, int x, int y)
{
    switch (mask)
    {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

static void ApplyMask(int mask)
{
    for (int y = 0; y < sSize; y++)
        for (int x = 0; x < sSize; x++)
            if (!sFunction[y][x] && MaskBit(mask, x, y))
                sModules[y][x] ^= 1;
}

static bool Dark(int x, int y, bool transpose)
{
    return transpose ? sModules[x][y] : sModules[y][x];
}

static long Penalty(void)
{
    long penalty = 0;
    int dark = 0;

    for (int t = 0; t < 2; t++) // rows, then columns
        for (int y = 0; y < sSize; y++)
        {
            int run = 1;
            for (int x = 1; x <= sSize; x++)
            {
                if (x < sSize && Dark(x, y, t) == Dark(x - 1, y, t))
                {
                    run++;
                    continue;
                }
                if (run >= 5)
                    penalty += 3 + (run - 5);
                run = 1;
            }
            // finder-like 1:1:3:1:1 with 4 light modules on a side
            for (int x = 0; x + 10 < sSize; x++)
            {
                static const uint8_t a[11] = { 1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0 };
                static const uint8_t b[11] = { 0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1 };
                bool ma = true, mb = true;
                for (int k = 0; k < 11; k++)
                {
                    bool d = Dark(x + k, y, t);
                    ma &= d == a[k];
                    mb &= d == b[k];
                }
                if (ma || mb)
                    penalty += 40;
            }
        }
    for (int y = 0; y + 1 < sSize; y++)
        for (int x = 0; x + 1 < sSize; x++)
        {
            bool c = sModules[y][x];
            if (c == sModules[y][x + 1] && c == sModules[y + 1][x] && c == sModules[y + 1][x + 1])
                penalty += 3;
        }
    for (int y = 0; y < sSize; y++)
        for (int x = 0; x < sSize; x++)
            dark += sModules[y][x];
    {
        int total = sSize * sSize;
        int k = ((dark * 20 - total * 10) < 0 ? -(dark * 20 - total * 10) : (dark * 20 - total * 10)) / total;
        penalty += k * 10;
    }
    return penalty;
}

int Qr_Encode(const uint8_t *text, int len)
{
    static uint8_t codewords[3706];
    static uint8_t interleaved[3706];
    static uint8_t ecc[30], divisor[30];
    int ver, dataCw, bitLen = 0, countBits;

    for (ver = 1; ver <= 40; ver++)
    {
        countBits = ver < 10 ? 8 : 16;
        if (4 + countBits + len * 8 <= DataCodewords(ver) * 8)
            break;
    }
    if (ver > 40)
        return 0;
    dataCw = DataCodewords(ver);
    sSize = ver * 4 + 17;

    // data bits: mode, length, bytes, terminator, padding
    memset(codewords, 0, sizeof(codewords));
#define PUT(value, n) do { for (int _i = (n) - 1; _i >= 0; _i--, bitLen++) \
        codewords[bitLen >> 3] |= (((value) >> _i) & 1) << (7 - (bitLen & 7)); } while (0)
    PUT(4, 4);
    PUT(len, countBits);
    for (int i = 0; i < len; i++)
        PUT(text[i], 8);
    {
        int term = dataCw * 8 - bitLen;
        PUT(0, term < 4 ? term : 4);
    }
    bitLen = (bitLen + 7) & ~7;
    for (int pad = 0xEC; bitLen < dataCw * 8; pad ^= 0xEC ^ 0x11)
        PUT(pad, 8);
#undef PUT

    // split into blocks, add error correction, interleave
    {
        int numBlocks = sBlocksL[ver], blockEcc = sEccPerBlockL[ver];
        int raw = RawDataModules(ver) / 8;
        int numShort = numBlocks - raw % numBlocks;
        int shortLen = raw / numBlocks;
        int n = 0;
        static uint8_t blocks[25][160];

        RsDivisor(blockEcc, divisor);
        for (int i = 0, k = 0; i < numBlocks; i++)
        {
            int datLen = shortLen - blockEcc + (i < numShort ? 0 : 1);
            memcpy(blocks[i], codewords + k, datLen);
            k += datLen;
            RsRemainder(blocks[i], datLen, divisor, blockEcc, ecc);
            if (i < numShort)
                blocks[i][datLen++] = 0; // short blocks: placeholder, skipped below
            memcpy(blocks[i] + datLen, ecc, blockEcc);
        }
        for (int i = 0; i < shortLen + 1; i++)
            for (int j = 0; j < numBlocks; j++)
                if (i != shortLen - blockEcc || j >= numShort)
                    interleaved[n++] = blocks[j][i];
    }

    memset(sModules, 0, sizeof(sModules));
    memset(sFunction, 0, sizeof(sFunction));
    DrawFunctionPatterns(ver);

    // codewords in the zigzag order
    {
        int i = 0, total = RawDataModules(ver) / 8 * 8;
        for (int right = sSize - 1; right >= 1; right -= 2)
        {
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < sSize; vert++)
                for (int j = 0; j < 2; j++)
                {
                    int x = right - j;
                    bool upward = ((right + 1) & 2) == 0;
                    int y = upward ? sSize - 1 - vert : vert;
                    if (!sFunction[y][x] && i < total)
                    {
                        sModules[y][x] = (interleaved[i >> 3] >> (7 - (i & 7))) & 1;
                        i++;
                    }
                }
        }
    }

    // best mask
    {
        int best = 0;
        long bestPenalty = -1;
        for (int mask = 0; mask < 8; mask++)
        {
            ApplyMask(mask);
            DrawFormatBits(mask);
            long p = Penalty();
            if (bestPenalty < 0 || p < bestPenalty)
            {
                best = mask;
                bestPenalty = p;
            }
            ApplyMask(mask); // undo (XOR)
        }
        ApplyMask(best);
        DrawFormatBits(best);
    }
    return sSize;
}

bool Qr_Module(int x, int y)
{
    return x >= 0 && y >= 0 && x < sSize && y < sSize && sModules[y][x];
}
#endif // PORTABLE
