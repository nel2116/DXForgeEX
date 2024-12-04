using DXForgeEditor.GameProject;
using System.ComponentModel;
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
        public static string DXForgePath { get; private set; } = @"F:\HAL\ThirdYear\DXForgeEX";

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
            OpenProjectBrowserDialog();
        }

        private void GetEnginePath()
        {
            var dxforgePath = Environment.GetEnvironmentVariable("DXFORGE_ENGINE", EnvironmentVariableTarget.User);
            if (dxforgePath == null || !Directory.Exists(Path.Combine(dxforgePath, @"DXForgeEngine\EngineAPI")))
            {
                var dlg = new EnginePathDialog();
                if (dlg.ShowDialog() == true)
                {

                }
                else
                {
                    Application.Current.Shutdown();
                }
            }
            else
            {
                DXForgePath = dxforgePath;
            }
        }

        private void OnMainWindowClosing(object? sender, CancelEventArgs e)
        {
            Closing -= OnMainWindowClosing;
            Project.Currnt?.Unload();
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
                Project.Currnt?.Unload();
                DataContext = projectBrowser.DataContext;
            }
        }
    }
}