using DXForgeEditor.DllWrappers;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Interop;

namespace DXForgeEditor.Utilities
{
    class FrameInfo
    {
        public int SurfaceId { get; init; }
        public ulong LightSetKey { get; init; }
        public int CameraId { get; init; } = ID.INVALID_ID;
    }

    delegate FrameInfo RenderFrameCallback(int frame);

    class RenderSurfaceHost : HwndHost
    {
        private static class RenderThread
        {
            private static readonly Dictionary<IntPtr, RenderFrameCallback> _callbackMap = new();
            private static readonly List<RenderFrameCallback> _callbacks = new();
            private static readonly List<int> _frameCounts = new();
            private static readonly object _lock = new();
            private static Thread? _renderThread;

            public static bool IsRunning => Volatile.Read(ref _renderThread) != null;

            // NOTE: Primal は最後のコールバックを外すと Task.Run で Stop(Join)を呼ぶが、Stop が終わる前に次の Register が来ると、
            //       スレッドを起動しないまま止まり、そのビューポートが描かれなくなる。ここでは、コールバックがなくなったら
            //       lock の中で自分から抜ける。Register は同じ lock の中で見るので、すれ違わない。
            private static void Render()
            {
                while (true)
                {
                    var start = Stopwatch.GetTimestamp();
                    lock (_lock)
                    {
                        if (_callbacks.Count == 0)
                        {
                            _renderThread = null;
                            return;
                        }

                        for (int i = 0; i < _callbacks.Count; i++)
                        {
                            var info = _callbacks[i](_frameCounts[i]++);
                            EngineAPI.RenderFrame(info.SurfaceId, info.CameraId, info.LightSetKey);
                        }
                    }

                    // NOTE: サーフェスがない(大きさが 0 など)と RenderFrame はすぐ戻り、VSync で待たない。
                    //       Primal はここに待ちがなく、1 コアを使い続ける。
                    if (Stopwatch.GetElapsedTime(start).TotalMilliseconds < 1.0)
                    {
                        Thread.Sleep(1);
                    }
                }
            }

            public static void RegisterCallback(IntPtr hwnd, RenderFrameCallback callback)
            {
                lock (_lock)
                {
                    if (!_callbackMap.ContainsKey(hwnd))
                    {
                        _callbackMap[hwnd] = callback;
                        _callbacks.Add(callback);
                        _frameCounts.Add(0);

                        if (_renderThread == null)
                        {
                            _renderThread = new Thread(Render)
                            {
                                Name = "RenderThread",
                                IsBackground = true
                            };
                            _renderThread.Start();
                        }
                    }
                }
            }

            public static void UnregisterCallback(IntPtr hwnd)
            {
                // NOTE: render thread は lock を持ったまま描くので、ここを抜けたあとにこのコールバックは呼ばれない。
                lock (_lock)
                {
                    if (_callbackMap.TryGetValue(hwnd, out var callback))
                    {
                        var index = _callbacks.IndexOf(callback);
                        _frameCounts.RemoveAt(index);
                        _callbacks.RemoveAt(index);
                        _callbackMap.Remove(hwnd);
                    }
                }
            }
        } // RenderThread

        private readonly int _width = 800;
        private readonly int _height = 600;
        private readonly DelayEventTimer _resizeTimer;
        private readonly RenderFrameCallback _renderFrameCallback;
        // NOTE: Primal は Task.Run の中で AutoResetEvent を待つが、BuildWindowCore が呼ばれないまま消えると、スレッドが待ち続ける。
        private readonly TaskCompletionSource _ready = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private IntPtr _windowHandle = IntPtr.Zero;
        private bool _disposed;

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool DestroyWindow(IntPtr hwnd);

        public int SurfaceId { get; private set; } = ID.INVALID_ID;

        public bool HostReady { get; private set; }

        private void Resize(object? sender, DelayEventTimerArgs e)
        {
            e.RepeaEvent = KeyboardHelper.GetAsyncKeyState(KeyboardHelper.VKey.MouseLeft) < 0;
            if (!e.RepeaEvent)
            {
                EngineAPI.ResizeRenderSurface(SurfaceId);
            }
        }

        public RenderSurfaceHost(double width, double height, RenderFrameCallback renderFrameCallback)
        {
            _width = (int)width;
            _height = (int)height;
            _resizeTimer = new DelayEventTimer(TimeSpan.FromMilliseconds(50.0));
            _resizeTimer.Triggered += Resize;
            _renderFrameCallback = renderFrameCallback;
            SizeChanged += (s, e) => _resizeTimer.Trigger();
        }

        protected override void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (disposing)
                {
                    _resizeTimer.Disable();
                    HostReady = false;
                    RenderThread.UnregisterCallback(_windowHandle);
                    // NOTE: BuildWindowCore が呼ばれる前に消されると、サーフェスはまだない(RemoveRenderSurface の assert に当たる)。
                    // RemoveRenderSurface は ShutdownEngine のあとでも呼べる。
                    if (ID.IsValid(SurfaceId))
                    {
                        EngineAPI.RemoveRenderSurface(SurfaceId);
                    }
                    _windowHandle = IntPtr.Zero;
                    SurfaceId = ID.INVALID_ID;
                    _ready.TrySetResult();
                }

                _disposed = true;
            }
            base.Dispose(disposing);
        }

        /// <summary>
        /// BuildWindowCore が終わるか、host が消されると完了する。消されたときは HostReady が false のまま。
        /// </summary>
        public Task WaitReady() => _ready.Task;

        protected override HandleRef BuildWindowCore(HandleRef hwndParent)
        {
            SurfaceId = EngineAPI.CreateRenderSurface(hwndParent.Handle, _width, _height);
            Debug.Assert(ID.IsValid(SurfaceId));
            _windowHandle = EngineAPI.GetWindowHandle(SurfaceId);
            Debug.Assert(_windowHandle != IntPtr.Zero);
            RenderThread.RegisterCallback(_windowHandle, _renderFrameCallback);
            HostReady = RenderThread.IsRunning;
            _ready.TrySetResult();
            return new HandleRef(this, _windowHandle);
        }

        protected override void DestroyWindowCore(HandleRef hwnd)
        {
            DestroyWindow(hwnd.Handle);
        }
    }
}
