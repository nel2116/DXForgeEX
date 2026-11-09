using System;
using System.Collections.Generic;
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
using System.Windows.Shapes;

namespace DXForgeEditor.GameProject
{
    /// <summary>
    /// OpenProjectView.xaml の相互作用ロジック
    /// </summary>
    public partial class OpenProjectView : UserControl
    {
        public OpenProjectView()
        {
            InitializeComponent();

            Loaded += (s, e) =>
            {
                var item = projectListBox.ItemContainerGenerator
                .ContainerFromIndex(projectListBox.SelectedIndex) as ListBoxItem;
                item?.Focus();
            };
        }

        private void OnOpen_Button_Click(object sender, RoutedEventArgs e)
        {
            OpenSelectProject();
        }

        private void OpenSelectProject()
        {
            // The recent-project list can be empty, leaving nothing to open.
            if (projectListBox.SelectedItem is not ProjectData data) return;
            var project = OpenProject.Open(data);
            if (project == null)
            {
                // 画面を閉じるとエディタが終了するので、理由を表示して別のプロジェクトを選べるようにする
                MessageBox.Show($"プロジェクトを読み込めませんでした: {data.FullPath}\n\n理由: {Project.LastLoadError ?? "不明"}", "プロジェクトを開けません", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }
            var win = Window.GetWindow(this);
            win.DataContext = project;
            win.DialogResult = true;
            win.Close();
        }

        private void On_ListBoxItem_Mouse_DoubleClick(object sender, MouseButtonEventArgs e)
        {
            OpenSelectProject();
        }
    }
}
