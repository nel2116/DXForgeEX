using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace DXForgeEditor.Content
{
    /// <summary>
    /// エディタの既定アセット(Primal Day 075)。プロジェクトによらないアセットを、エディタの実行ファイルの場所の
    /// Resources\DefaultAssets\ に置く。起動時になければ作る。
    /// </summary>
    static class DefaultAssets
    {
        /// <summary>
        /// 鏡面反射のIBL用のBRDF LUT(256x256、R16G16_FLOAT。ContentToolsのComputeBrdfIntegrationLutで作る)。
        /// 作れなかった場合は null。
        /// </summary>
        public static AssetInfo? BrdfIntegrationLut { get; private set; }

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
