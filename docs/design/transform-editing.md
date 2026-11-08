# Design: editing transforms in the inspector

<!-- last-reviewed: 2026-11-08 -->

- Status: Implemented in #244 PR 2b (port of Primal Day 086's editor side, `db1c19d`)
- Decided: 2026/11/08, by the user ([#244 comment](https://github.com/nel2116/DXForgeEX/issues/244#issuecomment-6719814045)); the fate of the DLL's unused `local`/`world` modes is #247
- Code: `DXForgeEditor/Components/Transform.cs` (`Transform.Apply*`, `MSTransform`), `DXForgeEditor/Editors/WorldEditor/TransformView.xaml(.cs)`; test `DXForgeEditor/Tests/transform_edit_test.ps1`
- Related: `rotation-units.md` (quaternion is the truth, degree Euler hint), `number-box.md` (the boxes and their events)

## Rules

1. **C# owns the values.** The editor's `Transform` (`Position`, `RotationQuaternion` + `RotationEulerHint`, `Scale`) is the truth. Values flow C# → engine only; the editor never reads the engine's transform back into the model. `EngineAPI.EntityAPI.Get*` is used only by tests.
2. **One place sends to the engine.** `Transform.ApplyPositions`/`ApplyRotations`/`ApplyScales` set the C# values of a whole selection and send them with one `SetPosition`/`SetRotation`/`SetScale` call (one lock) for the entities that exist in the engine (`ID.IsValid(Owner.EntityId)`). Both the inspector (`MSTransform.UpdateComponents`) and Undo/Redo (`TransformView`'s actions) call them, so an undo also moves the entity in the viewport. Entities without an engine entity (inactive scene, headless `dxforge`) change in C# only, and nothing calls the DLL when no selected entity is in the engine. A `false` from the DLL (a removed ID) is logged as a warning, never thrown or asserted: these paths run from inspector events and undo commands.
3. **Deltas are computed in C# and sent as absolute values** (`TransformSpace.Absolute`). The Local Offset row moves along each entity's local axes: `p' = p + Vector3.Transform(delta, q)`. The Rotation Offset row turns by a degree Euler delta `Δ = EulerRotation.FromDegrees(d)`: local is `Quaternion.Concatenate(Δ, q)` (= the engine's `XMQuaternionMultiply(Δ, q)`), world is `Quaternion.Concatenate(q, Δ)` (= `XMQuaternionMultiply(q, Δ)`), normalized. The hint is regenerated from the new quaternion (`EulerRotation.ToDegrees`), so it never disagrees with it. No new rotation unit is introduced.
4. **The DLL's `local`/`world` modes stay as PR 1 left them** and are unused by the editor. `transform_edit_test.ps1` checks that they give the same result as the editor's offsets (which also checks that the C# `TransformSpace` values match C++). Whether to keep them is #247.
5. **Live updates and undo granularity.** Every `ValueChanging` step of a box (each drag step, each text commit) reaches `MSTransform.UpdateComponents` through the binding and sends to the engine at once. `VectorBox.ValueChanged` (once per commit) records one undo entry. The value before an interaction is captured on mouse press or keyboard focus, per kind of value (position, rotation, scale; the offset rows share their kind), so pressing another row before a typed value commits cannot overwrite it.
6. **Offsets restart at zero.** The offset rows show the amount moved since the selection or the last commit; each step applies only the difference from the previous value, and committing resets them to 0 (`MSTransform.ResetOffsets`, which does not move anything).

## Where DXForgeEX differs from Primal

- Primal sends local/world offsets to the engine as deltas and reads the result back with `GetPosition`/`GetRotation` (Euler degrees) into the C# model. DXForgeEX computes the result in C# (rules 1 and 3): Primal's way needs the same math in C# anyway for entities that are not in the engine, and reading back would reintroduce an Euler conversion in the DLL (#205).
- Primal updates the engine from `MSTransform` for absolute edits and from `TransformComponentView` for undo, with separate code; DXForgeEX has one path (rule 2).
- Primal rounds the boxes' values to 3 decimals (`SetPropertyValue`) and wraps the rotation boxes to 0..360; DXForgeEX does neither (the hint keeps the typed value, `rotation-units.md`).
- The uniform-scale toggle (`IsUniformScale`, on by default, shared by all views as in Primal) sets all three components to the changed one. Primal also recomputes the toggle after an undo; DXForgeEX leaves it as the user set it.
