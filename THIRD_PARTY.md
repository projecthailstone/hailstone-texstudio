# Third-party code and data

| What | License | Use |
|---|---|---|
| hailstone-client (github.com/projecthailstone/hailstone-client) | MIT | built in from a checkout (`HAILSTONE_CLIENT_DIR`): the archive readers, `src/texpack`, the Lua UI, Vulkan and ImGui layers, and the modern UI pack's `lib/`, icons and pictures copied into the release; its own third-party parts (SDL3, Lua, Dear ImGui, bc7enc_rdo ...) are listed in its THIRD_PARTY.md |
| ncnn (github.com/Tencent/ncnn, tag 20261020), with its bundled glslang | BSD-3-Clause (ncnn); glslang's BSD-3-Clause, BSD-2-Clause, MIT and Apache-2.0 parts | the upscaler models on the GPU through Vulkan compute, or the CPU (`src/ncnn_model.cpp`) |
| PBRify models by Kim2091 (github.com/Kim2091/PBRify_Remix): `4x-PBRify_UpscalerSPANV4`, `1x-PBRify_NormalV3`, `1x-PBRify_RoughnessV2` | CC0-1.0 | shipped in the releases, converted to ncnn's format by `tools/convert_models.py` (with pnnx, BSD-3-Clause, on the developer's machine); not in this repository |

The packs the studio builds are made on the player's machine from their own game files and are never shipped.
