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
    public partial class ConfigureTextureImportSettingsView : UserControl
    {
        private void OnRemove_Button_Click(object sender, RoutedEventArgs e)
        {
            var vm = (ConfigureImportSettings)DataContext;
            vm.TextureImportSettingsConfigurator.RemoveFile((TextureProxy)((FrameworkElement)sender).DataContext);
        }


        private void OnImport_Button_Click(object sender, RoutedEventArgs e)
        {
            ((TextureImportSettingsConfigurator)((FrameworkElement)sender).DataContext).Import();
        }

        private void OnApplyToSelection_Button_Click(object sender, RoutedEventArgs e)
        {
            var settings = ((TextureProxy)((FrameworkElement)sender).DataContext).ImportSettings;
            var selection = textureListBox.SelectedItems;
            foreach (TextureProxy proxy in selection)
            {
                proxy.CopySettings(settings);
            }
        }

        private void OnApplyToAll_Button_Click(object sender, RoutedEventArgs e)
        {
            var settings = ((TextureProxy)((FrameworkElement)sender).DataContext).ImportSettings;
            var vm = (ConfigureImportSettings)DataContext;
            foreach (var proxy in vm.TextureImportSettingsConfigurator.TextureProxies)
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
            ImportingItemCollection.Clear(AssetType.Texture);
        }

        private void OnAddImageSource_Button_Click(object sender, RoutedEventArgs e)
        {
            var vm = ((ConfigureImportSettings)DataContext).TextureImportSettingsConfigurator;
            var target = (TextureProxy)((FrameworkElement)sender).DataContext;
            // NOTE: アイテムをループしている間にソース・コレクションを変更するのを避けるために、
            //       選択されたアイテムをコピーする必要があります。
            var items = textureListBox.SelectedItems;
            var selection = new TextureProxy[items.Count];
            items.CopyTo(selection, 0);

            foreach (TextureProxy proxy in selection)
            {
                vm.MoveToTarget(proxy, target);
            }
        }

        private void OnRemoveImageSource_Button_Click(object sender, RoutedEventArgs e)
        {
            var vm = ((ConfigureImportSettings)DataContext).TextureImportSettingsConfigurator;
            var target = (TextureProxy)((FrameworkElement)sender).DataContext;
            // NOTE: アイテムをループしている間にソース・コレクションを変更するのを避けるために、
            //       選択されたアイテムをコピーする必要があります。
            var items = imageSourcesListBox.SelectedItems;
            var selection = new TextureProxy[items.Count];
            items.CopyTo(selection, 0);

            foreach (TextureProxy proxy in selection)
            {
                vm.MoveFromTarget(proxy, target);
            }
        }

        private void MoveSelection(object sender, Action<TextureProxy, List<TextureProxy>> action)
        {
            var target = (TextureProxy)((FrameworkElement)sender).DataContext;
            var items = imageSourcesListBox.SelectedItems;
            var selection = new List<TextureProxy>();

            foreach (TextureProxy proxy in items)
            {
                selection.Add(proxy);
            }

            action(target, selection);
        }

        private void OnMoveSelectionUp_Button_Click(object sender, RoutedEventArgs e)
        {
            MoveSelection(sender, (target, selection) => target.MoveUp(selection));
        }

        private void OnMoveSelectionDown_Button_Click(object sender, RoutedEventArgs e)
        {
            MoveSelection(sender, (target, selection) => target.MoveDown(selection));

        }

        public ConfigureTextureImportSettingsView()
        {
            InitializeComponent();

            Loaded += (_, _) =>
            {
                var item = textureListBox.ItemContainerGenerator
                .ContainerFromIndex(textureListBox.SelectedIndex) as ListBoxItem;
                item?.Focus();
            };
        }
    }
}
