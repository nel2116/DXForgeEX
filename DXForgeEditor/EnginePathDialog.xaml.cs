using System;
using System.Collections.Generic;
using System.Diagnostics.Eventing.Reader;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DXForgeEditor
{
    /// <summary>
    /// EnginePathDialog.xaml の相互作用ロジック
    /// </summary>
    public partial class EnginePathDialog : Window
    {
        public string DXForgePath { get; private set; }
        public EnginePathDialog()
        {
            InitializeComponent();
        }

        private void OnOk_Button_Click(object sender, RoutedEventArgs e)
        {
            var path = pathTextBox.Text.Trim();
            messageTextBlock.Text = string.Empty;
            if (string.IsNullOrEmpty(path))
            {
                messageTextBlock.Text = "Invalid path.";
            }
            else if (path.IndexOfAny(Path.GetInvalidPathChars()) != -1)
            {
                messageTextBlock.Text = "Invalid charcter(s) used in path.";
            }
            else if (!Directory.Exists(Path.Combine(path, @"DXForgeEngine\EngineAPI\")))
            {
                messageTextBlock.Text = "Unable to fine the engine at the specifined location.";
            }
        }
    }
}
