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
            if (!GetEnginePath())
            {
                // エンジンのパスのダイアログをキャンセルした
                Shutdown();
                Application.Current.Shutdown();
                return;
            }
            // エディタの既定アセット(BRDF LUTなど)がなければ作る(Primal Day 075)。
            // 既定のマテリアル用シェーダーはエンジンのシェーダーをインクルードするため、エンジンのパスを決めてからコンパイルする(Primal Day 076)。
            DefaultAssets.GenerateDefaultAssets();
            // エンジン(シェーダーとグラフィックス)を初期化する(Primal Day 078)。失敗したら理由を出してから終了する。
            if (InitializeEngine())
            {
                OpenProjectBrowserDialog();
            }
            else
            {
                Shutdown();
                Application.Current.Shutdown();
            }
        }

        /// <summary>
        /// エンジンを初期化する。シェーダーのコンパイルに失敗したときは、再試行するかを聞く。
        /// 失敗したときは理由を MessageBox とログに出して false を返す。例外は投げない。
        /// </summary>
        private static bool InitializeEngine()
        {
            // NOTE: エンジンはシェーダーのソース(..\..\DXForgeEngine\...)と shaders.bin(.\shaders\d3d12\)を作業フォルダからの
            //       相対パスで探すので、エディタの実行ファイルのフォルダ(x64\<構成>)に移す。エディタの C# の側は作業フォルダに
            //       依らない(相対パスを使っていない)。
            try
            {
                Directory.SetCurrentDirectory(AppContext.BaseDirectory);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Warning, $"作業フォルダを {AppContext.BaseDirectory} に移せませんでした: {ex.Message}");
            }

            while (true)
            {
                var result = EngineAPI.InitializeEngine();
                if (result == EngineAPIStructs.EngineInitError.Succeeded)
                {
                    Debug.WriteLine("[DXForgeEditor] エンジンを初期化しました");
                    Logger.Log(MessageType.Info, "エンジンを初期化しました");
                    return true;
                }

                var message = EngineAPI.GetInitializeEngineErrorMessage(result);
                Debug.WriteLine($"[DXForgeEditor] {message}");
                Logger.Log(MessageType.Error, message);
                // NOTE: Primal は DLL の中で MessageBox(MB_RETRYCANCEL)を出す。ヘッドレスのテストで止まらないよう、エディタで聞く。
                if (result == EngineAPIStructs.EngineInitError.ShaderCompilation)
                {
                    if (MessageBox.Show($"{message}\n\n再試行しますか?(「いいえ」でエディタを終了します)", "エンジンのシェーダーのコンパイルエラー",
                        MessageBoxButton.YesNo, MessageBoxImage.Warning) == MessageBoxResult.Yes)
                    {
                        continue;
                    }
                }
                else
                {
                    MessageBox.Show($"{message}\n\nエディタを終了します。", "エンジンの初期化に失敗しました", MessageBoxButton.OK, MessageBoxImage.Error);
                }
                return false;
            }
        }

        /// <returns>エンジンのパスが決まったら true。ダイアログをキャンセルしたら false</returns>
        private bool GetEnginePath()
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
                    return false;
                }
            }
            else
            {
                DXForgePath = resolution.Path;
                EnginePathSource = resolution.Source;
            }
            LogEnginePath();
            return true;
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
                Shutdown();
            }
        }

        /// <summary>
        /// エディタを終えるときの後片付け(Primal Day 078)。ウィンドウを閉じたとき、Project Browser をキャンセルしたとき、
        /// エンジンの初期化に失敗したときに呼ぶ。ShutdownEngine は初期化していなくても、2回呼んでも何もしない。
        /// </summary>
        private void Shutdown()
        {
            Closing -= OnMainWindowClosing;
            Project.Current?.Unload();
            DataContext = null;
            try
            {
                ContentToolsAPI.ShutDownContentTools();
            }
            catch (Exception ex)
            {
                // NOTE: エンジンの初期化の失敗などから呼ぶので、ContentTools.dll を読み込めなくても例外を投げずに続ける
                Debug.WriteLine(ex.Message);
            }
            EngineAPI.ShutdownEngine();
        }

        private void OpenProjectBrowserDialog()
        {
            var projectBrowser = new ProjectBrowserDialog();
            if (projectBrowser.ShowDialog() == false || projectBrowser.DataContext == null)
            {
                Shutdown();
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