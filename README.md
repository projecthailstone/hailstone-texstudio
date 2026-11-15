# Hailstone Texture Studio

Texture Studio builds an HD texture pack from your own game files, on your own graphics card, for the
[Hailstone](https://github.com/projecthailstone/hailstone-client) EverQuest client. The textures are upscaled to two or
four times their size with the PBRify upscaler, with normal and roughness maps for Hailstone's modern lighting. You
don't need Python, PyTorch, ROCm or CUDA: it runs on any graphics card with a Vulkan driver, or slowly on the CPU. The
pack is made from your game's files, so it is built on each computer and never shipped.

It is a Hailstone **tool add-on**: a program of its own, installed and kept up to date by Hailstone's add-on manager.

## Install it

You need a Hailstone that installs tools (the client's `docs/addons.md` "Tools"). On the login screen, open
**Add-ons** and paste:

```
https://github.com/projecthailstone/hailstone-texstudio
```

Look at what it is, then press **Install**. It goes in the Hailstone folder's `tools/texstudio/`. Start it with
**Start** on its row in **My add-ons**, or in the game from **Options**, Graphics, Textures, **Texture Studio...**.

## Using it

1. **Game files:** the folder Hailstone plays from is filled in (`hailstone.ini` `assets.path`). **Browse** picks
   another. With "Include my servers' own files", the textures a server ships (`servers/<server>/assets`) are made too.
   The studio reads the archives once, which takes about a minute, and remembers what it found until they change.
2. **Quality:**
   - *Fast*: twice the size, colour only.
   - *Standard*: twice the size, with the normal and roughness maps the modern lighting uses. Hailstone was tested
     with this pack.
   - *High*: four times the size, with the maps. It takes four times Standard's disk space and video memory.
   - *Custom*: any installed upscaler, 1x to 4x, with the maps on or off.

   **Runs on** picks the GPU, or the CPU, which is dozens of times slower. The studio times the device once on ten
   textures, so the estimate below can say how long a build will take.
3. **What to build:** everything, or the zones you choose. The *shared* group holds the characters, equipment, sky
   and spells that every zone loads. With a slow GPU, start with the zones you play.
4. **Pack:** its name is the name of a folder in Hailstone's `textures/`. The estimate gives the number of textures to
   make, the time, the size on disk (about as much video memory as Hailstone shows of it) and the free space. A build
   that would not fit refuses to start.
5. **Build the pack.** Pause, Resume and Stop work at any time. Closing the studio keeps what has been made, and the
   next build carries on from there. *Low impact* rests the GPU for as long as it has worked and runs a quarter of the
   encoders. A game or a desktop beside it then stays smooth, and the build takes about twice as long.
6. **Use this pack in Hailstone** sets the pack Hailstone uses (`display.texture_folder`); so does Options, Graphics,
   Textures in the game. Hailstone reads it at its next start.

The **Preview** shows one of your textures made with the settings you chose. Drag across it to compare the original
with the HD version, look at the normal and roughness maps, or tile it 3 x 3 to check that its edges meet. **Another
texture** picks one at random; **Find** looks a texture up by part of its name.

**Your packs** lists the packs in `textures/`, with the settings and size each was built with, and "Off". Click one
to make it the pack Hailstone uses.

### Photo ground materials

With **Photo ground materials** on, the ground of the zones the studio has rules for is rebuilt from free photo
textures instead of upscaled. Grass, rock, dirt, mud and flagstone paths keep the original's layout and colour, so
the rebuilt tiles still meet the tiles round them, and gain the photos' detail, with normal and roughness maps. In
cities only the ground is rebuilt (streets, grass, dirt, rock), not walls or roofs. The studio has rules for
the outdoor zones of the original game (no expansions, and not the ones revamped later): West, North, South and
East Karana, Qeynos Hills, Lake Rathetear, the Rathe Mountains, the Gorge of King Xorbb, Kithicor Forest, Everfrost,
North Ro, Erud's Crossing, Greater and Lesser Faydark, Butcherblock, Steamfont, Dagnor's Cauldron; the outdoor cities
(North and South Qeynos, Surefall Glade, Halas, Rivervale, Grobb, Oggok, Erudin, Felwithe, Ak'Anon); Crushbone,
Castle Mistmoore, Kerra Isle and the Estate of Unrest; and the Planes of Sky, Fear and Hate (the Plane of Hate's ground is all stonework, so only its trees would change: it has none). The original versions of
zones revamped later, where the game folder still has them: West, North and East Freeport, Nektulos Forest,
Innothule Swamp and the Feerrott, with the Arena and Cazic-Thule. Jaggedpine Forest, Stonebrunt Mountains and the
Warrens, added before Kunark. And the dungeons and cave cities, where only natural rock and earth change: Neriak,
Kaladim, Paineel, the Erudin palace, Upper and Lower Guk, Solusek's caves A and B, Permafrost, Najena, Befallen,
Blackburrow, Runnyeye, the Hole, High Keep, Splitpaw, the Qeynos aqueducts and Kedge Keep (all its ground is
animated, so it keeps the upscaler).

The textures come from [Poly Haven](https://polyhaven.com) (CC0). The first build with this option downloads about
60 MB of them into the studio's cache, and later builds use the cache. A rebuilt tile is 512 times the pack's size on
a side (1024 for a Standard pack). The rules are text files in the studio's `materials/` folder: `materials.txt` names
the materials and their Poly Haven textures, and a file per zone (`qeytoqrg.txt`) says which of the zone's textures
are made of which, and may set a material's colour in that zone's art (`colour grass = 0.30 0.27 0.12`). The
client's `docs/graphics.md` "Photo materials" says how a tile is made. Changing the rules makes
the affected tiles again on the next build, and turning the option off gives them back to the upscaler.

### Generated trees

With **Generated trees** on, the trees of the zones the studio has tree rules for are grown again as 3D models in
place of the old ones, a trunk under a few crossed flat cards. Each new tree stands on the old trunk at about its
width, so the collision (which keeps the old trunk) matches what you see, and fills the shape the old leaves showed,
in their colours. Pines grow whorls of needle sprays, broadleaf trees grow branches into the crown, willows hang
strands, dead trees grow bare forked limbs and palms a crown of fronds. The bark is a Poly Haven photo texture (CC0), fetched with the photo materials. The trees go into the pack's
`models/` folder; Hailstone draws them with the pack. The zones with ground rules have tree rules too (37 of them have
trees to replace): lines such as `tree tree71 = willow bark` in their zone files (the client's `docs/graphics.md` "Generated trees"). The trees are grown
again on every build, which takes a few seconds a zone.

### Speed and size (RX 9060 XT)

- The live game folder of 2026-11 holds 68,582 distinct textures in 3,839 archives. Standard estimates 4 h 36 min and
  65 GB for all of them.
- One zone (Greater Faydark: 161 textures, 483 files, 132 MB) takes 40 to 50 s. The GPU and the CPU's BC7 encoders
  share the work, and on a 20-thread CPU the GPU sets the pace.
- An integrated GPU (Intel Iris Xe), NVIDIA cards and Windows' drivers have not been measured yet.

## Command line

```sh
texstudio [--data-dir DIR]
texstudio --build [--pack NAME] [--preset fast|standard|high] [--zones KEY,...] [--device N|cpu]
          [--assets DIR] [--low-impact] [--materials on|off] [--trees on|off]
texstudio --devices
texstudio --frames N [--screenshot FILE]
texstudio --version
```

- **The Hailstone folder:** Hailstone passes `--data-dir` when it starts the studio. Started by hand from
  `tools/texstudio/`, the studio works on the Hailstone folder two levels up.
- **`--build`** builds without a window. It uses the window's saved settings except for the options given, and saves
  none of those options. `--zones` takes zone short names and `shared`. The progress is printed every five seconds.
  It exits with 0 when every texture was made. On Windows it writes to the console it was started from.
- **`--materials`** turns the photo ground materials on or off for this build.
- **`--trees`** turns the generated trees on or off for this build.
- **`--devices`** lists the devices that `--device` numbers.
- **`--frames`** quits after N frames, saving the last one as a PNG with `--screenshot` (a smoke test).

## How it works

The rules for packs are the client's `src/texpack`, which the client's `tools/texpack` shares (its
`docs/graphics.md` "Built packs"):

- **The scan:** every texture in the archives, once per content.
- **The upscale:** 16 texels of padding (wrapped for surfaces that tile, mirrored for character skins and equipment),
  384-texel tiles feathered over 16 texels, and transparent texels bled over before the model. The alpha is Lanczos
  scaled, and the maps are made from the upscaled colour. Data maps and textures of 16 texels or less are only
  Lanczos scaled.
- **The build** runs in three overlapping stages: a reader thread, one GPU thread that owns the models, and a pool of
  BC7/BC5/BC4 encoders at low priority. What the pack already holds is not made again.
- **`pack.ini`** says what the pack was built with.

This repository holds:

- `src/ncnn_model.*`: the models on [ncnn](https://github.com/Tencent/ncnn), on the GPU through Vulkan compute, or on
  the CPU. It stores fp16 where the device can, and computes in fp32 (see Accuracy).
- `src/studio.*`: the studio's state. That covers the settings in `texstudio/texstudio.ini`, the scan cache in
  `cache/texstudio/`, the device and models, the estimate, the preview and the build.
- `src/studio_lua.cpp`: the `studio` table the window uses.
- `src/main.cpp`: the window (the client's SDL3, Vulkan and ImGui layers) and the command line.
- `ui/modern/tools/texstudio.lua`: the window, drawn by the client's Lua UI with the modern pack's `lib/` (copied in
  from the client at build time). It takes the player's palette and font from the modern pack's settings at each
  start, so it looks like their client.

### Accuracy (2026-11-05)

- **The models alone:** the three PBRify models converted to ncnn (pnnx 20261020) were compared with PyTorch fp32 on
  150 random textures. ncnn's Vulkan fp16 storage averages 71.6, 72.3 and 73.1 dB (SPAN 4x, NormalV3, RoughnessV2),
  and 64.3, 65.6 and 56.4 dB at worst. PyTorch's own fp16 path averages 69.8 to 69.9 dB. Below about 59 dB a
  difference is less than one 8-bit step.
- **The whole pipeline:** Greater Faydark was built by the studio and compared with the PyTorch pack's uncompressed
  outputs. The studio's DDS files average 45.4 dB, the PyTorch pack's own DDS files 45.5 dB: the difference comes from
  BC7, not the pipeline.

## Models

The shipped models are in `tools/texstudio/models/`. Each is an ncnn `<id>.param` and `<id>.bin`, with `<id>.ini`
saying what it is (`title`, `kind` color, normal or rough, `scale`, `license`, `source`, `note`). Your own models go
in the Hailstone folder's `texstudio/models/` (Custom, **Add models...**); one with the same id replaces the shipped
one. Any image model ncnn can run works as a colour model: RGB in, RGB out, any size.

The presets use `4x-PBRify_UpscalerSPANV4`, `1x-PBRify_NormalV3` and `1x-PBRify_RoughnessV2` by Kim2091 (CC0-1.0,
[PBRify_Remix](https://github.com/Kim2091/PBRify_Remix)). The `.ini` files are in this repository; the converted
models are only in the releases. To make them:

```sh
uv venv -p 3.12 venv && uv pip install -p venv/bin/python torch spandrel pnnx
venv/bin/python -I tools/convert_models.py <folder of the PBRify .pth files> <out folder>
```

## Building

The studio builds against a checkout of the client:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DHAILSTONE_CLIENT_DIR=../hailstone-client -DTEXSTUDIO_MODELS=<models>
cmake --build build
ctest --test-dir build
build/dist/texstudio --data-dir <a Hailstone folder>
```

`HAILSTONE_CLIENT_DIR` defaults to `../hailstone-client`. Only the client's libraries are built, not the client
itself. ncnn and its glslang are fetched; their first build takes a few minutes. `build/dist/` is what the add-on
installs into `tools/texstudio/`.

**Releases** are made by `tools/release.sh <version> [--publish]`. It builds Linux in the client's Ubuntu 24.04
image and Windows with MSVC under Wine (the client's `docs/ci.md`), packs both with the client's
`tools/make_addon.py`, signs the manifest with the add-on key and uploads a GitHub release.

## License

MIT (LICENSE). The shipped models are CC0-1.0, and ncnn is BSD-3-Clause (THIRD_PARTY.md).
