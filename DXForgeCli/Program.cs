using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using DXForgeEditor.Components;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;

namespace DXForgeCli
{
    /// <summary>
    /// dxforge 自身の終了コード。ランナー(EngineUnitTests --game-bin)の終了コード(0, 2, 3, 10〜15)はそのまま返すので、
    /// それと重ならない 30〜39 を使う(2 だけは共通。JSON の error.code で区別できる)。--help と README.md にも同じ一覧を書く。
    /// </summary>
    enum ExitCode
    {
        Success = 0,
        InvalidArguments = 2,
        RunnerAborted = 3,
        ProjectNotFound = 30,
        ProjectInvalid = 31,
        SceneNotFound = 32,
        ScriptsUnsupported = 33,
        RunnerNotFound = 34,
        RunnerFailed = 35,
        GameBinWriteFailed = 36,
        InternalError = 39,
    }

    class ExitException : Exception
    {
        public ExitCode Code { get; }
        public string ErrorCode { get; }
        public ExitException(ExitCode code, string errorCode, string message) : base(message)
        {
            Code = code;
            ErrorCode = errorCode;
        }
    }

    class CommandLineOptions
    {
        public string? Project { get; set; }
        public string? Scene { get; set; }
        public uint? Frames { get; set; }
        public double Dt { get; set; } = 0.016;
        public string? Runner { get; set; }
        public string? KeepGameBin { get; set; }
        public string? ExportGameBin { get; set; }
        public bool Json { get; set; }
        public bool Help { get; set; }

        public bool ExportOnly => ExportGameBin != null;

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
                        throw new FormatException($"{arg} の値がありません。");
                    }
                    return args[++i];
                }

                try
                {
                    switch (arg)
                    {
                        case "-h": case "--help": options.Help = true; break;
                        case "--json": break;
                        case "--load-project": options.Project = NextValue(); break;
                        case "--scene": options.Scene = NextValue(); break;
                        case "--frames":
                            {
                                var value = NextValue();
                                if (!uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var frames))
                                {
                                    throw new FormatException($"--frames は 0 以上の整数です: {value}");
                                }
                                options.Frames = frames;
                                break;
                            }
                        case "--dt":
                            {
                                var value = NextValue();
                                if (!double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var dt) || !double.IsFinite(dt) || dt < 0.0)
                                {
                                    throw new FormatException($"--dt は 0 以上の有限の数です: {value}");
                                }
                                options.Dt = dt;
                                break;
                            }
                        case "--runner": options.Runner = NextValue(); break;
                        case "--keep-game-bin": options.KeepGameBin = NextValue(); break;
                        case "--export-game-bin": options.ExportGameBin = NextValue(); break;
                        default: throw new FormatException($"知らない引数です: {arg}");
                    }
                }
                catch (FormatException ex)
                {
                    error = ex.Message;
                    return options;
                }
            }

            if (options.Help) return options;

            if (string.IsNullOrWhiteSpace(options.Project)) error = "--load-project を指定してください。";
            else if (options.ExportOnly && (options.Frames != null || options.Runner != null || options.KeepGameBin != null))
                error = "--export-game-bin は --frames, --runner, --keep-game-bin と一緒には使えません(書き出すだけで実行しない)。";
            else if (!options.ExportOnly && options.Frames == null) error = "--frames を指定してください(実行しないなら --export-game-bin)。";

            return options;
        }
    }

    /// <summary>結果のJSONと終了コードのもとになる、実行の状態。</summary>
    class RunResult
    {
        public string? ProjectPath { get; set; }
        public string? SceneName { get; set; }
        public List<string> EntityNames { get; } = new();
        public string? GameBin { get; set; }
        public string? RunnerPath { get; set; }
        public int ExitCode { get; set; }
        public string? ErrorCode { get; set; }
        public string? ErrorMessage { get; set; }
        /// <summary>ランナーが出したJSON(実行できたときだけ)。</summary>
        public JsonObject? RunnerJson { get; set; }
    }

    static class Program
    {
        private const string HelpText = """
            dxforge - エディタを開かずにプロジェクト(.dxproj)を読み込み、シーンを指定のフレーム数だけ実行する(Issue #11)。

            使い方:
              dxforge --load-project <.dxproj> --frames <N> [--dt <秒>] [--scene <名前>]
                      [--runner <EngineUnitTests.exe>] [--keep-game-bin <パス>] [--json]
              dxforge --load-project <.dxproj> --export-game-bin <パス> [--scene <名前>] [--json]

                  --load-project     読み込むプロジェクトファイル(エディタが保存した .dxproj)。
                  --frames           スクリプトの更新を回すフレーム数(0 以上)。
                  --dt               各フレームの経過時間(秒、既定 0.016)。
                  --scene            実行するシーンの名前(既定はアクティブなシーン。エディタと同じ)。
                  --runner           ヘッドレスのランナー(既定は <この exe のフォルダ>\..\<Debug|Release>\EngineUnitTests.exe。
                                     DebugEditor の dxforge は Debug、ReleaseEditor は Release)。
                  --keep-game-bin    実行に使った game.bin をこのパスに残す(既定は一時フォルダに書いて、実行後に消す)。
                  --export-game-bin  エディタがゲームの実行前に書くものと同じ game.bin を書き出すだけで、実行しない。
                  --json             結果をJSONで標準出力に出す(ログは常に標準エラー)。
              -h, --help             この説明を出す。

            この版ではスクリプト(Script コンポーネント)のあるシーンは実行できない(終了コード 33)。--export-game-bin は書き出せる。

            終了コード:
              0  成功
              2  引数の誤り(dxforge は error.code が invalid_arguments、ランナーは usage_error)
              3  ランナーが abort() で終わった(assert の失敗など)
              10〜15 ランナーが game.bin を読めなかった(EngineUnitTests --help を参照)
              30 .dxproj がない
              31 .dxproj を読み込めない(XMLが壊れている、プロジェクトの形式ではない)
              32 シーンがない(アクティブなシーンがない、--scene の名前のシーンがない)
              33 シーンにスクリプトがある(この版では実行できない)
              34 ランナーがない
              35 ランナーを起動できない、またはランナーの結果を読めない
              36 game.bin を書き出せない
              39 想定外のエラー
            """;

        private static readonly object _consoleLock = new();

        [STAThread]
        static int Main(string[] args)
        {
            Console.OutputEncoding = new UTF8Encoding(false);
            var options = CommandLineOptions.Parse(args, out var error);
            var result = new RunResult();

            if (options.Help && error == null)
            {
                Console.WriteLine(HelpText);
                return (int)ExitCode.Success;
            }

            try
            {
                if (error != null) throw new ExitException(ExitCode.InvalidArguments, "invalid_arguments", error);
                Run(options, result);
            }
            catch (ExitException ex)
            {
                result.ExitCode = (int)ex.Code;
                result.ErrorCode = ex.ErrorCode;
                result.ErrorMessage = ex.Message;
                WriteLog("error", ex.Message);
                if (ex.Code == ExitCode.InvalidArguments) Console.Error.WriteLine("使い方は --help を見てください。");
            }
            catch (Exception ex)
            {
                result.ExitCode = (int)ExitCode.InternalError;
                result.ErrorCode = "internal_error";
                result.ErrorMessage = $"想定外のエラー: {ex}";
                WriteLog("error", result.ErrorMessage);
            }

            if (options.Json) Console.WriteLine(ToJson(options, result).ToJsonString(_jsonOptions));
            else WriteHumanResult(options, result);
            Console.Out.Flush();
            return result.ExitCode;
        }

        private static readonly JsonSerializerOptions _jsonOptions = new()
        {
            // 日本語のエンティティ名やパスを \uXXXX にしない(標準出力は UTF-8)
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };

        private static void Run(CommandLineOptions options, RunResult result)
        {
            Debug.Assert(options.Project != null);
            var projectPath = Path.GetFullPath(options.Project);
            result.ProjectPath = projectPath;
            if (!File.Exists(projectPath))
            {
                throw new ExitException(ExitCode.ProjectNotFound, "project_not_found", $".dxproj がありません: {projectPath}");
            }

            // 実行しないときもランナーを先に探すと、ランナーがないだけで書き出せなくなるので、実行するときだけ探す
            if (!options.ExportOnly)
            {
                result.RunnerPath = Path.GetFullPath(options.Runner ?? DefaultRunnerPath());
                if (!File.Exists(result.RunnerPath))
                {
                    throw new ExitException(ExitCode.RunnerNotFound, "runner_not_found",
                        $"ランナーがありません: {result.RunnerPath}(./build.ps1 -Target native -Configuration {NativeConfiguration} -Project EngineUnitTests でビルドするか、--runner で指定してください)");
                }
            }

            var project = LoadProject(projectPath);
            var scene = SelectScene(project, options.Scene);
            result.SceneName = scene.Name;
            result.EntityNames.AddRange(scene.GameEntities.Select(x => x.Name));

            if (options.ExportOnly)
            {
                Debug.Assert(options.ExportGameBin != null);
                result.GameBin = Path.GetFullPath(options.ExportGameBin);
                WriteGameBin(scene, result.GameBin);
                return;
            }

            var scripts = scene.GameEntities.SelectMany(x => x.Components.OfType<Script>().Select(s => $"{x.Name}: {s.Name}")).ToList();
            if (scripts.Any())
            {
                throw new ExitException(ExitCode.ScriptsUnsupported, "scripts_unsupported",
                    $"シーン {scene.Name} にスクリプトがあります(この版ではスクリプトのあるシーンは実行できません): {string.Join(", ", scripts)}");
            }

            string? tempFolder = null;
            string gameBin;
            if (options.KeepGameBin != null)
            {
                gameBin = Path.GetFullPath(options.KeepGameBin);
                result.GameBin = gameBin;
            }
            else
            {
                tempFolder = Path.Combine(Path.GetTempPath(), "dxforge", Guid.NewGuid().ToString("N"));
                gameBin = Path.Combine(tempFolder, "game.bin");
            }

            try
            {
                WriteGameBin(scene, gameBin);
                RunRunner(result.RunnerPath!, gameBin, options, result);
            }
            finally
            {
                if (tempFolder != null) DeleteTempFolder(tempFolder);
            }
        }

        private static string NativeConfiguration =>
#if DEBUG
            "Debug";
#else
            "Release";
#endif

        /// <summary>build.ps1 の出力の配置(x64\&lt;構成&gt;\)で、この exe(x64\DebugEditor\ か x64\ReleaseEditor\)と対になる構成のランナー。</summary>
        private static string DefaultRunnerPath() =>
            Path.Combine(AppContext.BaseDirectory, "..", NativeConfiguration, "EngineUnitTests.exe");

        /// <summary>
        /// エディタと同じ Project.Load で .dxproj を読み込む。エディタの読み込みの副作用(ゲームのコードの DLL のビルド、
        /// エンジンへのエンティティの作成)は ProjectLoadEnvironment.Headless で止める。
        /// </summary>
        private static Project LoadProject(string path)
        {
            // エディタのログ(Logger.Log)は Application.Current.Dispatcher に積むので、Run() しない素の Application を作る。
            // NOTE: Dispatcher は回さず、Shutdown() もしない(止めた Dispatcher に Logger.Log が await するとプロセスが落ちる)。
            _ = Application.Current ?? new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            Logger.MessageLogged += OnMessageLogged;
            ProjectLoadEnvironment.Headless = true;

            // Project/Scene の [OnDeserialized] で失敗した Debug.Assert は、DebugEditor では FailFast でプロセスを止めるので、
            // 標準エラーに出すだけにして、このあと自分で確かめる。
            var listeners = Trace.Listeners.Cast<TraceListener>().ToList();
            Trace.Listeners.Clear();
            Trace.Listeners.Add(new AssertToStderrListener());

            // Project の [OnDeserialized] は async void なので、そこで投げられた例外は ReadObject の外へ出ず、
            // SynchronizationContext に渡される(なければスレッドプールで投げられてプロセスが落ちる)。それを捕まえる。
            var previousContext = SynchronizationContext.Current;
            var context = new CapturingSynchronizationContext();
            SynchronizationContext.SetSynchronizationContext(context);
            Project? project;
            try
            {
                project = Project.Load(path);
            }
            catch (Exception ex)
            {
                throw new ExitException(ExitCode.ProjectInvalid, "project_invalid", $".dxproj を読み込めません: {path}({ex.GetType().Name}: {ex.Message})");
            }
            finally
            {
                SynchronizationContext.SetSynchronizationContext(previousContext);
                Trace.Listeners.Clear();
                Trace.Listeners.AddRange(listeners.ToArray());
            }

            if (context.Exceptions.Any())
            {
                var ex = context.Exceptions[0];
                throw new ExitException(ExitCode.ProjectInvalid, "project_invalid", $".dxproj を読み込めません: {path}({ex.GetType().Name}: {ex.Message})");
            }
            // ルート要素が nil なら Serializer.FromFile は null を返す。Scenes がない XML では Scenes が作られない。
            if (project == null || project.Scenes == null)
            {
                throw new ExitException(ExitCode.ProjectInvalid, "project_invalid", $".dxproj にプロジェクトかシーンの一覧がありません: {path}");
            }
            return project;
        }

        private static Scene SelectScene(Project project, string? name)
        {
            Scene? scene = name == null
                ? project.ActiveScene
                : project.Scenes.FirstOrDefault(x => x.Name == name);
            if (scene == null)
            {
                var names = string.Join(", ", project.Scenes.Select(x => x.Name));
                throw new ExitException(ExitCode.SceneNotFound, "scene_not_found",
                    name == null ? $"アクティブなシーンがありません(シーン: {names})" : $"シーン {name} がありません(シーン: {names})");
            }
            // GameEntities/Components の要素がない XML では、コレクションが作られない
            if (scene.GameEntities == null || scene.GameEntities.Any(x => x.Components == null))
            {
                throw new ExitException(ExitCode.ProjectInvalid, "project_invalid", $"シーン {scene.Name} のエンティティかコンポーネントの一覧がありません");
            }
            return scene;
        }

        private static void WriteGameBin(Scene scene, string path)
        {
            try
            {
                var folder = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(folder)) Directory.CreateDirectory(folder);
                // エディタの Project.SaveToBinary と同じ開き方と同じ書き出し(Project.WriteGameBinary)
                using var bw = new BinaryWriter(File.Open(path, FileMode.Create, FileAccess.Write));
                Project.WriteGameBinary(scene, bw);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException || ex is NotSupportedException)
            {
                throw new ExitException(ExitCode.GameBinWriteFailed, "game_bin_write_failed", $"game.bin を書き出せません: {path}({ex.Message})");
            }
            WriteLog("info", $"game.bin を書き出しました: {path}({scene.GameEntities.Count} エンティティ)");
        }

        private static void RunRunner(string runner, string gameBin, CommandLineOptions options, RunResult result)
        {
            var startInfo = new ProcessStartInfo(runner)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };
            startInfo.ArgumentList.Add("--game-bin");
            startInfo.ArgumentList.Add(gameBin);
            startInfo.ArgumentList.Add("--frames");
            startInfo.ArgumentList.Add(options.Frames!.Value.ToString(CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--dt");
            startInfo.ArgumentList.Add(options.Dt.ToString("R", CultureInfo.InvariantCulture));
            startInfo.ArgumentList.Add("--json");

            string stdout;
            int exitCode;
            try
            {
                using var process = new Process { StartInfo = startInfo };
                // ランナーのログ(標準エラー)はそのまま標準エラーへ流す。標準出力は JSON だけにする。
                process.ErrorDataReceived += (_, e) => { if (e.Data != null) WriteRaw(e.Data); };
                process.Start();
                process.BeginErrorReadLine();
                stdout = process.StandardOutput.ReadToEnd();
                process.WaitForExit();
                exitCode = process.ExitCode;
            }
            catch (Exception ex) when (ex is System.ComponentModel.Win32Exception || ex is InvalidOperationException || ex is IOException)
            {
                throw new ExitException(ExitCode.RunnerFailed, "runner_failed", $"ランナーを起動できません: {runner}({ex.Message})");
            }

            JsonObject? json = null;
            try
            {
                json = JsonNode.Parse(stdout) as JsonObject;
            }
            catch (JsonException)
            {
            }

            if (json == null)
            {
                // abort()(assert の失敗など)は JSON を出さずに 3 で終わる。ランナーの終了コードとして、そのまま返す。
                if (exitCode == (int)ExitCode.RunnerAborted)
                {
                    throw new ExitException(ExitCode.RunnerAborted, "runner_aborted", $"ランナーが abort() で終わりました(終了コード {exitCode})");
                }
                throw new ExitException(ExitCode.RunnerFailed, "runner_failed",
                    $"ランナーの結果(JSON)を読めません(終了コード {exitCode}): {Truncate(stdout.Trim(), 200)}");
            }

            var reported = json["exit_code"]?.GetValueKind() == JsonValueKind.Number ? json["exit_code"]!.GetValue<int>() : (int?)null;
            if (reported != exitCode)
            {
                throw new ExitException(ExitCode.RunnerFailed, "runner_failed",
                    $"ランナーの終了コード({exitCode})と JSON の exit_code({reported?.ToString() ?? "なし"})が一致しません");
            }

            result.RunnerJson = json;
            result.ExitCode = exitCode;
            if (exitCode != 0 && json["error"] is JsonObject error)
            {
                result.ErrorCode = error["code"]?.ToString();
                result.ErrorMessage = error["message"]?.ToString();
            }
        }

        /// <summary>
        /// 結果のJSON。ランナー(EngineUnitTests --game-bin --json)のJSONの項目をそのまま使い、先頭にプロジェクトの情報を足す。
        /// 各エンティティには、エディタのエンティティ名(name)を足す(スクリプトのないシーンではエンティティは消えないので、
        /// ランナーのエンティティはシーンの順番どおりに並ぶ)。
        /// </summary>
        private static JsonObject ToJson(CommandLineOptions options, RunResult result)
        {
            var json = new JsonObject
            {
                ["mode"] = options.ExportOnly ? "export_game_bin" : "load_project",
                ["project"] = result.ProjectPath ?? options.Project,
                ["scene"] = result.SceneName,
                ["game_bin"] = result.GameBin,
            };

            if (options.ExportOnly)
            {
                json["success"] = result.ExitCode == 0;
                json["exit_code"] = result.ExitCode;
                json["error"] = ErrorJson(result);
                json["entity_count"] = result.ExitCode == 0 ? result.EntityNames.Count : 0;
                return json;
            }

            json["runner"] = result.RunnerPath;
            var runner = result.RunnerJson;
            if (runner == null)
            {
                json["frames_requested"] = options.Frames;
                json["frames_run"] = 0;
                json["dt"] = options.Dt;
                json["success"] = false;
                json["exit_code"] = result.ExitCode;
                json["error"] = ErrorJson(result);
                json["entity_count"] = 0;
                json["entities"] = new JsonArray();
                return json;
            }

            foreach (var key in new[] { "frames_requested", "frames_run", "dt", "success", "exit_code", "error", "entity_count" })
            {
                json[key] = runner[key]?.DeepClone();
            }

            var entities = new JsonArray();
            if (runner["entities"] is JsonArray runnerEntities)
            {
                var named = runnerEntities.Count == result.EntityNames.Count;
                for (int i = 0; i < runnerEntities.Count; ++i)
                {
                    if (runnerEntities[i] is not JsonObject entity) continue;
                    var copy = new JsonObject { ["id"] = entity["id"]?.DeepClone() };
                    if (named) copy["name"] = result.EntityNames[i];
                    foreach (var (key, value) in entity)
                    {
                        if (key != "id") copy[key] = value?.DeepClone();
                    }
                    entities.Add(copy);
                }
            }
            json["entities"] = entities;
            return json;
        }

        private static JsonNode? ErrorJson(RunResult result) =>
            result.ExitCode == 0 ? null : new JsonObject { ["code"] = result.ErrorCode, ["message"] = result.ErrorMessage };

        private static void WriteHumanResult(CommandLineOptions options, RunResult result)
        {
            if (result.ExitCode != 0)
            {
                Console.WriteLine($"dxforge: FAILED (exit code {result.ExitCode}) {result.ErrorCode}: {result.ErrorMessage}");
                return;
            }
            if (options.ExportOnly)
            {
                Console.WriteLine($"dxforge: OK, wrote {result.GameBin} ({result.EntityNames.Count} entities, scene {result.SceneName})");
                return;
            }

            var runner = result.RunnerJson!;
            Console.WriteLine($"dxforge: OK, scene {result.SceneName}, {runner["frames_run"]} frames, {runner["entity_count"]} entities");
            if (runner["entities"] is JsonArray entities)
            {
                for (int i = 0; i < entities.Count; ++i)
                {
                    var name = entities.Count == result.EntityNames.Count ? result.EntityNames[i] : "?";
                    Console.WriteLine($"  entity {entities[i]?["id"]} ({name}): position {entities[i]?["position"]?.ToJsonString()}");
                }
            }
        }

        private static void DeleteTempFolder(string tempFolder)
        {
            try
            {
                if (Directory.Exists(tempFolder)) Directory.Delete(tempFolder, true);
                // 共有の親フォルダ(%TEMP%\dxforge)は、空なら消す(同時に動いている別の実行があれば空ではない)。
                var parent = Path.GetDirectoryName(tempFolder);
                if (parent != null && Directory.Exists(parent) && !Directory.EnumerateFileSystemEntries(parent).Any()) Directory.Delete(parent);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                WriteLog("warning", $"一時フォルダを削除できませんでした: {tempFolder}({ex.Message})");
            }
        }

        private static string Truncate(string text, int length) => text.Length <= length ? text : text[..length] + "...";

        private static void OnMessageLogged(MessageType type, string message)
        {
            var level = type switch { MessageType.Error => "error", MessageType.Warning => "warning", _ => "info" };
            WriteLog(level, message);
        }

        internal static void WriteLog(string level, string message) => WriteRaw($"[{level}] {message}");

        private static void WriteRaw(string line)
        {
            lock (_consoleLock)
            {
                Console.Error.WriteLine(line);
            }
        }
    }

    /// <summary>失敗した Debug.Assert を、プロセスを止めずに標準エラーへ出す。</summary>
    class AssertToStderrListener : TraceListener
    {
        public override void Fail(string? message, string? detailMessage)
        {
            // エディタの Debug.Assert(条件) にはメッセージがないので、どこで失敗したかをエディタのメソッドの名前で示す
            var where = new StackTrace().GetFrames()
                .Select(f => f.GetMethod())
                .FirstOrDefault(m => m?.DeclaringType?.Namespace?.StartsWith("DXForgeEditor") == true);
            var location = where == null ? string.Empty : $"{where.DeclaringType!.FullName}.{where.Name}";
            Program.WriteLog("warning", $"Debug.Assert が失敗しました: {location} {message} {detailMessage}".TrimEnd());
        }
        public override void Write(string? message) { }
        public override void WriteLine(string? message) { }
    }

    /// <summary>
    /// async void のメソッドが投げた例外(Post される)を、その場で実行して捕まえる。
    /// それ以外の Post もその場で実行する(読み込みの間に Post されるのは、async void の例外だけ)。
    /// </summary>
    class CapturingSynchronizationContext : SynchronizationContext
    {
        public List<Exception> Exceptions { get; } = new();

        public override void Post(SendOrPostCallback d, object? state)
        {
            try
            {
                d(state);
            }
            catch (Exception ex)
            {
                Exceptions.Add(ex);
            }
        }

        public override void Send(SendOrPostCallback d, object? state) => Post(d, state);
    }
}
