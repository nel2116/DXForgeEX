using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using DXForgeEditor;
using DXForgeEditor.Content;
using DXForgeEditor.Utilities;

namespace DXForgeImport
{
    /// <summary>
    /// 終了コード。--help と README.md にも同じ一覧を書く。
    /// </summary>
    enum ExitCode
    {
        Success = 0,
        ImportFailed = 1,
        InvalidArguments = 2,
        InputNotFound = 3,
        UnsupportedFormat = 4,
        InvalidSettings = 5,
        OutputError = 6,
        ContentToolsUnavailable = 7,
        InternalError = 10,
    }

    class ExitException : Exception
    {
        public ExitCode Code { get; }
        public ExitException(ExitCode code, string message) : base(message) { Code = code; }
    }

    class CommandLineOptions
    {
        public string? Input { get; set; }
        public string? Output { get; set; }
        public string Type { get; set; } = "auto";
        public string? SettingsFile { get; set; }
        public List<string> Assignments { get; } = new();
        public bool Json { get; set; }
        public bool Help { get; set; }

        /// <summary>
        /// 引数を読む。--json は、ほかの引数に誤りがあっても結果をJSONで出せるよう、誤りを報告する前に読み取っておく。
        /// </summary>
        public static CommandLineOptions Parse(string[] args, out string? error)
        {
            var options = new CommandLineOptions { Json = args.Contains("--json") };
            error = null;

            for (int i = 0; i < args.Length; ++i)
            {
                var arg = args[i];
                string NextValue()
                {
                    if (i + 1 >= args.Length || args[i + 1].StartsWith("--"))
                    {
                        throw new ExitException(ExitCode.InvalidArguments, $"{arg} の値がありません。");
                    }
                    return args[++i];
                }

                try
                {
                    switch (arg)
                    {
                        case "-h": case "--help": options.Help = true; break;
                        case "--json": break;
                        case "-i": case "--input": options.Input = NextValue(); break;
                        case "-o": case "--output": options.Output = NextValue(); break;
                        case "-t": case "--type": options.Type = NextValue().ToLowerInvariant(); break;
                        case "-s": case "--settings": options.SettingsFile = NextValue(); break;
                        case "--set": options.Assignments.Add(NextValue()); break;
                        default: throw new ExitException(ExitCode.InvalidArguments, $"知らない引数です: {arg}");
                    }
                }
                catch (ExitException ex)
                {
                    error = ex.Message;
                    return options;
                }
            }

            if (options.Help) return options;

            if (string.IsNullOrWhiteSpace(options.Input)) error = "--input を指定してください。";
            else if (string.IsNullOrWhiteSpace(options.Output)) error = "--output を指定してください。";
            else if (options.Type != "auto" && options.Type != Program.GeometryType && options.Type != Program.TextureType)
                error = $"--type は auto, {Program.GeometryType}, {Program.TextureType} のどれかです: {options.Type}";

            return options;
        }
    }

    /// <summary>結果のJSONに書く、1つの入力ファイルのインポートの結果。</summary>
    class ImportResult
    {
        public string Input { get; set; } = string.Empty;
        public string? Type { get; set; }
        public bool Success { get; set; }
        public List<string> Files { get; } = new();
        public List<string> Errors { get; } = new();
        public List<string> Warnings { get; } = new();
    }

    static class Program
    {
        public const string GeometryType = "geometry";
        public const string TextureType = "texture";

        private const string HelpText = """
            DXForgeImport - エディタを開かずにアセットをインポートし、エディタと同じ .asset ファイルを書き出す(Issue #10)。

            使い方:
              DXForgeImport --input <ファイル> --output <フォルダ> [--type auto|geometry|texture]
                            [--settings <import.json>] [--set Name=Value ...] [--json]

              -i, --input     インポートするファイル。FBX(.fbx)はジオメトリ、画像
                              (.bmp .png .jpg .jpeg .tiff .tif .tga .dds .hdr)はテクスチャになる。
              -o, --output    .asset を書き出すフォルダ(なければ作る)。ファイル名は入力の名前を小文字にしたもの。
              -t, --type      auto(既定、拡張子から決める), geometry, texture。拡張子と合わなければ終了コード4。
              -s, --settings  インポート設定のJSON: { "geometry": { ... }, "texture": { ... } }(どちらも省略可)。
                  --set       インポート設定の1項目(入力の種類の設定に適用。設定ファイルより後に適用)。繰り返し指定できる。
                              項目名はエディタのインポート設定と同じ(大文字・小文字は区別しない)。
                              geometry: CalculateNormals, CalculateTangents, SmoothingAngle, ReverseHandedness,
                                        ImportEmbeddedTextures, ImportAnimations, CoalesceMeshes
                              texture:  Dimension, MipLevels, AlphaThreshold, PreferBC7, FormatIndex, Compress,
                                        CubeMapSize, MirrorCubeMap, PrefilterCubeMap
                  --json      結果をJSONで標準出力に出す(ログは常に標準エラー)。
              -h, --help      この説明を出す。

            FBXの埋め込みテクスチャ(ImportEmbeddedTextures)は、エディタと同じく既定のテクスチャ設定で同じフォルダへ取り込む。

            終了コード:
              0  成功
              1  インポートまたは保存に失敗した(ContentTools がファイルを読めなかった、壊れたファイルなど)
              2  引数の誤り
              3  入力ファイルがない
              4  対応していない形式(拡張子)、または --type と拡張子が合わない
              5  インポート設定の誤り(JSONの誤り、知らない項目、型の合わない値)
              6  出力先のフォルダを作れない
              7  ContentTools.dll を読み込めない(実行ファイルと同じフォルダに必要)
              10 想定外のエラー
            """;

        private static readonly object _consoleLock = new();
        private static ImportResult? _currentResult;

        [STAThread]
        static int Main(string[] args)
        {
            Console.OutputEncoding = new UTF8Encoding(false);
            var options = CommandLineOptions.Parse(args, out var error);
            var result = new ImportResult { Input = options.Input ?? string.Empty };
            var embedded = new List<ImportResult>();
            ExitCode exitCode;

            if (options.Help && error == null)
            {
                Console.WriteLine(HelpText);
                return (int)ExitCode.Success;
            }

            try
            {
                if (error != null) throw new ExitException(ExitCode.InvalidArguments, error);
                exitCode = Run(options, result, embedded);
            }
            catch (ExitException ex)
            {
                exitCode = ex.Code;
                result.Errors.Add(ex.Message);
                WriteLog("error", ex.Message);
                if (ex.Code == ExitCode.InvalidArguments) Console.Error.WriteLine("使い方は --help を見てください。");
            }
            catch (Exception ex)
            {
                exitCode = ExitCode.InternalError;
                result.Errors.Add($"想定外のエラー: {ex}");
                WriteLog("error", $"想定外のエラー: {ex}");
            }

            result.Success = exitCode == ExitCode.Success;

            if (options.Json)
            {
                var output = new
                {
                    input = result.Input,
                    type = result.Type,
                    output = options.Output != null ? Path.GetFullPath(options.Output) : null,
                    success = result.Success,
                    exitCode = (int)exitCode,
                    exitCodeName = exitCode.ToString(),
                    files = result.Files,
                    embeddedTextures = embedded.Select(x => new { input = x.Input, success = x.Success, files = x.Files, errors = x.Errors }),
                    settings = _settingsForJson,
                    errors = result.Errors,
                    warnings = result.Warnings,
                };
                Console.WriteLine(JsonSerializer.Serialize(output, new JsonSerializerOptions
                {
                    WriteIndented = true,
                    Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
                }));
            }
            else if (result.Success)
            {
                foreach (var file in result.Files.Concat(embedded.SelectMany(x => x.Files))) Console.WriteLine(file);
            }

            return (int)exitCode;
        }

        private static Dictionary<string, object?>? _settingsForJson;

        private static ExitCode Run(CommandLineOptions options, ImportResult result, List<ImportResult> embedded)
        {
            // 1. インポート設定(エディタの既定値に、設定ファイル、--set の順で上書きする)
            var geometrySettings = new GeometryImportSettings();
            var textureSettings = new TextureImportSettings();
            try
            {
                if (options.SettingsFile != null)
                {
                    if (!File.Exists(options.SettingsFile)) throw new InvalidSettingsException($"設定ファイルがありません: {options.SettingsFile}");
                    ImportSettingsBinder.ApplyFile(options.SettingsFile, geometrySettings, textureSettings);
                }
            }
            catch (InvalidSettingsException ex)
            {
                throw new ExitException(ExitCode.InvalidSettings, ex.Message);
            }

            // 2. 入力ファイルと種類
            var input = Path.GetFullPath(options.Input!);
            result.Input = input;
            if (!File.Exists(input))
            {
                throw new ExitException(ExitCode.InputNotFound, $"入力ファイルがありません: {input}");
            }

            var ext = Path.GetExtension(input).ToLowerInvariant();
            var detectedType = ContentHelper.MeshFileExtensions.Contains(ext) ? GeometryType :
                               ContentHelper.ImageFileExtensions.Contains(ext) ? TextureType : null;
            if (detectedType == null)
            {
                throw new ExitException(ExitCode.UnsupportedFormat,
                    $"対応していない形式です: {ext}(対応: {string.Join(" ", ContentHelper.MeshFileExtensions.Concat(ContentHelper.ImageFileExtensions))})");
            }
            if (options.Type != "auto" && options.Type != detectedType)
            {
                throw new ExitException(ExitCode.UnsupportedFormat, $"--type {options.Type} に {ext} は使えません({ext} は {detectedType})。");
            }
            result.Type = detectedType;

            IAssetImportSettings settings = detectedType == GeometryType ? geometrySettings : textureSettings;
            try
            {
                foreach (var assignment in options.Assignments)
                {
                    ImportSettingsBinder.ApplyAssignment(settings, detectedType, assignment);
                }
            }
            catch (InvalidSettingsException ex)
            {
                throw new ExitException(ExitCode.InvalidSettings, ex.Message);
            }

            // 3. 出力先
            string output;
            try
            {
                output = Path.GetFullPath(options.Output!);
                Directory.CreateDirectory(output);
            }
            catch (Exception ex)
            {
                throw new ExitException(ExitCode.OutputError, $"出力先のフォルダを作れません: {options.Output}({ex.Message})");
            }

            // 4. エディタのコードを動かす環境(WPF の Application とリソース、ContentTools.dll、ログ)
            HeadlessEnvironment.Initialize();
            Logger.MessageLogged += OnMessageLogged;
            _currentResult = result;

            var tempFolder = Path.Combine(Path.GetTempPath(), "DXForgeImport", Guid.NewGuid().ToString("N")) + Path.DirectorySeparatorChar;
            ContentImportEnvironment.TempFolder = tempFolder;
            ContentImportEnvironment.ImportEmbeddedFiles = (files, destination) =>
            {
                // エディタ(ConfigureImportSettings.AddFiles)と同じく、画像の拡張子のファイルだけを、既定のテクスチャ設定で取り込む。
                var previous = _currentResult;
                foreach (var file in files.Where(f => ContentHelper.ImageFileExtensions.Contains(Path.GetExtension(f).ToLower())))
                {
                    var embeddedResult = new ImportResult { Input = file, Type = TextureType };
                    _currentResult = embeddedResult;
                    embeddedResult.Success = ImportAndSave(file, new TextureImportSettings(), destination, embeddedResult);
                    embedded.Add(embeddedResult);
                    if (!embeddedResult.Success)
                    {
                        previous?.Warnings.Add($"埋め込みテクスチャを取り込めませんでした: {Path.GetFileName(file)}");
                    }
                }
                _currentResult = previous;
            };

            try
            {
                _settingsForJson = ImportSettingsBinder.ToDictionary(settings);
                var succeeded = ImportAndSave(input, settings, output, result);
                _settingsForJson = ImportSettingsBinder.ToDictionary(settings);
                return succeeded ? ExitCode.Success : ExitCode.ImportFailed;
            }
            finally
            {
                _currentResult = null;
                Logger.MessageLogged -= OnMessageLogged;
                ContentImportEnvironment.TempFolder = null;
                ContentImportEnvironment.ImportEmbeddedFiles = null;
                HeadlessEnvironment.Shutdown();
                try
                {
                    if (Directory.Exists(tempFolder)) Directory.Delete(tempFolder, true);
                    // 共有の親フォルダ(%TEMP%\DXForgeImport)は、空なら消す(同時に動いている別のインポートがあれば空ではない)。
                    var parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(tempFolder));
                    if (parent != null && Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any()) Directory.Delete(parent);
                }
                catch (Exception ex)
                {
                    WriteLog("warning", $"一時フォルダを削除できませんでした: {tempFolder}({ex.Message})");
                }
            }
        }

        /// <summary>
        /// エディタのインポート(ContentHelper.Import)と同じ手順で、アセットを作り、出力先のパスを決め、Import() して Save() する。
        /// エディタの画面に出す進捗(ImportingItem)は使わない。
        /// </summary>
        private static bool ImportAndSave(string file, IAssetImportSettings settings, string destination, ImportResult result)
        {
            if (settings is TextureImportSettings textureSettings)
            {
                // エディタではインポート設定の画面(TextureImportSettingsConfigurator.Import)がソース画像を設定する。
                textureSettings.Sources.Clear();
                textureSettings.Sources.Add(file);
            }

            var asset = ContentHelper.CreateAsset(file, settings);
            if (asset == null)
            {
                result.Errors.Add($"対応していない形式です: {file}");
                return false;
            }

            asset.FullPath = ContentHelper.GetAssetFullPath(ContentHelper.GetAssetName(file), destination);
            var imported = asset.Import(file);
            // FBXのインポートに失敗しても、Geometry.Import() は true を返すことがある(ContentToolsAPI.ImportFbx が失敗を例外にしないため)。
            // その状態で Save() すると LODグループがなく、保存するものがないので、ここで失敗にする。
            if (imported && asset is Geometry geometry && !geometry.HasLODGroups)
            {
                imported = false;
            }

            if (!imported)
            {
                if (!result.Errors.Any()) result.Errors.Add($"インポートに失敗しました: {file}");
                return false;
            }

            var saved = asset.Save(asset.FullPath).ToList();
            if (!saved.Any())
            {
                if (!result.Errors.Any()) result.Errors.Add($"保存に失敗しました: {asset.FullPath}");
                return false;
            }

            result.Files.AddRange(saved);
            return true;
        }

        private static void OnMessageLogged(MessageType type, string message)
        {
            var level = type switch { MessageType.Error => "error", MessageType.Warning => "warning", _ => "info" };
            WriteLog(level, message);
            lock (_consoleLock)
            {
                if (type == MessageType.Error) _currentResult?.Errors.Add(message);
                else if (type == MessageType.Warning) _currentResult?.Warnings.Add(message);
            }
        }

        internal static void WriteLog(string level, string message)
        {
            lock (_consoleLock)
            {
                Console.Error.WriteLine($"[{level}] {message}");
            }
        }
    }
}
