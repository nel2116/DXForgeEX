using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.DllWrappers;
using DXForgeEditor.GameDev;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.Serialization;
using System.Text;
using System.Windows.Input;

namespace DXForgeEditor.GameProject
{
    [DataContract(Name = "Game")]
    class Project : ViewModelBase
    {
        /// <summary>
        /// アクティブなシーンの描くもの(エンティティ、有効と無効、ジオメトリ)が変わった(Primal Day 084)。sender はシーン。
        /// WorldEditorView がビューポートに描くジオメトリの ID を渡し直す。
        /// </summary>
        public static event EventHandler? SceneUpdated;

        public static string Extension => ".dxproj";
        [DataMember]
        public string Name { get; private set; } = "New Project";
        [DataMember]

        /// <summary>
        /// 現在のプロジェクトを含むルート・フォルダーを取得する
        /// </summary>
        public string Path { get; set; }

        /// <summary>
        /// 現在の DXForge プロジェクトファイルのフルパスを取得する
        /// </summary>
        public string FullPath => $@"{Path}{Name}{Extension}";
        public string Solution => $@"{Path}{Name}.sln";
        public string ContentPath => $@"{Path}Content\";

        public string TempFolder => $@"{Path}.DXForge\Temp\";

        private int _buildConfig;

        [DataMember]
        public int BuildConfig
        {
            get => _buildConfig;
            set
            {
                if (_buildConfig != value)
                {
                    _buildConfig = value;
                    OnPropertyChanged(nameof(BuildConfig));
                }
            }
        }

        public BuildConfiguration StandAloneBuildConfig => BuildConfig == 0 ? BuildConfiguration.Debug : BuildConfiguration.Release;

        public BuildConfiguration DLLBuildConfig => BuildConfig == 0 ? BuildConfiguration.DebugEditor : BuildConfiguration.ReleaseEditor;

        private string[]? _availableScripts;
        public string[]? AvailableScripts
        {
            get => _availableScripts;
            private set
            {
                if (_availableScripts != value)
                {
                    _availableScripts = value;
                    OnPropertyChanged(nameof(AvailableScripts));
                }
            }
        }

        [DataMember(Name = nameof(Scenes))]
        private readonly ObservableCollection<Scene> _scenes = new();
        // Initialized by OnDeserialized before its first await.
        public ReadOnlyObservableCollection<Scene> Scenes { get; private set; } = null!;

        // Initialized by OnDeserialized before its first await.
        private Scene _activeScene = null!;

        public Scene ActiveScene
        {
            get => _activeScene;
            set
            {
                if (_activeScene != value)
                {
                    _activeScene = value;
                    OnPropertyChanged(nameof(ActiveScene));
                }
            }
        }

        /// <summary>
        /// エディタで開いているプロジェクト。Load で設定し、Unload で null に戻す(Primal Day 081)。
        /// ヘッドレスの読み込み(ProjectLoadEnvironment.Headless)では設定しない。
        /// </summary>
        public static Project? Current { get; private set; }

        public static UndoRedo UndoRedo { get; } = new UndoRedo();

        /// <summary>
        /// アクティブなシーンが変わったことを SceneUpdated で伝える(Primal Day 084)。エディタで開いていないとき
        /// (ヘッドレスの読み込み、Current が null)は何もしない。
        /// </summary>
        public void UpdateScene()
        {
            if (Current != null) SceneUpdated?.Invoke(ActiveScene, EventArgs.Empty);
        }

        public ICommand? UndoCommand { get; private set; }

        public ICommand? RedoCommand { get; private set; }

        public ICommand? AddSceneCommand { get; private set; }

        public ICommand? RemoveSceneCommand { get; private set; }
        public ICommand? SaveCommand { get; private set; }

        public ICommand? DebugStartCommand { get; private set; }
        public ICommand? DebugStartWithoutDebuggingCommand { get; private set; }
        public ICommand? DebugStopCommand { get; private set; }
        public ICommand? BuildCommand { get; private set; }

        private void SetCommands()
        {
            AddSceneCommand = new RelayCommand<object>(x =>
            {
                AddScene($"New Scene {_scenes.Count}");
                var newScene = _scenes.Last();
                var sceneIndex = _scenes.Count - 1;

                UndoRedo.Add(new UndoRedoAction(
                    () => RemoveScene(newScene),
                    () => _scenes.Insert(sceneIndex, newScene),
                    $"Add {newScene.Name}"));
            });

            RemoveSceneCommand = new RelayCommand<Scene>(x =>
            {
                var sceneIndex = _scenes.IndexOf(x);
                RemoveScene(x);

                UndoRedo.Add(new UndoRedoAction(
                    () => _scenes.Insert(sceneIndex, x),
                    () => RemoveScene(x),
                    $"Remove {x.Name}"));
            }, x => !x.IsActive);

            UndoCommand = new RelayCommand<object>(x => UndoRedo.Undo(), x => UndoRedo.UndoList.Any());
            RedoCommand = new RelayCommand<object>(x => UndoRedo.Redo(), x => UndoRedo.RedoList.Any());
            SaveCommand = new RelayCommand<object>(x => Save(this));
            DebugStartCommand = new RelayCommand<object>(async x => await RunGame(true), x => VisualStudio.CanStart());
            DebugStartWithoutDebuggingCommand = new RelayCommand<object>(async x => await RunGame(false), x => VisualStudio.CanStart());
            DebugStopCommand = new RelayCommand<object>(async x => await StopGame(), x => VisualStudio.IsDebugging());
            BuildCommand = new RelayCommand<bool>(async x => await BuildGameCodeDLL(x), x => VisualStudio.CanStart());

            OnPropertyChanged(nameof(AddSceneCommand));
            OnPropertyChanged(nameof(RemoveSceneCommand));
            OnPropertyChanged(nameof(UndoCommand));
            OnPropertyChanged(nameof(RedoCommand));
            OnPropertyChanged(nameof(SaveCommand));
            OnPropertyChanged(nameof(DebugStartCommand));
            OnPropertyChanged(nameof(DebugStartWithoutDebuggingCommand));
            OnPropertyChanged(nameof(DebugStopCommand));
            OnPropertyChanged(nameof(BuildCommand));
        }

        public void AddScene(string sceneName)
        {
            Debug.Assert(!string.IsNullOrEmpty(sceneName.Trim()));
            _scenes.Add(new Scene(this, sceneName));
        }

        public void RemoveScene(Scene scene)
        {
            Debug.Assert(_scenes.Contains(scene));
            _scenes.Remove(scene);
        }

        /// <summary>
        /// Load の最後に呼ぶ。デシリアライズのときに読み込めなかったコンポーネント(Geometry)を持つ、アクティブなシーンの
        /// エンティティを作り直す(Codexの指摘, #32)。.dxproj の Path が今の場所と違う(フォルダを移した)プロジェクトは、
        /// Path を直すまで登録したアセットのフォルダと一致しないので、Geometry.Load() が遅らせる。
        /// </summary>
        private void LoadEntitiesWithAssets()
        {
            var entities = ActiveScene.GameEntities
                .Where(x => x.IsActive && x.GetComponent<Components.Geometry>() is Components.Geometry geometry && !geometry.IsLoaded)
                .ToList();
            foreach (var entity in entities)
            {
                entity.IsActive = false;
                entity.IsActive = true;
            }
        }

        /// <summary>
        /// 直近の Load が読み込めなかった理由(例外の型とメッセージ、InnerException も含む)。読み込めたとき、またはまだ Load していないときは null。
        /// 呼び出し元(OpenProjectView / NewProjectView)がダイアログに出す(Issue #250)。
        /// </summary>
        public static string? LastLoadError { get; private set; }

        /// <summary>例外の型とメッセージを、InnerException をたどって1つの文字列にする</summary>
        internal static string DescribeException(Exception ex)
        {
            var sb = new StringBuilder();
            for (Exception? e = ex; e != null; e = e.InnerException)
            {
                if (sb.Length > 0) sb.Append(" <- ");
                sb.Append(e.GetType().Name).Append(": ").Append(e.Message);
            }
            return sb.ToString();
        }

        /// <returns>読み込んだプロジェクト。エディタでは、読み込めなければ null で、理由は LastLoadError に入る(ヘッドレスでは例外を投げる)</returns>
        public static Project? Load(string file)
        {
            Debug.Assert(File.Exists(file));
            LastLoadError = null;

            // コマンドラインの dxforge(Issue #11)はアセットを使わず、読み込めなければ例外で終了コードを決める
            if (ProjectLoadEnvironment.Headless) return Serializer.FromFile<Project>(file);

            var path = System.IO.Path.GetDirectoryName(file) ?? string.Empty;
            if (!path.EndsWith(System.IO.Path.DirectorySeparatorChar)) path += System.IO.Path.DirectorySeparatorChar;

            Project? project = null;
            try
            {
                // エンティティ(Geometry)がデシリアライズの中でアセットを探すので、先に登録する(Primal Day 081)
                // NOTE: Geometry.Load() は AssetRegistry.ContentFolder と ContentPath を比べるので、ContentPath と同じ形(末尾の区切り文字あり)で渡す
                ContentWatcher.Reset($@"{path}Content\", path);

                // NOTE: ルートが i:nil="true" のファイルでは、FromFile は例外を投げず null を返す
                project = Serializer.FromFile<Project>(file) ?? throw new InvalidDataException($"ルート要素が nil です: {file}");
                project.Path = path;
                Current = project;
                project.LoadEntitiesWithAssets();
                project.UpdateScene();
                return project;
            }
            catch (Exception ex)
            {
                // NOTE: OpenProjectView の経路で例外を投げるとエディタが落ちる。null を返し、呼び出し元がダイアログに残す
                LastLoadError = DescribeException(ex);
                Debug.WriteLine(LastLoadError);
                Logger.Log(MessageType.Error, $"プロジェクトを読み込めませんでした: {file} ({LastLoadError})");
                Current = null;
                // デシリアライズのあとに失敗したら、作ったエンティティをエンジンに残さない
                project?.ReleaseEntities();
                // 開けなかったプロジェクトのフォルダを見続けない(AssetRegistry は次の Load で登録し直す)
                ContentWatcher.Reset(string.Empty, string.Empty);
                return null;
            }
        }

        public void Unload()
        {
            // 描いているジオメトリを先にビューポートから外してから、エンジンから消す(Primal Day 084, #214)
            // NOTE: Primal と同じく、エンティティの IsEnabled を false にしたままにする(このあとプロジェクトは保存しない)
            ActiveScene.DisableAndUpdate(ActiveScene.GameEntities.ToList());
            UnloadGameCodeDLL();
            // スクリプトのないエンティティもエンジンから消し、コンポーネントがアップロードしたもの(Geometry のメッシュと
            // マテリアル)を外す(Codexの指摘, #32)。UnloadGameCodeDLL はゲームのコードの読み直しでも呼ぶので、そこでは消さない。
            _scenes.SelectMany(x => x.GameEntities).ToList().ForEach(x => x.IsActive = false);
            VisualStudio.CloseVisualStudio();
            UndoRedo.Reset();
            Logger.Clear();
            DeleteTmpFolder();
            if (Current == this) Current = null;
        }

        private void DeleteTmpFolder()
        {
            if (Directory.Exists(TempFolder))
            {
                // 読み取り専用のファイルも消せるよう、属性を戻す(Primal Day 079)
                _ = new DirectoryInfo(TempFolder) { Attributes = FileAttributes.Normal };
                Directory.Delete(TempFolder, true);
            }
        }

        private static void Save(Project project)
        {
            Serializer.ToFile(project, project.FullPath);
            Logger.Log(MessageType.Info, $"プロジェクトの保存: {project.FullPath}");
        }

        private void SaveToBinary()
        {
            var configName = VisualStudio.GetConfigurationName(StandAloneBuildConfig);
            var bin = $@"{Path}x64\{configName}\game.bin";

            using (var bw = new BinaryWriter(File.Open(bin, FileMode.Create, FileAccess.Write)))
            {
                WriteGameBinary(ActiveScene, bw);
            }
        }

        /// <summary>
        /// シーンのエンティティを game.bin の形式(ゲームの exe とヘッドレスのランナーが content::load_game で読む)で書く。
        /// エディタ(SaveToBinary)とコマンドラインの dxforge(Issue #11)が同じ形式で書くよう、ここだけで実装する。
        /// game.bin に書けないコンポーネント(Component.CanWriteToBinary が false の Geometry)は書かずに、警告のログを出す。
        /// NOTE: Primal Day 079 の Geometry は WriteToBinary で NotImplementedException を投げるが、ここは async void の
        ///       RunGame から呼ばれるので、投げるとエディタが落ちる。エンジンの content::load_game も Geometry を読まない(#32)。
        /// </summary>
        internal static void WriteGameBinary(Scene scene, BinaryWriter bw)
        {
            bw.Write(scene.GameEntities.Count);
            foreach (var entity in scene.GameEntities)
            {
                var components = entity.Components.Where(x => x.CanWriteToBinary).ToList();
                foreach (var skipped in entity.Components.Where(x => !x.CanWriteToBinary))
                {
                    Logger.Log(MessageType.Warning, $"エンティティ {entity.Name} の {skipped.GetType().Name} コンポーネントは game.bin に書き出せないので、飛ばします。");
                }

                bw.Write(0); // entity type (reserved for later)
                bw.Write(components.Count);
                foreach (var component in components)
                {
                    bw.Write((int)component.ToEnumType());
                    component.WriteToBinary(bw);
                }
            }
        }

        private async Task RunGame(bool debug)
        {
            await Task.Run(() => VisualStudio.BuildSolution(this, StandAloneBuildConfig, debug));
            // 実行ファイルがなければgame.binも書き出さない(出力先のフォルダがないとSaveToBinaryが例外を投げる)
            if (VisualStudio.BuildSucceeded && VisualStudio.ExecutableExists(this, StandAloneBuildConfig))
            {
                SaveToBinary();
                await Task.Run(() => VisualStudio.Run(this, StandAloneBuildConfig, debug));
            }
        }

        private async Task StopGame() => await Task.Run(VisualStudio.Stop);

        private async Task BuildGameCodeDLL(bool showWindow = true)
        {
            try
            {
                UnloadGameCodeDLL();
                await Task.Run(() => VisualStudio.BuildSolution(this, DLLBuildConfig, showWindow));
                if (VisualStudio.BuildSucceeded)
                {
                    LoadGameCodeDLL();
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                throw;
            }
        }

        private void LoadGameCodeDLL()
        {
            var configName = VisualStudio.GetConfigurationName(DLLBuildConfig);
            var dll = $@"{Path}x64\{configName}\{Name}.dll";
            AvailableScripts = null;
            if (File.Exists(dll) && EngineAPI.LoadGameCodeDll(dll) != 0)
            {
                AvailableScripts = EngineAPI.GetScriptNames();
                ActiveScene.GameEntities.Where(x => x.GetComponent<Script>() != null).ToList().ForEach(x => x.IsActive = true);
                UpdateScene();
                Logger.Log(MessageType.Info, "ゲームコードDLLは正常にロードされました。");
            }
            else
            {
                Logger.Log(MessageType.Warning, "ゲームコードDLLファイルのロードに失敗しました。 まずプロジェクトをビルドしてみてください！");
            }
        }

        /// <summary>
        /// ゲームのコードの DLL を外す。スクリプトを持つエンティティはエンジンから消す(LoadGameCodeDLL で作り直す)。
        /// NOTE: Primal Day 084 は RemoveScriptComponents でスクリプトだけを外すが、Dispatcher.BeginInvoke の中の戻り値を捨てて
        ///       常に空の一覧を返すので、外したスクリプトを戻さない(保存するとスクリプトが消える)。DXForgeEX は今までどおり
        ///       エンティティを作り直し、消す前にビューポートから外す(消したジオメトリの ID を描かない, #214)。
        /// </summary>
        private void UnloadGameCodeDLL()
        {
            var scriptEntities = ActiveScene.GameEntities.Where(x => x.GetComponent<Script>() != null).ToList();
            var enableList = ActiveScene.DisableAndUpdate(scriptEntities);
            scriptEntities.ForEach(x => x.IsActive = false);
            ActiveScene.EnableAndUpdate(enableList);
            if (EngineAPI.UnloadGameCodeDll() != 0)
            {
                Logger.Log(MessageType.Info, "ゲームコードDLLのアンロード");
                AvailableScripts = null;
            }
        }

        /// <summary>
        /// 読み込んだシーンのエンティティをエンジンに作る。途中で失敗したら、作ったものを消して例外を Project.Load に渡す。
        /// NOTE: Primal は Scene.OnDeserialized で作るが、デシリアライズがあとのシーンで失敗すると、作ったものへの参照がなくなりエンジンに残る
        ///       (Codexの指摘, #185)。ファイル全体を読めてから作る。
        /// </summary>
        private void ActivateEntities()
        {
            try
            {
                foreach (var scene in _scenes)
                {
                    foreach (var entity in scene.GameEntities)
                    {
                        entity.IsActive = scene.IsActive;
                    }
                }
            }
            catch
            {
                ReleaseEntities();
                throw;
            }
        }

        /// <summary>
        /// このプロジェクトのエンティティをエンジンから外す(IsActive を false にする)。読み込みに失敗したとき、途中まで作ったものを
        /// エンジンに残さないために使う(残ると、終了時に DXForgeEngineDLL が FreeList.h の assert(!_size) で止まる, Issue #250)。
        /// 失敗の後始末なので、ここでは例外を投げず、外せなかったエンティティはログに残して続ける。
        /// </summary>
        internal void ReleaseEntities()
        {
            foreach (var entity in _scenes.SelectMany(x => x.GameEntities).ToList())
            {
                try
                {
                    entity.IsActive = false;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine(ex.Message);
                    Logger.Log(MessageType.Error, $"エンティティ {entity.Name} をエンジンから外せませんでした: {DescribeException(ex)}");
                }
            }
        }

        private async void BuildGameCodeDLLAfterLoad() => await BuildGameCodeDLL(false);

        // NOTE: async void にしない。ActivateEntities の例外を Project.Load の catch に渡すため(async void だと捕まらずエディタが落ちる)
        [OnDeserialized]
        private void OnDeserialized(StreamingContext context)
        {
            if (_scenes != null)
            {
                Scenes = new ReadOnlyObservableCollection<Scene>(_scenes);
                OnPropertyChanged(nameof(Scenes));
            }

            Debug.Assert(_scenes != null);
            // コマンドラインの dxforge(Issue #11)は、エンティティをエンジン(DXForgeEngineDLL)に作らない
            // NOTE: ビルド(UnloadGameCodeDLL と LoadGameCodeDLL がスクリプトのエンティティを作り直す)の前に作る
            if (!ProjectLoadEnvironment.Headless)
            {
                ActivateEntities();
            }

            // アクティブなシーンは必ず存在する前提(直後のDebug.Assertで検証)。ロード中にthrowすると
            // 呼び出し元(OpenProjectView)で捕捉されずエディタが落ちるため、従来どおりthrowしない
            ActiveScene = _scenes.FirstOrDefault(x => x.IsActive)!;
            Debug.Assert(ActiveScene != null);
            // ビルドの間もメニューのコマンドを使えるよう、ビルドを待つ前に設定する(Primal Day 081)
            SetCommands();

            // コマンドラインの dxforge(Issue #11)は、Visual Studio でのビルドもエンジンの DLL も使わない
            if (!ProjectLoadEnvironment.Headless)
            {
                BuildGameCodeDLLAfterLoad();
            }
        }


        public Project(string name, string path)
        {
            Name = name;
            Path = path;

            Debug.Assert(File.Exists((Path + Name + Extension).ToLower()));
            OnDeserialized(new StreamingContext());
        }
    }
}
