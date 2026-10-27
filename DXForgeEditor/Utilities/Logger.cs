using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Data;

namespace DXForgeEditor.Utilities
{
    enum MessageType
    {
        Info = 0x01,
        Warning = 0x02,
        Error = 0x04,
    }

    class LogMessage
    {
        public DateTime Time { get; }
        public MessageType MessageType { get; }
        public string Message { get; }
        public string File { get; }
        public string Caller { get; }
        public int Line { get; }
        public string MetaData => $"{File}: {Caller}: ({Line})";

        public LogMessage(MessageType type, string msg, string file, string caller, int line)
        {
            Time = DateTime.Now;
            MessageType = type;
            Message = msg;
            File = Path.GetFileName(file);
            Caller = caller;
            Line = line;
        }
    }

    static class Logger
    {
        private static int _messageFilter = (int)(MessageType.Info) | (int)(MessageType.Warning) | (int)(MessageType.Error);
        private static readonly ObservableCollection<LogMessage> _messages = new();
        public static ReadOnlyObservableCollection<LogMessage> Messages { get; } = new ReadOnlyObservableCollection<LogMessage>(_messages);
        public static CollectionViewSource FilterdMessages { get; } = new CollectionViewSource() { Source = Messages };

        /// <summary>
        /// Log() を呼んだスレッドで、Dispatcher に積む前に同期的に通知する。
        /// エディタの画面(LoggerView)は Messages を使い、これは使わない。コマンドラインのインポーター(DXForgeImport, Issue #10)が
        /// Dispatcher を回さずにログを標準エラーと結果のJSONへ出すために使う。
        /// </summary>
        public static event Action<MessageType, string>? MessageLogged;

        public static async void Log(MessageType type, string msg,
            [CallerFilePath] string file = "", [CallerMemberName] string caller = "",
            [CallerLineNumber] int line = 0)
        {
            MessageLogged?.Invoke(type, msg);
            await Application.Current.Dispatcher.BeginInvoke(new Action(() =>
            {
                _messages.Add(new LogMessage(type, msg, file, caller, line));
            }));
        }

        public static async void Clear()
        {
            await Application.Current.Dispatcher.BeginInvoke(new Action(_messages.Clear));
        }

        public static void SetMessageFillter(int mask)
        {
            _messageFilter = mask;
            FilterdMessages.View.Refresh();
        }

        static Logger()
        {
            FilterdMessages.Filter += (s, e) =>
            {
                var type = (int)((LogMessage)e.Item).MessageType;
                e.Accepted = (type & _messageFilter) != 0;
            };
        }
    }
}
