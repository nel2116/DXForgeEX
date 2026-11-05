using DXForgeEditor.Components;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
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

namespace DXForgeEditor.Editors
{
    /// <summary>
    /// ProjectLayoutView.xaml の相互作用ロジック
    /// </summary>
    public partial class ProjectLayoutView : UserControl
    {
        // NOTE: 選択の undo/redo はエンティティではなく番号で覚える(Primal Day 081)。エンティティを消したあとでも、
        //       ContainerFromItem の null で落ちない
        private List<int> _previousSelectedIndices = [];

        public ProjectLayoutView()
        {
            InitializeComponent();
        }

        private void OnRenameScene_Button_Click(object sender, RoutedEventArgs e)
        {
            if (sender is Button { Tag: TextBox textBox })
            {
                textBox.Visibility = Visibility.Visible;
                textBox.Focus();
            }
        }

        private void OnAddGameEntity_Button_Click(object sender, RoutedEventArgs e)
        {
            var btn = (Button)sender;
            var vm = (Scene)btn.DataContext;
            vm.AddGameEntityCommand.Execute(new GameEntity(vm) { Name = "Empty Entity" });
        }

        private void OnGameEntities_ListBox_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            var listBox = (ListBox)sender;
            // プロジェクトを閉じる途中など、シーンから外れたリストは何もしない
            if (listBox.DataContext is not Scene scene) return;

            var newSelection = listBox.SelectedItems.Cast<GameEntity>().ToList();
            var newSelectedIndices = newSelection.Select(x => scene.GameEntities.IndexOf(x)).ToList();
            var previousSelectedIndices = _previousSelectedIndices;
            _previousSelectedIndices = newSelectedIndices;

            void select(List<int> indices)
            {
                listBox.UnselectAll();
                indices.ForEach(x =>
                {
                    if (listBox.ItemContainerGenerator.ContainerFromIndex(x) is ListBoxItem item) item.IsSelected = true;
                });
            }

            Project.UndoRedo.Add(new UndoRedoAction(
                () => select(previousSelectedIndices),   // undo Action
                () => select(newSelectedIndices),        // redo Action
                "Selection Changed"
            ));

            MSGameEntity? msEntity = null;
            if (newSelection.Any())
            {

                msEntity = new MSGameEntity(newSelection);
            }
            GameEntityView.Instance.DataContext = msEntity;
        }

        /// <summary>
        /// Content Browser のメッシュのアセットをドロップすると、その名前のエンティティを Geometry 付きで作る(Primal Day 081)。
        /// 失敗はログに出して戻る(イベントハンドラなので例外を投げない)。
        /// </summary>
        private void OnGameEntities_ListBox_Drop(object sender, DragEventArgs e)
        {
            try
            {
                if (sender is FrameworkElement { DataContext: Scene scene } && scene.IsActive && e.Data.GetData(DataFormats.FileDrop) is string[] files)
                {
                    scene.AddGeometryEntities(files);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "ドロップしたアセットからエンティティを作れませんでした。");
            }
            finally
            {
                e.Handled = true;
            }
        }

        /// <summary>
        /// アクティブなシーンのリストが読み込まれたら、先頭のエンティティを選ぶ。空ならインスペクターを空にする(Primal Day 081)。
        /// </summary>
        private void OnGameEntities_ListBox_Loaded(object sender, RoutedEventArgs e)
        {
            var listBox = (ListBox)sender;
            if (!listBox.IsEnabled) return;

            // 前のプロジェクトの選択を、undo の履歴に持ち込まない
            _previousSelectedIndices = [];
            if (listBox.Items.Count > 0)
            {
                listBox.SelectedIndex = 0;
                (listBox.ItemContainerGenerator.ContainerFromIndex(0) as ListBoxItem)?.Focus();
            }
            else
            {
                GameEntityView.Instance.DataContext = null;
            }
        }
    }
}

