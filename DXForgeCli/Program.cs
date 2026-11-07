using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using DXForgeEditor.Automation;
using DXForgeEditor.Components;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;

namespace DXForgeCli
{
    /// <summary>
    /// dxforge 自身の終了コード。ランナー(EngineUnitTests --game-bin)の終了コード(0, 2, 3, 10〜15)はそのまま返すので、
    /// それと重ならない 30〜39 と 40〜44(ops、#183)を使う(2 だけは共通。JSON の error.code で区別できる)。--help と README.md にも同じ一覧を書く。
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
        OpsInvalid = 40,
        OpsTargetNotFound = 41,
        OpsFailed = 42,
        OpsSaveFailed = 43,
        OpsEditorOpen = 44,
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
        public string? OpsFile { get; set; }
        public string? SingleOp { get; set; }
        public string? Importer { get; set; }
        public bool DryRun { get; set; }

        public bool ExportOnly => ExportGameBin != null;
        public bool OpsMode => OpsFile != null || SingleOp != null;

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
                        case "--ops": options.OpsFile = NextValue(); break;
                        case "--op": options.SingleOp = NextValue(); break;
                        case "--dry-run": options.DryRun = true; break;
                        case "--importer": options.Importer = NextValue(); break;
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
            else if (options.OpsFile != null && options.SingleOp != null) error = "--ops と --op は一緒には使えません。";
            else if (options.Importer != null && !options.OpsMode) error = "--importer は --ops か --op と一緒に使います。";
            else if (options.OpsMode && (options.Frames != null || options.ExportOnly || options.Runner != null || options.KeepGameBin != null || options.Scene != null || args.Contains("--dt")))
                error = "--ops, --op は --frames, --dt, --scene, --runner, --keep-game-bin, --export-game-bin と一緒には使えません(シーンは操作の中で指定する)。";
            else if (options.OpsMode) { }
            else if (options.DryRun) error = "--dry-run は --ops か --op と一緒に使います。";
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
        /// <summary>--ops, --op のときの、操作ごとの結果。</summary>
        public JsonArray OpResults { get; set; } = new();
        public int OpCount { get; set; }
        public bool Changed { get; set; }
        public bool Saved { get; set; }
        /// <summary>ops の失敗の場所(error に入れる)。</summary>
        public int? ErrorOpIndex { get; set; }
        public string? ErrorKey { get; set; }
        /// <summary>取り込みの片付けで消せずに残ったパス。</summary>
        public IReadOnlyList<string> LeftoverPaths { get; set; } = [];
    }

    static class Program
    {
        private const string HelpText = """
            dxforge - エディタを開かずにプロジェクト(.dxproj)を読み込み、シーンを指定のフレーム数だけ実行する(Issue #11)。

            使い方:
              dxforge --load-project <.dxproj> --frames <N> [--dt <秒>] [--scene <名前>]
                      [--runner <EngineUnitTests.exe>] [--keep-game-bin <パス>] [--json]
              dxforge --load-project <.dxproj> --export-game-bin <パス> [--scene <名前>] [--json]
              dxforge --load-project <.dxproj> --ops <ops.json> [--dry-run] [--importer <DXForgeImport.exe>] [--json]
              dxforge --load-project <.dxproj> --op '<1 つの操作の JSON>' [--dry-run] [--importer <DXForgeImport.exe>] [--json]

                  --load-project    読み込むプロジェクトファイル(エディタが保存した .dxproj)。
                  --frames           スクリプトの更新を回すフレーム数(0 以上)。
                  --dt               各フレームの経過時間(秒、既定 0.016)。
                  --scene            実行するシーンの名前(既定はアクティブなシーン。エディタと同じ)。
                  --runner           ヘッドレスのランナー(既定は <この exe のフォルダ>\..\<Debug|Release>\EngineUnitTests.exe。
                                     DebugEditor の dxforge は Debug、ReleaseEditor は Release)。
                  --keep-game-bin    実行に使った game.bin をこのパスに残す(既定は一時フォルダに書いて、実行後に消す)。
                  --export-game-bin  エディタがゲームの実行前に書くものと同じ game.bin を書き出すだけで、実行しない。
                  --ops              シーンを操作する JSON({"ops":[...]})のファイルを順に実行する(Issue #183)。
                  --op               1 つの操作の JSON を実行する(--ops の糖衣)。
                  --dry-run          操作を検証して結果を返すが、保存しない。
                  --importer         import-asset に使うインポーター(既定は <この exe のフォルダ>\DXForgeImport.exe)。
                  --json             結果をJSONで標準出力に出す(ログは常に標準エラー)。
              -h, --help             この説明を出す。

            この版ではスクリプト(Script コンポーネント)のあるシーンは実行できない(終了コード 33)。--export-game-bin は書き出せる。

            操作:
              読む    list-scenes, list-entities, get-entity
              変える  create-entity, remove-entity, rename, set-enabled, set-transform,
                      add-component (transform/script/geometry), remove-component, set-geometry,
                      create-scene, remove-scene, set-active-scene,
                      import-asset (source: 絶対パス, folder: Content からの相対フォルダ, settings: インポート設定)
              対象    guid:<GUID> | name:<名前> | index:<シーン名>/<番号> | $<別名>(create-entity の "as")
              アセット guid:<GUID> | path:<Content からの相対パス> | default:cube
              回転    rotation_deg か rotation_rad(結果には両方)
              保存    変更があり、すべて成功したときだけ最後に 1 回保存する(途中で失敗したら何も保存しない)。結果の changed と saved を見る。
              取り込み 一時フォルダ(.dxforge-import-*)に書き、すべて成功したら Content へ移してから保存する。既存のファイルは上書きしない。
                      消せずに残ったものは結果の leftover_paths に出す。
              エディタがそのプロジェクトを開いていると、--dry-run でも変更の操作は断る(44)。読むだけの操作は許す。

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
              40 ops の形式の誤り(何も実行しない。error に op_index と key)
              41 対象(エンティティ、シーン、アセット、取り込むファイル)が見つからない、1 つに決まらない
              42 操作の失敗(すでに持つコンポーネントの追加、Transform の削除、最後のシーンやアクティブなシーンの削除、取り込みの失敗など)
              43 .dxproj を保存できない、取り込んだファイルを Content へ移せない
              44 エディタがそのプロジェクトを開いている
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
                Console.WriteLine();
                Console.WriteLine($"操作の JSON Schema: {Path.Combine(AppContext.BaseDirectory, "Schema", "ops.schema.json")}(リポジトリでは DXForgeCli/Schema/ops.schema.json)");
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

            if (options.OpsMode)
            {
                RunOps(options, projectPath, result);
                return;
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

        /// <summary>
        /// 操作を実行する(Issue #183)。本体はエディタの DXForgeEditor.Automation で、ここは呼ぶだけ。
        /// 判定の順番: 40(形式)→ 31(読み込み)→ 44(エディタ)→ 41/42(実行)→ 43(保存)。
        /// </summary>
        private static void RunOps(CommandLineOptions options, string projectPath, RunResult result)
        {
            try
            {
                var ops = ReadOps(options);
                result.OpCount = ops.Count;

                var project = LoadProject(projectPath);
                // シーンを選ばないので、すべてのシーンの一覧を確かめる(SelectScene と同じ理由)
                foreach (var scene in project.Scenes)
                {
                    if (scene.GameEntities == null || scene.GameEntities.Any(x => x.Components == null))
                    {
                        throw new ExitException(ExitCode.ProjectInvalid, "project_invalid", $"シーン {scene.Name} のエンティティかコンポーネントの一覧がありません");
                    }
                }

                // .dxproj に保存された Path は古いことがあり、エディタのロックはその Path の下にできる。実際のフォルダも見る。
                if (ops.Any(SceneOpDocument.ChangesProject)
                    && (ProjectEditorLock.IsHeld(Path.GetDirectoryName(projectPath)!) || ProjectEditorLock.IsHeld(project.Path)))
                {
                    throw new SceneOpException(SceneOpErrorKind.EditorOpen, "editor_open",
                        $"エディタがこのプロジェクトを開いているので、変更の操作はできません(読むだけの操作はできます): {projectPath}");
                }

                // 前の実行が強制終了で残した一時フォルダを片付ける。--dry-run と読むだけの操作では何も変えない
                IReadOnlyList<string> sweepLeftovers = [];
                if (!options.DryRun && ops.Any(SceneOpDocument.ChangesProject))
                {
                    var sweep = SceneOpAssetStaging.SweepAbandoned(projectPath);
                    foreach (var folder in sweep.Removed) WriteLog("info", $"前の実行が残した一時フォルダを消しました: {folder}");
                    foreach (var warning in sweep.Warnings) WriteLog("warning", warning);
                    sweepLeftovers = sweep.Leftovers;
                }

                var assets = new SceneOpAssetStaging(projectPath, Path.GetFullPath(options.Importer ?? SceneOpAssetStaging.DefaultImporterPath));
                try
                {
                    var run = new SceneOpRunner(project, projectPath, assets).Run(ops);
                    result.OpResults = run.Results;
                    if (run.Error != null) throw run.Error;
                    result.Changed = run.Changed;

                    if (run.Changed)
                    {
                        if (options.DryRun)
                        {
                            WriteLog("info", "--dry-run なので保存していません。");
                        }
                        else
                        {
                            assets.Commit(() => SceneOpProjectWriter.Save(project, projectPath));
                            result.Saved = true;
                        }
                    }
                }
                finally
                {
                    assets.Dispose();
                    result.LeftoverPaths = [.. sweepLeftovers, .. assets.LeftoverPaths];
                }

                foreach (var warning in result.OpResults.SelectMany(x => x?["warnings"]?.AsArray() ?? new JsonArray()))
                {
                    WriteLog("warning", warning?.ToString() ?? string.Empty);
                }
            }
            catch (SceneOpException ex)
            {
                result.ErrorOpIndex = ex.OpIndex;
                result.ErrorKey = ex.Key;
                result.Changed = false;
                result.Saved = false;
                var code = ex.Kind switch
                {
                    SceneOpErrorKind.InvalidOps => ExitCode.OpsInvalid,
                    SceneOpErrorKind.NotFound => ExitCode.OpsTargetNotFound,
                    SceneOpErrorKind.OperationFailed => ExitCode.OpsFailed,
                    SceneOpErrorKind.SaveFailed => ExitCode.OpsSaveFailed,
                    _ => ExitCode.OpsEditorOpen,
                };
                throw new ExitException(code, ex.Code, ex.Message);
            }
        }

        private static List<SceneOp> ReadOps(CommandLineOptions options)
        {
            if (options.SingleOp != null) return SceneOpDocument.ParseSingle(options.SingleOp);

            string json;
            try
            {
                json = File.ReadAllText(options.OpsFile!);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is ArgumentException || ex is NotSupportedException)
            {
                throw new SceneOpException(SceneOpErrorKind.InvalidOps, "ops_invalid", $"ops のファイルを読めません: {options.OpsFile}({ex.Message})");
            }
            return SceneOpDocument.ParseDocument(json);
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
            if (options.OpsMode)
            {
                var error = ErrorJson(result);
                if (error != null)
                {
                    error["op_index"] = result.ErrorOpIndex;
                    error["key"] = result.ErrorKey;
                }
                return new JsonObject
                {
                    ["mode"] = "ops",
                    ["project"] = result.ProjectPath ?? options.Project,
                    ["dry_run"] = options.DryRun,
                    ["success"] = result.ExitCode == 0,
                    ["exit_code"] = result.ExitCode,
                    ["error"] = error,
                    ["changed"] = result.Changed,
                    ["saved"] = result.Saved,
                    ["leftover_paths"] = new JsonArray(result.LeftoverPaths.Select(x => (JsonNode?)x).ToArray()),
                    ["op_count"] = result.OpCount,
                    ["results"] = result.OpResults,
                };
            }

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

        private static JsonObject? ErrorJson(RunResult result) =>
            result.ExitCode == 0 ? null : new JsonObject { ["code"] = result.ErrorCode, ["message"] = result.ErrorMessage };

        private static void WriteHumanResult(CommandLineOptions options, RunResult result)
        {
            if (result.ExitCode != 0)
            {
                Console.WriteLine($"dxforge: FAILED (exit code {result.ExitCode}) {result.ErrorCode}: {result.ErrorMessage}");
                return;
            }
            if (options.OpsMode)
            {
                Console.WriteLine($"dxforge: OK, {result.OpCount} ops, changed: {result.Changed}, saved: {result.Saved}");
                foreach (var op in result.OpResults) Console.WriteLine($"  {op?.ToJsonString(_jsonOptions)}");
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
