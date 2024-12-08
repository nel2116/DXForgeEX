using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.InteropServices.ComTypes;
using System.Text;

namespace DXForgeEditor.GameDev
{
    static class VisualStudio
    {
        private static EnvDTE80.DTE2 _vsInstance = null;            // Visual Studio instance
        // 16.0 is for Visual Studio 2019
        private static readonly string _progID = "VisualStudio.DTE.17.0";


        [DllImport("ole32.dll")]
        private static extern int CreateBindCtx(int reserved, out IBindCtx ppbc);

        [DllImport("ole32.dll")]
        private static extern int GetRunningObjectTable(int reserved, out IRunningObjectTable pprot);
        public static void OpenVisualStudio(string solutionPath)
        {
            IRunningObjectTable rot = null;
            IEnumMoniker monikerTable = null;
            IBindCtx bindCtx = null;
            try
            {
                // VisualStudioを見つけて開く
                // 実行中のオブジェクト・テーブルを取得する
                var hr = GetRunningObjectTable(0, out rot);
                if (hr < 0 || rot == null) throw new COMException($"GetRunningObjectTable() returned HRESULT: {hr:x8}");

                // 実行中のオブジェクト・テーブルからモニカを取得する
                // モニカとは、オブジェクトを一意に識別するための名前付きオブジェクト
                rot.EnumRunning(out monikerTable);
                monikerTable.Reset();

                // バインド・コンテキストを作成する
                hr = CreateBindCtx(0, out bindCtx);
                if (hr < 0 || bindCtx == null) throw new COMException($"CreateBindCtx() returned HRESULT: {hr:x8}");

                // モニカを列挙して、VisualStudioのインスタンスを取得する
                IMoniker[] crrentMoniker = new IMoniker[1];
                while (monikerTable.Next(1, crrentMoniker, IntPtr.Zero) == 0)
                {
                    string name = string.Empty;
                    crrentMoniker[0]?.GetDisplayName(bindCtx, null, out name);

                    // VisualStudioのインスタンスを取得
                    if (name.Contains(_progID))
                    {
                        hr = rot.GetObject(crrentMoniker[0], out object obj);
                        if (hr < 0 || obj == null) throw new COMException($"Running object table's GetObject() returned HRESULT: {hr:x8}");

                        EnvDTE80.DTE2 dte = obj as EnvDTE80.DTE2;
                        var solutionName = dte.Solution.FullName;

                        // ソリューションが開かれているかチェック
                        if (solutionName == solutionPath)
                        {
                            _vsInstance = dte;
                            break;
                        }
                    }

                }

                if (_vsInstance == null)
                {
                    Type visalStudioType = Type.GetTypeFromProgID(_progID, true);
                    _vsInstance = Activator.CreateInstance(visalStudioType) as EnvDTE80.DTE2;
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "VisualStudioを開けません。");
            }
            finally
            {
                // ComObjectの解放
                if (monikerTable != null) Marshal.ReleaseComObject(monikerTable);
                if (rot != null) Marshal.ReleaseComObject(rot);
                if (bindCtx != null) Marshal.ReleaseComObject(bindCtx);
            }
        }

        public static void CloseVisualStudio()
        {
            if (_vsInstance?.Solution.IsOpen == true)
            {
                _vsInstance.ExecuteCommand("File.SaveAll");
                _vsInstance.Solution.Close();
            }
            _vsInstance?.Quit();
        }

        public static bool AddFilesToSolution(string solution, string projectName, string[] files)
        {
            Debug.Assert(files?.Length > 0);
            OpenVisualStudio(solution);
            try
            {
                if (_vsInstance != null)
                {
                    if (!_vsInstance.Solution.IsOpen)
                    {
                        _vsInstance.Solution.Open(solution);
                    }
                    else
                    {
                        _vsInstance.ExecuteCommand("File.SaveAll");
                    }

                    foreach (EnvDTE.Project project in _vsInstance.Solution.Projects)
                    {
                        if (project.UniqueName.Contains(projectName))
                        {
                            foreach (string file in files)
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
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "ファイルをソリューションに追加できません。");
                return false;
            }
            return true;
        }
    }
}
