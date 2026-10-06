# Shinobi Striker Import for 3ds Max

Import characters and animations straight from **Naruto to Boruto: Shinobi Striker** (PC) into 3ds Max. No extraction
tools, no FBX round trip. The plugin reads the game's `.pak` archives (`NARUTO\Content\Paks`) directly.

*Unofficial fan project. Not affiliated with CyberConnect2, Bandai Namco Entertainment, Masashi Kishimoto / Shueisha or
Autodesk. You need your own copy of the game.*

## Features

- **Characters tab**: 148 characters with their in-game names - every master and DLC character (Naruto, Sasuke,
  Kakashi, Kawaki, Madara (Six Paths), Isshiki, Kaguya ...), alternate versions (Next, Last Battle, Young Ver. ...),
  NPC ninja, summons and tailed beasts (Kurama, Gamabunta, Gyuki ...), plus a ready-made **male and female avatar**
  (head, hair, jacket, pants from the character creator).
- **Single meshes**: every skeletal mesh in the game (1,760) in tabs - hero meshes, avatar parts (1,177 clothing
  pieces, hair, headgear), weapons + items, other. Ctrl+click several avatar parts to dress one character.
- Skeleton as Max bones, meshes with all UV channels, normals and vertex colours, Skin with up to 8 influences.
- Materials: the game's toon textures are palette atlases; they are decoded (DXT1/DXT5/BC5 ...) to PNG with
  their normal maps. Avatar clothing is tinted with the game's colour masks and its default colours
  (`DT_PartsColors`), just like the character creator.
- **Animation window**: all 19,978 animations, filtered to the selected character's skeleton (or all of them),
  search, root motion on/off, **Load all to timeline** with a note track and `NeoDexSequenceData` sequence
  attributes. Avatar animations (3,677 clips) also play on most masters, matching bones by name.
- Animations use UE4's classic key compression (per-track, constant and variable key lerp, all quantisation
  formats); every clip in the game decodes (19,978 of 19,978).

## Requirements

- 3ds Max 2016 – 2027 (one plugin build per version; fully tested in 3ds Max 2026)
- Naruto to Boruto: Shinobi Striker (Steam) installed. The game folder is found automatically.

## Usage

1. Menu **Shinobi Striker Tool → Import Shinobi Striker**. The first start builds an index (about 10–20 s).
2. Tab **Characters** → pick a character → **Import** (or single meshes in the other tabs).
3. **Animations…** → pick a clip → **Load**, or select several → **Load all to timeline**.

MAXScript: `ShinobiCpp.importFigure "" "Naruto Uzumaki" 3`, `ShinobiCpp.importCharacter "" "path1;path2" 3`,
`ShinobiCpp.applyAnimation "" path true`, `ShinobiCpp.loadAnimations "" "path1;path2" 10`, `ShinobiCpp.showDialog()`,
`ShinobiCpp.showAnimDialog()`.

Files: settings, log and texture cache in `%LOCALAPPDATA%\NSImport`.

### Pak encryption

The pak index of Shinobi Striker is AES-256 encrypted. The plugin contains the key that has been public in the
modding community for years (the same key UE viewers such as umodel and FModel use for this game). Should a game
update ever change it, set the environment variable `NSIMPORT_AES` to the new key (32 characters or `0x` + 64 hex
digits).

## Building

`BUILD.bat` builds every version whose SDK is installed, `BUILD.bat 2026` only one (Visual Studio 2022/2026 with
CMake). `nsdump.exe` (no SDK needed): `cmake -B build_tool -A x64` – a command line reader for checking the formats
(`nsdump meshtest`, `nsdump animtest`, `nsdump katalog` ...).

## Third-party code

- [bcdec](https://github.com/iOrange/bcdec) – MIT / Unlicense
- [miniz](https://github.com/richgel999/miniz) – MIT

## License

GPL-3.0 with a linking exception for the 3ds Max SDK (see `LICENSE` and `LICENSE-EXCEPTION.md`).
