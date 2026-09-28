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
