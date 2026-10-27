using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;

namespace DXForgeEditor.GameDev
{
    enum BuildConfiguration
    {
        Debug,
        DebugEditor,
        Release,
        ReleaseEditor,
    }
    static class VisualStudio
    {
        private static readonly ManualResetEventSlim _resetEvent = new(false);
        private static readonly string _progID = "VisualStudio.DTE.17.0";
        private static readonly object _lock = new();
        private static readonly string[] _buildConfigurationNames = new string[] { "Debug", "DebugEditor", "Release", "ReleaseEditor" };
        private static EnvDTE80.DTE2? _vsInstance = null;
        // DTEのイベントはRCWへの参照が無くなるとGCで登録が外れるため、フィールドに保持する
        private static EnvDTE.BuildEvents? _buildEvents = null;
        private static EnvDTE80.DTE2? _buildEventsOwner = null;

        public static bool BuildSucceeded { get; private set; } = true;
        public static bool BuildDone { get; private set; } = true;

        public static string GetConfigurationName(BuildConfiguration config) => _buildConfigurationNames[(int)config];

        [DllImport("ole32.dll")]
        private static extern int CreateBindCtx(uint reserved, out IBindCtx ppbc);

        [DllImport("ole32.dll")]
        private static extern int GetRunningObjectTable(uint reserved, out IRunningObjectTable pprot);

        private static void CallOnSTAThread(Action action)
        {
            Debug.Assert(action != null);
            var thread = new Thread(() =>
            {
                MessageFilter.Register();
                try { action(); }
                catch (Exception ex)
                {
                    Logger.Log(MessageType.Warning, ex.Message);
                }
                finally { MessageFilter.Revoke(); }
            });

            thread.SetApartmentState(ApartmentState.STA);
            thread.Start();
            thread.Join();
        }

        private static void OpenVisualStudio_Internal(string solutionPath)
        {
            IRunningObjectTable? rot = null;
            IEnumMoniker? monikerTable = null;
            IBindCtx? bindCtx = null;
            try
            {
                if (_vsInstance == null)
                {
                    // Finde and open visual
                    var hResult = GetRunningObjectTable(0, out rot);
                    if (hResult < 0 || rot == null) throw new COMException($"GetRunningObjectTable() returned HRESULT: {hResult:X8}");

                    rot.EnumRunning(out monikerTable);
                    monikerTable.Reset();

                    hResult = CreateBindCtx(0, out bindCtx);
                    if (hResult < 0 || bindCtx == null) throw new COMException($"CreateBindCtx() returned HRESULT: {hResult:X8}");

                    IMoniker[] currentMoniker = new IMoniker[1];
                    while (monikerTable.Next(1, currentMoniker, IntPtr.Zero) == 0)
                    {
                        string name = string.Empty;
                        currentMoniker[0]?.GetDisplayName(bindCtx, null, out name);
                        if (name.Contains(_progID))
                        {
                            hResult = rot.GetObject(currentMoniker[0], out object obj);
                            if (hResult < 0 || obj == null) throw new COMException($"Running object table's GetObject() returned HRESULT: {hResult:X8}");
                            EnvDTE80.DTE2 dte = (EnvDTE80.DTE2)obj;

                            var solutionName = string.Empty;
                            CallOnSTAThread(() =>
                            {
                                solutionName = dte.Solution.FullName;
                            });

                            if (solutionName == solutionPath)
                            {
                                _vsInstance = dte;
                                break;
                            }
                        }
                    }

                    if (_vsInstance == null)
                    {
                        Type visualStudioType = Type.GetTypeFromProgID(_progID, true)
                            ?? throw new COMException("Visual Studio is not registered.");
                        _vsInstance = Activator.CreateInstance(visualStudioType) as EnvDTE80.DTE2;
                    }
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "Visual Studioを開くのに失敗しました");
            }
            finally
            {
                if (monikerTable != null) Marshal.ReleaseComObject(monikerTable);
                if (rot != null) Marshal.ReleaseComObject(rot);
                if (bindCtx != null) Marshal.ReleaseComObject(bindCtx);
            }
        }

        public static void OpenVisualStudio(string solutionPath)
        {
            lock (_lock) { OpenVisualStudio_Internal(solutionPath); }
        }

        private static void CloseVisualStudio_Internal()
        {
            CallOnSTAThread(() =>
            {
                UnregisterBuildEvents();
                if (_vsInstance?.Solution.IsOpen == true)
                {
                    _vsInstance.ExecuteCommand("File.SaveAll");
                    _vsInstance.Solution.Close(true);
                }
                _vsInstance?.Quit();
                _vsInstance = null;
            });
        }

        public static void CloseVisualStudio()
        {
            lock (_lock) { CloseVisualStudio_Internal(); }
        }

        private static bool AddFilesToSolution_Internal(string solution, string projectName, string[] files)
        {
            Debug.Assert(files?.Length > 0);
            OpenVisualStudio_Internal(solution);
            try
            {
                if (_vsInstance != null)
                {
                    CallOnSTAThread(() =>
                    {
                        if (!_vsInstance.Solution.IsOpen) _vsInstance.Solution.Open(solution);
                        else _vsInstance.ExecuteCommand("File.SaveAll");

                        foreach (EnvDTE.Project project in _vsInstance.Solution.Projects)
                        {
                            if (project.UniqueName.Contains(projectName))
                            {
                                foreach (var file in files)
                                {
                                    project.ProjectItems.AddFromFile(file);
                                }
                            }
                        }

                        var cpp = files.FirstOrDefault(x => Path.GetExtension(x) == ".cpp");
                        if (!string.IsNullOrEmpty(cpp))
                        {
                            _vsInstance.ItemOperations.OpenFile(cpp, EnvDTE.Constants.vsViewKindTextView).Visible = true;
                        }
                        _vsInstance.MainWindow.Activate();
                        _vsInstance.MainWindow.Visible = true;
                    });
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Debug.WriteLine("failed to add files to Visual Studio project");
                return false;
            }
            return true;
        }

        public static bool AddFilesToSolution(string solution, string projectName, string[] files)
        {
            lock (_lock) { return AddFilesToSolution_Internal(solution, projectName, files); }
        }

        private static void OnBuildSolutionBegin(string project, string projectConfig, string platform, string solutionConfig)
        {
            if (BuildDone) return;
            Logger.Log(MessageType.Info, $"Build: {project}, {projectConfig}, {platform}, {solutionConfig}");
        }

        private static void OnBuildSolutionDone(string project, string projectConfig, string platform, string solutionConfig, bool success)
        {
            if (BuildDone) return;

            if (success) Logger.Log(MessageType.Info, $"Build: {projectConfig} configuration 成功");
            else Logger.Log(MessageType.Error, $"Build: {projectConfig} configuration 失敗");

            BuildDone = true;
            BuildSucceeded = success;
            _resetEvent.Set();
        }

        // ビルドのたびに+=すると同じハンドラが重複して登録されるため、VSのインスタンスごとに1回だけ登録する
        private static void RegisterBuildEvents(EnvDTE80.DTE2 instance)
        {
            if (_buildEvents != null && ReferenceEquals(_buildEventsOwner, instance)) return;
            UnregisterBuildEvents();

            var buildEvents = instance.Events.BuildEvents;
            buildEvents.OnBuildProjConfigBegin += OnBuildSolutionBegin;
            buildEvents.OnBuildProjConfigDone += OnBuildSolutionDone;
            _buildEvents = buildEvents;
            _buildEventsOwner = instance;
        }

        private static void UnregisterBuildEvents()
        {
            var buildEvents = _buildEvents;
            _buildEvents = null;
            _buildEventsOwner = null;
            if (buildEvents == null) return;

            // VSが既に終了している場合などは解除に失敗するが、登録先ごと無くなっているので無視してよい
            try
            {
                buildEvents.OnBuildProjConfigBegin -= OnBuildSolutionBegin;
                buildEvents.OnBuildProjConfigDone -= OnBuildSolutionDone;
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }
        }

        private static bool IsDebugging_Internal()
        {
            bool result = false;
            CallOnSTAThread(() =>
            {
                result = _vsInstance != null &&
                    (_vsInstance.Debugger.CurrentProgram != null || _vsInstance.Debugger.CurrentMode == EnvDTE.dbgDebugMode.dbgRunMode);
            });

            return result;
        }

        public static bool IsDebugging()
        {
            lock (_lock) { return IsDebugging_Internal(); }
        }

        private static void BuildSolution_Internal(Project project, BuildConfiguration buildConfig, bool showWindow)
        {

            if (IsDebugging_Internal())
            {
                Logger.Log(MessageType.Error, "Visual Studioは現在プロセスを実行しています。");
                return;
            }

            OpenVisualStudio_Internal(project.Solution);
            // RunGame()はasync voidのコマンドから呼ばれ例外が捕捉されないため、throwせずにビルド失敗として扱う
            var instance = _vsInstance;
            if (instance == null)
            {
                BuildSucceeded = false;
                Logger.Log(MessageType.Error, "Visual Studioを起動できなかったため、ビルドできませんでした。");
                return;
            }
            BuildDone = BuildSucceeded = false;

            CallOnSTAThread(() =>
            {
                instance.MainWindow.Visible = showWindow;
                if (!instance.Solution.IsOpen)
                    instance.Solution.Open(project.Solution);

                RegisterBuildEvents(instance);
            });

            var configName = GetConfigurationName(buildConfig);

            try
            {
                foreach (var pdbFile in Directory.GetFiles(Path.Combine($"{project.Path}", $@"x64\{configName}"), "*.pdb"))
                {
                    File.Delete(pdbFile);
                }
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }

            CallOnSTAThread(() =>
            {
                instance.Solution.SolutionBuild.SolutionConfigurations.Item(configName).Activate();
                instance.ExecuteCommand("Build.BuildSolution");
                _resetEvent.Wait();
                _resetEvent.Reset();
            });
        }

        public static void BuildSolution(Project project, BuildConfiguration buildConfig, bool showWindow = true)
        {
            lock (_lock) { BuildSolution_Internal(project, buildConfig, showWindow); }
        }

        // VSのDebug.Start/Debug.StartWithoutDebuggingは、ゲームのプロジェクトの出力先(既定のOutDir)にある実行ファイルを起動する
        public static string GetExecutablePath(Project project, BuildConfiguration buildConfig) =>
            $@"{project.Path}x64\{GetConfigurationName(buildConfig)}\{project.Name}.exe";

        // 実行ファイルがないままVSに実行を指示すると、VSが「開始できません」のモーダルダイアログを出す。
        // それが閉じられるまでDTEの呼び出しが戻らず、CanExecuteから呼ばれるIsDebugging()でエディタのUIスレッドも止まる(#55)
        public static bool ExecutableExists(Project project, BuildConfiguration buildConfig)
        {
            var exe = GetExecutablePath(project, buildConfig);
            if (File.Exists(exe)) return true;

            Logger.Log(MessageType.Warning, $"実行ファイルが見つからないため、ゲームを実行しません: {exe}" +
                " (プロジェクトにソースファイルがないと、ビルドが成功しても実行ファイルは作られません。New Scriptでスクリプトを追加してください)");
            return false;
        }

        private static void Run_Internal(Project project, BuildConfiguration buildConfig, bool debug)
        {
            if (!ExecutableExists(project, buildConfig)) return;

            CallOnSTAThread(() =>
            {
                if (_vsInstance != null && !IsDebugging_Internal() && BuildSucceeded)
                {
                    _vsInstance.ExecuteCommand(debug ? "Debug.Start" : "Debug.StartWithoutDebugging");
                }
            });
        }

        public static void Run(Project project, BuildConfiguration buildConfig, bool debug)
        {
            lock (_lock) { Run_Internal(project, buildConfig, debug); }
        }

        private static void Stop_Internal()
        {
            CallOnSTAThread(() =>
            {
                if (_vsInstance != null && IsDebugging_Internal())
                {
                    _vsInstance.ExecuteCommand("Debug.StopDebugging");
                }
            });
        }

        public static void Stop()
        {
            lock (_lock) { Stop_Internal(); }
        }
    }

    // Class containing the IOleMEssageFilter thread error-handling function
    public class MessageFilter : IMessageFilter
    {
        private const int SERVERCALL_ISHANDLED = 0;
        private const int PENDINGMSG_WAITDEFPROCESS = 2;
        private const int SERVERCALL_RETRYLATER = 2;

        [DllImport("Ole32.dll")]
        private static extern int CoRegisterMessageFilter(IMessageFilter? newFilter, out IMessageFilter? oldFilter);

        public static void Register()
        {
            IMessageFilter newFilter = new MessageFilter();
            int hr = CoRegisterMessageFilter(newFilter, out var oldFilter);
            Debug.Assert(hr >= 0, "Registering COM IMessageFilter failed.");
        }

        public static void Revoke()
        {
            int hr = CoRegisterMessageFilter(null, out var oldFilter);
            Debug.Assert(hr >= 0, "Unregistering COM IMessageFilter failed.");
        }


        int IMessageFilter.HandleInComingCall(int dwCallType, System.IntPtr hTaskCaller, int dwTickCount, System.IntPtr lpInterfaceInfo)
        {
            //returns the flag SERVERCALL_ISHANDLED.
            return SERVERCALL_ISHANDLED;
        }


        int IMessageFilter.RetryRejectedCall(System.IntPtr hTaskCallee, int dwTickCount, int dwRejectType)
        {
            // Thread call was refused, try again.
            if (dwRejectType == SERVERCALL_RETRYLATER)
            {
                // retry thread call at once, if return value >=0 & <100.
                Debug.WriteLine("COM server busy. Retrying call to EnvDTE interface.");
                return 500;
            }
            // Too busy. Cancel call.
            return -1;
        }


        int IMessageFilter.MessagePending(System.IntPtr hTaskCallee, int dwTickCount, int dwPendingType)
        {
            return PENDINGMSG_WAITDEFPROCESS;
        }
    }

    [ComImport(), Guid("00000016-0000-0000-C000-000000000046"),
    InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMessageFilter
    {

        [PreserveSig]
        int HandleInComingCall(int dwCallType, IntPtr hTaskCaller, int dwTickCount, IntPtr lpInterfaceInfo);


        [PreserveSig]
        int RetryRejectedCall(IntPtr hTaskCallee, int dwTickCount, int dwRejectType);


        [PreserveSig]
        int MessagePending(IntPtr hTaskCallee, int dwTickCount, int dwPendingType);
    }
}
