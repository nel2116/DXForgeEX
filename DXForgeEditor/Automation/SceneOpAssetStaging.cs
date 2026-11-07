using DXForgeEditor.Content;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace DXForgeEditor.Automation
{
    /// <summary>
    /// import-asset の取り込み先(Issue #183)。DXForgeImport を別のプロセスで動かして、プロジェクトのフォルダの一時フォルダに書き、
    /// すべての操作が成功したときだけ Content へ移す。Content と同じボリュームなので、移動はファイルごとに不可分。
    /// NOTE: 別のプロセスにするのは、インポートのコード(DXForgeImport の ImportAndSave と HeadlessEnvironment)を二重にしないためと、
    ///       ContentTools のネイティブのクラッシュを操作の失敗にするため。
    /// NOTE: プロセスが強制終了されると一時フォルダ(.dxforge-import-*)は残るが、Content には何も残らない。
    ///       残ったフォルダは、次の変更の操作のときに SweepAbandoned が片付ける。
    /// </summary>
    class SceneOpAssetStaging(string projectFile, string importerPath) : IDisposable
    {
        public const string ImporterFileName = "DXForgeImport.exe";
        public const string StagingFolderPrefix = ".dxforge-import-";
        private const string StagingLockFileName = "staging.lock";
        private const int ErrorSharingViolation = 32;

        // DXForgeImport の終了コード(DXForgeImport/Program.cs の ExitCode)
        private const int ImporterInputNotFound = 3;
        private const int ImporterUnsupportedFormat = 4;
        private const int ImporterInvalidSettings = 5;
        private const int ImporterContentToolsUnavailable = 7;

        private readonly string _projectFolder = Path.GetDirectoryName(projectFile) ?? string.Empty;
        private readonly string _contentFolder = Path.Combine(Path.GetDirectoryName(projectFile) ?? string.Empty, "Content");
        private readonly string _importerPath = importerPath;
        // Content からの相対パス → 一時フォルダの中のファイル。取り込んだ順に移す
        private readonly List<KeyValuePair<string, string>> _stagedFiles = [];
        private readonly Dictionary<string, string> _stagedByRelativePath = new(StringComparer.OrdinalIgnoreCase);
        private readonly List<AssetInfo> _stagedAssets = [];
        private readonly List<string> _movedFiles = [];
        private readonly List<string> _createdFolders = [];
        private readonly List<string> _leftoverPaths = [];
        private string? _stagingFolder;
        private FileStream? _stagingLock;

        public static string DefaultImporterPath => Path.Combine(AppContext.BaseDirectory, ImporterFileName);

        internal TimeSpan ImporterTimeout { get; set; } = TimeSpan.FromMinutes(10);

        /// <summary>消せずに残ったファイルとフォルダ。失敗したときも成功したときも、結果に出す。</summary>
        public IReadOnlyList<string> LeftoverPaths => _leftoverPaths;

        /// <summary>
        /// 強制終了で残った一時フォルダを片付ける。例外は投げない。ロックを共有なしで開けたフォルダだけを消す(所有者も、その子の importer も、もういない)。
        /// 共有違反は使用中なので触らない。ロックがない、または確かめられないフォルダは、使っていないと言えないので消さずに警告する。
        /// </summary>
        public static StagingSweepResult SweepAbandoned(string projectFile)
        {
            var result = new StagingSweepResult();
            var projectFolder = Path.GetDirectoryName(Path.GetFullPath(projectFile)) ?? string.Empty;
            List<string> folders;
            try
            {
                folders = Directory.EnumerateDirectories(projectFolder, StagingFolderPrefix + "*").ToList();
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                result.Warnings.Add($"残った一時フォルダを確かめられません: {projectFolder}({ex.Message})");
                return result;
            }

            foreach (var folder in folders)
            {
                // 自分の形の名前だけを見る
                if (!Guid.TryParseExact(Path.GetFileName(folder)[StagingFolderPrefix.Length..], "N", out _)) continue;

                FileStream? held = null;
                try
                {
                    held = new FileStream(Path.Combine(folder, StagingLockFileName), FileMode.Open, FileAccess.ReadWrite, FileShare.None);
                }
                catch (Exception ex) when (ex is FileNotFoundException || ex is DirectoryNotFoundException)
                {
                    // 確かめる間に所有者が消したなら、何も残っていない
                    if (Directory.Exists(folder)) result.Warnings.Add($"ロックのない一時フォルダは消しません。使っている dxforge がなければ、手で消してください: {folder}");
                }
                catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
                {
                    if ((ex.HResult & 0xFFFF) != ErrorSharingViolation) result.Warnings.Add($"一時フォルダが使用中かを確かめられないので、消しません: {folder}({ex.Message})");
                }
                if (held == null) continue;

                var error = DeleteStagingFolder(folder, held);
                if (error == null)
                {
                    result.Removed.Add(folder);
                }
                else
                {
                    result.Leftovers.Add(folder);
                    result.Warnings.Add($"残った一時フォルダを消せませんでした: {folder}({error})");
                }
            }
            return result;
        }

        /// <summary>取り込んで一時フォルダに書く。結果の assets と warnings を埋める。Content には触らない。</summary>
        public void Import(SceneOp op, JsonObject result, JsonArray warnings)
        {
            var source = op.Source!;
            if (!File.Exists(source)) throw new SceneOpException(SceneOpErrorKind.NotFound, "source_not_found", $"取り込むファイルがありません: {source}", key: "source");

            var opFolder = Path.Combine(EnsureStagingFolder(), op.Index.ToString(System.Globalization.CultureInfo.InvariantCulture));
            var relativeFolder = op.Folder ?? string.Empty;
            var output = Path.GetFullPath(Path.Combine(opFolder, relativeFolder));
            var finalOutput = Path.GetFullPath(Path.Combine(_contentFolder, relativeFolder));
            RunImporter(source, output, finalOutput, op.ImportAssignments, warnings);

            var files = EnumerateFiles(opFolder).Select(x => new KeyValuePair<string, string>(Path.GetRelativePath(opFolder, x), x)).ToList();
            if (!files.Any(x => IsAsset(x.Value)))
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "import_failed", $"インポーターが .asset を書きませんでした: {source}", key: "source");
            }
            foreach (var (relative, _) in files)
            {
                // 上書きはしない。Geometry.Save は書き出す先の既存のファイルから GUID を引き継ぐので、一時フォルダに書くと GUID が変わり、参照が切れる
                if (_stagedByRelativePath.ContainsKey(relative))
                {
                    throw new SceneOpException(SceneOpErrorKind.OperationFailed, "asset_exists", $"この ops の中で同じファイルを取り込んでいます: {relative}", key: "source");
                }
                var destination = Path.Combine(_contentFolder, relative);
                if (File.Exists(destination) || Directory.Exists(destination))
                {
                    throw new SceneOpException(SceneOpErrorKind.OperationFailed, "asset_exists", $"Content にすでにあります(上書きはしません): {destination}", key: "source");
                }
            }

            var assets = new JsonArray();
            foreach (var (relative, staged) in files)
            {
                _stagedFiles.Add(new(relative, staged));
                _stagedByRelativePath[relative] = staged;
                if (!IsAsset(staged)) continue;

                var info = Asset.TryGetAssetInfo(staged)
                    ?? throw new SceneOpException(SceneOpErrorKind.OperationFailed, "import_failed", $"インポーターが書いた .asset を読めません: {relative}", key: "source");
                _stagedAssets.Add(info);
                assets.Add(new JsonObject
                {
                    ["path"] = "path:" + relative.Replace('\\', '/'),
                    ["guid"] = info.Guid.ToString(),
                    ["type"] = info.Type.ToString().ToLowerInvariant(),
                });
            }
            result["assets"] = assets;
        }

        /// <summary>まだ Content にない、この ops で取り込んだアセットを探す。</summary>
        /// <remarks>
        /// Content にあるアセットと同じ表記だけを通す。./ や大文字と小文字の違いはファイルシステムが解決するので、ここでは正規化して探す。
        /// 拡張子は Asset.TryGetAssetInfo と同じく、書かれた表記のまま比べる。NUL を含むパスは File.Exists と同じくないものとする(GetFullPath は例外を投げる)。
        /// </remarks>
        public AssetInfo? FindStaged(string relativePath)
        {
            if (relativePath.Contains('\0') || Path.GetExtension(relativePath) != Asset.AssetFileExtension) return null;
            var normalized = Path.GetRelativePath(_contentFolder, Path.GetFullPath(Path.Combine(_contentFolder, relativePath)));
            return _stagedByRelativePath.TryGetValue(normalized, out var staged) ? Asset.TryGetAssetInfo(staged) : null;
        }

        public AssetInfo? FindStaged(Guid guid) => _stagedAssets.FirstOrDefault(x => x.Guid == guid);

        /// <summary>
        /// 取り込んだファイルを Content へ移してから save を呼ぶ。.dxproj が、まだないアセットを指す瞬間を作らないため、この順番にする。
        /// どちらかが失敗したら、移したファイルを戻してから例外を投げ直す。
        /// </summary>
        public void Commit(Action save)
        {
            try
            {
                MoveToContent();
                save();
            }
            catch (SceneOpException)
            {
                RollbackMoved();
                throw;
            }
            _movedFiles.Clear();
            _createdFolders.Clear();
        }

        internal void MoveToContent()
        {
            try
            {
                foreach (var (relative, staged) in _stagedFiles)
                {
                    var destination = Path.Combine(_contentFolder, relative);
                    CreateFolders(Path.GetDirectoryName(destination)!);
                    File.Move(staged, destination, overwrite: false);
                    _movedFiles.Add(destination);
                }
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                throw new SceneOpException(SceneOpErrorKind.SaveFailed, "asset_commit_failed", $"取り込んだファイルを Content へ移せません({ex.Message})");
            }
        }

        /// <summary>
        /// MoveToContent で移したファイルと作ったフォルダを消す。もともと Content にあったものには触らない。
        /// 消せなかったものは LeftoverPaths に入れる(一時フォルダの中身は捨てるので、戻す先はない)。
        /// </summary>
        internal void RollbackMoved()
        {
            foreach (var file in _movedFiles)
            {
                TryDelete(file, () => File.Delete(file));
            }
            _movedFiles.Clear();
            // 深いフォルダから消す。ファイルが残ったフォルダは空ではないので、それも残ったものとして出す
            foreach (var folder in Enumerable.Reverse(_createdFolders))
            {
                TryDelete(folder, () => Directory.Delete(folder));
            }
            _createdFolders.Clear();
        }

        /// <summary>一時フォルダを消す。成功したときも失敗したときも、最後に必ず呼ぶ。</summary>
        public void Dispose()
        {
            if (_stagingLock != null && _stagingFolder != null)
            {
                var error = DeleteStagingFolder(_stagingFolder, _stagingLock);
                if (error != null)
                {
                    _leftoverPaths.Add(_stagingFolder);
                    Logger.Log(MessageType.Warning, $"消せませんでした: {_stagingFolder}({error})");
                }
            }
            else if (_stagingFolder != null && Directory.Exists(_stagingFolder))
            {
                TryDelete(_stagingFolder, () => Directory.Delete(_stagingFolder, recursive: true));
            }
            _stagingLock = null;
            _stagingFolder = null;
        }

        /// <summary>
        /// 一時フォルダを作り、このプロセスが生きている間はロックを持つ。OS がハンドルを閉じるので、強制終了されてもロックは残らない。
        /// NOTE: Inheritable にして importer にも継承させる。dxforge だけが殺されても、importer が書き終わるまでは使用中と判定するため。
        /// </summary>
        private string EnsureStagingFolder()
        {
            if (_stagingFolder != null) return _stagingFolder;
            _stagingFolder = Path.Combine(_projectFolder, StagingFolderPrefix + Guid.NewGuid().ToString("N"));
            try
            {
                Directory.CreateDirectory(_stagingFolder);
                _stagingLock = new FileStream(Path.Combine(_stagingFolder, StagingLockFileName), FileMode.CreateNew, FileAccess.ReadWrite, FileShare.None | FileShare.Inheritable);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "import_failed", $"一時フォルダを作れません: {_stagingFolder}({ex.Message})", key: "source");
            }
            return _stagingFolder;
        }

        /// <summary>
        /// ロックを持ったまま中身を消し、最後にロックを閉じてフォルダごと消す。held は必ず閉じる。消せなければその理由を返し、ロックのファイルは残す(次の実行が片付ける)。
        /// </summary>
        private static string? DeleteStagingFolder(string folder, FileStream held)
        {
            try
            {
                foreach (var entry in Directory.EnumerateFileSystemEntries(folder).ToList())
                {
                    if (string.Equals(Path.GetFileName(entry), StagingLockFileName, StringComparison.OrdinalIgnoreCase)) continue;
                    if (Directory.Exists(entry)) Directory.Delete(entry, recursive: true);
                    else File.Delete(entry);
                }
                held.Dispose();
                Directory.Delete(folder, recursive: true);
                return null;
            }
            catch (DirectoryNotFoundException)
            {
                return null; // ロックを閉じたあとに、別の実行の片付けが先に消した
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                return ex.Message;
            }
            finally
            {
                held.Dispose();
            }
        }

        private void TryDelete(string path, Action delete)
        {
            try
            {
                delete();
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                _leftoverPaths.Add(path);
                Logger.Log(MessageType.Warning, $"消せませんでした: {path}({ex.Message})");
            }
        }

        // 深いフォルダが後ろに来るように、作ったフォルダを覚える
        private void CreateFolders(string folder)
        {
            var missing = new Stack<string>();
            for (var current = folder; !string.IsNullOrEmpty(current) && !Directory.Exists(current); current = Path.GetDirectoryName(current))
            {
                missing.Push(current);
            }
            foreach (var current in missing)
            {
                Directory.CreateDirectory(current);
                _createdFolders.Add(current);
            }
        }

        private static IEnumerable<string> EnumerateFiles(string folder) =>
            Directory.Exists(folder) ? Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.OrdinalIgnoreCase) : [];

        private static bool IsAsset(string file) => string.Equals(Path.GetExtension(file), Asset.AssetFileExtension, StringComparison.OrdinalIgnoreCase);

        private void RunImporter(string source, string output, string finalOutput, IReadOnlyList<string>? assignments, JsonArray warnings)
        {
            if (!File.Exists(_importerPath))
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "importer_unavailable",
                    $"インポーターがありません: {_importerPath}(./build.ps1 -Target editor でビルドしてください。別の場所なら --importer で指定します)", key: "source");
            }

            var startInfo = new ProcessStartInfo(_importerPath)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = new UTF8Encoding(false),
                StandardErrorEncoding = new UTF8Encoding(false),
            };
            foreach (var argument in new[] { "--input", source, "--output", output, "--sources-output", finalOutput, "--json" }) startInfo.ArgumentList.Add(argument);
            foreach (var assignment in assignments ?? [])
            {
                startInfo.ArgumentList.Add("--set");
                startInfo.ArgumentList.Add(assignment);
            }

            string stdout, stderr;
            int exitCode;
            try
            {
                using var process = Process.Start(startInfo)
                    ?? throw new SceneOpException(SceneOpErrorKind.OperationFailed, "importer_unavailable", $"インポーターを起動できません: {_importerPath}", key: "source");
                var stdoutTask = process.StandardOutput.ReadToEndAsync();
                var stderrTask = process.StandardError.ReadToEndAsync();
                if (!process.WaitForExit(ImporterTimeout))
                {
                    process.Kill(entireProcessTree: true);
                    process.WaitForExit();
                    throw new SceneOpException(SceneOpErrorKind.OperationFailed, "import_failed", $"インポーターが {ImporterTimeout.TotalSeconds} 秒で終わらなかったので止めました: {source}", key: "source");
                }
                stdout = stdoutTask.GetAwaiter().GetResult();
                stderr = stderrTask.GetAwaiter().GetResult();
                exitCode = process.ExitCode;
            }
            catch (Exception ex) when (ex is Win32Exception || ex is IOException || ex is InvalidOperationException)
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "importer_unavailable", $"インポーターを起動できません: {_importerPath}({ex.Message})", key: "source");
            }

            var (errors, importerWarnings) = ReadImporterMessages(stdout);
            foreach (var warning in importerWarnings) warnings.Add((JsonNode?)warning);
            if (exitCode == 0) return;

            var detail = errors.Count > 0 ? string.Join(" / ", errors) : stderr.Trim();
            var message = $"{source} を取り込めません(インポーターの終了コード {exitCode}): {detail}";
            throw exitCode switch
            {
                ImporterInputNotFound => new SceneOpException(SceneOpErrorKind.NotFound, "source_not_found", message, key: "source"),
                ImporterUnsupportedFormat => new SceneOpException(SceneOpErrorKind.OperationFailed, "import_unsupported_format", message, key: "source"),
                ImporterInvalidSettings => new SceneOpException(SceneOpErrorKind.OperationFailed, "import_invalid_settings", message, key: "settings"),
                ImporterContentToolsUnavailable => new SceneOpException(SceneOpErrorKind.OperationFailed, "importer_unavailable", message, key: "source"),
                _ => new SceneOpException(SceneOpErrorKind.OperationFailed, "import_failed", message, key: "source"),
            };
        }

        /// <summary>インポーターの --json の errors と warnings を読む。クラッシュすると JSON は出ないので、読めなくても失敗にしない(終了コードで判断する)。</summary>
        private static (List<string> Errors, List<string> Warnings) ReadImporterMessages(string stdout)
        {
            var errors = new List<string>();
            var warnings = new List<string>();
            try
            {
                using var document = JsonDocument.Parse(stdout);
                Collect(document.RootElement, "errors", errors);
                Collect(document.RootElement, "warnings", warnings);
            }
            catch (JsonException) { }
            return (errors, warnings);

            static void Collect(JsonElement root, string key, List<string> list)
            {
                if (root.ValueKind == JsonValueKind.Object && root.TryGetProperty(key, out var items) && items.ValueKind == JsonValueKind.Array)
                {
                    list.AddRange(items.EnumerateArray().Where(x => x.ValueKind == JsonValueKind.String).Select(x => x.GetString()!));
                }
            }
        }
    }

    /// <summary>SceneOpAssetStaging.SweepAbandoned の結果。</summary>
    class StagingSweepResult
    {
        public List<string> Removed { get; } = [];
        public List<string> Leftovers { get; } = [];
        public List<string> Warnings { get; } = [];
    }
}
