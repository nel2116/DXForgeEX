using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.EngineAPIStructs;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.EngineAPIStructs
{
    // DXForgeEngineDLL の editor::engine_init_error(EngineInit.h)と同じ値(Primal Day 078)。
    // DXForgeEditor/Tests/engine_init_test.ps1 が、C++ の値と一致することを確かめる。
    enum EngineInitError : int
    {
        [Description("エンジンの初期化に成功しました")]
        Succeeded = 0,
        [Description("エンジンの初期化中に不明なエラーが起きました")]
        Unknown,
        [Description("エンジンの組み込みシェーダーのコンパイルに失敗しました")]
        ShaderCompilation,
        [Description("グラフィックスモジュールの初期化に失敗しました")]
        Graphics,
    }

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

    // DXForgeEngineDLL の geometry_component(EntityAPI.cpp)と同じ並び(Primal Day 079)
    [StructLayout(LayoutKind.Sequential)]
    class GeometryComponent : IDisposable
    {
        public int GeometryContentId = ID.INVALID_ID;
        public int MaterialCount;
        public IntPtr MaterialIds;

        public GeometryComponent() { }

        /// <summary>
        /// アップロード済みのジオメトリと、サブメッシュごとのマテリアルのIDを詰める。マテリアルのIDの配列は Dispose() で解放する。
        /// </summary>
        public GeometryComponent(Components.Geometry geometry)
        {
            var materials = geometry.MaterialsList;
            Debug.Assert(materials.Count > 0 && materials.All(x => ID.IsValid(x.UploadedAsset?.ContentId ?? ID.INVALID_ID)));

            using var writer = new BinaryWriter(new MemoryStream());
            materials.ForEach(mtl => writer.Write(mtl.UploadedAsset?.ContentId ?? ID.INVALID_ID));
            writer.Flush();
            var data = ((MemoryStream)writer.BaseStream).ToArray();
            Debug.Assert(data.Length == materials.Count * sizeof(int));

            GeometryContentId = geometry.ContentId;
            MaterialCount = materials.Count;
            MaterialIds = Marshal.AllocCoTaskMem(data.Length);
            Marshal.Copy(data, 0, MaterialIds, data.Length);
        }

        public void Dispose()
        {
            if (MaterialIds != IntPtr.Zero)
            {
                Marshal.FreeCoTaskMem(MaterialIds);
                MaterialIds = IntPtr.Zero;
            }
            GC.SuppressFinalize(this);
        }

        ~GeometryComponent()
        {
            Dispose();
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class GameEntityDescriptor : IDisposable
    {
        public TransformComponent Transform = new();
        public ScriptComponent Script = new();
        public GeometryComponent Geometry = new();

        public void Dispose()
        {
            Geometry.Dispose();
            GC.SuppressFinalize(this);
        }

        ~GameEntityDescriptor()
        {
            Dispose();
        }
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

        [DllImport(_engineDll, EntryPoint = "InitializeEngine", CharSet = CharSet.Unicode)]
        private static extern EngineInitError InitializeEngineNative(string? enginePath);

        /// <summary>
        /// エンジンのシェーダーをコンパイルし、グラフィックスを初期化する(Primal Day 078)。
        /// enginePath はエンジン(リポジトリ)のルートのフォルダ(MainWindow.DXForgePath)で、シェーダーのソースはその
        /// DXForgeEngine\Graphics\Direct3D12\Shaders から探す。shaders.bin は作業フォルダからの相対パス(shaders\d3d12\)なので、
        /// 作業フォルダをエディタの実行ファイルのフォルダ(x64\&lt;構成&gt;)にしてから呼ぶ。
        /// NOTE: Primal の InitializeEngine は引数を取らず、作業フォルダからの相対パスでソースを探す。エディタをリポジトリの外に置き、
        ///       DXFORGE_ENGINE などでエンジンのパスを決めて起動したときも初期化できるよう、パスを渡す(#117、#31)。
        /// NOTE: Primal は DllImport をそのまま公開するが、OnMainWindowLoaded から呼ぶので、DLL を読み込めないときなども
        ///       例外を投げずに Unknown を返す。
        /// </summary>
        public static EngineInitError InitializeEngine(string? enginePath)
        {
            try
            {
                return InitializeEngineNative(enginePath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"エンジンを初期化できませんでした: {ex.Message}");
                return EngineInitError.Unknown;
            }
        }

        [DllImport(_engineDll, EntryPoint = "GetGraphicsRuntimeDescription")]
        private static extern IntPtr GetGraphicsRuntimeDescriptionNative(out uint agilitySdk);

        /// <summary>
        /// 使っている D3D12 のランタイム(Agility SDK か OS か、バージョンとパス)の説明(#155)。InitializeEngine のあとで呼ぶ。
        /// agilitySdk は Agility SDK を使っているとき true。DLL を呼べないときは空を返し、例外は投げない。
        /// </summary>
        public static string GetGraphicsRuntimeDescription(out bool agilitySdk)
        {
            agilitySdk = false;
            try
            {
                var description = GetGraphicsRuntimeDescriptionNative(out uint agility);
                agilitySdk = agility != 0;
                // NOTE: 文字列は DLL の持ち物なので、IntPtr で受けて写す(string で受けると CoTaskMemFree される)
                return description == IntPtr.Zero ? string.Empty : Marshal.PtrToStringUni(description) ?? string.Empty;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                return string.Empty;
            }
        }

        [DllImport(_engineDll, EntryPoint = "ShutdownEngine")]
        private static extern void ShutdownEngineNative();

        /// <summary>
        /// InitializeEngine で初期化したグラフィックスを終了する。初期化していないとき、2回目は何もしない(DLL の側で確かめる)。
        /// </summary>
        public static void ShutdownEngine()
        {
            try
            {
                ShutdownEngineNative();
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"エンジンを終了できませんでした: {ex.Message}");
            }
        }

        /// <summary>
        /// InitializeEngine が失敗したときにエディタが出す文言(理由と、確かめること)。例外は投げない。
        /// </summary>
        internal static string GetInitializeEngineErrorMessage(EngineInitError error, string? enginePath)
        {
            try
            {
                var reason = Enum.IsDefined(error) ? error.GetDescription() : $"エンジンの初期化に失敗しました(不明なエラーのコード {(int)error})";
                var hint = error switch
                {
                    EngineInitError.ShaderCompilation =>
                        @"エンジンのシェーダー(エンジンのパスの DXForgeEngine\Graphics\Direct3D12\Shaders)が見つからないか、" +
                        "コンパイルできませんでした。dxcompiler.dll がエディタの隣にあるか、シェーダーにエラーがないか(デバッグ出力)を確かめてください。",
                    EngineInitError.Graphics =>
                        @"Direct3D 12 を初期化できませんでした。shaders\d3d12\shaders.bin を読めるか、" +
                        "GPU とドライバーが Direct3D 12(機能レベル 11_0 以上)に対応しているかを確かめてください。",
                    EngineInitError.Succeeded => string.Empty,
                    _ => "DXForgeEngineDLL.dll を読み込めないか、初期化の途中で予期しないエラーが起きました(ログとデバッグ出力を確かめてください)。",
                };
                string workingDirectory;
                try
                {
                    workingDirectory = Environment.CurrentDirectory;
                }
                catch (Exception ex)
                {
                    workingDirectory = $"(取得できません: {ex.Message})";
                }
                return string.IsNullOrEmpty(hint) ? reason :
                    $"{reason}。\n{hint}\nエンジンのパス: {(string.IsNullOrEmpty(enginePath) ? "(なし)" : enginePath)}\n作業フォルダ: {workingDirectory}";
            }
            catch (Exception ex)
            {
                return $"エンジンの初期化に失敗しました(コード {(int)error}): {ex.Message}";
            }
        }

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
        /// InitializeEngine のあとで呼ぶ。NOTE: エディタの画面から呼ぶのは Geometry コンポーネント(Primal Day 079〜, #32)から。
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

        // ビューポートに描くジオメトリのコンポーネントの ID を設定する(Primal Day 083)
        [DllImport(_engineDll)]
        public static extern void SetGeometryIds(int surfaceId, [In] int[] geometryComponentIds, int count);

        // NOTE: render thread(RenderSurfaceHost)から呼ぶ。DLL の関数は同じミューテックス(ticket_mutex)を取る。
        [DllImport(_engineDll)]
        public static extern void RenderFrame(int surfaceId, int cameraId, ulong lightSet);

        [DllImport(_engineDll)]
        public static extern int GetComponentId(int entityId, ComponentType type);

        internal static class EntityAPI
        {
            [DllImport(_engineDll)]
            private static extern int CreateGameEntity(GameEntityDescriptor desc);

            public static int CreateGameEntity(GameEntity entity)
            {
                using GameEntityDescriptor desc = new();

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
                }
                // geometry component(Primal Day 079)
                {
                    // NOTE: アップロードできなかった(Geometry.Load() が失敗した)ときは、Geometry なしで作る
                    var c = entity.GetComponent<Components.Geometry>();
                    if (c != null && ID.IsValid(c.ContentId) && c.MaterialsList.Count > 0)
                    {
                        desc.Geometry = new(c);
                    }
                }

                return CreateGameEntity(desc);
            }

            [DllImport(_engineDll)]
            public static extern void RemoveGameEntity(int id);
            public static void RemoveGameEntity(GameEntity entity)
            {
                RemoveGameEntity(entity.EntityId);
            }

            [DllImport(_engineDll)]
            private static extern int UpdateComponent(int entityId, GameEntityDescriptor desc, ComponentType type);

            /// <summary>
            /// エンティティの Script か Geometry を更新する(Primal Day 083)。Transform は更新できない。
            /// </summary>
            public static bool UpdateComponent(GameEntity entity, ComponentType type)
            {
                Debug.Assert(ID.IsValid(entity.EntityId));
                Debug.Assert(type != ComponentType.Transform);

                using GameEntityDescriptor desc = new();

                switch (type)
                {
                    case ComponentType.Transform: return false;
                    case ComponentType.Script:
                        {
                            var c = entity.GetComponent<Script>();
                            if (c != null)
                            {
                                if (Project.Current?.AvailableScripts?.Contains(c.Name) == true)
                                {
                                    desc.Script.ScriptCreater = GetScriptCreater(c.Name);
                                }
                                else
                                {
                                    Logger.Log(MessageType.Error, $"{c.Name}という名前のスクリプトが見つかりません！");
                                }
                            }
                        }
                        break;
                    case ComponentType.Geometry:
                        {
                            // NOTE: CreateGameEntity と同じく、アップロードできなかったときは Geometry なしで渡す
                            var c = entity.GetComponent<Components.Geometry>();
                            if (c != null && ID.IsValid(c.ContentId) && c.MaterialsList.Count > 0)
                            {
                                desc.Geometry = new(c);
                            }
                        }
                        break;
                    default:
                        break;
                }

                return UpdateComponent(entity.EntityId, desc, type) != 0;
            }
        }
    }
}
