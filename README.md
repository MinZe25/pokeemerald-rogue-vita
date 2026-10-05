# Pokémon Emerald Rogue for PS Vita

A **native PS Vita port** of [Pokabbie's Pokémon Emerald Rogue](https://github.com/Pokabbie/pokeemerald-rogue) (`expansion-dev`). It is not an emulator: the game's C code is compiled for the Vita's ARM CPU, with a small platform layer that stands in for the GBA hardware. The same code also builds for **PC** (Linux and Windows) through SDL2. The PC build was the first step towards the Vita port and is now mainly used for testing.

The platform layer is based on [NTx86/pokeemerald-sdl2pc](https://github.com/NTx86/pokeemerald-sdl2pc). All game logic, data and balancing are Emerald Rogue's.

## Install (Vita)

The port contains no game data. Its graphics, sounds and maps are loaded at startup from **your own ROM** of Pokémon Emerald Rogue **EX v2.2.1**: the official 2.2.1 patch applied to your own Pokémon Emerald ROM. The expected file is a 32 MB `.gba` with SHA-1 `7600af1fe08444c850c3c1227fd7dfd81336ae8e`.

1. Download `pokeemerald_rogue.vpk` from the [Releases](../../releases) page.
2. Copy it to the Vita and install it with **VitaShell**. The app is *Pokémon Emerald Rogue*, title ID `PKMR00001`.
3. Copy your Emerald Rogue EX v2.2.1 `.gba` into `ux0:data/pokeemerald_rogue/` (see [The ROM](#the-rom) below). If the ROM is missing or a different version, the game shows a screen saying so.
4. Saves, settings and save states are stored in the same folder. Updating over an existing install keeps them.

> If the bubble or LiveArea art doesn't change after an update, delete the app from the home screen and install the vpk again. Your data in `ux0:data` is kept.

**Bringing an emulator save:** the save format matches the GBA's, so a Rogue save from an emulator (`.sav` / `.srm`, 128 KB) can be copied to `ux0:data/pokeemerald_rogue/pokeemerald.sav`.

## The ROM

The vpk contains code only. Graphics, music, sounds and maps come from your ROM, so you need exactly this one:

| | |
|---|---|
| Game | Pokémon Emerald Rogue **EX** (not the Vanilla variant) |
| Version | **2.2.1**, Pokabbie's official release |
| Size | 32 MB (33,554,432 bytes) |
| SHA-1 | `7600af1fe08444c850c3c1227fd7dfd81336ae8e` |

Other versions don't work, older or newer, nor Vanilla or other hacks: the port reads every asset from a fixed position in that exact file.

**Making the ROM:**
1. Dump your own Pokémon Emerald cartridge to a `.gba` file.
2. Get the official **EX v2.2.1** patch from Pokabbie's Emerald Rogue release channels.
3. Apply the patch to your Emerald ROM with a patcher such as [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/). If the patcher reports a wrong source ROM, your Emerald dump is not the version the patch expects.
4. Check the SHA-1 of the result:
   - Windows (PowerShell): `Get-FileHash -Algorithm SHA1 "Emerald Rogue.gba"`
   - Linux / macOS: `sha1sum "Emerald Rogue.gba"` (macOS: `shasum`)

   It must be `7600af1fe08444c850c3c1227fd7dfd81336ae8e`. If you already play Emerald Rogue 2.2.1 EX on an emulator, check that ROM first: it is probably the right one.

**Installing it:** copy the `.gba` into `ux0:data/pokeemerald_rogue/` (for example with VitaShell's FTP or USB mode).
- Any file name works, but it must end in lowercase `.gba`.
- Other `.gba` files in the folder are ignored, as long as one of them is the right ROM.
- The ROM is read once at startup to copy the assets into memory, then released. It is never changed, and the game does not need it again until the next launch.

**If it doesn't start:**
- *"no ROM found"*: no `.gba` in `ux0:data/pokeemerald_rogue/`. Check the folder name and the extension (`.GBA` or `.gba.zip` won't be found).
- *"wrong ROM version"*: a `.gba` was found but it isn't EX v2.2.1. Check its SHA-1 as above. `ux0:data/pokeemerald_rogue/log.txt` lists every file that was checked.

Please don't share ROMs or ask for them: that's why the port loads its data from your own.

## Controls

| Vita | PC keyboard | Action (remappable) |
|---|---|---|
| Cross / Circle | Z / X | A / B |
| L / R | A / S | L / R |
| Start / Select | Enter / Backspace | Start / Select |
| Triangle (hold) | Space | Fast forward |
| Square, or a tap on the touch screen | C, F1, Esc | Open the port menu |

## Port menu

Opened with Square by default (the button can be remapped, and the touch screen can be turned off):

- **Save / load state:** 5 slots per save file. A state only loads in the same build that made it, so use the in-game save for real progress.
- **Scale:** 1x, 2x, 3x, Fit (keeps the aspect ratio) or Stretch, with a Sharp or Smooth filter.
- **Fast forward:** 2x to 5x while held.
- **Save anywhere:** adds a real in-game **Save** during adventures (like the save-scum charm), and loading such a save puts you back where you were. Handy against crashes.
- **Party QR:** shows your party as a QR code in Pokémon Showdown's export format. Scan it with your phone, copy the text and paste it into the [Showdown damage calculator](https://calc.pokemonshowdown.com/)'s Import box.
- **Buttons:** remap every action, including the menu button, turn touch-to-open on or off, and let the right stick use the key items registered on Rogue's Select wheel directly (up, right, down, left = the wheel's slots).
- **Save file:** 3 independent save files.
- **Reset game** and **Quit**.

## Performance

The game runs at a full 60 fps on a Vita 1000. The app raises the clocks itself (CPU 444 MHz, bus and GPU 222 MHz). A frame takes about 10 ms to draw and 2.5 ms to mix audio. Audio is mixed by the game's own sound engine and resampled to 48 kHz for the Vita.

## What changed compared to Emerald Rogue

**No bundled game data.** The Vita build (`ROM_ASSETS=1`) replaces every embedded asset with a same-size placeholder. At startup it fills them in from the player's ROM, using an offset table (`src/platform/rom_assets_table.c`) generated by `tools/pc/rom_assets.py` from an official ROM; the table holds only offsets and sizes. The source is based on Pokabbie's 2.2.1 release commit so that every asset matches that ROM.

**Platform layer** (`src/platform/`):
- SDL2 video, audio and input for Vita / Linux / Windows.
- A faster tile renderer.
- The port menu, save states and soft reset.
- LiveArea art (`src/platform/vita/`).

**Portability.** The GBA quietly tolerates some mistakes that crash on PC and Vita. These were fixed without changing gameplay:
- Reading or writing near address 0 (NULL pointers) and division by zero. The GBA returns garbage or 0; the port now does the same instead of crashing.
- Out-of-range table lookups, such as move `0xFFFF` ("no move") and animation lists that are too short.
- Structure layout that matches the GBA's, so saves stay compatible.

**Game bugs found by the test suite, most of them upstream, all fixed:**
- **Freezes:** Doodle in double battles; Magnetic Flux or Gear Up in a single battle after a double battle; a Pokémon fainting twice in doubles.
- **Memory corruption:**
  - the HP display overflowed a small buffer on every battle start;
  - field-wide moves (Perish Song, Flower Shield…) in doubles wrote past the end of the battle arrays.
- **Crashes and wrong behaviour in menus and battles:**
  - party menu relearn check and the bag list;
  - PC box storage;
  - registered key items (DNA Splicers, Reins of Unity…) used before the bag was ever opened;
  - binding moves printing a garbage message;
  - item names longer than 16 characters;
  - Lucky Chant, Toxic Debris and Round in doubles.
- **Berries:** planting on empty soil stopped working after upstream's "don't interact with invisible objects" change.

## Testing

`tools/pc/battle_coverage.sh` runs the PC build headless. It plays one short, deterministic battle per case:
- every move, in 5 terrain/weather variants;
- every ability;
- every mega, primal and Ultra Burst form;
- in singles and doubles.

It reports every crash, NULL access, division by zero, freeze and (with an AddressSanitizer build) out-of-bounds access in a single report:

```sh
make tools && make PORTABLE=1 TARGET_OS=LINUX RELEASE=1   # add ASAN=1 for the AddressSanitizer build
sudo sysctl -w vm.mmap_min_addr=0                         # lets the NULL trap map page 0
tools/pc/battle_coverage.sh <a save standing in the hub>
```

## Building

The tools and the Vita build run on Linux / WSL:

```sh
./init_deps.sh                                # downloads poryscript
make tools
make PORTABLE=1 TARGET_OS=VITA RELEASE=1      # pokeemerald_rogue.vpk (needs VitaSDK + SDL2 from vdpm)
make PORTABLE=1 TARGET_OS=LINUX RELEASE=1     # pokeemerald_rogue (32-bit, needs libsdl2-dev:i386)
make PORTABLE=1 TARGET_OS=WINDOWS RELEASE=1   # pokeemerald_rogue.exe (i686-w64-mingw32 + SDL2 in ./SDL2)
```

The Vita build always loads its assets from the ROM. PC builds embed them by default; with `ROM_ASSETS=1` the Linux build also loads them from the ROM, looking for the `.gba` in its working directory.

The **Vita release** GitHub Action (`.github/workflows/vita-release.yml`) builds the vpk and publishes a release. To trigger it, push a tag named `vita-v*`, or run it from the Actions tab.

## Credits

- **Pokabbie**: Pokémon Emerald Rogue.
- **RHH**: pokeemerald-expansion.
- **pret**: the pokeemerald decompilation.
- **NTx86**: pokeemerald-sdl2pc, the base of the platform layer.
- **VitaSDK**: the Vita toolchain.

---

# pokeemerald-expansion

## What is pokeemerald-expansion?

pokeemerald-expansion is a decomp hack base project based off pret's [pokeemerald](https://github.com/pret/pokeemerald) decompilation project. It's recommended that any new projects that plan on using it, to clone this repository instead of pret's vanilla repository, as we regurlarly incorporate pret's documentation changes. This is ***NOT*** a standalone romhack, and as such, most features will be unavailable and/or unbalanced if played as is.

If you use pokeemerald-expansion in your hack, please add RHH (Rom Hacking Hideout) to your credits list. Optionally, you can list the version used, so it can help players know what features to expect.
You can phrase it as the following:
```
Based off RHH's pokeemerald-expansion v1.7.3 https://github.com/rh-hideout/pokeemerald-expansion/
```

## What features are included?
- ***IMPORTANT*❗❗ Read through these to learn what features you can toggle**:
    - [Battle configurations](/include/config/battle.h)
    - [Pokémon configurations](/include/config/pokemon.h)
    - [Item configurations](/include/config/item.h)
    - [Overworld configurations](/include/config/overworld.h)
    - [Debug configurations](/include/config/debug.h)
- ***Upgraded battle engine.***
    - Gen5+ damage calculation.
    - 2v2 Wild battles support.
    - 1v2/2v1 battles support.
    - Fairy Type (configurable).
    - Physical/Special/Status Category Split (configurable).
    - New moves and abilities up to Scarlet and Violet.
        - Custom Contest data up to SwSh, newer moves are WIP. ([source](https://pokemonurpg.com/info/contests/rse-move-list/))
    - Mega Evolution
    - Primal Reversion
    - Ultra Burst
    - Z-Moves
        - Gen 8+ damaging moves are given power extrapolated from Gen 7.
        - Gen 8+ status moves have no additional effects, like Healing Wish.
    - Dynamax
        - Gigantamax forms
    - Initial battle parameters
        - Queueing stat boosts (aka, Totem Boosts)
        - Setting Terrains.
    - Mid-turn speed recalculation.
    - Quick Poké Ball selection in Wild Battles
        - Press `R` to use last selected Poké Ball.
        - Hold `R` to change selection with the D-Pad.
    - Run option shortcut
    - Faster battle intro
        - Message and animation/cry happens at the same time.
    - Faster HP drain.
    - Battle Debug menu.
        - Accessed by pressing `Select` on the "Fight/Bag/Pokémon/Run" menu.
    - Option to use AI flags in wild Pokémon battles.
    - FRLG/Gen4+ whiteout money calculation.
    - Configurable experience settings
        - Experience on catch.
        - Splitting experience.
        - Trainer experience.
        - Scaled experience.
        - Unevolved experience boost.
    - Frostbite.
        - Doesn't replace freezing unless a config is enabled, so you can mix and match.
    - Critical capture.
    - Removed badge boosts (configurable).
    - Recalculating stats at the end of every battle.
    - Level 100 Pokémon can earn EVs.
    - Inverse battle support.
    - TONS of other features listed [here](/include/config/battle.h).
- ***Full Trainer customization***
    - Nickname, EVs, IVs, moves, ability, ball, friendship, nature, gender, shininess.
    - Custom tag battle support (teaming up an NPC in a double battle).
    - Sliding trainer messages.
    - Upgraded Trainer AI
        - Considers newer move effects.
        - New flag options to let you customize the intelligence of your trainers.
        - Faster calculations.
    - Specify Poké Balls by Trainer class.
- ***Pokémon Species from Generations 1-9.***
    - Simplified process to add new Pokémon.
    - Option to disable unwanted families.
    - Updated sprites to DS style.
    - Updated stats, types, abilities and egg groups (configurable).
    - Updated Hoenn's Regional Dex to match ORAS' (configurable).
    - Updated National Dex incorporating the new species.
    - Sprite and animation visualizer.
        - Accesible by pressing `Select` on a Pokémon's Summary screen.
    - Gen4+ evolution methods, with some changes:
        - Mossy Rock, Icy Rock and Magnetic Field locations match ORAS'.
            - Leaf, Ice and Thunder Stones may also be used.
        - Inkay just needs level 30 to evolve.
            - You can't physically have both the RTC and gyroscope, so we skip this requirement.
        - Sylveon uses Gen8+'s evolution method (friendship + Fairy Move).
        - Option to use hold evolution items directly like stones.
    - Hidden Abilities.
        - Available via Ability Patch.
        - Compatible with Ghoul's DexNav branch.
    - All gender differences.
        - Custom female icons for female Hippopotas Hippowdon, Pikachu and Wobbufett
    - 3 Perfect IVs on Legendaries, Mythicals and Ultra Beasts.
- ***Customizable form change tables. Full list of methods [here](/include/constants/form_change_types.h).***
    - Item holding (eg. Giratina/Arceus)
    - Item using (eg. Oricorio)
        - Time of day option for Shaymin
    - Fainting
    - Battle begin and end (eg. Xerneas)
        - Move change option for Zacian/Zamazenta
    - Battle end in terrains (eg. Burmy)
    - Switched in battle (eg. Palafin)
    - HP Threshold (eg. Darmanitan)
    - Weather (eg. Castform)
    - End of turn (eg. Morpeko)
    - Time of day (Shaymin)
- ***Breeding Improvements***
    - Incense Baby Pokémon now happen automatically (configurable).
    - Level 1 eggs (configurable).
    - Poké Ball inheriting (configurable).
    - Egg Move Transfer, including Mirror Herb (configurable).
    - Nature inheriting 100% of the time with Everstone (configurable)
    - Gen6+ Ability inheriting (configurable).
- ***Items from newer Generations. Full list [here](/include/constants/items.h).***
    - ***Gen 6+ Exp. Share*** (configurable)
    - Berserk Gene
    - Most battle items from Gen 4+
    - Existing item data but missing effects:
        - Mints
        - Dynamax Candy
        - Mulches
        - Gimmighoul Coin
        - Booster Energy
        - Tera Shards
        - Tera Orb
- ***Feature branches incorporated (with permission):***
    - [RHH intro credits](https://github.com/Xhyzi/pokeemerald/tree/rhh-intro-credits) by @Xhyzi.
        - A small signature from all of us to show the collective effort in the project :)
    - [Overworld debug](https://github.com/TheXaman/pokeemerald/tree/tx_debug_system) by @TheXaman
        - May be disabled.
        - Accesible by pressing `R + Start` in the overworld by default.
        - **Additional features**:
            - *Clear Boxes*: cleans every Pokémon from the Boxes.
            - *Hatch an Egg*: lets you choose an Egg in your party and immediately hatch it.
    - [HGSS Pokédex](https://github.com/TheXaman/pokeemerald/tree/tx_pokedexPlus_hgss) by @TheXaman
        - May be disabled.
        - **Additional features**:
            - *Support for new evolution methods*.
            - *Dark Mode*.
    - [Nature Colors](https://github.com/DizzyEggg/pokeemerald/tree/nature_color) in summary screen by @DizzyEggg
- ***Other features***
    - Pressing B while holding a Pokémon drops them like in modern games (configurable).
    - Running indoors (configurable).
    - Configurable overworld poison damage.
    - Configurable flags for disabling Wild encounters and Trainer battles.
    - Configurable flags for forcing or disabling Shinies.
    - Reusable TM (configurable).
    - B2W2+ Repel system that also supports LGPE's Lures
    - Gen6+'s EV cap.
    - All bugfixes from pret included.
    - Fixed overworld snow effect.

There are some mechanics, moves and abilities that are missing and being developed. Check [the project's milestones](https://github.com/rh-hideout/pokeemerald-expansion/milestones) to see which ones.


### [Documentation on features can be found here](https://github.com/rh-hideout/pokeemerald-expansion/wiki)

## If I already have a project based on regular pokeemerald, can I use pokeemerald-expansion?
Yes! Keep in mind that we keep up with pret's documentation of pokeemerald, which means that if your project a bit old, you might get merge conflicts that you need to solve manually.
- If you haven't set up a remote, run the command `git remote add RHH https://github.com/rh-hideout/pokeemerald-expansion`.
- Once you have your remote set up, run the command `git pull RHH master`.

With this, you'll get the latest version of pokeemerald-expansion, plus a couple of bugfixes that haven't been released into the next patch version :)

## **How do I update my version of pokeemerald-expansion?**
- If you haven't set up a remote, run the command `git remote add RHH https://github.com/rh-hideout/pokeemerald-expansion`.
- Once you have your remote set up, run the command `git pull RHH expansion/1.7.4`.

### Please consider crediting the entire [list of contributors](https://github.com/rh-hideout/pokeemerald-expansion/wiki/Credits) in your project, as they have all worked hard to develop this project :)

## There's a bug in the project. How do I let you guys know?
Please submit any issues with the project [here](https://github.com/rh-hideout/pokeemerald-expansion/issues). Make sure that the issue wasn't reported by someone else by searching using the filters.

## Can I contribute even if I'm not a member of ROM Hacking Hideout?

Yes! Contributions are welcome via Pull Requests and they will be reviewed by maintainers. Don't feel discouraged if we take a bit to review your PR, we'll get to it.

## Who maintains the project?

The project was originally started by DizzyEgg alongside other contributors.

The project has now gotten larger and DizzyEgg is now maintaining the project as part of the ROM Hacking Hideout community. Some members of this community are taking on larger roles to help maintain the project.

## What is the ROM Hacking Hideout?

A Discord-based ROM hacking community that has many members who hack using the disassembly and decompilation projects for Pokémon. Quite a few contributors to the original feature branches by DizzyEgg were members of ROM Hacking Hideout. You can call it RHH for short!

[Click here to join the RHH Discord Server!](https://discord.gg/6CzjAG6GZk)
