# Design: editor number boxes (NumberBox, ScalarBox, VectorBox)

<!-- last-reviewed: 2026-11-08 -->

- Status: Implemented in #244 PR 2a (port of Primal Day 086's control rewrite, `db1c19d`)
- Decided: 2026/11/08, by the user (port the rewrite before the transform editing; [#244 comment](https://github.com/nel2116/DXForgeEX/issues/244#issuecomment-6719814045))
- Code: `DXForgeEditor/Utilities/Controls/NumberBox.cs`, `VectorBox.cs`, `ScalarBox.cs`, `DXForgeEditor/Dictionaries/ControlTemplates.xaml`; test `DXForgeEditor/Tests/number_box_test.ps1`

## What the controls do (same as Primal)

- `NumberBox.Value` is `double?`; `null` means "mixed values" (multi-selection) and shows as empty text with a gray background.
- `Minimum`, `Maximum`, `WrapAround` and `IsInteger` constrain the value. An integer box rounds and then stays in range (with `Maximum` 3.7, 4 becomes 3; with wrapping 0..360, 359.6 becomes 0); if the range holds no integer, the value is only clamped. Dragging is incremental and its speed grows with the value's magnitude (`max(log10|v|, 1)^4`, updated after each commit).
- Two routed events:
  - `ValueChanging` (`RoutedEventHandler`): after every user change (each drag step, each text commit). For live updates.
  - `ValueChanged` (`RoutedPropertyChangedEventHandler<double>`): once per commit (mouse release after a drag, Enter, Tab or clicking elsewhere after typing) with the values before and after the interaction. For one undo entry per interaction.
  - Neither is raised when code or a binding changes `Value`, so refreshing a view model never records undo.
- `VectorBox` forwards `Minimum`/`Maximum`/`WrapAround`/`Multiplier` to its `NumberBox`es and re-raises their events as its own `ValueChanging` and `ValueChanged` (`Vector4` old/new). The inner events are marked handled.
- The template is a `TextBox` only (no `TextBlock` + hidden `TextBox` pair).

## Where DXForgeEX differs from Primal

1. **Wrapping with a non-zero minimum.** Primal's `ClampOrWrap` is `value %= max; if (value < min) value += max;`, correct only for `min == 0` (with -180..180, 190 stays 190). DXForgeEX uses `min + ((value - min) mod (max - min))` with a non-negative remainder, giving [min, max).
2. **Who commits the text.** Primal leaves the commit to the default `TextBox` style (binding with `UpdateSourceTrigger=Explicit`) and tells drags from typing by keyboard focus and mouse capture. In DXForgeEX the `NumberBox` alone reads and writes its `TextBox` (the template's `TextBox` uses `NumberBoxTextBoxStyle`, without the default `TextBox` event setters). Drag and commit are the internal methods `BeginDrag`/`DragTo`/`EndDrag` and `CommitText`, which the mouse and key handlers call and which the headless test calls directly. Text that is not a finite number is rejected and the text goes back to the current value (Primal's binding leaves a validation error).
3. **Constrain before setting.** A user change constrains the value before assigning `Value`. Relying only on `CoerceValueCallback` sends the unconstrained value to a two-way binding's source (the `VectorBox` X kept 190 while the box showed -170).
4. **Drag threshold.** A move up to `SystemParameters.MinimumHorizontalDragDistance` still counts as a click (which starts typing), as in DXForgeEX before; Primal starts dragging on any movement.
5. **Old value of a mixed box.** `ValueChanged.OldValue` is `NaN` when the box was mixed; Primal reports 0.
6. **No wrapping on the rotation boxes.** The inspector's rotation shows the Euler hint (`docs/design/rotation-units.md`), which `EulerRotation.ToDegrees` returns in -180..180 and which keeps the typed value otherwise; Primal's 0..360 `WrapAround` is not used there.
