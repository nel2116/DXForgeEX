using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media.Media3D;

namespace DXForgeEditor.Content
{
    public class ContentModifiedEventArgs : EventArgs
    {
        public string FullPath { get; }

        public ContentModifiedEventArgs(string path)
        {
            FullPath = path;
        }
    }

    static class ContentWatcher
    {
        private static readonly DelayEventTimer _refreshTimer = new(TimeSpan.FromMilliseconds(250));
        private static readonly FileSystemWatcher _contentWatcher = new()
        {
            IncludeSubdirectories = true,
            Filter = "",
            NotifyFilter = NotifyFilters.CreationTime | NotifyFilters.DirectoryName | NotifyFilters.FileName | NotifyFilters.LastWrite
        };

        // ファイル・ウォッチャーは、このカウンタが0のときのみ有効になる。
        private static int _fileWatcherEnableCounter = 0;
        public static event EventHandler<ContentModifiedEventArgs> ContentModified;

        public static void EnableFileWatcher(bool isEnabled)
        {
            if (_fileWatcherEnableCounter > 0 && isEnabled)
            {
                --_fileWatcherEnableCounter;
            }
            else if (!isEnabled)
            {
                ++_fileWatcherEnableCounter;
            }
        }

        public static void Reset(string contentFolder, string projeectPath)
        {
            _contentWatcher.EnableRaisingEvents = false;

            ContentInfoCache.Reset(projeectPath);

            if (!string.IsNullOrEmpty(contentFolder))
            {
                Debug.Assert(Directory.Exists(contentFolder));
                _contentWatcher.Path = contentFolder;
                _contentWatcher.EnableRaisingEvents = true;
                AssetRegistry.Reset(contentFolder);
            }
        }

        private static async void OnContentModified(object sender, FileSystemEventArgs e)
        {
            await Application.Current.Dispatcher.BeginInvoke(new Action(() => _refreshTimer.Trigger(e)));
        }

        private static void Refresh(object? sender, DelayEventTimerArgs e)
        {
            if (_fileWatcherEnableCounter > 0)
            {
                e.RepeaEvent = true;
                return;
            }

            e.Data
                .Cast<FileSystemEventArgs>()
                .GroupBy(x => x.FullPath)
                .Select(x => x.First())
                .ToList().ForEach(x => ContentModified?.Invoke(null, new ContentModifiedEventArgs(x.FullPath)));
        }

        static ContentWatcher()
        {
            _contentWatcher.Changed += OnContentModified;
            _contentWatcher.Created += OnContentModified;
            _contentWatcher.Deleted += OnContentModified;
            _contentWatcher.Renamed += OnContentModified;

            _refreshTimer.Triggered += Refresh;
        }
    }
}
