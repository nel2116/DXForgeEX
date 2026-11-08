# Design: rotation and angle units

<!-- last-reviewed: 2026-11-08 -->

- Status: Implemented. #205 PR 1 (#242) implements points 2, 3 and 5 (editor model, `.dxproj`, dxforge, DLL); #205 PR 2 implements points 4 and 6 (`game.bin` quaternion, `ContentLoader`, FoV in radians)
- Decided: 2026/11/08, by the user
- Issues: #208 (decision), #205 (bug and implementation), #183 (dxforge value conventions, partly superseded)

## Goal

Every path that carries a rotation or angle (editor UI, `.dxproj`, dxforge, the DLL, `game.bin`, the engine API) uses one documented unit, so that values are never interpreted in the wrong unit (#205) and agents/dxforge cannot mix them up.

## Current state (develop dd88644)

- Editor -> engine: Euler **degrees**. `DXForgeEngineDLL/EntityAPI.cpp:56` calls `to_quat(rot, true)`.
- `game.bin` -> engine: Euler **radians**. `DXForgeEngine/Content/ContentLoader.cpp:112` uses `XMQuaternionRotationRollPitchYawFromVector`, while the editor's `Transform.cs` writes its (degree) values as-is. This is the #205 mismatch; Primal has the same one.
- dxforge stores radians and accepts `rotation_deg` / `rotation_rad` (`DXForgeEditor/Automation/SceneOpDocument.cs:286-287`). The #183 comment saying `.dxproj` holds radians predates Day 082 making editor -> engine degrees.
- `.dxproj` has no unit marker or format version, so old values cannot be told apart.
- The engine already stores rotation as a quaternion (`DXForgeEngine/Components/Transform.h:22`), and the public API takes a quaternion (`GameEntity.h` `set_rotation(math::v4 rotation_quaternion)`).
- FoV is stored in "multiples of pi": `Renderer.h:195` default `0.25f`, `D3D12Camera.cpp:333` multiplies by `XM_PI`, `DXForgeEngineDLL/EngineAPI.cpp:722` divides degrees by 180.
- The engine is right-handed (`D3D12Camera.cpp` `*RH` matrices, `FrontCounterClockwise = 1`).
- Primal Day 086 (db1c19d) keeps degrees in the editor and converts in the DLL's `SetRotation`/`GetRotation`; it does not touch `game.bin`, so Primal's mismatch remains.
- Other engines: Unity stores quaternions and shows degree Euler angles; Unreal stores and shows degrees; Godot and Blender use radians internally and degrees in the UI.

## Options considered

- **Display vs. internal unit**: one unit everywhere, or degrees for people and radians for math. Radians in the UI are unreadable; degrees inside the engine make the math tedious.
- **Editor/`.dxproj` storage**: Euler degrees, Euler radians, quaternion only, or quaternion plus an Euler display hint (Unity style). Euler-only loses information for the editor's display (e.g. 180-degree flips) and depends on rotation order; quaternion-only cannot reproduce what the user typed.
- **`game.bin`**: Euler (in either unit) converted at load, or the quaternion as-is.
- **Old projects**: migrate with a format version, or drop old values.
- **Primal Day 086+**: port as-is (entrenches "editor = degrees" and the `game.bin` mismatch) or redesign.
- **Engine public API units**: radians with the unit in parameter names, degree helpers, or unit types.

## Decision

1. **Display in degrees, compute in radians/quaternions.** The UI shows all angles in degrees; the engine works in radians and quaternions.
2. **Rotation truth is a quaternion.** Editor and `.dxproj` store the quaternion plus an Euler hint in **degrees** that only drives display.
   - On load, the quaternion wins: if the hint does not produce the same rotation, regenerate the hint from the quaternion.
   - Writers (dxforge, editor) take Euler degrees and write both the quaternion and the hint.
3. **Euler order** is the current `XMQuaternionRotationRollPitchYaw` convention: x = pitch, y = yaw, z = roll, applied Z -> X -> Y. C# and C++ must agree, enforced by a test.
4. **`game.bin` stores the quaternion** (4 floats). `ContentLoader` drops its Euler conversion; the tests that build `game.bin` (e.g. `EngineUnitTests/HeadlessRender.cpp`) change with it.
5. **No migration.** There are no projects to keep. New data members (e.g. `RotationQuaternion`, `RotationEulerHint`) replace `Rotation`; an old `Rotation` is not migrated but reset to zero with a logged warning.
6. **FoV** is a vertical field of view in radians in the engine; the "x pi" unit goes away (`Renderer.h` default, `D3D12Camera.cpp`, the DLL's `/ 180`). The editor shows degrees.
7. **Engine public API** stays in radians/quaternions, with the unit in parameter names and comments (e.g. `fov_radians`, `rotation_quaternion`). Add degree helpers or unit types only when a real need appears.
8. **Primal divergence**: maintainability and avoiding confusion beat mirroring Primal's code (project-wide policy, see `AGENTS.md`). Day 086 is redesigned on top of these decisions instead of ported as-is.

## Consequences

- #205 implements points 2-6: editor model and `.dxproj` data members, dxforge (`rotation_deg` input, writes both forms; `rotation_rad` may stay as input), DLL conversion, `game.bin` writer and `ContentLoader`, FoV, and a C#/C++ Euler-order agreement test.
- Where the tests are: the C++ Euler order is `euler_order_matches_the_editor` in `EngineUnitTests` (expected values computed with C#'s `Quaternion.CreateFromYawPitchRoll`). Because `game.bin` now carries the quaternion, `ops_cli_test.ps1` section 7 checks that dxforge's `rotation_quat`, the 4 floats in the exported `game.bin` and the engine's loaded rotation are the same values, and `Tests_ContentLoader.cpp` checks that `ContentLoader` keeps a known quaternion. `camera_field_of_view_is_vertical_radians` checks that the default projection equals the old `0.25 * XM_PI` one, and `EngineUnitTests --render` checks that `SetCameraFoV(45)` gives the default projection and 90 degrees gives `pi / 2`.
- The #183 statement that `.dxproj` holds radians is superseded by this record.
- Day 086 (#244, PR 1) adds the DLL's `GetRotation`/`SetRotation` with quaternions only (4 floats per entity): `absolute` sets or reads the stored quaternion as-is (normalized on set), and `local`/`world` take a **delta quaternion** that the engine multiplies onto the stored one about the entity's local axes (`XMQuaternionMultiply(delta, q)`) or the world axes (`XMQuaternionMultiply(q, delta)`). The DLL never converts Euler angles: the editor turns a degree Euler delta (or an axis and angle) into a quaternion with `Utilities/EulerRotation.cs`, in the order of point 3. Primal's Euler-degree `SetRotation(absolute)`/`GetRotation` (`get_euler_angles_from_local_frame`) and `calculate_absolute_rotation` are not ported; Primal's world rotation, which rotates about only one nonzero axis, becomes a plain multiplication by the delta. `SetPosition` takes `absolute`/`local`/`world` too, where `world` (a delta along world axes) is an addition to Primal's `is_local` flag. Positions and scales have no angle unit.
- The editor camera (Day 085) keeps its own radians-internal state via `to_quat(rot, false)`; it is not entity rotation and is unaffected except for FoV.
