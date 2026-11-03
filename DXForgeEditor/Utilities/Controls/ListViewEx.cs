using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;

namespace DXForgeEditor.Utilities.Controls
{
    /// <summary>
    /// 選択済みの項目を押したときに、選択の解除をボタンを離すまで遅らせる ListViewItem(Primal Day 080)。
    /// 複数選択した項目を、選択を崩さずにドラッグし始められるようにする。
    /// Adapted from https://stackoverflow.com/a/23151248/3111536 [WPF Drag &amp; drop from ListBox with SelectionMode Multiple]
    /// </summary>
    class ListViewItemEx : ListViewItem
    {
        private bool _deferSelection = false;

        protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
        {
            if (e.ClickCount == 1 && IsSelected)
            {
                // 選択済みの項目からドラッグを始めるかもしれないので、選択の変更を Up のときまで遅らせる
                _deferSelection = true;
            }
            else
            {
                base.OnMouseLeftButtonDown(e);
            }
        }

        protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
        {
            if (_deferSelection)
            {
                try
                {
                    base.OnMouseLeftButtonDown(e);
                }
                finally
                {
                    _deferSelection = false;
                }
            }
            base.OnMouseLeftButtonUp(e);
        }

        protected override void OnMouseLeave(MouseEventArgs e)
        {
            // 遅らせていた Down を取り消す
            _deferSelection = false;
            base.OnMouseLeave(e);
        }
    }

    /// <summary>
    /// 項目に ListViewItemEx を使う ListView(Primal Day 080)。
    /// </summary>
    class ListViewEx : ListView
    {
        protected override DependencyObject GetContainerForItemOverride() => new ListViewItemEx();
    }
}
