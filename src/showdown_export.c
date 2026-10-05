#ifdef PORTABLE
// The player's party as Pokemon Showdown export text, for the port menu's
// "Party QR" (paste it into the Showdown damage calculator's Import box).
#include "global.h"
#include "battle_main.h"
#include "characters.h"
#include "data.h"
#include "item.h"
#include "pokemon.h"
#include "constants/abilities.h"
#include "constants/items.h"
#include "constants/moves.h"
#include "constants/species.h"
#include <stdio.h>
#include <string.h>

#include "data/showdown_names.h"

static const char *const sTypeNames[NUMBER_OF_MON_TYPES] = {
    "Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost", "Steel", "???",
    "Fire", "Water", "Grass", "Electric", "Psychic", "Ice", "Dragon", "Dark", "Fairy", "Stellar",
};
static const char *const sStatNames[NUM_STATS] = { "HP", "Atk", "Def", "Spe", "SpA", "SpD" };

// game text (letters, digits, common punctuation) -> ASCII
static void ToAscii(const u8 *s, char *out, int n)
{
    int i = 0;
    for (; *s != EOS && i < n - 1; s++)
    {
        u8 c = *s;
        if (c >= 0xBB && c <= 0xD4) out[i++] = 'A' + (c - 0xBB);
        else if (c >= 0xD5 && c <= 0xEE) out[i++] = 'a' + (c - 0xD5);
        else if (c >= 0xA1 && c <= 0xAA) out[i++] = '0' + (c - 0xA1);
        else if (c == 0x00) out[i++] = ' ';
        else if (c == 0xAE) out[i++] = '-';
        else if (c == 0xAD) out[i++] = '.';
        else if (c == 0xB4) out[i++] = '\'';
        else if (c == 0xAB) out[i++] = '!';
        else if (c == 0xAC) out[i++] = '?';
        else if (c == 0xB8) out[i++] = ',';
    }
    out[i] = 0;
}

static const char *Name(const char *const *table, int count, int id, char *fallback, int n, const u8 *gameName)
{
    if (id > 0 && id < count && table[id] != NULL)
        return table[id];
    ToAscii(gameName, fallback, n);
    return fallback;
}

static int Append(char *buf, int size, int len, const char *fmt, const char *a, int b)
{
    int r;
    if (len >= size)
        return len;
    r = snprintf(buf + len, size - len, fmt, a, b);
    return r < 0 ? len : len + r;
}

// returns the length written to buf (NUL terminated)
int Showdown_ExportParty(char *buf, int size)
{
    static const u8 sEvData[NUM_STATS] = { MON_DATA_HP_EV, MON_DATA_ATK_EV, MON_DATA_DEF_EV, MON_DATA_SPEED_EV, MON_DATA_SPATK_EV, MON_DATA_SPDEF_EV };
    static const u8 sIvData[NUM_STATS] = { MON_DATA_HP_IV, MON_DATA_ATK_IV, MON_DATA_DEF_IV, MON_DATA_SPEED_IV, MON_DATA_SPATK_IV, MON_DATA_SPDEF_IV };
    // Showdown's stat order: HP Atk Def SpA SpD Spe
    static const u8 sOrder[NUM_STATS] = { 0, 1, 2, 4, 5, 3 };
    int len = 0, i, j;

    buf[0] = 0;
    for (i = 0; i < PARTY_SIZE; i++)
    {
        struct Pokemon *mon = &gPlayerParty[i];
        u16 species = GetMonData(mon, MON_DATA_SPECIES);
        u16 item = GetMonData(mon, MON_DATA_HELD_ITEM);
        u16 ability = GetMonAbility(mon);
        u8 nickname[POKEMON_NAME_LENGTH + 1];
        char nick[24], fb[24], line[64];
        const char *speciesName;
        bool8 first;

        if (species == SPECIES_NONE || species >= NUM_SPECIES || GetMonData(mon, MON_DATA_IS_EGG))
            continue;
        if (len > 0)
            len = Append(buf, size, len, "\n%s", "", 0);

        speciesName = Name(gShowdownSpeciesNames, NUM_SPECIES, species, fb, sizeof(fb), GetSpeciesName(species));
        GetMonData(mon, MON_DATA_NICKNAME, nickname);
        ToAscii(nickname, nick, sizeof(nick));
        {
            char base[24];
            ToAscii(GetSpeciesName(species), base, sizeof(base));
            if (nick[0] != 0 && strcmp(nick, base) != 0 && strchr(nick, '(') == NULL)
                len = Append(buf, size, len, "%s (", nick, 0);
            len = Append(buf, size, len, "%s", speciesName, 0);
            if (nick[0] != 0 && strcmp(nick, base) != 0 && strchr(nick, '(') == NULL)
                len = Append(buf, size, len, ")", "", 0);
        }
        if (item != ITEM_NONE && item < ITEMS_COUNT)
        {
            u8 itemName[ITEM_NAME_LENGTH + 1];
            CopyItemName(item, itemName);
            len = Append(buf, size, len, " @ %s", Name(gShowdownItemNames, ITEMS_COUNT, item, fb, sizeof(fb), itemName), 0);
        }
        len = Append(buf, size, len, "\n", "", 0);

        if (ability != ABILITY_NONE && ability < ABILITIES_COUNT)
            len = Append(buf, size, len, "Ability: %s\n", Name(gShowdownAbilityNames, ABILITIES_COUNT, ability, fb, sizeof(fb), gAbilityNames[ability]), 0);
        len = Append(buf, size, len, "Level: %s%d\n", "", GetMonData(mon, MON_DATA_LEVEL));
        {
            u8 tera = GetMonData(mon, MON_DATA_TERA_TYPE);
            if (tera < NUMBER_OF_MON_TYPES && tera != TYPE_MYSTERY)
                len = Append(buf, size, len, "Tera Type: %s\n", sTypeNames[tera], 0);
        }

        // EVs (only the ones that aren't 0), nature, IVs (only the ones that aren't 31)
        first = TRUE;
        for (j = 0; j < NUM_STATS; j++)
        {
            int ev = GetMonData(mon, sEvData[sOrder[j]]);
            if (ev == 0)
                continue;
            snprintf(line, sizeof(line), "%s%d %s", first ? "EVs: " : " / ", ev, sStatNames[sOrder[j]]);
            len = Append(buf, size, len, "%s", line, 0);
            first = FALSE;
        }
        if (!first)
            len = Append(buf, size, len, "\n", "", 0);
        len = Append(buf, size, len, "%s Nature\n", gShowdownNatureNames[GetNature(mon)], 0);
        first = TRUE;
        for (j = 0; j < NUM_STATS; j++)
        {
            int iv = GetMonData(mon, sIvData[sOrder[j]]);
            if (iv == 31)
                continue;
            snprintf(line, sizeof(line), "%s%d %s", first ? "IVs: " : " / ", iv, sStatNames[sOrder[j]]);
            len = Append(buf, size, len, "%s", line, 0);
            first = FALSE;
        }
        if (!first)
            len = Append(buf, size, len, "\n", "", 0);

        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            u16 move = GetMonData(mon, MON_DATA_MOVE1 + j);
            if (move == MOVE_NONE || move >= MOVES_COUNT)
                continue;
            len = Append(buf, size, len, "- %s\n", Name(gShowdownMoveNames, MOVES_COUNT_ALL, move, fb, sizeof(fb), gMoveNames[move]), 0);
        }
    }
    return len < size ? len : size - 1;
}
#endif // PORTABLE
