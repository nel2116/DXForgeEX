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
using System.Windows.Shapes;

namespace DXForgeEditor.GameProject
{
    /// <summary>
    /// NewProjectView.xaml の相互作用ロジック
    /// </summary>
    public partial class NewProjectView : UserControl
    {
        public NewProjectView()
        {
            InitializeComponent();
        }

        private void OnCreate_Button_Click(object sender, RoutedEventArgs e)
        {
            // An empty template list has no project template to create.
            if (templateListBox.SelectedItem is not ProjectTemplate template) return;
            var vm = (NewProject)DataContext;
            var projectPath = vm.CreateProject(template);
            if (string.IsNullOrEmpty(projectPath))
            {
                // 作成に失敗しても画面は閉じない(閉じるとエディタが終了する)。理由を表示して入力し直せるようにする
                if (!string.IsNullOrEmpty(vm.ErrorMsg))
                {
                    MessageBox.Show(vm.ErrorMsg, "プロジェクトの作成に失敗", MessageBoxButton.OK, MessageBoxImage.Error);
                }
                return;
            }

            Project project;
            try
            {
                project = OpenProject.Open(new ProjectData() { ProjectName = vm.ProjectName, ProjectPath = projectPath });
            }
            catch (Exception ex)
            {
                // UIのボタンから呼ばれるので、例外でエディタを落とさない
                Debug.WriteLine(ex.Message);
                MessageBox.Show($"作成したプロジェクトを開けませんでした: {ex.Message}", "プロジェクトを開けません", MessageBoxButton.OK, MessageBoxImage.Error);
                return;
            }
            var win = Window.GetWindow(this);
            win.DataContext = project;
            win.DialogResult = true;
            win.Close();
        }
    }
}
