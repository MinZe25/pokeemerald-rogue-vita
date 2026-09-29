#ifdef PORTABLE
// Test hooks for the PC/Vita automated harness (see src/platform/sdl2.c).
// Called once per frame; does nothing unless the harness asks for it.
#include "global.h"
char *getenv(const char *name);
#include "main.h"
#include "battle_setup.h"
#include "item.h"
#include "overworld.h"
#include "pokemon.h"
#include "script_pokemon_util.h"
#include "constants/items.h"
#include "constants/species.h"

void Harness_StartTestBattle(void)
{
    if (gMain.callback2 != CB2_Overworld)
        return;

    if (gPlayerPartyCount == 0)
        ScriptGiveMon(SPECIES_TREECKO, 10, ITEM_NONE, 0, 0, 0);
    if (getenv("ROGUE_FULLPARTY") != NULL)
    {
        static const u16 sFill[] = { SPECIES_POOCHYENA, SPECIES_WURMPLE, SPECIES_LOTAD, SPECIES_SEEDOT, SPECIES_RALTS };
        // write straight into the party: outside a run Rogue sends extra gifts to the PC
        for (int slot = CalculatePlayerPartyCount(), i = 0; slot < PARTY_SIZE; slot++, i++)
            CreateMon(&gPlayerParty[slot], sFill[i % 5], 8, USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
        CalculatePlayerPartyCount();
    }
    if (getenv("ROGUE_FULLPARTY") != NULL)
    {
        // so the R quick-ball shortcut picks the Master Ball
        static const u16 sBalls[] = { ITEM_POKE_BALL, ITEM_GREAT_BALL, ITEM_ULTRA_BALL, ITEM_PREMIER_BALL };
        for (int i = 0; i < 4; i++)
            RemoveBagItem(sBalls[i], CountTotalItemQuantityInBag(sBalls[i]));
    }
    AddBagItem(ITEM_MASTER_BALL, 5);
    CreateScriptedWildMon(SPECIES_ZIGZAGOON, 3, ITEM_NONE, FALSE);
    BattleSetup_StartScriptedWildBattle();
}
#endif

#ifdef PORTABLE
#include "script.h"

static u32 sFuzzRng;

static u32 FuzzRand(u32 n)
{
    sFuzzRng = sFuzzRng * 1103515245 + 12345;
    return ((sFuzzRng >> 8) & 0xFFFFFF) % n;
}

static u16 FuzzSpecies(void)
{
    for (;;)
    {
        u16 species = 1 + FuzzRand(NUM_SPECIES - 1);
        if (gSpeciesInfo[species].baseHP != 0)
            return species;
    }
}

static void FuzzMon(struct Pokemon *mon)
{
    u8 abilityNum = FuzzRand(3);
    u16 item = FuzzRand(4) == 0 ? ITEM_NONE : 1 + FuzzRand(ITEMS_COUNT - 1);

    CreateMon(mon, FuzzSpecies(), 20 + FuzzRand(81), USE_RANDOM_IVS, FALSE, 0, OT_ID_PLAYER_ID, 0);
    SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
}

// ROGUE_BATTLEFUZZ=<seed>: back-to-back wild battles (singles and doubles) with
// random parties, abilities and held items; the input monkey plays them
void Harness_BattleFuzzFrame(unsigned long seed)
{
    static unsigned long sIdleFrames;
    int i, count;

    if (sFuzzRng == 0)
        sFuzzRng = seed | 1;
    if (gMain.callback2 != CB2_Overworld || ArePlayerFieldControlsLocked() || ScriptContext_IsEnabled())
    {
        sIdleFrames = 0;
        return;
    }
    if (++sIdleFrames < 90)
        return;
    sIdleFrames = 0;

    count = 1 + FuzzRand(PARTY_SIZE);
    ZeroPlayerPartyMons();
    for (i = 0; i < count; i++)
        FuzzMon(&gPlayerParty[i]);
    CalculatePlayerPartyCount();

    if (FuzzRand(3) == 0 && count >= 2)
    {
        CreateScriptedDoubleWildMon(FuzzSpecies(), 20 + FuzzRand(81), ITEM_NONE, FALSE,
                                    FuzzSpecies(), 20 + FuzzRand(81), ITEM_NONE, FALSE);
        for (i = 0; i < 2; i++)
        {
            u8 abilityNum = FuzzRand(3);
            u16 item = 1 + FuzzRand(ITEMS_COUNT - 1);
            SetMonData(&gEnemyParty[i], MON_DATA_ABILITY_NUM, &abilityNum);
            SetMonData(&gEnemyParty[i], MON_DATA_HELD_ITEM, &item);
        }
        BattleSetup_StartScriptedDoubleWildBattle();
    }
    else
    {
        u8 abilityNum = FuzzRand(3);
        u16 item = 1 + FuzzRand(ITEMS_COUNT - 1);
        CreateScriptedWildMon(FuzzSpecies(), 20 + FuzzRand(81), ITEM_NONE, FALSE);
        SetMonData(&gEnemyParty[0], MON_DATA_ABILITY_NUM, &abilityNum);
        SetMonData(&gEnemyParty[0], MON_DATA_HELD_ITEM, &item);
        BattleSetup_StartScriptedWildBattle();
    }
}
#endif

#ifdef PORTABLE
#include <stdio.h>
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "berry.h"
#include "rogue_controller.h"

extern const u8 BerryTreeScript[];
bool8 Rogue_IsRunActive(void);

// ROGUE_STATEDUMP=<frame>: prints run state and the map's object events
void Harness_DumpState(void)
{
    int i;
    s16 x, y;

    PlayerGetDestCoords(&x, &y);
    printf("STATE run=%d map=%d.%d player=(%d,%d) elev=%d facing=%d BerryTreeScript=%p\n",
           Rogue_IsRunActive(), gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum,
           x, y, gObjectEvents[gPlayerAvatar.objectEventId].currentElevation, GetPlayerFacingDirection(), (const void *)BerryTreeScript);
    for (i = 0; i < OBJECT_EVENTS_COUNT; i++)
    {
        struct ObjectEvent *obj = &gObjectEvents[i];
        if (!obj->active)
            continue;
        printf("  obj %2d local=%3d gfx=%4d at (%d,%d) script=%p trainerType=%d\n", i, obj->localId, obj->graphicsId,
               obj->currentCoords.x, obj->currentCoords.y,
               (const void *)GetObjectEventScriptPointerByObjectEventId(i), obj->trainerType);
        if (GetObjectEventScriptPointerByObjectEventId(i) == BerryTreeScript)
        {
            u8 id = GetObjectEventBerryTreeId(i);
            struct BerryTree *tree = GetBerryTreeInfo(id);
            printf("       berryTreeId=%d berry=%d stage=%d stopGrowth=%d minutes=%d yield=%d invisible=%d elev=%d/%d spriteInvisible=%d\n",
                   id, tree->berry, tree->stage, tree->stopGrowth, tree->minutesUntilNextStage, tree->berryYield,
                   obj->invisible, obj->currentElevation, obj->previousElevation, gSprites[obj->spriteId].invisible);
        }
    }
    for (i = 0; i < gSaveBlock1Ptr->objectEventTemplatesCount; i++)
    {
        const struct ObjectEventTemplate *t = &gSaveBlock1Ptr->objectEventTemplates[i];
        if (t->script == BerryTreeScript || (t->x >= x - 3 && t->x <= x + 3 && t->y >= y - 3 && t->y <= y + 3))
            printf("  tmpl %2d local=%3d at (%d,%d) script=%p%s\n", i, t->localId, t->x + 7, t->y + 7,
                   (const void *)t->script, t->script == BerryTreeScript ? " (BerryTreeScript)" : "");
    }
    fflush(stdout);
}
#endif

#ifdef PORTABLE
#include "event_data.h"
#include "constants/flags.h"
void FollowMon_ClearCachedPartnerSpecies(void);
void SetupFollowParterMonObjectEvent(void);

// Give the player a party mon and turn on the following Pokémon
void Harness_EnableFollower(void)
{
    if (gPlayerPartyCount == 0)
        ScriptGiveMon(SPECIES_TREECKO, 10, ITEM_NONE, 0, 0, 0);
    FlagSet(FLAG_SYS_SHOW_POKE_FOLLOWER);
    FollowMon_ClearCachedPartnerSpecies();
    SetupFollowParterMonObjectEvent();
}
#endif

#ifdef PORTABLE
#include "sound.h"
#include "m4a.h"
#include <stdio.h>

// Plays a cry and a series of sound effects at fixed frame offsets
void Harness_SoundTestFrame(unsigned long t)
{
    static bool8 sCryWasPlaying = FALSE;

    if (t == 0)
        m4aMPlayAllStop();
    if (t == 30)
    {
        PlayCry_Normal(SPECIES_PIKACHU, 0);
        sCryWasPlaying = TRUE;
        printf("SOUNDTEST cry start t=%lu\n", t);
    }
    if (t > 30 && t < 400 && sCryWasPlaying && !IsCryPlaying())
    {
        sCryWasPlaying = FALSE;
        printf("SOUNDTEST cry finished t=%lu (IsCryPlaying false)\n", t);
    }
    if (t >= 400 && t < 400 + 40 * 300 && (t - 400) % 40 == 0)
    {
        u16 se = 1 + (t - 400) / 40;
        m4aMPlayAllStop(); // isolate each effect (priorities would otherwise reject some)
        PlaySE(se);
        printf("SOUNDTEST se %u t=%lu\n", se, t);
    }
}
#endif
