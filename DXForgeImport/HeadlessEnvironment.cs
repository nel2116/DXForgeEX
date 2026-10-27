using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using DXForgeEditor.DllWrappers;

namespace DXForgeImport
{
    /// <summary>
    /// エディタの Content のコードを、エディタのウィンドウもプロジェクトもなしで動かすための準備。
    /// - WPF の Application: Logger が Application.Current.Dispatcher にメッセージを積み、Geometry のアイコン生成が
    ///   Application.Current.Dispatcher と FindResource を使うため。エディタの App は使わず、素の Application に
    ///   エディタのリソース辞書(EditorColors.xaml)だけをマージする。App.xaml の辞書は pack://application:,,,/Dictionaries/...
    ///   (= 実行中の exe のアセンブリ)を指すため、DXForgeImport.exe から生成すると見つからないうえ、
    ///   StartupUri(MainWindow)を持つため。Run() は呼ばないので、ウィンドウは開かず、Dispatcher も回さない。
    /// - ContentTools.dll: 実行ファイルと同じフォルダから読む。Debug の ContentTools.dll の assert がダイアログを出して
    ///   止まらないよう、Debug の CRT(ucrtbased.dll)が読み込まれていれば、assert を標準エラーへ出すようにする。
    /// </summary>
    static class HeadlessEnvironment
    {
        private const string ContentToolsDll = "ContentTools.dll";
        private static Application? _application;
        private static bool _contentToolsLoaded;

        private delegate int SetErrorMode(int mode);
        private delegate uint SetAbortBehavior(uint flags, uint mask);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string moduleName);

        public static void Initialize()
        {
            var path = Path.Combine(AppContext.BaseDirectory, ContentToolsDll);
            if (!NativeLibrary.TryLoad(path, out _))
            {
                throw new ExitException(ExitCode.ContentToolsUnavailable,
                    $"{ContentToolsDll} を読み込めません: {path}(ContentTools を *Editor 構成でビルドし、実行ファイルと同じフォルダに置いてください)");
            }
            _contentToolsLoaded = true;
            RedirectDebugCrtAsserts();

            _application = Application.Current ?? new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
            _application.Resources.MergedDictionaries.Add(new ResourceDictionary
            {
                Source = new Uri("pack://application:,,,/DXForgeEditor;component/Dictionaries/EditorColors.xaml", UriKind.Absolute),
            });
        }

        public static void Shutdown()
        {
            // NOTE: Application.Shutdown() は呼ばない。止めた Dispatcher に Logger.Log(async void)が BeginInvoke して await すると、
            //       取り消しの例外が async void から投げられてプロセスが落ちるため。Dispatcher は回さないまま終了する。
            if (_contentToolsLoaded)
            {
                ContentToolsAPI.ShutDownContentTools();
                _contentToolsLoaded = false;
            }
        }

        private static void RedirectDebugCrtAsserts()
        {
            // すでに読み込まれている ucrtbased.dll(Debug の ContentTools.dll が使うもの)だけを設定する。Release なら読み込まれていない。
            var crt = GetModuleHandleW("ucrtbased.dll");
            if (crt == IntPtr.Zero) return;
            if (NativeLibrary.TryGetExport(crt, "_set_error_mode", out var setErrorMode))
            {
                Marshal.GetDelegateForFunctionPointer<SetErrorMode>(setErrorMode)(1); // _OUT_TO_STDERR
            }
            if (NativeLibrary.TryGetExport(crt, "_set_abort_behavior", out var setAbortBehavior))
            {
                Marshal.GetDelegateForFunctionPointer<SetAbortBehavior>(setAbortBehavior)(0, 3); // _WRITE_ABORT_MSG | _CALL_REPORTFAULT
            }
        }
    }
}
