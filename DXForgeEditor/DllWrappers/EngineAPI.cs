using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.EngineAPIStructs;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.EngineAPIStructs
{
    [StructLayout(LayoutKind.Sequential)]
    class TransformComponent
    {
        public Vector3 Position;
        public Vector3 Rotation;
        public Vector3 Scale = new(1, 1, 1);
    }

    [StructLayout(LayoutKind.Sequential)]
    class ScriptComponent
    {
        public IntPtr ScriptCreater;
    }

    [StructLayout(LayoutKind.Sequential)]
    class GameEntityDescriptor
    {
        public TransformComponent Transform = new();
        public ScriptComponent Script = new();
    }

    // DXForgeEngineDLL の shader_data(EngineAPI.cpp)と同じ並び
    [StructLayout(LayoutKind.Sequential)]
    class ShaderData : IDisposable
    {
        public int Type;
        public int CodeSize;
        public int ByteCodeSize;
        public int ErrorsSize;
        public int AssemblySize;
        public int HashSize;
        public IntPtr Code;
        public IntPtr ByteCodeErrorAssemblyHash;
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string FunctionName = string.Empty;
        [MarshalAs(UnmanagedType.LPUTF8Str)]
        public string ExtraArgs = string.Empty;

        public void Dispose()
        {
            Marshal.FreeCoTaskMem(ByteCodeErrorAssemblyHash);
            ByteCodeErrorAssemblyHash = IntPtr.Zero;
            Marshal.FreeCoTaskMem(Code);
            Code = IntPtr.Zero;
            GC.SuppressFinalize(this);
        }

        ~ShaderData()
        {
            Dispose();
        }
    }

    // DXForgeEngineDLL の shader_group_data(EngineAPI.cpp)と同じ並び
    [StructLayout(LayoutKind.Sequential)]
    class ShaderGroupData : IDisposable
    {
        public int Type;
        public int Count;
        public int DataSize;
        public IntPtr Data;

        public void Dispose()
        {
            Marshal.FreeCoTaskMem(Data);
            Data = IntPtr.Zero;
            GC.SuppressFinalize(this);
        }

        ~ShaderGroupData()
        {
            Dispose();
        }
    }
}

namespace DXForgeEditor.DllWrappers
{
    static class EngineAPI
    {
        private const string _engineDll = "DXForgeEngineDLL.dll";
        [DllImport(_engineDll, CharSet = CharSet.Ansi)]
        public static extern int LoadGameCodeDll(string dllPath);

        [DllImport(_engineDll)]
        public static extern int UnloadGameCodeDll();

        [DllImport(_engineDll)]
        public static extern IntPtr GetScriptCreater(string name);

        [DllImport(_engineDll)]
        [return: MarshalAs(UnmanagedType.SafeArray)]
        public static extern string[] GetScriptNames();

        [DllImport(_engineDll)]
        public static extern int CreateRenderSurface(IntPtr host, int width, int height);

        [DllImport(_engineDll)]
        public static extern void RemoveRenderSurface(int surfaceId);

        [DllImport(_engineDll)]
        public static extern IntPtr GetWindowHandle(int surfaceId);

        [DllImport(_engineDll)]
        public static extern void ResizeRenderSurface(int surfaceId);

        [DllImport(_engineDll)]
        private static extern int CreateResource(IntPtr data, int type);

        /// <summary>
        /// アセットの PackForEngine() のデータから、エンジンのリソースを作る(Primal Day 077)。作れなければ ID.INVALID_ID。
        /// NOTE: エンジンの初期化(Primal Day 078, #31)がまだないので、エディタからは呼ばれない。
        /// </summary>
        public static int CreateResource(byte[] resourceData, AssetType type)
        {
            if (resourceData == null || resourceData.Length == 0) return ID.INVALID_ID;

            var data = IntPtr.Zero;
            try
            {
                data = Marshal.AllocCoTaskMem(resourceData.Length);
                Marshal.Copy(resourceData, 0, data, resourceData.Length);
                return CreateResource(data, (int)type);
            }
            catch (Exception ex)
            {
                // NOTE: Primal は例外をそのまま投げる(DXForgeEngineDLL.dll が見つからない場合など)
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"{type} のリソースをエンジンに作れませんでした: {ex.Message}");
                return ID.INVALID_ID;
            }
            finally
            {
                Marshal.FreeCoTaskMem(data);
            }
        }

        [DllImport(_engineDll)]
        private static extern void DestroyResource(int id, int type);

        public static void DestroyResource(int id, AssetType type)
        {
            try
            {
                DestroyResource(id, (int)type);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"{type} のリソースをエンジンから破棄できませんでした: {ex.Message}");
            }
        }

        [DllImport(_engineDll)]
        private static extern int AddShaderGroup([In] ShaderGroupData data);

        /// <summary>
        /// コンパイル済みのシェーダーグループをエンジンに登録する(Primal Day 076)。登録できなければ ID.INVALID_ID。
        /// </summary>
        public static int AddShaderGroup(ShaderGroup shaderGroup)
        {
            var packedData = shaderGroup.PackForEngine();

            // NOTE: Primalは例外を投げるが、ここではログに出して無効なIDを返す
            if (packedData == null || packedData.Length == 0)
            {
                Logger.Log(MessageType.Error, $"シェーダー {shaderGroup.FunctionName} はコンパイルされていないため、エンジンに登録できません。");
                return ID.INVALID_ID;
            }

            try
            {
                using var data = new ShaderGroupData();
                data.Type = (int)shaderGroup.Type;
                data.Count = shaderGroup.Count;
                data.DataSize = packedData.Length;
                data.Data = Marshal.AllocCoTaskMem(data.DataSize);
                Marshal.Copy(packedData, 0, data.Data, data.DataSize);
                return AddShaderGroup(data);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"シェーダー {shaderGroup.FunctionName} をエンジンに登録できませんでした。");
                return ID.INVALID_ID;
            }
        }

        [DllImport(_engineDll)]
        public static extern void RemoveShaderGroup(int id);

        [DllImport(_engineDll)]
        private static extern int CompileShader([In, Out] ShaderData data);

        /// <summary>
        /// shaderGroup の Code を、ExtraArgs の引数の組ごとにコンパイルする(Primal Day 076)。
        /// 結果(ByteCode・Errors・Assembly・Hash)は ExtraArgs と同じ数だけ入れ、コンパイルできなかった変種のバイトコードは空にする。
        /// DXCのエラーと警告はログに出す。すべての変種をコンパイルできたら true。
        /// NOTE: Primalは失敗すると例外を投げるが、エディタの起動の途中や async void のコマンドから呼ぶため、
        ///       例外は投げずにログに出して false を返す。
        /// </summary>
        public static bool CompileShader(ShaderGroup shaderGroup)
        {
            Debug.Assert(!string.IsNullOrEmpty(shaderGroup.Code));
            Debug.Assert(!string.IsNullOrEmpty(shaderGroup.FunctionName));
            Debug.Assert(shaderGroup.ExtraArgs.Count > 0 && shaderGroup.ExtraArgs.Count == shaderGroup.Keys.Count);
            shaderGroup.ByteCode.Clear();
            shaderGroup.Errors.Clear();
            shaderGroup.Assembly.Clear();
            shaderGroup.Hash.Clear();

            var succeeded = true;
            var code = Encoding.UTF8.GetBytes(shaderGroup.Code);

            foreach (var args in shaderGroup.ExtraArgs)
            {
                byte[] byteCode = [];
                var errors = string.Empty;
                var assembly = string.Empty;
                byte[] hash = [];
                var argsText = string.Join(" ", args);

                try
                {
                    using var data = new ShaderData();
                    data.Type = (int)shaderGroup.Type;
                    data.CodeSize = code.Length;
                    data.FunctionName = shaderGroup.FunctionName;
                    data.ExtraArgs = string.Join(";", args);
                    data.Code = Marshal.AllocCoTaskMem(Math.Max(code.Length, 1));
                    Marshal.Copy(code, 0, data.Code, code.Length);
                    var result = CompileShader(data);

                    if (data.ByteCodeErrorAssemblyHash != IntPtr.Zero)
                    {
                        var bytes = new byte[data.ByteCodeSize + data.ErrorsSize + data.AssemblySize + data.HashSize];
                        Marshal.Copy(data.ByteCodeErrorAssemblyHash, bytes, 0, bytes.Length);
                        var offset = 0;
                        byteCode = bytes[offset..(offset += data.ByteCodeSize)];
                        errors = Encoding.UTF8.GetString(bytes, offset, data.ErrorsSize);
                        offset += data.ErrorsSize;
                        assembly = Encoding.UTF8.GetString(bytes, offset, data.AssemblySize);
                        offset += data.AssemblySize;
                        hash = bytes[offset..(offset + data.HashSize)];
                    }

                    if (result == 0 || byteCode.Length == 0)
                    {
                        succeeded = false;
                        byteCode = [];
                        hash = [];
                        Logger.Log(MessageType.Error, string.IsNullOrEmpty(errors) ?
                            $"シェーダー {shaderGroup.FunctionName}({argsText})をコンパイルできませんでした(DXCを読み込めないか、引数が正しくありません)。" :
                            $"シェーダー {shaderGroup.FunctionName}({argsText})をコンパイルできませんでした:\n{errors}");
                    }
                    else if (!string.IsNullOrEmpty(errors))
                    {
                        Logger.Log(MessageType.Warning, $"シェーダー {shaderGroup.FunctionName}({argsText}):\n{errors}");
                    }
                }
                catch (Exception ex)
                {
                    // DXForgeEngineDLL.dll が見つからない場合など
                    Debug.WriteLine(ex.Message);
                    succeeded = false;
                    byteCode = [];
                    hash = [];
                    Logger.Log(MessageType.Error, $"シェーダー {shaderGroup.FunctionName}({argsText})をコンパイルできませんでした: {ex.Message}");
                }

                shaderGroup.ByteCode.Add(byteCode);
                shaderGroup.Errors.Add(errors);
                shaderGroup.Assembly.Add(assembly);
                shaderGroup.Hash.Add(hash);
            }

            return succeeded;
        }

        internal static class EntityAPI
        {
            [DllImport(_engineDll)]
            private static extern int CreateGameEntity(GameEntityDescriptor desc);

            public static int CreateGameEntity(GameEntity entity)
            {
                GameEntityDescriptor desc = new();

                // transform component
                {
                    // Transform is required on every entity and cannot be removed.
                    var c = entity.GetComponent<Transform>()!;
                    desc.Transform.Position = c.Position;
                    desc.Transform.Rotation = c.Rotation;
                    desc.Transform.Scale = c.Scale;
                }
                // script component
                {
                    // NOTE: ここでは、現在のプロジェクトがNULLでないかどうかもチェックします。
                    // DLLがロードされているかどうかを知ることができます。
                    // こうすることで、ScriptComponentを持つEntityの作成がDLLがロードされるまで延期されます。
                    var c = entity.GetComponent<Script>();
                    if (c != null && Project.Current != null)
                    {
                        if (Project.Current.AvailableScripts?.Contains(c.Name) == true)
                        {
                            desc.Script.ScriptCreater = GetScriptCreater(c.Name);
                        }
                        else
                        {
                            Logger.Log(MessageType.Error, $"{c.Name}という名前のスクリプトが見つかりません！");
                        }
                    }
                    return CreateGameEntity(desc);
                }
            }

            [DllImport(_engineDll)]
            public static extern void RemoveGameEntity(int id);
            public static void RemoveGameEntity(GameEntity entity)
            {
                RemoveGameEntity(entity.EntityId);
            }
        }
    }
}
