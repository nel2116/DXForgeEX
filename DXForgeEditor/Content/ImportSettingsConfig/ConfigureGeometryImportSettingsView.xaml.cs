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

namespace DXForgeEditor.Content
{
    public partial class ConfigureGeometryImportSettingsView : UserControl
    {
        private void OnRemove_Button_Click(object sender, RoutedEventArgs e)
        {
            var vm = (ConfigureImportSettings)DataContext;
            vm.GeometryImportSettingsConfigurator.RemoveFile((GeometryProxy)((FrameworkElement)sender).DataContext);
        }


        private void OnImport_Button_Click(object sender, RoutedEventArgs e)
        {
            ((GeometryImportSettingsConfigurator)((FrameworkElement)sender).DataContext).Import();
        }

        private void OnApplyToSelection_Button_Click(object sender, RoutedEventArgs e)
        {
            var settings = ((GeometryProxy)((FrameworkElement)sender).DataContext).ImportSettings;
            var selection = geometryListBox.SelectedItems;
            foreach (GeometryProxy proxy in selection)
            {
                proxy.CopySettings(settings);
            }
        }

        private void OnApplyToAll_Button_Click(object sender, RoutedEventArgs e)
        {
            var settings = ((GeometryProxy)((FrameworkElement)sender).DataContext).ImportSettings;
            var vm = (ConfigureImportSettings)DataContext;
            foreach (var proxy in vm.GeometryImportSettingsConfigurator.GeometryProxies)
            {
                proxy.CopySettings(settings);
            }
        }

        private void OnListBox_Drop(object sender, DragEventArgs e)
        {
            ConfigureImportSettingsWindow.AddDroppedFiles((ConfigureImportSettings)DataContext, (ListBox)sender, e);
        }

        private void OnClearImportingItems_Button_Click(object sender, RoutedEventArgs e)
        {
            ImportingItemCollection.Clear(AssetType.Animation);
            ImportingItemCollection.Clear(AssetType.Material);
            ImportingItemCollection.Clear(AssetType.Mesh);
            ImportingItemCollection.Clear(AssetType.Skeleton);
        }

        public ConfigureGeometryImportSettingsView()
        {
            InitializeComponent();

            Loaded += (_, _) =>
            {
                var item = geometryListBox.ItemContainerGenerator
                .ContainerFromIndex(geometryListBox.SelectedIndex) as ListBoxItem;
                item?.Focus();
            };
        }
    }
}
