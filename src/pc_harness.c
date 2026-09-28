#ifdef PORTABLE
// Test hooks for the PC/Vita automated harness (see src/platform/sdl2.c).
// Called once per frame; does nothing unless the harness asks for it.
#include "global.h"
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
    AddBagItem(ITEM_MASTER_BALL, 5);
    CreateScriptedWildMon(SPECIES_ZIGZAGOON, 3, ITEM_NONE, FALSE);
    BattleSetup_StartScriptedWildBattle();
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
