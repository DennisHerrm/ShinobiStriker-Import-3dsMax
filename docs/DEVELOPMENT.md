# Shinobi Striker Import – developer notes

## Layout

| Part | What it does |
|---|---|
| `src/ns_io.*` | Pak archives (version 4), AES-256-ECB index via Windows BCrypt, zlib entries via miniz |
| `src/ns_paket.*` | UE 4.16 packages (`.uasset` + `.uexp`): summary, name/import/export maps, tagged properties, bulk data |
| `src/ns_mesh.*` | SkeletalMesh LOD0 (sections, GPU skin vertex buffer, skin weight buffer, colours), skeleton, sockets |
| `src/ns_anim.*` | AnimSequence: UE4 key compression (per-track, constant/variable key lerp) resampled to 30 fps |
| `src/ns_figur.*` | Merges several meshes onto one skeleton (leader pose by bone name, socket attachments) |
| `src/ns_tex.*` | Material instances → textures (bcdec → PNG via miniz), avatar colour masks |
| `src/ns_katalog.*` | Index of characters, meshes and animations, display names, avatar presets, cached |
| `src/nsimport_*` | 3ds Max plugin (scene building, the two windows, MAXScript interface `ShinobiCpp`) |
| `tools/nsdump.cpp` | Command line reader without the Max SDK (`meshtest`, `animtest`, `katalog`, `pose`, `textur` ...) |

## Format notes

- Engine 4.16 (`Engine\Build\Build.version`). Two paks, version 4; the footer's flag byte before the magic marks an
  encrypted index. Mount points: `../../../` and `../../../NARUTO/Content/`. Only 39 files are encrypted, none compressed.
- Packages are unversioned cooked (`LegacyFileVersion -7`). The summary includes `SearchableNamesOffset`; export
  entries are 104 bytes, imports 28 bytes, names carry 4 hash bytes. Serial offsets count from the start of the
  `.uasset` with the `.uexp` appended.
- SkeletalMesh: after the properties `bHasGuid`, strip flags, bounds, materials (index, slot name, 24 bytes UV
  channel data), reference skeleton, LOD count. Sections carry the APEX cloth fields (three arrays, cloth asset
  index, clothing data guid + LOD index); several bytes are uninitialised (`0xcd`). The GPU skin vertex is
  `TangentX, TangentZ (unsigned FPackedNormal), Position, UVs (half or float)`; weights are a separate buffer
  (4 or 8 influences). UE front faces are clockwise; after mirroring Y into 3ds Max the index order is kept.
- AnimSequence: `bHasGuid`, raw data guid, strip flags, `bSerializeCompressedData`, four format bytes, track
  offsets, scale offsets (+ strip size), compressed track map, curve data (tagged), byte stream, `bUseRawDataOnly`.
  Per-track data with scale tracks is stored track after track although the offsets point elsewhere (an UE4 bug
  before 4.23) and is rearranged first. Keys are spread evenly over `SequenceLength` and resampled to 30 fps.
- Textures: `FTexturePlatformData` with an int32 skip offset and `NumSlices`; most character textures are inline.
- Materials (CyberConnect2 toon shading, master `M_CustChara`): `BC_Map` is a palette atlas (the UVs point at colour
  swatches), `SC_Map` the shadow palette, `N_Map` the normal map. Avatar clothing adds `ColorMask_RGB`: red = skin,
  green = colour 0, blue = colour 1 (`IDPartsColorDef0/1` of the character creator row, `DT_PartsColors`, FColor BGRA).
- Display names: `L10N/en/Localize/DataTable/*` are CC2 "Spreadsheet" tables (row count, then tagged rows with
  `Id` + string); `Character.<folder>` gives the name (`Character.D21` = Naruto Uzumaki (BORUTO)).
