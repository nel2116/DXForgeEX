using DXForgeEditor.Content;
using DXForgeEditor.DllWrappers;
using DXForgeEditor.GameDev;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;

namespace DXForgeEditor
{
    public partial class MainWindow : Window
    {
        public static string DXForgePath { get; private set; } = string.Empty;
        internal static EnginePathSource EnginePathSource { get; private set; } = EnginePathSource.None;

        public MainWindow()
        {
            InitializeComponent();
            Loaded += OnMainWindowLoaded;
            Closing += OnMainWindowClosing;
        }

        private void OnMainWindowLoaded(object sender, RoutedEventArgs e)
        {
            Loaded -= OnMainWindowLoaded;
            GetEnginePath();
            // エディタの既定アセット(BRDF LUTなど)がなければ作る(Primal Day 075)。
            // 既定のマテリアル用シェーダーはエンジンのシェーダーをインクルードするため、エンジンのパスを決めてからコンパイルする(Primal Day 076)。
            DefaultAssets.GenerateDefaultAssets();
            OpenProjectBrowserDialog();
        }

        private void GetEnginePath()
        {
            // 実行中のエディタのリポジトリを優先し、見つからなければユーザー環境変数DXFORGE_ENGINEを使う(#117)
            var dxforgePath = Environment.GetEnvironmentVariable(EnginePath.EnvironmentVariableName, EnvironmentVariableTarget.User);
            var resolution = EnginePath.Resolve(AppContext.BaseDirectory, dxforgePath);
            if (resolution.Source == EnginePathSource.None)
            {
                var dlg = new EnginePathDialog();
                if (dlg.ShowDialog() == true)
                {
                    DXForgePath = dlg.DXForgePath;
                    EnginePathSource = EnginePathSource.EnvironmentVariable;
                    Environment.SetEnvironmentVariable(EnginePath.EnvironmentVariableName, DXForgePath.ToUpper(), EnvironmentVariableTarget.User);
                }
                else
                {
                    Application.Current.Shutdown();
                    return;
                }
            }
            else
            {
                DXForgePath = resolution.Path;
                EnginePathSource = resolution.Source;
            }
            LogEnginePath();
        }

        // どちらのエンジンのパスを使っているかをLoggerに出す。起動時とゲームのビルド時に呼ぶ
        public static void LogEnginePath()
        {
            var environmentPath = Environment.GetEnvironmentVariable(EnginePath.EnvironmentVariableName, EnvironmentVariableTarget.User);
            var (type, message) = EnginePath.Describe(DXForgePath, EnginePathSource, environmentPath);
            Logger.Log(type, message);
        }

        private void OnMainWindowClosing(object? sender, CancelEventArgs e)
        {
            if (DataContext == null)
            {
                e.Cancel = true;
                Application.Current.MainWindow.Hide();
                OpenProjectBrowserDialog();
                if (DataContext != null)
                {
                    Application.Current.MainWindow.Show();
                }
            }
            else
            {
                Closing -= OnMainWindowClosing;
                Project.Current?.Unload();
                DataContext = null;
                ContentToolsAPI.ShutDownContentTools();
            }
        }

        private void OpenProjectBrowserDialog()
        {
            var projectBrowser = new ProjectBrowserDialog();
            if (projectBrowser.ShowDialog() == false || projectBrowser.DataContext == null)
            {
                Application.Current.Shutdown();
            }
            else
            {
                Project.Current?.Unload();
                var project = projectBrowser.DataContext as Project;
                Debug.Assert(project != null);
                ContentWatcher.Reset(project.ContentPath, project.Path);
                DataContext = projectBrowser.DataContext;
            }
        }
    }
}