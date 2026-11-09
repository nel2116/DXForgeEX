# Design: packed material surface and texture inputs

<!-- last-reviewed: 2026-11-09 -->

- Status: Implemented (#252)
- Decided: 2026/11/09, by the coordinator (questions 1-3 and a-d of #252)
- Issues: #252 (Primal Day 087, commit `0475639`), #180 (default texture, open), #247 (per-property sync, open)

## Goal

Port Primal Day 087: the material properties are no longer in `PerObjectData`. They are packed into 12 bytes and passed together with the texture descriptor indices through the root parameter `material_data` (`MaterialData`, `t2`), so each submesh can have its own properties and its textures are optional. Where Primal's code would change the rendered result or depends on a missing feature, DXForgeEX diverges, as recorded below.

## Current state (after #252)

- `graphics::material_surface` (`DXForgeEngine/Graphics/Renderer.h`) is 12 bytes: `u8 base_color[4]`, `u8 emissive[3]`, `u8 metallic`, `u8 roughness`, `u8 input_mask`, `u16 emissive_intensity`. `emissive_intensity` maps 0..65535 to 0..`max_emissive_intensity` (10000, the same as `MaxEmissiveIntensity` in `Shaders/CommonConstants.hlsli` and `Material.MaxEmissiveIntensity` in the editor).
- GPass `prepare_render_frame` (`D3D12GPass.cpp`) writes, per render item, the surface (3 `uint`s) followed by that material's texture descriptor indices into one constant-buffer allocation and always binds it to `material_data`.
- The shaders read the surface with `GetSurfaceData` (`EngineTest/TestShader.hlsl`, `DXForgeEditor/Resources/MaterialEditor/DefaultMaterialShaders.hlsl`): `MaterialData[0]` = base color, `[1]` = emissive + metallic, `[2]` = roughness (bits 0-7), input mask (bits 8-15), emissive intensity (bits 16-31).
- The `.asset` file keeps the float form (`MaterialSurface.ToBinary(writer, asInteger: false)`), so existing material assets load unchanged. Only the data for the engine (`AppliedMaterial.PackForEngine`, `asInteger: true`) is packed. The packed size is pinned in `EngineUnitTests/Tests_ResourceUpload.cpp` and `DXForgeEditor/Tests/material_asset_test.ps1` (`packed_material_size(0)` = 64; `material_init_info` is read without its 4 trailing padding bytes).
- The depth prepass PSO (`create_pso` in `D3D12Content.cpp`) uses the material's vertex shader variant with key 0 (`elements_type::position_only`) when there is no amplification/geometry/hull/domain shader.

## Decision

1. **Defaults stay DXForgeEX's.** Primal Day 087 also changes the editor defaults to BaseColor 0.7/0.2/0.7, Metallic 1, Roughness 0.3 and the engine default to `roughness{ 225 }`, which look like debugging values. DXForgeEX keeps its defaults: editor 0.7 gray / metallic 0 / roughness 0.9, engine white / metallic 0 / roughness 255 (= 1.0) / emissive intensity 7 (the nearest value to the old 1.0).
2. **`MaterialSurface` keeps its `ViewModelBase` properties.** Primal turns them into plain fields. Nothing binds to them today, but keeping them keeps the diff small and leaves the change-notification design to #247.
3. **Colors are packed from linear values.** Primal writes `Color.R` (the sRGB byte) but the shader reads the byte as a linear value, which changes the color. DXForgeEX packs `ScR`/`ScG`/`ScB`/`ScA` (linear), as the float form did.
4. **Rounding, not truncation.** Floats are packed with `(u8)(clamp(x, 0, 1) * 255 + 0.5)` (0.5 → 128, 0.9 → 230) and the emissive intensity with `(u16)(clamp(x, 0, 10000) * 65535 / 10000 + 0.5)`; Primal truncates. The same formula is in the engine (`material_surface::to_unorm8`, `to_emissive_intensity`) and the editor (`MaterialSurface.ToUnorm8`, `ToEmissiveIntensity`). The emissive intensity step is about 0.153, so 1.0 becomes 7 → about 1.068 in the shader. This is Primal's precision; it only matters when the emissive color is not black.
5. **Input mask and texture index order.** Bit i of `input_mask` is set when input i of the applied material (the order of `DefaultMaterialInputs`: Base Color 0, Emissive 1, Normal Map 2, Metallic and Roughness 3, Ambient Occlusion 4) has an asset with a non-empty GUID (`AppliedMaterial.GetInputMask`). Primal sets the bit for every input that is not the default texture and reads input i's index at the fixed `MaterialData[3 + i]`, because every input has a texture (the default texture fills the gaps). DXForgeEX has no default texture (#180), and `GetReferencedAssets` packs only the inputs that have an asset, so `DefaultMaterialShaders.hlsl` reads input i's index at `MaterialData[3 + countbits(mask & (bit_i - 1))]` (`TextureIndex`). This stays correct when a default texture is added later. Tested in `material_asset_test.ps1` item 6 (inputs 1 and 3 → mask `0b01010`, two packed ids).
6. **Depth prepass falls back instead of asserting.** When the material's vertex shader has no key-0 variant, Primal asserts. DXForgeEX writes a warning with `OutputDebugStringW` and uses the drawing vertex shader for the depth PSO. The default material, EngineTest and the headless render test all compile the `ELEMENTS_TYPE=0` variant.
7. **Not ported.** The "Exaggerate emissive color for dramatic effect" block in Primal's `DefaultMaterialShaders.hlsl` (debug look), the switch of `TestShader.hlsl`'s textured path from `LinearSampler` to `AnisotropicSampler` (would change EngineTest's look for no stated reason), Primal's skip of the surface read when every input is textured (a micro-optimization that complicates the code), and the `GameEntityView` `UpdateScene` call (already in DXForgeEX).

## Consequences

- The headless render baselines (`EngineUnitTests/Tests/baselines/render_warp_320x240_{Debug,Release}.png`) were regenerated with `render_run_test.py --update-baselines`. Before that, the new code against the old baselines measured 0 pixels over the 1-level tolerance, 2071 (Debug) / 2069 (Release) pixels with a difference of 1, max difference 1, while origin/develop measured 0 different pixels (max 0) on the same machine, so the difference comes from this change (estimate: quantizing base color/roughness to 8 bits, e.g. roughness 0.9 → 230/255). Keeping the old baselines would leave those pixels with no margin under the tolerance, so they were regenerated; afterwards both configurations measure 0 pixels within, max difference 0.
- Material changes in the editor have no UI yet (no XAML binds `MaterialSurface`), so the conversion is tested headless (`EngineUnitTests` `ResourceUpload_MaterialSurface*`, `material_asset_test.ps1` item 3).
- Any new material shader that samples textures must use the counting rule of point 5, not fixed slots.
- Material `.asset` files store compiled shader bytecode, so a material compiled against the old `PerObjectData` layout would read wrong values. Today the only material asset is the default material, and `DefaultAssets.CreateDefaultMaterial` rebuilds it at startup whenever the compiled shaders' hash differs from the saved one (it does after this change); the editor has no UI to create other materials. A future material editor must recompile saved materials when the shader interface changes (Codex review of #254).
