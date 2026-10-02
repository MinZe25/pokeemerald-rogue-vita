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
#include "rogue_settings.h"
#include "rogue.h"
#include "rogue_adventurepaths.h"
#include "event_data.h"
#include "constants/flags.h"

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
    printf("MODE config=%d runGenerator=%d difficulty=%d roomId=%d baseSeed=%u\n",
           Rogue_GetConfigRange(CONFIG_RANGE_GAME_MODE_NUM), gRogueRun.gameRules.adventureGenerator,
           Rogue_GetCurrentDifficulty(), gRogueRun.adventureRoomId, gRogueRun.baseSeed);
    for (i = 0; i < ROGUE_ADVENTURE_REPLAY_COUNT; i++)
        printf("REPLAY %d valid=%d seed=%u mode=%d\n", i, gRogueSaveBlock->adventureReplay[i].isValid,
               gRogueSaveBlock->adventureReplay[i].baseSeed,
               gRogueSaveBlock->adventureReplay[i].difficultyConfig.rangeValues[CONFIG_RANGE_GAME_MODE_NUM]);
    printf("REPLAY active=%d\n", FlagGet(FLAG_ROGUE_ADVENTURE_REPLAY_ACTIVE));
    printf("PATH length=%d rooms=%d y=%d..%d:", gRogueAdvPath.pathLength, gRogueAdvPath.roomCount,
           gRogueAdvPath.pathMinY, gRogueAdvPath.pathMaxY);
    for (i = 0; i < gRogueAdvPath.roomCount; i++)
        printf(" (%d,%d)t%d", gRogueAdvPath.rooms[i].coords.x, gRogueAdvPath.rooms[i].coords.y, gRogueAdvPath.rooms[i].roomType);
    printf("\n");
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

#ifdef PORTABLE
// ---- battle coverage -------------------------------------------------------
// ROGUE_COVERAGE=<pass>:<first>:<last>  (pass = moves | abilities | megas)
// Plays one short wild battle per case, deterministic from the case id:
//   moves      every move, used by both sides, 5 terrain/weather variants each
//              (case = variant * (MOVES_COUNT - 1) + move - 1), some doubles
//   abilities  every ability that some species has, on the player and enemy
//   megas      every mega / primal / ultra burst species holding its item,
//              the player triggers the gimmick in the move menu
// The harness drives the battle menus itself and ends each battle after a few
// turns. Problems are logged by src/platform/nulltrap.c, tagged with the case.
#include <string.h>
#include "characters.h"
#include "battle.h"
#include "battle_anim.h"
#include "battle_controllers.h"
#include "battle_gfx_sfx_util.h"
#include "battle_main.h"
#include "sprite.h"
#include "task.h"
#include "battle_util.h"
#include "random.h"
#include "constants/abilities.h"
#include "constants/battle.h"
#include "constants/form_change_types.h"
#include "constants/moves.h"

int Harness_PlayerControllerState(u32 battler);
void NullTrap_Note(const char *line);
void exit(int status); // <stdlib.h> clashes with the game's macros
void Platform_DumpFrameNow(void);

enum { COV_NONE, COV_MOVES, COV_ABILITIES, COV_MEGAS, COV_ANIMS };
// anims pass: every general / special / status animation (tables in
// data/battle_anim_scripts.s), played by the player or the opponent, in
// singles and doubles
#define COV_ANIMS_GENERAL 51
#define COV_ANIMS_SPECIAL 8
#define COV_ANIMS_STATUS  10
#define COV_ANIM_IDS      (COV_ANIMS_GENERAL + COV_ANIMS_SPECIAL + COV_ANIMS_STATUS)
static u8 sCovAnimType, sCovAnimId, sCovAnimEnemy, sCovAnimState;
#define COV_ENV_VARIANTS 5
#define COV_TURNS        3      // turns of move use before the battle is ended
static unsigned long sCovAnimFrames; // frames with a battle anim script running
#define COV_CASE_FRAMES  36000  // longer than this: hang

static int sCovPass;
static u32 sCovCase, sCovLast, sCovCaseFrames, sCovIdle;
static bool8 sCovInBattle, sCovEnvDone, sCovStarted;
static u32 sCovPhase;
static char sCovTag[48] = "-";
static u16 sCovMegaSpecies[256], sCovMegaItem[256];
static int sCovMegaCount = -1;

const char *NullTrap_CaseTag(void)
{
    return sCovTag;
}

static void GameToAscii(const u8 *s, char *out, int n)
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
        else out[i++] = '?';
    }
    out[i] = 0;
}

static u16 CovMove(void)
{
    return 1 + FuzzRand(MOVES_COUNT - 1);
}

static void CovSetMoves(struct Pokemon *mon, u16 m0, u16 m1, u16 m2, u16 m3)
{
    u16 moves[4] = { m0, m1, m2, m3 };
    int i;
    for (i = 0; i < 4; i++)
    {
        u8 pp = gBattleMoves[moves[i]].pp ? gBattleMoves[moves[i]].pp : 10;
        SetMonData(mon, MON_DATA_MOVE1 + i, &moves[i]);
        SetMonData(mon, MON_DATA_PP1 + i, &pp);
    }
}

static void CovCreateMon(struct Pokemon *mon, u16 species, u8 abilityNum, u16 item)
{
    CreateMon(mon, species, 70 + FuzzRand(21), 20, TRUE, FuzzRand(0xFFFF) << 16 | FuzzRand(0xFFFF), OT_ID_PLAYER_ID, 0);
    SetMonData(mon, MON_DATA_ABILITY_NUM, &abilityNum);
    SetMonData(mon, MON_DATA_HELD_ITEM, &item);
}

// first species (from a case-dependent start) that has the ability
static bool8 CovFindAbility(u16 ability, u32 start, u16 *species, u8 *abilityNum)
{
    u32 i;
    int n;
    for (i = 0; i < NUM_SPECIES; i++)
    {
        u16 s = 1 + (start + i) % (NUM_SPECIES - 1);
        if (gSpeciesInfo[s].baseHP == 0)
            continue;
        for (n = 0; n < NUM_ABILITY_SLOTS; n++)
            if (gSpeciesInfo[s].abilities[n] == ability)
            {
                *species = s;
                *abilityNum = n;
                return TRUE;
            }
    }
    return FALSE;
}

static void CovBuildMegaList(void)
{
    u32 s;
    sCovMegaCount = 0;
    for (s = 1; s < NUM_SPECIES && sCovMegaCount < (int)ARRAY_COUNT(sCovMegaSpecies); s++)
    {
        const struct FormChange *f = GetSpeciesFormChanges(s);
        if (f == NULL || gSpeciesInfo[s].baseHP == 0)
            continue;
        for (; f->method != FORM_CHANGE_TERMINATOR; f++)
            if (f->method == FORM_CHANGE_BATTLE_MEGA_EVOLUTION_ITEM || f->method == FORM_CHANGE_BATTLE_PRIMAL_REVERSION
             || f->method == FORM_CHANGE_BATTLE_ULTRA_BURST)
            {
                sCovMegaSpecies[sCovMegaCount] = s;
                sCovMegaItem[sCovMegaCount] = f->param1;
                sCovMegaCount++;
                break;
            }
    }
}

// Sets up the parties for the case and starts the battle
static void CovStartCase(void)
{
    static const u32 sTerrains[] = { 0, STATUS_FIELD_ELECTRIC_TERRAIN, STATUS_FIELD_GRASSY_TERRAIN, STATUS_FIELD_MISTY_TERRAIN, STATUS_FIELD_PSYCHIC_TERRAIN };
    char line[200], name[32], name2[32];
    u16 pSpecies = 0, eSpecies = 0, pItem = ITEM_NONE, eItem = ITEM_NONE, move = MOVE_NONE, m[4];
    u8 pAbility = FuzzRand(3), eAbility = FuzzRand(3);
    bool8 doubles = FALSE;
    int i;

    sFuzzRng = sCovCase * 2654435761u + sCovPass * 97 + 1;
    SeedRng(sCovCase * 7919 + sCovPass);
    m[0] = CovMove(); m[1] = CovMove(); m[2] = CovMove(); m[3] = CovMove();

    switch (sCovPass)
    {
    case COV_MOVES:
    {
        u32 variant = sCovCase / (MOVES_COUNT - 1);
        move = 1 + sCovCase % (MOVES_COUNT - 1);
        pSpecies = FuzzSpecies();
        eSpecies = FuzzSpecies();
        doubles = (move + variant) % 3 == 0;
        if (FuzzRand(3) == 0) pItem = 1 + FuzzRand(ITEMS_COUNT - 1);
        if (FuzzRand(3) == 0) eItem = 1 + FuzzRand(ITEMS_COUNT - 1);
        GameToAscii(gMoveNames[move], name, sizeof(name));
        snprintf(sCovTag, sizeof(sCovTag), "moves:%lu", (unsigned long)sCovCase);
        snprintf(line, sizeof(line), "CASE %s move=%d(%s) variant=%lu doubles=%d species=%d/%d items=%d/%d\n",
                 sCovTag, move, name, (unsigned long)variant, doubles, pSpecies, eSpecies, pItem, eItem);
        m[0] = m[1] = m[2] = m[3] = move;
        break;
    }
    case COV_ABILITIES:
    {
        u16 ability = 1 + sCovCase, eAbilityId = 1 + (sCovCase * 37 + 11) % (ABILITIES_COUNT - 1);
        if (!CovFindAbility(ability, sCovCase * 131, &pSpecies, &pAbility))
        {
            GameToAscii(gAbilityNames[ability], name, sizeof(name));
            snprintf(line, sizeof(line), "SKIP abilities:%lu ability=%d(%s) no species has it\n", (unsigned long)sCovCase, ability, name);
            printf("%s", line);
            NullTrap_Note(line);
            sCovStarted = FALSE;
            return;
        }
        if (!CovFindAbility(eAbilityId, sCovCase * 17, &eSpecies, &eAbility))
            eSpecies = FuzzSpecies();
        doubles = sCovCase % 3 == 0;
        GameToAscii(gAbilityNames[ability], name, sizeof(name));
        GameToAscii(gAbilityNames[eAbilityId], name2, sizeof(name2));
        snprintf(sCovTag, sizeof(sCovTag), "abilities:%lu", (unsigned long)sCovCase);
        snprintf(line, sizeof(line), "CASE %s ability=%d(%s) vs %d(%s) doubles=%d species=%d/%d moves=%d,%d,%d,%d\n",
                 sCovTag, ability, name, eAbilityId, name2, doubles, pSpecies, eSpecies, m[0], m[1], m[2], m[3]);
        break;
    }
    case COV_MEGAS:
        if (sCovMegaCount < 0)
            CovBuildMegaList();
        if ((int)sCovCase >= sCovMegaCount)
        {
            sCovStarted = FALSE;
            return;
        }
        pSpecies = sCovMegaSpecies[sCovCase];
        pItem = sCovMegaItem[sCovCase];
        // mega evolution needs the Mega Ring in the bag (CanMegaEvolve), Z moves / Ultra Burst the Z-Power Ring
        if (!CheckBagHasItem(ITEM_MEGA_RING, 1))
            AddBagItem(ITEM_MEGA_RING, 1);
        if (!CheckBagHasItem(ITEM_Z_POWER_RING, 1))
            AddBagItem(ITEM_Z_POWER_RING, 1);
        eSpecies = FuzzSpecies();
        doubles = sCovCase % 3 == 0;
        snprintf(sCovTag, sizeof(sCovTag), "megas:%lu", (unsigned long)sCovCase);
        snprintf(line, sizeof(line), "CASE %s species=%d item=%d vs %d doubles=%d moves=%d,%d,%d,%d\n",
                 sCovTag, pSpecies, pItem, eSpecies, doubles, m[0], m[1], m[2], m[3]);
        break;
    case COV_ANIMS:
    {
        static const char *const sTypeNames[] = { [ANIM_TYPE_GENERAL] = "general", [ANIM_TYPE_SPECIAL] = "special", [ANIM_TYPE_STATUS] = "status" };
        u32 id = sCovCase % COV_ANIM_IDS, variant = sCovCase / COV_ANIM_IDS;
        if (id < COV_ANIMS_GENERAL)
            sCovAnimType = ANIM_TYPE_GENERAL, sCovAnimId = id;
        else if (id < COV_ANIMS_GENERAL + COV_ANIMS_SPECIAL)
            sCovAnimType = ANIM_TYPE_SPECIAL, sCovAnimId = id - COV_ANIMS_GENERAL;
        else
            sCovAnimType = ANIM_TYPE_STATUS, sCovAnimId = id - COV_ANIMS_GENERAL - COV_ANIMS_SPECIAL;
        if ((sCovAnimType == ANIM_TYPE_GENERAL && sCovAnimId == B_ANIM_POKEBLOCK_THROW)
         || (sCovAnimType == ANIM_TYPE_SPECIAL && sCovAnimId == B_ANIM_BALL_THROW_WITH_TRAINER))
        {
            // Safari / Wally only: they wait for the throwing trainer's back sprite anim
            snprintf(line, sizeof(line), "SKIP anims:%lu anim=%s:%d needs a Safari / Wally battle\n",
                     (unsigned long)sCovCase, sTypeNames[sCovAnimType], sCovAnimId);
            printf("%s", line);
            NullTrap_Note(line);
            sCovStarted = FALSE;
            return;
        }
        sCovAnimEnemy = variant & 1;
        sCovAnimState = 0;
        doubles = (variant & 2) != 0;
        pSpecies = FuzzSpecies();
        eSpecies = FuzzSpecies();
        snprintf(sCovTag, sizeof(sCovTag), "anims:%lu", (unsigned long)sCovCase);
        snprintf(line, sizeof(line), "CASE %s anim=%s:%d by=%s doubles=%d species=%d/%d\n", sCovTag,
                 sTypeNames[sCovAnimType], sCovAnimId, sCovAnimEnemy ? "opponent" : "player", doubles, pSpecies, eSpecies);
        break;
    }
    }
    printf("%s", line);
    fflush(stdout);
    NullTrap_Note(line);

    // ROGUE_ANIMS=1: play the battle animations (Rogue forces them on in boss
    // battles even when the options turn them off)
    if (getenv("ROGUE_ANIMS") != NULL || sCovPass == COV_ANIMS)
    {
        gSaveBlock2Ptr->optionsWildBattleScene = OPTIONS_BATTLE_SCENE_4X;
        gSaveBlock2Ptr->optionsTrainerBattleScene = OPTIONS_BATTLE_SCENE_4X;
        gSaveBlock2Ptr->optionsBossBattleScene = OPTIONS_BATTLE_SCENE_4X;
    }

    ZeroPlayerPartyMons();
    for (i = 0; i < (doubles ? 2 : 1); i++)
    {
        CovCreateMon(&gPlayerParty[i], i == 0 ? pSpecies : FuzzSpecies(), i == 0 ? pAbility : FuzzRand(3), i == 0 ? pItem : ITEM_NONE);
        CovSetMoves(&gPlayerParty[i], m[0], m[1], m[2], m[3]);
    }
    CalculatePlayerPartyCount();

    if (doubles)
        CreateScriptedDoubleWildMon(eSpecies, 50, ITEM_NONE, FALSE, FuzzSpecies(), 50, ITEM_NONE, FALSE);
    else
        CreateScriptedWildMon(eSpecies, 50, ITEM_NONE, FALSE);
    for (i = 0; i < (doubles ? 2 : 1); i++)
    {
        u16 item = i == 0 ? eItem : ITEM_NONE;
        u8 num = i == 0 ? eAbility : FuzzRand(3);
        // Rogue scales scripted wild mons to the hub level: recreate them
        // strong enough to last a few turns
        CovCreateMon(&gEnemyParty[i], GetMonData(&gEnemyParty[i], MON_DATA_SPECIES), num, item);
        if (sCovPass == COV_MOVES)
            CovSetMoves(&gEnemyParty[i], move, move, move, move);
        else
            CovSetMoves(&gEnemyParty[i], CovMove(), CovMove(), CovMove(), CovMove());
    }
    if (doubles)
        BattleSetup_StartScriptedDoubleWildBattle();
    else
        BattleSetup_StartScriptedWildBattle();

    // field conditions applied when the first turn starts
    sCovEnvDone = FALSE;
    (void)sTerrains;
}

static void CovApplyEnvironment(void)
{
    static const u32 sTerrains[] = { 0, STATUS_FIELD_ELECTRIC_TERRAIN, STATUS_FIELD_GRASSY_TERRAIN, STATUS_FIELD_MISTY_TERRAIN, STATUS_FIELD_PSYCHIC_TERRAIN };
    static const u16 sWeathers[] = { 0, B_WEATHER_RAIN_TEMPORARY, B_WEATHER_SUN_TEMPORARY, B_WEATHER_SANDSTORM_TEMPORARY, B_WEATHER_HAIL_TEMPORARY, B_WEATHER_SNOW_TEMPORARY };
    u32 a = sCovCase % (MOVES_COUNT - 1), b = sCovCase / (MOVES_COUNT - 1);

    if (sCovPass != COV_MOVES)
    {
        a = sCovCase;
        b = sCovCase / 5;
    }
    gFieldStatuses &= ~STATUS_FIELD_TERRAIN_ANY;
    gFieldStatuses |= sTerrains[(a + b) % 5];
    gFieldTimers.terrainTimer = 5;
    gBattleWeather = sWeathers[(a * 3 + b) % 6];
    gWishFutureKnock.weatherDuration = 5;
    if ((a + b) % 4 == 0)
    {
        gFieldStatuses |= STATUS_FIELD_TRICK_ROOM;
        gFieldTimers.trickRoomTimer = 5;
    }
}

static u16 CovBattleKeys(void)
{
    int b;
    for (b = 0; b < MAX_BATTLERS_COUNT; b += 2) // player left / right
    {
        if (b >= gBattlersCount)
            break;
        switch (Harness_PlayerControllerState(b))
        {
        case 1: // choose action: fight; past the turn limit, end the battle
            if (!sCovEnvDone)
            {
                CovApplyEnvironment();
                sCovEnvDone = TRUE;
            }
            if (gBattleResults.battleTurnCounter >= COV_TURNS || (sCovPass == COV_ANIMS && sCovAnimState == 2))
                gBattleOutcome = B_OUTCOME_RAN;
            // PP drained (Spite, Eerie Spell, 1 PP moves...): top up, or the
            // move menu refuses every move and the test never ends
            {
                int i;
                for (i = 0; i < MAX_MON_MOVES; i++)
                    if (gBattleMons[b].moves[i] != MOVE_NONE && gBattleMons[b].pp[i] == 0)
                        gBattleMons[b].pp[i] = 1;
            }
            gActionSelectionCursor[b] = 0;
            return A_BUTTON;
        case 2: // choose move: trigger the gimmick first in the megas pass
            if (sCovPass == COV_MEGAS && CanMegaEvolve(b) && !gBattleStruct->mega.playerSelect)
                return START_BUTTON;
            if (sCovPass == COV_MEGAS && CanUltraBurst(b) && !gBattleStruct->burst.playerSelect)
                return START_BUTTON;
            {
                // a move with PP left, slot 0 (the move under test) first; when
                // the game refuses it (Disable, Torment, Taunt...) the next
                // press tries the next slot
                static u32 sMoveTries;
                int i, n;
                gMoveSelectionCursor[b] = 0;
                for (n = 0; n < MAX_MON_MOVES; n++)
                {
                    i = (n + sMoveTries) % MAX_MON_MOVES;
                    if (gBattleMons[b].moves[i] != MOVE_NONE && gBattleMons[b].pp[i] > 0)
                    {
                        gMoveSelectionCursor[b] = i;
                        break;
                    }
                }
                sMoveTries++;
            }
            return A_BUTTON;
        case 3: // choose target: if A is refused (e.g. a Commander Tatsugiri), move the cursor
        {
            static u32 sTargetPresses;
            sTargetPresses++;
            if (sTargetPresses % 4 == 3)
                return (sTargetPresses & 4) ? DPAD_LEFT : DPAD_RIGHT;
            return A_BUTTON;
        }
        }
    }
    // messages, party menu after a faint, ...
    return (sCovPhase & 8) ? DPAD_DOWN : A_BUTTON;
}

// Called every frame from the platform layer; returns the keys to press
u16 Harness_CoverageFrame(const char *spec, unsigned long frame, unsigned long fromFrame)
{
    u16 keys = 0;

    if (sCovPass == COV_NONE)
    {
        char pass[16] = { 0 };
        unsigned long first = 0, last = 0;
        if (!strcmp(spec, "info")) // case counts for tools/pc/battle_coverage.sh
        {
            CovBuildMegaList();
            printf("COVERAGE_INFO moves=%d abilities=%d megas=%d anims=%d\n",
                   (MOVES_COUNT - 1) * COV_ENV_VARIANTS, ABILITIES_COUNT - 1, sCovMegaCount, COV_ANIM_IDS * 4);
            fflush(stdout);
            exit(0);
        }
        if (sscanf(spec, "%15[a-z]:%lu:%lu", pass, &first, &last) < 2)
            return 0;
        sCovPass = !strcmp(pass, "moves") ? COV_MOVES : !strcmp(pass, "abilities") ? COV_ABILITIES
                 : !strcmp(pass, "anims") ? COV_ANIMS : COV_MEGAS;
        sCovCase = first;
        sCovLast = last ? last : first;
    }
    if (frame < fromFrame)
        return 0;

    sCovPhase++;
    sCovCaseFrames++;
    if (sCovStarted && sCovCaseFrames > COV_CASE_FRAMES)
    {
        char line[200];
        int i, sprites = 0, tasks = 0;
        for (i = 0; i < MAX_SPRITES; i++)
            sprites += gSprites[i].inUse;
        for (i = 0; i < NUM_TASKS; i++)
            tasks += gTasks[i].isActive;
        snprintf(line, sizeof(line), "HANG %s after %lu frames sprites=%d tasks=%d anim=%d visualTasks=%d soundTasks=%d mainFunc=%p"
                 " execFlags=0x%x ctrl=%p,%p,%p,%p script=%p action=%d turnAction=%d/%d\n",
                 sCovTag, (unsigned long)sCovCaseFrames, sprites, tasks, gAnimScriptActive, gAnimVisualTaskCount,
                 gAnimSoundTaskCount, (void *)gBattleMainFunc, (unsigned)gBattleControllerExecFlags,
                 (void *)gBattlerControllerFuncs[0], (void *)gBattlerControllerFuncs[1],
                 (void *)gBattlerControllerFuncs[2], (void *)gBattlerControllerFuncs[3],
                 (const void *)gBattlescriptCurrInstr, gCurrentActionFuncId, gCurrentTurnActionNumber, gBattlersCount);
        printf("%s", line);
        NullTrap_Note(line);
        for (i = 0; i < gBattlersCount; i++)
        {
            struct Sprite *spr = &gSprites[gBattlerSpriteIds[i]];
            printf("  battler %d: sprite %d inUse=%d invisible=%d y=%d y2=%d callback=%p status3=0x%lx hp=%d\n",
                   i, gBattlerSpriteIds[i], spr->inUse, spr->invisible, spr->y, spr->y2, (void *)spr->callback,
                   (unsigned long)gStatuses3[i], gBattleMons[i].hp);
        }
        Platform_DumpFrameNow(); // what it was stuck on
        exit(3);
    }

    if (gMain.inBattle && gBattleStruct != NULL)
    {
        sCovInBattle = TRUE;
        sCovIdle = 0;
        if (gAnimScriptActive)
            sCovAnimFrames++;
        if (getenv("ROGUE_COVDEBUG") && sCovCaseFrames % 300 == 0)
            printf("COVDEBUG f=%lu turn=%d state=%d/%d cursor=%d/%d mainFunc=%p outcome=%d curMove=%d attacker=%d\n",
                   (unsigned long)sCovCaseFrames, gBattleResults.battleTurnCounter,
                   Harness_PlayerControllerState(0), gBattlersCount > 2 ? Harness_PlayerControllerState(2) : -1,
                   gMoveSelectionCursor[0], gBattlersCount > 2 ? gMoveSelectionCursor[2] : -1,
                   (void *)gBattleMainFunc, gBattleOutcome, gCurrentMove, gBattlerAttacker);
        if (sCovPass == COV_ANIMS && sCovAnimState == 0 && Harness_PlayerControllerState(0) == 1)
        {
            // at the first action menu: play the animation under test, the
            // way the battle controllers launch them
            u8 atk = GetBattlerAtPosition(sCovAnimEnemy ? B_POSITION_OPPONENT_LEFT : B_POSITION_PLAYER_LEFT);
            u8 def = GetBattlerAtPosition(sCovAnimEnemy ? B_POSITION_PLAYER_LEFT : B_POSITION_OPPONENT_LEFT);
            gBattlerAttacker = atk;
            gBattlerTarget = def;
            if (sCovAnimType == ANIM_TYPE_GENERAL)
                TryHandleLaunchBattleTableAnimation(atk, atk, def, sCovAnimId, 0);
            else if (sCovAnimType == ANIM_TYPE_SPECIAL)
                InitAndLaunchSpecialAnimation(atk, atk, def, sCovAnimId);
            else
                LaunchStatusAnimation(atk, sCovAnimId);
            sCovAnimState = 1;
            return 0;
        }
        if (sCovPass == COV_ANIMS && sCovAnimState == 1)
        {
            if (!gAnimScriptActive) // the launch task runs the script
            {
                char line[64];
                snprintf(line, sizeof(line), "ANIMDONE %s animframes=%lu\n", sCovTag, (unsigned long)sCovAnimFrames);
                printf("%s", line);
                NullTrap_Note(line);
                sCovAnimState = 2;
            }
            return 0;
        }
        if (sCovPhase & 1)
            return 0; // release between presses so JOY_NEW sees each one
        return CovBattleKeys();
    }

    if (gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled())
    {
        if (++sCovIdle < 30)
            return 0;
        sCovIdle = 0;
        if (sCovStarted && sCovInBattle)
        {
            char line[96];
            snprintf(line, sizeof(line), "DONE %s frames=%lu animframes=%lu\n", sCovTag,
                     (unsigned long)sCovCaseFrames, (unsigned long)sCovAnimFrames);
            printf("%s", line);
            NullTrap_Note(line);
            sCovCase++;
        }
        else if (sCovStarted)
        {
            return 0; // battle not started yet
        }
        if (sCovCase > sCovLast || (sCovPass == COV_MEGAS && sCovMegaCount >= 0 && (int)sCovCase >= sCovMegaCount))
        {
            printf("COVERAGE FINISHED\n");
            NullTrap_Note("COVERAGE FINISHED\n");
            exit(0);
        }
        sCovStarted = TRUE;
        sCovInBattle = FALSE;
        sCovCaseFrames = 0;
        sCovAnimFrames = 0;
        CovStartCase();
        if (!sCovStarted) // skipped case
            sCovCase++;
        return 0;
    }

    // outside battles: dismiss messages (B does not talk to anyone)
    sCovIdle = 0;
    if (sCovPhase & 1)
        return 0;
    keys = (sCovPhase & 6) == 0 ? B_BUTTON : 0;
    return keys;
}
#endif

#ifdef PORTABLE
// ROGUE_SCOUT=<spec>: shows what a run's path holds without playing it
//   info      print the rooms of the current path (starts a run from the hub)
//   room:I    enter room I of the path and print its map and wild Pokémon
//   route:K   enter the first route room, forced to route map K (crash tests)
//   pools     print every route's wild pool (tools/pc/encounters.py)
// ROGUE_SCOUT_DIFF=d regenerates the path at difficulty d (badges) first.
#include "battle_main.h"
#include "field_screen_effect.h"
#include "rogue_pokedex.h"
#include "rogue_baked.h"
#include "constants/map_groups.h"
void Harness_EnterAdvPathRoom(u8 roomIdx);
void Harness_PrintRoutePools(void);
void DoWarp(void);
unsigned long strtoul(const char *s, char **end, int base); // <stdlib.h> clashes with the game's macros

static void ScoutPrintSpecies(const char *label, const u16 *species, int count)
{
    char name[32];
    int i;
    printf("  %s:", label);
    for (i = 0; i < count; i++)
    {
        if (species[i] == SPECIES_NONE)
            continue;
        GameToAscii(GetSpeciesName(species[i]), name, sizeof(name));
        printf(" %s(%d)", name, species[i]);
    }
    printf("\n");
}

static void ScoutPrintPath(void)
{
    char type[16];
    int i;
    printf("SCOUT_PATH difficulty=%d seed=%u rooms=%d\n", Rogue_GetCurrentDifficulty(), gRogueRun.baseSeed, gRogueAdvPath.roomCount);
    for (i = 0; i < gRogueAdvPath.roomCount; i++)
    {
        struct RogueAdvPathRoom *room = &gRogueAdvPath.rooms[i];
        printf("  room %2d at (%d,%d) type=%d", i, room->coords.x, room->coords.y, room->roomType);
        if (room->roomType == ADVPATH_ROOM_ROUTE)
        {
            GameToAscii(gTypeNames[Rogue_GetTypeForHintForRoom(room)], type, sizeof(type));
            printf(" route=%d map=%d.%d difficulty=%d hint=%s", room->roomParams.roomIdx,
                   gRogueRouteTable.routes[room->roomParams.roomIdx].map.group, gRogueRouteTable.routes[room->roomParams.roomIdx].map.num,
                   room->roomParams.perType.route.difficulty, type);
        }
        printf("\n");
    }
    fflush(stdout);
}

static bool8 ScoutOverworldIdle(void)
{
    return gMain.callback2 == CB2_Overworld && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled();
}

u16 Harness_ScoutFrame(const char *spec, unsigned long frame)
{
    static int sPhase, sRoom = -1;
    static unsigned long sWait;
    static u32 sFrames;
    const char *diff = getenv("ROGUE_SCOUT_DIFF");

    if (++sFrames > 30000)
    {
        printf("SCOUT_FAIL stuck in phase %d\n", sPhase);
        exit(4);
    }
    switch (sPhase)
    {
    case 0: // in the overworld: go to the path screen (starts a run from the hub)
        if (!ScoutOverworldIdle())
            break;
        if (Rogue_IsRunActive() && gRogueAdvPath.isOverviewActive)
        {
            sPhase = 2;
            break;
        }
        SetWarpDestination(MAP_GROUP(ROGUE_ADVENTURE_PATHS), MAP_NUM(ROGUE_ADVENTURE_PATHS), WARP_ID_NONE, 0, 0);
        DoWarp();
        sPhase = 1;
        break;
    case 1:
        if (ScoutOverworldIdle() && Rogue_IsRunActive() && gRogueAdvPath.isOverviewActive)
            sPhase = 2;
        break;
    case 2: // on the path screen
        if (diff != NULL)
        {
            Rogue_SetCurrentDifficulty(strtoul(diff, NULL, 10));
            gRogueAdvPath.roomCount = 0;
            gRogueRun.adventureRoomId = ADVPATH_INVALID_ROOM_ID;
            RogueAdv_GenerateAdventurePathsIfRequired();
        }
        ScoutPrintPath();
        if (!strcmp(spec, "info"))
            exit(0);
        if (!strcmp(spec, "pools"))
        {
            int species;
            char name[32];
            Harness_PrintRoutePools();
            for (species = 1; species < NUM_SPECIES; species++)
            {
                GameToAscii(GetSpeciesName(species), name, sizeof(name));
                printf("NAME %d %d %d %s\n", species, RoguePokedex_GetSpeciesType(species, 0),
                       RoguePokedex_GetSpeciesType(species, 1), name);
            }
            // evolutions as Rogue changes them (e.g. Eevee -> Umbreon with a Moon Stone)
            for (species = 1; species < NUM_SPECIES; species++)
            {
                int i, count = Rogue_GetMaxEvolutionCount(species);
                for (i = 0; i < count; i++)
                {
                    struct Evolution evo;
                    u8 item[ITEM_NAME_LENGTH + 1];
                    char itemAscii[32] = "";
                    Rogue_ModifyEvolution(species, i, &evo);
                    if (evo.method == 0 || evo.targetSpecies == SPECIES_NONE || evo.method == EVOLUTIONS_END)
                        continue;
                    if (evo.method == EVO_ITEM || evo.method == EVO_ITEM_MALE || evo.method == EVO_ITEM_FEMALE)
                    {
                        CopyItemName(evo.param, item);
                        GameToAscii(item, itemAscii, sizeof(itemAscii));
                    }
                    printf("EVO %d %d %d %d %s\n", species, evo.targetSpecies, evo.method, evo.param, itemAscii);
                }
            }
            fflush(stdout);
            exit(0);
        }
        if (!strncmp(spec, "room:", 5))
        {
            sRoom = strtoul(spec + 5, NULL, 10);
        }
        else if (!strncmp(spec, "route:", 6))
        {
            int i;
            for (i = 0; i < gRogueAdvPath.roomCount && sRoom < 0; i++)
                if (gRogueAdvPath.rooms[i].roomType == ADVPATH_ROOM_ROUTE)
                    sRoom = i;
            if (sRoom >= 0)
                gRogueAdvPath.rooms[sRoom].roomParams.roomIdx = strtoul(spec + 6, NULL, 10);
        }
        if (sRoom < 0 || sRoom >= gRogueAdvPath.roomCount)
        {
            printf("SCOUT_FAIL no such room (%s)\n", spec);
            exit(2);
        }
        printf("SCOUT_ENTER room %d\n", sRoom);
        fflush(stdout);
        Harness_EnterAdvPathRoom(sRoom);
        sPhase = 3;
        break;
    case 3: // arrived: let the map scripts run, then report
        if (!ScoutOverworldIdle() || gRogueAdvPath.isOverviewActive)
        {
            sWait = 0;
            break;
        }
        if (++sWait < 120)
            break;
        {
            struct RogueAdvPathRoom *room = &gRogueAdvPath.rooms[sRoom];
            char type[16] = "-";
            if (room->roomType == ADVPATH_ROOM_ROUTE)
                GameToAscii(gTypeNames[Rogue_GetTypeForHintForRoom(room)], type, sizeof(type));
            printf("SCOUT room=%d type=%d map=%d.%d route=%d difficulty=%d hint=%s\n", sRoom, room->roomType,
                   gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum, room->roomParams.roomIdx,
                   room->roomParams.perType.route.difficulty, type);
            ScoutPrintSpecies("grass", gRogueRun.wildEncounters.species, WILD_ENCOUNTER_GRASS_CAPACITY);
            ScoutPrintSpecies("water", gRogueRun.wildEncounters.species + WILD_ENCOUNTER_GRASS_CAPACITY, WILD_ENCOUNTER_WATER_CAPACITY);
            fflush(stdout);
            Platform_DumpFrameNow(); // what the map looks like
            exit(0);
        }
    }
    // dismiss messages / popups
    return (frame % 20 == 0) ? B_BUTTON : 0;
}
#endif
