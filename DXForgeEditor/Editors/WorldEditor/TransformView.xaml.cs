using DXForgeEditor.Components;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;

namespace DXForgeEditor.Editors
{
    /// <summary>
    /// TransformView.xaml の相互作用ロジック
    /// </summary>
    public partial class TransformView : UserControl
    {
        // 編集を始めたときの値に戻すアクション(操作の前の値を覚えておく)。行ごとに持つ。
        // NOTE: 1 つにすると、Position に入力して Enter を押さずに Rotation を押したとき、Rotation の PreviewMouseLeftButtonDown が
        //       Position の確定(PreviewLostKeyboardFocus)より先に来て上書きし、Position の Undo が回転を戻すだけになる(Codex の指摘)
        private Action? _positionUndo;
        private Action? _rotationUndo;
        private Action? _scaleUndo;

        public TransformView()
        {
            InitializeComponent();
        }

        private Action? GetAction<T>(Func<Transform, (Transform transform, T)> selector,
            Action<(Transform transform, T)> forEachAction)
        {
            if (!(DataContext is MSTransform vm)) return null;

            var selection = vm.SelectedComponents.Select(x => selector(x)).ToList();
            return new Action(() =>
            {
                selection.ForEach(x => forEachAction(x));
                // Transform は削除できないため、すべてのエンティティが MSTransform を持つ
                (GameEntityView.Instance.DataContext as MSEntity)?.GetMSComponent<MSTransform>()!.Refresh();
            });
        }

        private Action? GetPositionAction() => GetAction((x) => (x, x.Position), (x) => x.transform.Position = x.Item2);

        // クォータニオン(正)と度数法の手がかりの両方を戻す
        private Action? GetRotationAction() => GetAction((x) => (x, (x.RotationQuaternion, x.RotationEulerHint)),
            (x) => x.transform.SetRotation(x.Item2.RotationQuaternion, x.Item2.RotationEulerHint));

        private Action? GetScaleAction() => GetAction((x) => (x, x.Scale), (x) => x.transform.Scale = x.Item2);

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

        private void OnPosition_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _positionUndo = GetPositionAction();
        private void OnPosition_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _positionUndo = GetPositionAction();
        private void OnRotation_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _rotationUndo = GetRotationAction();
        private void OnRotation_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _rotationUndo = GetRotationAction();
        private void OnScale_VectorBox_PreviewMouse_LBD(object sender, MouseButtonEventArgs e) => _scaleUndo = GetScaleAction();
        private void OnScale_VectorBox_GotKeyboardFocus(object sender, KeyboardFocusChangedEventArgs e) => _scaleUndo = GetScaleAction();

        private void OnPosition_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _positionUndo, GetPositionAction(), "Position Changed");
        private void OnRotation_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _rotationUndo, GetRotationAction(), "Rotation Changed");
        private void OnScale_VectorBox_ValueChanged(object sender, RoutedPropertyChangedEventArgs<Vector4> e) => RecordAction(ref _scaleUndo, GetScaleAction(), "Scale Changed");
    }
}
