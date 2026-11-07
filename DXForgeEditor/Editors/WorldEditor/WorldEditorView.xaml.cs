using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.GameDev;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.Specialized;
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
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;

namespace DXForgeEditor.Editors
{
    /// <summary>
    /// WorldEditorView.xaml の相互作用ロジック
    /// </summary>
    public partial class WorldEditorView : UserControl
    {
        public WorldEditorView()
        {
            InitializeComponent();
            Project.SceneUpdated += OnSceneUpdated;
            DataContextChanged += OnWorldEditorDataContextChanged;
        }

        private void OnWorldEditorDataContextChanged(object sender, DependencyPropertyChangedEventArgs e)
        {
            Focus();
        }

        /// <summary>
        /// アクティブなシーンのジオメトリを、ビューポートに描かせる(Primal Day 084)。
        /// NOTE: Primal は 4 つのビューポート(sv1〜sv4)に渡す。DXForgeEX はビューポートが 1 つ(PR #212)
        /// </summary>
        private void OnSceneUpdated(object? sender, EventArgs e)
        {
            Debug.Assert((sender as Scene)?.Project == Project.Current);

            if (sender is Scene scene)
            {
                var ids = scene.IsActive ? scene.GetGeometryComponentIds() : [];
                sv1.RenderSurfaceControl.SetComponentIds(ids);
            }
        }

        private void OnNewScript_Button_Click(object sender, RoutedEventArgs e)
        {
            new NewScriptDialog().ShowDialog();
        }

        private void OnCreatePrimitiveMesh_Button_Click(object sender, RoutedEventArgs e)
        {
            new PrimitiveMeshDialog().ShowDialog();
        }

        /// <summary>
        /// エディタの既定アセットのBRDF LUT(プロジェクトのContentの外にある)を、テクスチャエディタで開く。
        /// </summary>
        private void OnViewBrdfIntegrationLut_MenuItem_Click(object sender, RoutedEventArgs e)
        {
            if (DefaultAssets.BrdfIntegrationLut is AssetInfo info && System.IO.File.Exists(info.FullPath))
            {
                ContentBrowserView.OpenAssetEditor(info);
            }
            else
            {
                Logger.Log(MessageType.Error, $"BRDF LUTの既定アセットがありません: {DefaultAssets.BrdfIntegrationLutFile}");
            }
        }

        private void UnloadAndCloseAllWindows()
        {
            var mainWindow = Application.Current.MainWindow;

            foreach (Window win in Application.Current.Windows)
            {
                if (win != mainWindow)
                {
                    win.Close();
                }
            }

            // NOTE: 順番が大事(Primal Day 084)。DataContext を外してビューポートの host を Dispose し(render thread が止まる)、
            //       そのあとでプロジェクトを閉じて、エンジンからエンティティとリソースを消す(#214)
            mainWindow.DataContext = null;
            Project.Current?.Unload();
            mainWindow.Close();
        }

        private void OnNewProject(object sender, ExecutedRoutedEventArgs e)
        {
            ProjectBrowserDialog.GotoNewProjectTab = true;
            UnloadAndCloseAllWindows();
        }

        private void OnOpenProject(object sender, ExecutedRoutedEventArgs e) => UnloadAndCloseAllWindows();

        private void OnEditorClose(object sender, ExecutedRoutedEventArgs e)
        {
            Application.Current.MainWindow.Close();
        }

        private void OnBackgroundClick(object sender, MouseButtonEventArgs e)
        {
            // サブメニューを閉じる
            if (FocusManager.GetFocusedElement(this) is MenuItem menuItem)
            {
                menuItem.IsSubmenuOpen = false;
            }
        }

        private void OnContentBrowser_Loaded(object sender, RoutedEventArgs e) => OnContentBrowser_IsVisibleChanged(sender, default);

        private void OnContentBrowser_IsVisibleChanged(object sender, DependencyPropertyChangedEventArgs e)
        {
            if (((FrameworkElement)sender).DataContext is ContentBrowser contentBrowser && string.IsNullOrEmpty(contentBrowser.SelectedFolder?.Trim()))
            {
                contentBrowser.SelectedFolder = contentBrowser.ContentFolder;
            }
        }

        // NOTE: Primal は 4 つのビューポートと MSEntity.CurrentSelection を使う。ここではビューポートが 1 つで、今のインスペクターの選択を使う。
        private void OnProjectLayoutView_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.F && e.OriginalSource is not TextBox &&
                GameEntityView.Instance.DataContext is MSEntity selection && selection.SelectedEntities.Count > 0)
            {
                var avgPos = Vector3.Zero;
                foreach (var entity in selection.SelectedEntities)
                {
                    avgPos += entity.GetComponent<Components.Transform>()?.Position ?? Vector3.Zero;
                }

                avgPos /= selection.SelectedEntities.Count;

                sv1.RenderSurfaceControl.FocusPosition(avgPos);
            }
        }
    }
}
