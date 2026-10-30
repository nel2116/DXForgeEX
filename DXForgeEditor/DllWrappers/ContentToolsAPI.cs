using DXForgeEditor.Content;
using DXForgeEditor.ContentToolsAPIStructs;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.ContentToolsAPIStructs
{
    enum TextureImportError : int
    {
        [Description("インポートに成功")]
        Succeeded = 0,
        [Description("不明なエラー")]
        Unknown,
        [Description("テクスチャ圧縮に失敗しました。")]
        Compress,
        [Description("テクスチャの解凍に失敗しました。")]
        Decompress,
        [Description("テクスチャをメモリにロードできませんでした。")]
        Load,
        [Description("テクスチャのミップマップ生成に失敗しました。")]
        MipmapGeneration,
        [Description("サブリソースの最大サイズが4GBを超えています。")]
        MaxSizeExceeded,
        [Description("ソース画像の寸法が同じでないです。")]
        SizeMismatch,
        [Description("ソース画像が同じフォーマットでないです。")]
        FormatMismatch,
        [Description("ソース画像ファイルが見つかりません。")]
        FileNotFound,
        [Description("キューブマップの画像数は6の倍数であるか、ソース画像が同じサイズとフォーマットの正距円筒画像でなければならない。")]
        NeedSixImages,
    }

    [StructLayout(LayoutKind.Sequential)]
    class TextureImportSettings
    {
        public string Sources = string.Empty;
        public int SourceCount;
        public int Dimension;
        public int MipLevels;
        public float AlphaThreshold;
        public int PreferBC7;
        public int OutputFormat;
        public int Compress;
        public int CubeMapSize;
        public int MirrorCubeMap;
        public int PrefilterCubeMap;

        public void FromContentSettings(Texture texture)
        {
            var settings = texture.ImportSettings;

            Sources = string.Join(";", settings.Sources);
            SourceCount = settings.Sources.Count;
            Dimension = (int)settings.Dimension;
            MipLevels = settings.MipLevels;
            AlphaThreshold = settings.AlphaThreshold;
            PreferBC7 = settings.PreferBC7 ? 1 : 0;
            OutputFormat = (int)settings.OutputFormat;
            Compress = settings.Compress ? 1 : 0;
            CubeMapSize = settings.CubeMapSize;
            MirrorCubeMap = settings.MirrorCubeMap ? 1 : 0;
            PrefilterCubeMap = settings.PrefilterCubeMap ? 1 : 0;
        }

        public TextureImportSettings Clone() => (TextureImportSettings)MemberwiseClone();
    }

    [StructLayout(LayoutKind.Sequential)]
    class TextureInfo
    {
        public int Width;
        public int Height;
        public int ArraySize;
        public int MipLevels;
        public int Format;
        public int ImportError;
        public int Flags;

        public TextureInfo Clone() => (TextureInfo)MemberwiseClone();
    }

    [StructLayout(LayoutKind.Sequential)]
    class TextureData : IDisposable
    {
        public IntPtr SubresourceData;
        public int SubresourceSize;
        public IntPtr Icon;
        public int IconSize;
        public TextureInfo Info = new();
        public TextureImportSettings ImportSettings = new();

        /// <summary>
        /// サブリソース、テクスチャの情報、インポート設定をコピーする(アイコンはコピーしない)。
        /// </summary>
        public TextureData Clone()
        {
            var data = new TextureData();
            if (SubresourceData != IntPtr.Zero && SubresourceSize > 0)
            {
                var bytes = new byte[SubresourceSize];
                Marshal.Copy(SubresourceData, bytes, 0, SubresourceSize);
                data.SubresourceData = Marshal.AllocCoTaskMem(SubresourceSize);
                data.SubresourceSize = SubresourceSize;
                Marshal.Copy(bytes, 0, data.SubresourceData, SubresourceSize);
            }
            data.Info = Info.Clone();
            data.ImportSettings = ImportSettings.Clone();
            return data;
        }

        public void Dispose()
        {
            Marshal.FreeCoTaskMem(SubresourceData);
            Marshal.FreeCoTaskMem(Icon);
            SubresourceData = IntPtr.Zero;
            Icon = IntPtr.Zero;
            GC.SuppressFinalize(this);
        }

        ~TextureData()
        {
            Dispose();
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class GeometryImportSettings
    {
        public float SmoothingAngle = 178f;
        public byte CalculateNormals = 0;
        public byte CalculateTangents = 1;
        public byte ReverseHandedness = 0;
        public byte ImportEnabededTextures = 1;
        public byte ImportAnimations = 1;
        public byte CoalesceMeshes = 0;

        private byte ToByte(bool value) => value ? (byte)1 : (byte)0;

        public void FromContentSettings(Content.Geometry geometry)
        {
            var settings = geometry.ImportSettings;
            SmoothingAngle = settings.SmoothingAngle;
            CalculateNormals = ToByte(settings.CalculateNormals);
            CalculateTangents = ToByte(settings.CalculateTangents);
            ReverseHandedness = ToByte(settings.ReverseHandedness);
            ImportEnabededTextures = ToByte(settings.ImportEmbeddedTextures);
            ImportAnimations = ToByte(settings.ImportAnimations);
            CoalesceMeshes = ToByte(settings.CoalesceMeshes);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class SceneData : IDisposable
    {
        public IntPtr Data;
        public int DataSize;
        public GeometryImportSettings ImportSettings = new();

        public void Dispose()
        {
            Marshal.FreeCoTaskMem(Data);
            GC.SuppressFinalize(this);
        }

        ~SceneData()
        {
            Dispose();
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class PrimitiveInitInfo
    {
        public Content.PrimitiveMeshType Type;
        public int SegmentsX = 1;
        public int SegmentsY = 1;
        public int SegmentsZ = 1;
        public Vector3 Size = new(1f);
        public int LOD = 0;
    }
}

namespace DXForgeEditor.DllWrappers
{
    static class ContentToolsAPI
    {
        private const string _toolsDLL = "ContentTools.dll";
        private delegate void ProgressCallback(int value, int maxValue);

        [DllImport(_toolsDLL)]
        public static extern void ShutDownContentTools();


        #region Texture
        private static SliceArray3D GetSlices(TextureData data)
        {
            Debug.Assert(data.Info.MipLevels > 0);
            Debug.Assert(data.SubresourceData != IntPtr.Zero && data.SubresourceSize > 0);

            var subresourceData = new byte[data.SubresourceSize];
            Marshal.Copy(data.SubresourceData, subresourceData, 0, data.SubresourceSize);

            return SlicesFromBinary(subresourceData, data.Info.ArraySize, data.Info.MipLevels, ((TextureFlags)data.Info.Flags).HasFlag(TextureFlags.IsVolumeMap));
        }

        private static Slice? GetIcon(TextureData data)
        {
            // サブリソースは圧縮されない。 最初の画像をアイコンに使用するだけです。
            if (data.ImportSettings.Compress == 0) return null;

            Debug.Assert(data.Icon != IntPtr.Zero && data.IconSize > 0);

            var icon = new byte[data.IconSize];
            Marshal.Copy(data.Icon, icon, 0, data.IconSize);

            return SlicesFromBinary(icon, 1, 1, false).First()?.First()?.First();
        }

        private static void SetSubresourceData(SliceArray3D slices, TextureData data)
        {
            var subresourceData = SlicesToBinary(slices);
            data.SubresourceData = Marshal.AllocCoTaskMem(subresourceData.Length);
            data.SubresourceSize = subresourceData.Length;
            Marshal.Copy(subresourceData, 0, data.SubresourceData, data.SubresourceSize);
        }

        private static void GetTextureDataInfo(Texture texture, TextureData data)
        {
            var info = data.Info;

            info.Width = texture.Width;
            info.Height = texture.Height;
            info.ArraySize = texture.ArraySize;
            info.MipLevels = texture.MipLevels;
            info.Format = (int)texture.Format;
            info.Flags = (int)texture.Flags;
        }

        private static void GetTextureInfo(Texture texture, TextureData data)
        {
            var info = data.Info;
            // NOTE: 一部のプロパティは設定時にフラグを確認するため、フラグを最初に設定する。
            texture.Flags = (TextureFlags)info.Flags;
            texture.Width = info.Width;
            texture.Height = info.Height;
            texture.ArraySize = info.ArraySize;
            texture.MipLevels = info.MipLevels;
            texture.Format = (DXGI_FORMAT)info.Format;
        }

        public static SliceArray3D SlicesFromBinary(byte[] data, int arraySize, int mipLevels, bool is3D)
        {
            Debug.Assert(data?.Length > 0 && arraySize > 0);
            Debug.Assert(mipLevels > 0 && mipLevels < Texture.MaxMipLevels);

            var depthPerMipLevel = Enumerable.Repeat(1, mipLevels).ToList();

            if (is3D)
            {
                var depth = arraySize;
                arraySize = 1;
                for (var i = 0; i < mipLevels; ++i)
                {
                    depthPerMipLevel[i] = depth;
                    depth = Math.Max(depth >> 1, 1);
                }
            }

            using var reader = new BinaryReader(new MemoryStream(data));
            var slices = new SliceArray3D();
            for (var i = 0; i < arraySize; ++i)
            {
                var arraySlice = new List<List<Slice>>();
                for (var j = 0; j < mipLevels; ++j)
                {
                    var mipSlice = new List<Slice>();
                    for (var k = 0; k < depthPerMipLevel[j]; ++k)
                    {
                        var slice = new Slice();
                        slice.Width = reader.ReadInt32();
                        slice.Height = reader.ReadInt32();
                        slice.RowPitch = reader.ReadInt32();
                        slice.SlicePitch = reader.ReadInt32();
                        slice.RawContent = reader.ReadBytes(slice.SlicePitch);

                        mipSlice.Add(slice);
                    }

                    arraySlice.Add(mipSlice);
                }

                slices.Add(arraySlice);
            }

            return slices;
        }

        public static byte[] SlicesToBinary(SliceArray3D slices)
        {
            Debug.Assert(slices?.Any() == true && slices.First()?.Any() == true);
            using var writer = new BinaryWriter(new MemoryStream());
            foreach (var arraySlice in slices)
            {
                foreach (var mipLevel in arraySlice)
                {
                    foreach (var slice in mipLevel)
                    {
                        writer.Write(slice.Width);
                        writer.Write(slice.Height);
                        writer.Write(slice.RowPitch);
                        writer.Write(slice.SlicePitch);
                        writer.Write(slice.RawContent);
                    }
                }
            }

            writer.Flush();
            var data = (writer.BaseStream as MemoryStream)?.ToArray();
            Debug.Assert(data?.Length > 0);

            return data;
        }

        [DllImport(_toolsDLL)]
        private static extern void Decompress([In, Out] TextureData data);

        public static SliceArray3D Decompress(Texture texture)
        {
            Debug.Assert(texture.ImportSettings.Compress);
            using var textureData = new TextureData();

            try
            {
                GetTextureDataInfo(texture, textureData);
                textureData.ImportSettings.FromContentSettings(texture);
                SetSubresourceData(texture.Slices, textureData);

                Decompress(textureData);

                if (textureData.Info.ImportError != 0)
                {
                    Logger.Log(MessageType.Error, $"Error: {EnumExtensions.GetDescription((TextureImportError)textureData.Info.ImportError)}");
                    throw new Exception($"Error while trying to decompress mipmaps. Error code {textureData.Info.ImportError}");
                }

                return GetSlices(textureData);
            }
            catch (Exception ex)
            {
                Logger.Log(MessageType.Error, $"ミップマップの解凍に失敗しました:{texture.FileName}");
                Debug.WriteLine(ex.Message);
                return new();
            }
        }

        [DllImport(_toolsDLL)]
        private static extern void Import([In, Out] TextureData data);

        [DllImport(_toolsDLL)]
        private static extern void PrefilterDiffuseIBL([In, Out] TextureData data);

        [DllImport(_toolsDLL)]
        private static extern void PrefilterSpecularIBL([In, Out] TextureData data);

        [DllImport(_toolsDLL)]
        private static extern void ComputeBrdfIntegrationLut([In, Out] TextureData data);

        /// <summary>
        /// 鏡面反射のIBL用のBRDF LUT(256x256、ミップ1つ、R16G16_FLOAT、圧縮なし)を作り、テクスチャに設定する(Primal Day 075)。
        /// ソース画像はないので、再インポートはできない。
        /// </summary>
        /// <param name="file">保存先のファイル(ログ用)</param>
        public static bool ComputeBrdfIntegrationLut(Texture texture, string file)
        {
            using var textureData = new TextureData();

            try
            {
                texture.ImportSettings.Compress = false;
                texture.ImportSettings.MipLevels = 1;
                textureData.ImportSettings.FromContentSettings(texture);
                ComputeBrdfIntegrationLut(textureData);
                ThrowIfImportError(textureData, "BRDF LUTの作成");

                GetTextureInfo(texture, textureData);
                return texture.SetData(GetSlices(textureData), null, null, file);
            }
            catch (Exception ex)
            {
                Logger.Log(MessageType.Error, $"BRDF LUTの作成に失敗しました: {file}");
                Debug.WriteLine(ex.Message);
                return false;
            }
        }

        private static void ThrowIfImportError(TextureData data, string step)
        {
            if (data.Info.ImportError != 0)
            {
                Logger.Log(MessageType.Error, $"テクスチャの{step}エラー: {EnumExtensions.GetDescription((TextureImportError)data.Info.ImportError)}");
                throw new Exception($"Error while trying to {step}. Error code {data.Info.ImportError}");
            }
        }

        /// <summary>
        /// テクスチャをインポートする。インポート設定で事前フィルタするキューブマップの場合は、
        /// 拡散光のIBLのキューブマップ(irradiance map)も作り、diffuseIBLとして返す(Primal Day 073)。
        /// 元のキューブマップは鏡面反射のIBL用に、ラフネスごとのミップ(1面256x256、ミップ6つ)へ事前フィルタする(Primal Day 074)。
        /// </summary>
        public static (SliceArray3D? slices, Slice? icon, Texture? diffuseIBL) Import(Texture texture)
        {
            Debug.Assert(texture.ImportSettings.Sources.Any());
            using var textureData = new TextureData();

            try
            {
                textureData.ImportSettings.FromContentSettings(texture);
                Import(textureData);
                ThrowIfImportError(textureData, "インポート");

                Texture? diffuseIBL = null;
                if (texture.ImportSettings.PrefilterCubeMap &&
                    ((TextureFlags)textureData.Info.Flags).HasFlag(TextureFlags.IsCubeMap))
                {
                    // NOTE: Import()は事前フィルタするキューブマップを圧縮しない。拡散光のIBLはそのコピーから作り、
                    //       元のキューブマップはPrefilterSpecularIBL()で鏡面反射のIBL用にフィルタしてから圧縮する。
                    using var diffuseData = textureData.Clone();
                    var diffuseTask = Task.Run(() => PrefilterDiffuseIBL(diffuseData));
                    PrefilterSpecularIBL(textureData);
                    diffuseTask.Wait();
                    ThrowIfImportError(diffuseData, "拡散光のIBLの事前フィルタ");
                    ThrowIfImportError(textureData, "鏡面反射のIBLの事前フィルタ");

                    // 再インポートでは、既存の拡散光のIBLのテクスチャ(GUIDとファイル)を使い回す。
                    diffuseIBL = texture.IBLPair ?? new Texture();
                    IAssetImportSettings.CopyImportSettings(texture.ImportSettings, diffuseIBL.ImportSettings);
                    diffuseIBL.ImportSettings.Sources.Clear();
                    GetTextureInfo(diffuseIBL, diffuseData);
                    if (!diffuseIBL.SetData(GetSlices(diffuseData), GetIcon(diffuseData), texture, texture.FullPath))
                    {
                        throw new Exception("Failed to set the diffuse IBL texture data.");
                    }
                }

                GetTextureInfo(texture, textureData);
                return (GetSlices(textureData), GetIcon(textureData), diffuseIBL);
            }
            catch (Exception ex)
            {
                Logger.Log(MessageType.Error, $"インポートに失敗しました: {texture.FileName}");
                Debug.WriteLine(ex.Message);
                return new();
            }
        }

        #endregion Texture
        #region Geometry

        /// <summary>
        /// 失敗したら(エラーをログに出して) false を返す。async void のコマンドの経路から呼ばれるので、例外では伝えない(#110)。
        /// </summary>
        private static bool GeometryFromSceneData(Content.Geometry geometry, Action<SceneData> sceneDataGenerator, string failureMessage)
        {
            Debug.Assert(geometry != null);
            using var sceneData = new SceneData();
            try
            {
                sceneData.ImportSettings.FromContentSettings(geometry);
                sceneDataGenerator(sceneData);
                if (sceneData.Data == IntPtr.Zero || sceneData.DataSize == 0)
                {
                    throw new Exception(failureMessage);
                }

                var data = new byte[sceneData.DataSize];
                Marshal.Copy(sceneData.Data, data, 0, sceneData.DataSize);
                geometry.FromRawData(data);
                return true;
            }
            catch (Exception ex)
            {
                Logger.Log(MessageType.Error, failureMessage);
                Debug.WriteLine(ex.Message);
                return false;
            }
        }

        [DllImport(_toolsDLL)]
        private static extern void CreatePrimitiveMesh([In, Out] SceneData data, PrimitiveInitInfo info);
        public static bool CreatePrimitiveMesh(Geometry geometry, PrimitiveInitInfo info)
        {
            return GeometryFromSceneData(geometry, (sceneData) => CreatePrimitiveMesh(sceneData, info), $"{info.Type}プリミティブ・メッシュの作成に失敗しました。");
        }

        [DllImport(_toolsDLL)]
        private static extern void ImportFbx(string file, [In, Out] SceneData data, ProgressCallback? callback);

        public static bool ImportFbx(string file, Geometry geometry)
        {
            var item = ImportingItemCollection.GetItem(geometry);
            ProgressCallback? callback = item != null ? item.SetProgress : null;
            return GeometryFromSceneData(geometry, (sceneData) => ImportFbx(file, sceneData, callback), $"FBXファイルのインポートに失敗しました： {file}");
        }
        #endregion Geometry
    }
}
