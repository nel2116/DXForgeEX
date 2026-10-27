using DXForgeEditor.Utilities;
using System.IO;

namespace DXForgeEditor.GameDev
{
    enum EnginePathSource
    {
        // エディタの実行ファイルがあるリポジトリ(x64\<構成>\ の2つ上)
        EditorRepository,
        // ユーザー環境変数DXFORGE_ENGINE
        EnvironmentVariable,
        // どちらからも見つからない(EnginePathDialogで選んでもらう)
        None,
    }

    record EnginePathResolution(string Path, EnginePathSource Source, string? EnvironmentPath);

    // ゲームのビルドに使うエンジンのパスを決める(#117)。
    // worktreeでビルドしたエディタから実行したゲームが、DXFORGE_ENGINE(普段はルートのチェックアウト)のエンジンと
    // リンクされないように、実行中のエディタのリポジトリを優先し、見つからなければDXFORGE_ENGINEを使う。
    // ユーザー環境変数は書き換えない。ファイルシステムを読むだけなので、エディタのウィンドウなしで確かめられる。
    static class EnginePath
    {
        public const string EnvironmentVariableName = "DXFORGE_ENGINE";
        private const string _engineApiFolder = @"DXForgeEngine\EngineAPI";

        public static bool IsEnginePath(string? path)
        {
            if (string.IsNullOrWhiteSpace(path)) return false;
            try
            {
                return Directory.Exists(Path.Combine(path, _engineApiFolder));
            }
            catch (Exception)
            {
                return false;
            }
        }

        // <リポジトリ>\x64\<構成>\ にあるエディタの実行ファイルのフォルダから、リポジトリのフォルダ(末尾は\)を探す
        public static string? FindEditorRepository(string? editorDirectory)
        {
            if (string.IsNullOrWhiteSpace(editorDirectory)) return null;
            try
            {
                var configDir = new DirectoryInfo(Path.TrimEndingDirectorySeparator(Path.GetFullPath(editorDirectory)));
                var platformDir = configDir.Parent;
                var repoDir = platformDir?.Parent;
                if (platformDir == null || repoDir == null) return null;
                if (!string.Equals(platformDir.Name, "x64", StringComparison.OrdinalIgnoreCase)) return null;
                return IsEnginePath(repoDir.FullName) ? WithTrailingSeparator(repoDir.FullName) : null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        public static EnginePathResolution Resolve(string? editorDirectory, string? environmentPath)
        {
            var repository = FindEditorRepository(editorDirectory);
            if (repository != null) return new EnginePathResolution(repository, EnginePathSource.EditorRepository, environmentPath);
            if (environmentPath != null && IsEnginePath(environmentPath)) return new EnginePathResolution(environmentPath, EnginePathSource.EnvironmentVariable, environmentPath);
            return new EnginePathResolution(string.Empty, EnginePathSource.None, environmentPath);
        }

        public static bool IsSamePath(string? a, string? b)
        {
            if (string.IsNullOrWhiteSpace(a) || string.IsNullOrWhiteSpace(b)) return false;
            try
            {
                return string.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(a)),
                    Path.TrimEndingDirectorySeparator(Path.GetFullPath(b)), StringComparison.OrdinalIgnoreCase);
            }
            catch (Exception)
            {
                return false;
            }
        }

        // ログに出すメッセージ。エディタのリポジトリを使っていて、DXFORGE_ENGINEと食い違うときはWarningにする
        public static (MessageType Type, string Message) Describe(string enginePath, EnginePathSource source, string? environmentPath)
        {
            switch (source)
            {
                case EnginePathSource.EditorRepository:
                    if (string.IsNullOrWhiteSpace(environmentPath))
                    {
                        return (MessageType.Info, $"エンジンのパス: {enginePath}(エディタのリポジトリ。{EnvironmentVariableName}は未設定)");
                    }
                    if (IsSamePath(enginePath, environmentPath))
                    {
                        return (MessageType.Info, $"エンジンのパス: {enginePath}(エディタのリポジトリ。{EnvironmentVariableName}と同じ)");
                    }
                    return (MessageType.Warning, $"エンジンのパス: {enginePath}(エディタのリポジトリ)。" +
                        $"{EnvironmentVariableName}({environmentPath})とは違います。ゲームはエディタのリポジトリのエンジンでビルドされます。");
                case EnginePathSource.EnvironmentVariable:
                    return (MessageType.Info, $"エンジンのパス: {enginePath}({EnvironmentVariableName}。エディタのリポジトリが見つからないため)");
                default:
                    return (MessageType.Error, "エンジンのパスがわかりません。");
            }
        }

        private static string WithTrailingSeparator(string path)
            => path.EndsWith(Path.DirectorySeparatorChar) ? path : path + Path.DirectorySeparatorChar;
    }
}
