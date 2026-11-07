using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace DXForgeEditor.Editors
{
    /// <summary>
    /// インスペクターの Geometry コンポーネントのビュー(Primal Day 080)。
    /// </summary>
    public partial class GeometryComponentView : UserControl
    {
        public GeometryComponentView()
        {
            InitializeComponent();
        }

        /// <summary>
        /// undo/redo で、各エンティティのジオメトリとサブメッシュごとのマテリアルを戻す。
        /// </summary>
        private static void ResetGeometry(List<(Components.Geometry Geometry, Guid Guid, List<AppliedMaterialData>? Materials)> selection)
        {
            if (selection.Count == 0) return;

            // ビューポートから外してからジオメトリを変え、描き直す(MSGeometry.SetGeometry と同じ。#214)
            var entities = selection.Select(x => x.Geometry.Owner).ToList();
            var scene = entities[0].ParentScene;
            var enableList = scene.DisableAndUpdate(entities);
            selection.ForEach(x => x.Geometry.SetGeometry(x.Guid, x.Materials));
            scene.EnableAndUpdate(enableList);

            // NOTE: Primal は MSEntity.CurrentSelection を足して使う。ここでは今のインスペクターの選択を使う
            //       (選択が変わっていて Geometry を持たなければ、更新するものはない)
            (GameEntityView.Instance.DataContext as MSEntity)?.GetMSComponent<MSGeometry>()?.Refresh();
        }

        /// <summary>
        /// コンテンツブラウザからドロップしたジオメトリのアセットに差し替える(undo/redo あり)。
        /// NOTE: Primal は SetGeometry を Task.Run で別のスレッドで呼ぶが、DXForgeEX はアセットとエンジンの呼び出しに
        ///       ロックがない(#171)ので、UI スレッドで呼ぶ。失敗はログに出して戻る(イベントハンドラなので例外を投げない)。
        /// </summary>
        private void OnGeometry_Border_Drop(object sender, DragEventArgs e)
        {
            try
            {
                if (!e.Data.GetDataPresent(DataFormats.FileDrop) || DataContext is not MSGeometry vm) return;
                if (e.Data.GetData(DataFormats.FileDrop) is not string[] files) return;

                var assetInfo = files
                    .Where(x => string.Equals(Path.GetExtension(x), Asset.AssetFileExtension, StringComparison.OrdinalIgnoreCase))
                    .Select(Asset.TryGetAssetInfo)
                    .FirstOrDefault(x => x?.Type == AssetType.Mesh);
                if (assetInfo == null)
                {
                    Logger.Log(MessageType.Warning, "ドロップしたファイルに、ジオメトリのアセットがありません。");
                    return;
                }

                // NOTE: 差し替えるのはプロジェクトに登録したアセット(GUID で探す)。プロジェクトの外のファイルは使えない
                if (AssetRegistry.GetAssetInfo(assetInfo.Guid) == null && DefaultAssets.DefaultGeometry?.Guid != assetInfo.Guid)
                {
                    Logger.Log(MessageType.Warning, $"ジオメトリ {assetInfo.FullName} はこのプロジェクトのアセットではないので、使えません。");
                    return;
                }

                var undoSelection = vm.SelectedComponents
                    .Select(geometry => (geometry, geometry.GeometryGuid, (List<AppliedMaterialData>?)geometry.GetMaterialsData())).ToList();

                vm.SetGeometry(assetInfo.Guid);

                var redoSelection = vm.SelectedComponents
                    .Select(geometry => (geometry, geometry.GeometryGuid, (List<AppliedMaterialData>?)geometry.GetMaterialsData())).ToList();

                Project.UndoRedo.Add(new UndoRedoAction(
                    () => ResetGeometry(undoSelection),
                    () => ResetGeometry(redoSelection),
                    $"ジオメトリを {assetInfo.FullName} にする"));
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "ジオメトリを差し替えられませんでした。");
            }
            finally
            {
                e.Handled = true;
            }
        }

        private void OnTabControl_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            if (sender is TabControl tabControl && tabControl.SelectedIndex == -1 && tabControl.Items.Count > 0) tabControl.SelectedIndex = 0;
        }

        private void OnGeometry_Border_Mouse_LBD(object sender, MouseButtonEventArgs e)
        {
            if (e.ClickCount > 1 && DataContext is MSGeometry vm && vm.GeometryGuid != Guid.Empty)
            {
                var defaultGeometry = DefaultAssets.DefaultGeometry;
                var info = defaultGeometry?.Guid == vm.GeometryGuid ? defaultGeometry : AssetRegistry.GetAssetInfo(vm.GeometryGuid);
                if (info != null) ContentBrowserView.OpenAssetEditor(info);
            }
        }
    }
}
