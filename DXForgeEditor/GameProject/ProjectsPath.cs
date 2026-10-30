// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ProjectsPath.cs]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要
// 　Create Project の Path 欄の既定値(プロジェクトを作るフォルダ)を決める
// 更新履歴
// 2026/10/30 新規作成 (#141)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
using System.IO;

namespace DXForgeEditor.GameProject
{
    // Warningがnullでないときは、DXFORGE_PROJECTSの値を使わずに既定のフォルダにした(理由はLoggerViewに出す)
    record ProjectsPathResolution(string Path, string? Warning);

    // Create Projectの既定のフォルダを決める(#141)。
    // ドキュメントの下(OneDriveに移されていることがある)ではなく、ユーザー環境変数DXFORGE_PROJECTSか、
    // なければ%USERPROFILE%\DXForgeProjects\ にする。ユーザー環境変数は書き換えない。
    // 値とユーザーフォルダを引数で受け取るので、エディタのウィンドウなしで確かめられる。
    static class ProjectsPath
    {
        public const string EnvironmentVariableName = "DXFORGE_PROJECTS";
        private const string _defaultFolderName = "DXForgeProjects";
        // Path.GetInvalidPathChars()に含まれないが、Windowsのフォルダ名には使えない文字
        private static readonly char[] _invalidNameChars = { '"', '<', '>', '|', '*', '?', ':' };

        public static string GetDefault(string userProfile)
            => WithTrailingSeparator(Path.Combine(userProfile, _defaultFolderName));

        public static ProjectsPathResolution Resolve(string? environmentPath, string userProfile)
        {
            var defaultPath = GetDefault(userProfile);
            if (string.IsNullOrWhiteSpace(environmentPath)) return new ProjectsPathResolution(defaultPath, null);

            // ExpandEnvironmentVariablesはNULで切り詰めるので、無効な文字は展開の前に調べる
            var path = environmentPath.Trim();
            var reason = path.IndexOfAny(Path.GetInvalidPathChars()) != -1 ? "無効な文字を含む" : null;
            if (reason == null)
            {
                path = Environment.ExpandEnvironmentVariables(path);
                reason = GetUnusableReason(path);
            }
            if (reason != null)
            {
                return new ProjectsPathResolution(defaultPath,
                    $"{EnvironmentVariableName}「{environmentPath}」は{reason}ので使いません。既定の {defaultPath} を使います。");
            }
            return new ProjectsPathResolution(WithTrailingSeparator(path), null);
        }

        // 使えない理由。使えるならnull。フォルダがないだけなら使える(作成時に作られる)
        private static string? GetUnusableReason(string path)
        {
            if (path.IndexOfAny(Path.GetInvalidPathChars()) != -1) return "無効な文字を含む";
            if (!Path.IsPathFullyQualified(path)) return "絶対パスでない";
            try
            {
                var root = Path.GetPathRoot(path);
                if (string.IsNullOrEmpty(root)) return "ドライブがない";
                if (path.Substring(root.Length).IndexOfAny(_invalidNameChars) != -1) return "無効な文字を含む";
                if (!Directory.Exists(root)) return $"ドライブ {root} がない";
            }
            catch (Exception)
            {
                return "無効なパスな";
            }
            return null;
        }

        private static string WithTrailingSeparator(string path)
            => Path.EndsInDirectorySeparator(path) ? path : path + Path.DirectorySeparatorChar;
    }
}
