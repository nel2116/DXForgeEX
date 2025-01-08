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
        public static string DXForgePath { get; private set; }

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
                    DXForgePath = dlg.DXForgePath;
                    Environment.SetEnvironmentVariable("DXFORGE_ENGINE", DXForgePath.ToUpper(), EnvironmentVariableTarget.User);
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
                DataContext = projectBrowser.DataContext;
            }
        }
    }
}