using System.Windows;
using System.Windows.Controls;

namespace DXForgeEditor.Utilities
{
    /// <summary>
    /// RenderSurfaceView.xaml の相互作用ロジック
    /// </summary>
    public partial class RenderSurfaceView : UserControl
    {
        internal RenderSurfaceControl RenderSurfaceControl => renderSurface;

        public RenderSurfaceView()
        {
            InitializeComponent();
            Loaded += OnRenderSurfaceViewLoaded;
        }

        private void OnRenderSurfaceViewLoaded(object sender, RoutedEventArgs e)
        {
            Loaded -= OnRenderSurfaceViewLoaded;

            // NOTE: render thread から呼ばれる。Application.Current は終了時に null になるので、Dispatcher は自分のものを使う。
            renderSurface.FrameStatsUpdated += (_, stats) =>
            {
                _ = Dispatcher.BeginInvoke(() =>
                {
                    frameTime.Text = $"DT: {stats.AverageFrameTime * 1000f:F1} ms";
                    frameRate.Text = $"FPS: {stats.FPS} Hz";
                    drawnFrames.Text = $"Frames: {stats.DrawnFrameCount}";
                });
            };
        }
    }
}
