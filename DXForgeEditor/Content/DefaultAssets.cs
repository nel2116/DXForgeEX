using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace DXForgeEditor.Content
{
    /// <summary>
    /// エディタの既定アセット(Primal Day 075)。プロジェクトによらないアセットを、エディタの実行ファイルの場所の
    /// Resources\DefaultAssets\ に置く。起動時になければ作る。
    /// 既定のマテリアル用シェーダー(Resources/MaterialEditor/DefaultMaterialShaders.hlsl)は起動時にコンパイルする(Primal Day 076)。
    /// </summary>
    static class DefaultAssets
    {
        /// <summary>
        /// 鏡面反射のIBL用のBRDF LUT(256x256、R16G16_FLOAT。ContentToolsのComputeBrdfIntegrationLutで作る)。
        /// 作れなかった場合は null。
        /// </summary>
        public static AssetInfo? BrdfIntegrationLut { get; private set; }

        /// <summary>
        /// 既定のマテリアル用の頂点シェーダー(MainVS)。頂点要素の種類(ElementsType)ごとの変種を持つ。コンパイルできなければ null。
        /// NOTE: Primal Day 076 はコンパイルした結果を使わない(マテリアルを作る処理はコメントアウトされている)。
        ///       マテリアルとエンジンへのアップロード(Primal Day 077, #30)で使うため、ここに持っておく。
        /// </summary>
        public static ShaderGroup? DefaultMaterialVertexShaders { get; private set; }

        /// <summary>
        /// 既定のマテリアル用のピクセルシェーダー(MainPS)。コンパイルできなければ null。
        /// </summary>
        public static ShaderGroup? DefaultMaterialPixelShaders { get; private set; }

        public const string DefaultMaterialShadersResource = @"Resources/MaterialEditor/DefaultMaterialShaders.hlsl";

        public static string DefaultAssetsPath { get; } = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, @"Resources\DefaultAssets\");

        public static string BrdfIntegrationLutFile => Path.Combine(DefaultAssetsPath, $"BrdfIntegrationLut{Asset.AssetFileExtension}");

        /// <summary>
        /// 既定アセットがなければ作る。
        /// </summary>
        public static void GenerateDefaultAssets()
        {
            try
            {
                Directory.CreateDirectory(DefaultAssetsPath);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"既定アセットのフォルダを作れませんでした: {DefaultAssetsPath}");
                return;
            }

            var brdfLutFile = BrdfIntegrationLutFile;
            if (!File.Exists(brdfLutFile))
            {
                ComputeBrdfIntegrationLut(brdfLutFile);
            }

            // NOTE: Primalは作ったときだけ BrdfIntegrationLut を設定するので、2回目以降の起動では null のままになる。
            //       ここでは既存のファイルからも読む。
            BrdfIntegrationLut = File.Exists(brdfLutFile) ? Asset.TryGetAssetInfo(brdfLutFile) : null;

            CompileDefaultMaterialShaders();
        }

        /// <summary>
        /// エンジンのシェーダーのフォルダ(既定のマテリアル用シェーダーがインクルードする Common.hlsli などがある)。
        /// </summary>
        public static string GetEngineShadersPath(string enginePath) =>
            Path.Combine(enginePath, @"DXForgeEngine\Graphics\Direct3D12\Shaders");

        /// <summary>
        /// defines の要素ごとに1つの変種(-D と -I のコンパイラの引数の組)を持つシェーダーグループを作る。キーは keys。
        /// </summary>
        public static ShaderGroup CreateShaderGroup(ShaderType type, string code, string functionName, string[] defines, uint[] keys, string engineShadersPath)
        {
            Debug.Assert(defines.Length == keys.Length);
            var extraArgs = new List<List<string>>();

            foreach (var def in defines)
            {
                var args = new List<string>();
                if (!string.IsNullOrEmpty(def.Trim())) args.AddRange(["-D", def]);
                // NOTE: Primalはエンジンのシェーダーのフォルダを実行時のカレントフォルダからの相対パスで探すため、
                //       Visual Studioから起動したときしか見つからない。ここではエンジンのパスから絶対パスで渡す。
                args.AddRange(["-I", engineShadersPath]);
                extraArgs.Add(args);
            }

            return new ShaderGroup() { Type = type, Code = code, FunctionName = functionName, ExtraArgs = extraArgs, Keys = [.. keys] };
        }

        private static void CompileDefaultMaterialShaders()
        {
            DefaultMaterialVertexShaders = null;
            DefaultMaterialPixelShaders = null;

            if (string.IsNullOrEmpty(MainWindow.DXForgePath))
            {
                Logger.Log(MessageType.Error, "エンジンのパスがわからないため、既定のマテリアル用シェーダーをコンパイルできません。");
                return;
            }

            // NOTE: キーは ContentTools の elements::elements_type(Geometry.h)の値
            var vsDefines = new[] { "ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1", "ELEMENTS_TYPE=3" };
            var vsKeys = new[] { (uint)ElementsType.Position, (uint)ElementsType.Normals, (uint)ElementsType.TSpace };
            var psDefines = new[] { string.Empty };
            var psKeys = new[] { unchecked((uint)ID.INVALID_ID) };

            try
            {
                var code = string.Empty;
                var shaderUri = ContentHelper.GetPackUri(DefaultMaterialShadersResource, typeof(DefaultAssets));
                var info = System.Windows.Application.GetResourceStream(shaderUri);
                using (var reader = new StreamReader(info.Stream))
                {
                    code = reader.ReadToEnd();
                }

                var engineShadersPath = GetEngineShadersPath(MainWindow.DXForgePath);
                var vertexShaders = CreateShaderGroup(ShaderType.Vertex, code, "MainVS", vsDefines, vsKeys, engineShadersPath);
                var pixelShaders = CreateShaderGroup(ShaderType.Pixel, code, "MainPS", psDefines, psKeys, engineShadersPath);

                var stopwatch = Stopwatch.StartNew();
                var compiled = EngineAPI.CompileShader(vertexShaders);
                compiled = EngineAPI.CompileShader(pixelShaders) && compiled;
                Debug.WriteLine($"Default material shaders compiled in {stopwatch.ElapsedMilliseconds} ms");

                if (compiled)
                {
                    DefaultMaterialVertexShaders = vertexShaders;
                    DefaultMaterialPixelShaders = pixelShaders;
                    Logger.Log(MessageType.Info, "既定のマテリアル用シェーダーをコンパイルしました。");
                }
                else
                {
                    Logger.Log(MessageType.Error, "既定のマテリアル用シェーダーをコンパイルできませんでした。");
                }

                // TODO: マテリアルを作り、DefaultMaterial.asset として保存する(Primal Day 077, #30)。
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, "既定のマテリアル用シェーダーをコンパイルできませんでした。");
            }
        }

        private static void ComputeBrdfIntegrationLut(string file)
        {
            try
            {
                var brdfLut = new Texture();
                if (!ContentToolsAPI.ComputeBrdfIntegrationLut(brdfLut, file) || !brdfLut.Save(file).Any())
                {
                    Logger.Log(MessageType.Error, $"BRDF LUTを作れませんでした: {file}");
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"BRDF LUTを作れませんでした: {file}");
            }
        }
    }
}
