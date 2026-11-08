using DXForgeEditor.Components;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace DXForgeEditor.Editors
{
    /// <summary>
    /// TransformView.xaml の相互作用ロジック
    /// </summary>
    // NOTE: 値の変更とエンジンへの送信は、インスペクターの編集(MSTransform)も Undo/Redo(ここのアクション)も
    //       Transform.ApplyPositions/ApplyRotations/ApplyScales を通る(Primal Day 086、docs/design/transform-editing.md)
    public partial class TransformView : UserControl
    {
        // 編集を始めたときの値に戻すアクション(操作の前の値を覚えておく)。値の種類ごとに持つ(オフセットの行は同じ種類の値を変える)。
        // NOTE: 1 つにすると、Position に入力して Enter を押さずに Rotation を押したとき、Rotation の PreviewMouseLeftButtonDown が
        //       Position の確定(PreviewLostKeyboardFocus)より先に来て上書きし、Position の Undo が回転を戻すだけになる(#248 の Codex の指摘)
        private Action? _positionUndo;
        private Action? _rotationUndo;
        private Action? _scaleUndo;

        public TransformView()
        {
            InitializeComponent();
        }

        // 今の選択の値を覚え、あとでその値に戻す(エンジンにも送る)アクションを作る
        private Action? GetAction<T>(Func<Transform, T> selector, Action<List<(Transform transform, T value)>> apply)
        {
            if (DataContext is not MSTransform vm) return null;

            var values = vm.SelectedComponents.Select(x => (x, selector(x))).ToList();
            return () =>
            {
                apply(values);
                // Transform は削除できないため、すべてのエンティティが MSTransform を持つ
                (GameEntityView.Instance.DataContext as MSEntity)?.GetMSComponent<MSTransform>()?.Refresh();
            };
        }

        private Action? GetPositionAction() => GetAction(x => x.Position, Transform.ApplyPositions);

        // クォータニオン(正)と度数法の手がかりの両方を戻す
        private Action? GetRotationAction() => GetAction(x => (x.RotationQuaternion, x.RotationEulerHint),
            values => Transform.ApplyRotations(values.Select(x => (x.transform, x.value.RotationQuaternion, x.value.RotationEulerHint)).ToList()));

        private Action? GetScaleAction() => GetAction(x => x.Scale, Transform.ApplyScales);

        // NOTE: VectorBox の ValueChanged は操作の確定(ドラッグを離す、入力の確定)ごとに 1 回出る(Primal Day 086)。
        //       そのときにはもう値が変わっているので、操作の前の値はマウスのボタンを押したときかフォーカスを得たときに覚えておく
        private static void RecordAction(ref Action? undoAction, Action? redoAction, string name)
        {
            if (undoAction != null && redoAction != null)
            {
                Project.UndoRedo.Add(new UndoRedoAction(undoAction, redoAction, name));
            }
            // 同じフォーカスのまま続けて確定したときは、いま確定した値が次の操作の前の値になる
            undoAction = redoAction;
        }

        // オフセットの行は確定のたびに 0 に戻す(次の操作は、動かしたあとの位置・回転からの差分になる)
        private void ResetOffsets()
        {
            (DataContext as MSTransform)?.ResetOffsets();
        }

        private void OnPosition_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _positionUndo = GetPositionAction();
        private void OnPosition_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _positionUndo = GetPositionAction();
        private void OnRotation_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _rotationUndo = GetRotationAction();
        private void OnRotation_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _rotationUndo = GetRotationAction();
        private void OnScale_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _scaleUndo = GetScaleAction();
        private void OnScale_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _scaleUndo = GetScaleAction();

        private void OnPosition_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _positionUndo, GetPositionAction(), "Position Changed");
        private void OnRotation_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _rotationUndo, GetRotationAction(), "Rotation Changed");
        private void OnScale_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _scaleUndo, GetScaleAction(), "Scale Changed");

        private void OnLocalPosition_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e)
        {
            RecordAction(ref _positionUndo, GetPositionAction(), "Position Changed (local offset)");
            ResetOffsets();
        }

        private void OnRotationOffset_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e)
        {
            var frame = (DataContext as MSTransform)?.IsLocalRotation == false ? "world" : "local";
            RecordAction(ref _rotationUndo, GetRotationAction(), $"Rotation Changed ({frame} offset)");
            ResetOffsets();
        }
    }
}
