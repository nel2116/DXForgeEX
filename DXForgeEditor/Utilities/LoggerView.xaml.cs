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

namespace DXForgeEditor.Utilities
{
    /// <summary>
    /// LoggerView.xaml の相互作用ロジック
    /// </summary>
    public partial class LoggerView : UserControl
    {
        public LoggerView()
        {
            InitializeComponent();

            Loaded += (s, e) =>
            {
                Logger.Log(MessageType.Info, "Infomation message");
                Logger.Log(MessageType.Warning, "Warning message");
                Logger.Log(MessageType.Error, "Error message");
            };
        }

        private void OnClear_Button_Click(object sender, RoutedEventArgs e)
        {
            Logger.Clear();
        }

        private void OnMessageFilter_Button_Click(object sender, RoutedEventArgs e)
        {
            var filter = 0x0;
            if (toggleInfo.IsChecked == true) filter |= (int)MessageType.Info;
            if (toggleWarnings.IsChecked == true) filter |= (int)MessageType.Warning;
            if (toggleError.IsChecked == true) filter |= (int)MessageType.Error;
            Logger.SetMessageFillter(filter);
        }
    }
}
