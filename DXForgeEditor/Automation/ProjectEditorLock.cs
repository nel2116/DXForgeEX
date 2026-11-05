using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;

namespace DXForgeEditor.Automation
{
    /// <summary>
    /// エディタがプロジェクトを開いていることを、dxforge に知らせるロックファイル(Issue #183)。
    /// エディタはプロジェクトを開いている間 &lt;プロジェクトのフォルダ&gt;\.DXForge\editor.lock を開いたままにする。
    /// 同じプロジェクトを複数のエディタが開けるので、ハンドルは共有で持つ。dxforge は共有なしで開けるかどうかで、
    /// どれか 1 つでも開いているかを確かめる。エディタが落ちると OS がハンドルを閉じ、ファイルが残っても「開いている」とは判定しない。
    /// </summary>
    static class ProjectEditorLock
    {
        private const int ErrorSharingViolation = 32;
        private const int AcquireAttempts = 5; // IsHeld や ReleaseHandle が共有なしで開いている一瞬を待つ
        private const int AcquireRetryMilliseconds = 20;
        private static FileStream? _held;

        private static string LockFile(string projectFolder) => Path.Combine(projectFolder, ".DXForge", "editor.lock");

        /// <summary>
        /// エディタの MainWindow.DataContext(開いているプロジェクト)が変わったときに呼ぶ。
        /// NOTE: UI のイベントから呼ばれるので、取得できなくても例外を投げない。
        /// </summary>
        internal static void OnProjectChanged(Project? project)
        {
            if (_held != null) ReleaseHandle(_held);
            _held = null;
            if (project == null) return;

            try
            {
                if (!Directory.Exists(project.Path))
                {
                    // .dxproj に保存された Path が古い(プロジェクトを動かした)ときに、古い場所へフォルダを作らない
                    Logger.Log(MessageType.Warning, $"プロジェクトのフォルダがないので、エディタが開いていることを dxforge に知らせられません: {project.Path}");
                    return;
                }
                _held = AcquireHandle(project.Path);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException || ex is NotSupportedException)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Warning, $"エディタが開いていることを dxforge に知らせられません: {ex.Message}");
            }
        }

        /// <summary>ロックのハンドルを取る。ほかのエディタのハンドルとは共存する。</summary>
        internal static FileStream AcquireHandle(string projectFolder)
        {
            var file = LockFile(projectFolder);
            var folder = Path.GetDirectoryName(file);
            Debug.Assert(folder != null);
            Directory.CreateDirectory(folder);
            for (var attempt = 1; ; ++attempt)
            {
                try
                {
                    return new FileStream(file, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.ReadWrite);
                }
                catch (Exception ex) when (attempt < AcquireAttempts && (ex is IOException || ex is UnauthorizedAccessException))
                {
                    Thread.Sleep(AcquireRetryMilliseconds);
                }
            }
        }

        /// <summary>ハンドルを閉じる。ほかのエディタが持っていなければ、ファイルも消す。</summary>
        internal static void ReleaseHandle(FileStream handle)
        {
            var file = handle.Name;
            handle.Dispose();
            try
            {
                // NOTE: 共有なしで開けるのは誰も持っていないときだけ。持っているエディタがあれば、閉じるときにそちらが消す
                using var last = new FileStream(file, FileMode.Open, FileAccess.ReadWrite, FileShare.None, 1, FileOptions.DeleteOnClose);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                Debug.WriteLine(ex.Message);
            }
        }

        /// <summary>
        /// projectFolder のプロジェクトをエディタが開いているか。確かめられないとき(権限など)は、上書きを防ぐ側に倒して true を返す。
        /// </summary>
        internal static bool IsHeld(string projectFolder)
        {
            try
            {
                var file = LockFile(projectFolder);
                if (!File.Exists(file)) return false;
                using var probe = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.None);
                return false;
            }
            catch (FileNotFoundException)
            {
                return false; // 確かめる間にエディタが閉じた
            }
            catch (DirectoryNotFoundException)
            {
                return false;
            }
            catch (ArgumentException)
            {
                return false; // フォルダのパスとして読めない(.dxproj の Path が壊れている)なら、そこで開いているエディタはない
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                if ((ex.HResult & 0xFFFF) != ErrorSharingViolation) Logger.Log(MessageType.Warning, $"エディタのロックファイルを確かめられません: {ex.Message}");
                return true;
            }
        }
    }
}
