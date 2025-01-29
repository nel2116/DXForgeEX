using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Input;

namespace DXForgeEditor.Content
{
    sealed class ContentInfo : ViewModelBase
    {
        public static int IconWidth => 90;
        public byte[] Icon { get; }
        public byte[] IconSmall { get; }
        public string FullPath { get; private set; }
        public string FileName => Path.GetFileNameWithoutExtension(FullPath);
        public bool IsDirectory { get; }
        public DateTime DateModified { get; private set; }
        public long? Size { get; }

        public ICommand RenameCommand { get; private set; }

        private void Rename(string newFileName)
        {
            if (string.IsNullOrEmpty(newFileName.Trim())) return;

            var extension = IsDirectory ? string.Empty : Asset.AssetFileExtension;
            var path = $"{Path.GetDirectoryName(FullPath)}{Path.DirectorySeparatorChar}{newFileName}{extension}";

            if (!Validate(path, newFileName)) return;

            try
            {
                if (IsDirectory)
                {
                    Directory.Move(FullPath, path);
                }
                else
                {
                    File.Move(FullPath, path);
                }
                FullPath = path;
                var info = new FileInfo(FullPath);
                DateModified = info.LastWriteTime;

                OnPropertyChanged(nameof(FullPath));
                OnPropertyChanged(nameof(DateModified));
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }


        }

        private bool Validate(string path, string newFileName)
        {
            var fileName = Path.GetFileName(path);
            var dirName = IsDirectory ? path : Path.GetDirectoryName(path);
            var errorMsg = string.Empty;

            if (!string.IsNullOrEmpty(Path.GetDirectoryName(newFileName)))
            {
                errorMsg = "ファイル名やフォルダ名にサブディレクトリを含めることはできません";
            }

            if (!IsDirectory)
            {
                if (fileName.IndexOfAny(Path.GetInvalidFileNameChars()) != -1)
                {
                    errorMsg = "ファイル名に無効な文字が使用されています";
                }
                if (File.Exists(path))
                {
                    errorMsg = "同じ名前のファイルがすでに存在しています";
                }
            }
            else
            {
                if (Directory.Exists(path))
                {
                    errorMsg = "同じ名前のディレクトリがすでに存在します";
                }
            }

            if (dirName.IndexOfAny(Path.GetInvalidPathChars()) != -1)
            {
                errorMsg = "パスに無効な文字が使用されています";
            }

            if (!string.IsNullOrEmpty(errorMsg))
            {
                MessageBox.Show(errorMsg, "エラー", MessageBoxButton.OK, MessageBoxImage.Error);
            }

            return string.IsNullOrEmpty(errorMsg);
        }

        public ContentInfo(string fullpayh, byte[] icon = null, byte[] smallIcon = null, DateTime? lastModified = null)
        {
            Debug.Assert(File.Exists(fullpayh) || Directory.Exists(fullpayh));
            var info = new FileInfo(fullpayh);
            IsDirectory = ContentHelper.IsDirectory(fullpayh);
            DateModified = lastModified ?? info.LastWriteTime;
            Size = IsDirectory ? (long?)null : info.Length;
            Icon = icon;
            IconSmall = smallIcon ?? icon;
            FullPath = fullpayh;

            RenameCommand = new RelayCommand<string>(x => Rename(x));
        }
    }

    class ContentBrowser : ViewModelBase, IDisposable
    {
        private readonly DelayEventTimer _refreshTimer = new(TimeSpan.FromMilliseconds(250));

        public string ContentFolder { get; }

        private readonly ObservableCollection<ContentInfo> _folderContent = new();
        public ReadOnlyObservableCollection<ContentInfo> FolderContent { get; }

        private string _selectedFolder;
        public string SelectedFolder
        {
            get => _selectedFolder;
            set
            {
                if (_selectedFolder != value)
                {
                    _selectedFolder = value;
                    if (!string.IsNullOrEmpty(_selectedFolder))
                    {
                        _ = GetFolderContent();
                    }
                    OnPropertyChanged(nameof(SelectedFolder));
                }
            }
        }

        private void OnContentModified(object? sender, ContentModifiedEventArgs e)
        {
            if (Path.GetDirectoryName(e.FullPath) != SelectedFolder) return;
            _refreshTimer.Trigger();
        }

        private void Refresh(object? sender, DelayEventTimerArgs e)
        {
            _ = GetFolderContent();
        }

        private async Task GetFolderContent()
        {
            var folderContent = new List<ContentInfo>();
            await Task.Run(() =>
            {
                folderContent = GetFolderContent(SelectedFolder);
            });
            _folderContent.Clear();
            folderContent.ForEach(x => _folderContent.Add(x));
        }

        private static List<ContentInfo> GetFolderContent(string path)
        {
            Debug.Assert(!string.IsNullOrEmpty(path));
            var folderContent = new List<ContentInfo>();

            try
            {
                // サブフォルダの取得
                foreach (var dir in Directory.GetDirectories(path))
                {
                    folderContent.Add(new ContentInfo(dir));
                }

                // ファイルの取得
                foreach (var file in Directory.GetFiles(path, $"*{Asset.AssetFileExtension}"))
                {
                    folderContent.Add(ContentInfoCache.Add(file));
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
            }

            return folderContent;
        }

        public void Dispose()
        {
            ContentWatcher.ContentModified -= OnContentModified;
            ContentInfoCache.Save();
        }

        public ContentBrowser(Project project)
        {
            Debug.Assert(project != null);
            var contentFolder = project.ContentPath;
            Debug.Assert(!string.IsNullOrEmpty(contentFolder.Trim()));
            contentFolder = Path.TrimEndingDirectorySeparator(contentFolder);
            ContentFolder = contentFolder;
            FolderContent = new ReadOnlyObservableCollection<ContentInfo>(_folderContent);

            ContentWatcher.ContentModified += OnContentModified;
            _refreshTimer.Triggered += Refresh;

        }
    }
}
