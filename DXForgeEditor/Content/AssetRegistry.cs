using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows;

namespace DXForgeEditor.Content
{
    static class AssetRegistry
    {
        private static readonly Dictionary<string, AssetInfo> _assetDictionary = new();
        private static readonly ObservableCollection<AssetInfo> _assets = new();
        public static ReadOnlyObservableCollection<AssetInfo> Assets { get; } = new ReadOnlyObservableCollection<AssetInfo>(_assets);

        private static void RegisterAllAssets(string path)
        {
            Debug.Assert(Directory.Exists(path));
            foreach (var entry in Directory.GetFileSystemEntries(path))
            {
                if (ContentHelper.IsDirectory(entry))
                {
                    RegisterAllAssets(entry);
                }
                else
                {
                    RegisterAsset(entry);
                }
            }
        }

        private static void RegisterAsset(string file)
        {
            // Content フォルダには .asset 以外のファイル(FBX の埋め込みテクスチャの .fbm フォルダなど)も置かれる。
            // Asset.GetAssetInfo は .asset だけを受け取る前提なので、それ以外は登録しない。
            if (Path.GetExtension(file) != Asset.AssetFileExtension) return;

            Debug.Assert(File.Exists(file));
            try
            {
                var fileInfo = new FileInfo(file);
                if (!_assetDictionary.ContainsKey(file) ||
                    _assetDictionary[file].RegisterTime.IsOlder(fileInfo.LastWriteTime))
                {
                    var info = Asset.GetAssetInfo(file);
                    Debug.Assert(info != null);
                    info.RegisterTime = DateTime.Now;
                    // 読み直したときは、古い AssetInfo を同じ位置で置き換える(Add だけだと古いものが Assets に残る)
                    var index = _assetDictionary.TryGetValue(file, out var oldInfo) ? _assets.IndexOf(oldInfo) : -1;
                    _assetDictionary[file] = info;
                    if (index >= 0) _assets[index] = info;
                    else _assets.Add(info);
                }
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }
        }

        private static void UnregisterAsset(string file)
        {
            if (_assetDictionary.ContainsKey(file))
            {
                _assets.Remove(_assetDictionary[file]);
                _assetDictionary.Remove(file);
            }
        }

        private static void OnContentModified(object? sender, ContentModifiedEventArgs e)
        {
            if (ContentHelper.IsDirectory(e.FullPath))
            {
                RegisterAllAssets(e.FullPath);
            }
            else if (File.Exists(e.FullPath))
            {
                RegisterAsset(e.FullPath);
            }

            _assets.Where(x => !File.Exists(x.FullPath)).ToList().ForEach(x => UnregisterAsset(x.FullPath));
        }

        public static void Reset(string contentFolder)
        {
            ContentWatcher.ContentModified -= OnContentModified;

            _assetDictionary.Clear();
            _assets.Clear();

            Debug.Assert(Directory.Exists(contentFolder));
            RegisterAllAssets(contentFolder);

            ContentWatcher.ContentModified += OnContentModified;
        }

        public static AssetInfo? GetAssetInfo(string file) => _assetDictionary.ContainsKey(file) ? _assetDictionary[file] : null;
        public static AssetInfo? GetAssetInfo(Guid guid) => _assets.FirstOrDefault(x => x.Guid == guid);
    }
}
