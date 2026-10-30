global using SliceArray3D = System.Collections.Generic.List<System.Collections.Generic.List<System.Collections.Generic.List<DXForgeEditor.Content.Slice>>>;
using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace DXForgeEditor.Content
{
    enum DXGI_FORMAT : int
    {
        DXGI_FORMAT_UNKNOWN = 0,
        DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
        DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
        DXGI_FORMAT_R32G32B32A32_UINT = 3,
        DXGI_FORMAT_R32G32B32A32_SINT = 4,
        DXGI_FORMAT_R32G32B32_TYPELESS = 5,
        DXGI_FORMAT_R32G32B32_FLOAT = 6,
        DXGI_FORMAT_R32G32B32_UINT = 7,
        DXGI_FORMAT_R32G32B32_SINT = 8,
        DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
        DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
        DXGI_FORMAT_R16G16B16A16_UNORM = 11,
        DXGI_FORMAT_R16G16B16A16_UINT = 12,
        DXGI_FORMAT_R16G16B16A16_SNORM = 13,
        DXGI_FORMAT_R16G16B16A16_SINT = 14,
        DXGI_FORMAT_R32G32_TYPELESS = 15,
        DXGI_FORMAT_R32G32_FLOAT = 16,
        DXGI_FORMAT_R32G32_UINT = 17,
        DXGI_FORMAT_R32G32_SINT = 18,
        DXGI_FORMAT_R32G8X24_TYPELESS = 19,
        DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
        DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
        DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
        DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
        DXGI_FORMAT_R10G10B10A2_UNORM = 24,
        DXGI_FORMAT_R10G10B10A2_UINT = 25,
        DXGI_FORMAT_R11G11B10_FLOAT = 26,
        DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
        DXGI_FORMAT_R8G8B8A8_UNORM = 28,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
        DXGI_FORMAT_R8G8B8A8_UINT = 30,
        DXGI_FORMAT_R8G8B8A8_SNORM = 31,
        DXGI_FORMAT_R8G8B8A8_SINT = 32,
        DXGI_FORMAT_R16G16_TYPELESS = 33,
        DXGI_FORMAT_R16G16_FLOAT = 34,
        DXGI_FORMAT_R16G16_UNORM = 35,
        DXGI_FORMAT_R16G16_UINT = 36,
        DXGI_FORMAT_R16G16_SNORM = 37,
        DXGI_FORMAT_R16G16_SINT = 38,
        DXGI_FORMAT_R32_TYPELESS = 39,
        DXGI_FORMAT_D32_FLOAT = 40,
        DXGI_FORMAT_R32_FLOAT = 41,
        DXGI_FORMAT_R32_UINT = 42,
        DXGI_FORMAT_R32_SINT = 43,
        DXGI_FORMAT_R24G8_TYPELESS = 44,
        DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
        DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
        DXGI_FORMAT_R8G8_TYPELESS = 48,
        DXGI_FORMAT_R8G8_UNORM = 49,
        DXGI_FORMAT_R8G8_UINT = 50,
        DXGI_FORMAT_R8G8_SNORM = 51,
        DXGI_FORMAT_R8G8_SINT = 52,
        DXGI_FORMAT_R16_TYPELESS = 53,
        DXGI_FORMAT_R16_FLOAT = 54,
        DXGI_FORMAT_D16_UNORM = 55,
        DXGI_FORMAT_R16_UNORM = 56,
        DXGI_FORMAT_R16_UINT = 57,
        DXGI_FORMAT_R16_SNORM = 58,
        DXGI_FORMAT_R16_SINT = 59,
        DXGI_FORMAT_R8_TYPELESS = 60,
        DXGI_FORMAT_R8_UNORM = 61,
        DXGI_FORMAT_R8_UINT = 62,
        DXGI_FORMAT_R8_SNORM = 63,
        DXGI_FORMAT_R8_SINT = 64,
        DXGI_FORMAT_A8_UNORM = 65,
        DXGI_FORMAT_R1_UNORM = 66,
        DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
        DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
        DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
        DXGI_FORMAT_BC1_TYPELESS = 70,
        DXGI_FORMAT_BC1_UNORM = 71,
        [Description("BC1 (sRGBA) Low Quality Alpha")]
        DXGI_FORMAT_BC1_UNORM_SRGB = 72,
        DXGI_FORMAT_BC2_TYPELESS = 73,
        DXGI_FORMAT_BC2_UNORM = 74,
        DXGI_FORMAT_BC2_UNORM_SRGB = 75,
        DXGI_FORMAT_BC3_TYPELESS = 76,
        DXGI_FORMAT_BC3_UNORM = 77,
        [Description("BC3 (sRGBA) Medium Quality")]
        DXGI_FORMAT_BC3_UNORM_SRGB = 78,
        DXGI_FORMAT_BC4_TYPELESS = 79,
        DXGI_FORMAT_BC4_UNORM = 80,
        DXGI_FORMAT_BC4_SNORM = 81,
        DXGI_FORMAT_BC5_TYPELESS = 82,
        DXGI_FORMAT_BC5_UNORM = 83,
        DXGI_FORMAT_BC5_SNORM = 84,
        DXGI_FORMAT_B5G6R5_UNORM = 85,
        DXGI_FORMAT_B5G5R5A1_UNORM = 86,
        DXGI_FORMAT_B8G8R8A8_UNORM = 87,
        DXGI_FORMAT_B8G8R8X8_UNORM = 88,
        DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
        DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
        DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
        DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
        DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
        DXGI_FORMAT_BC6H_TYPELESS = 94,
        DXGI_FORMAT_BC6H_UF16 = 95,
        DXGI_FORMAT_BC6H_SF16 = 96,
        DXGI_FORMAT_BC7_TYPELESS = 97,
        DXGI_FORMAT_BC7_UNORM = 98,
        [Description("BC7 (sRGBA) High Quality")]
        DXGI_FORMAT_BC7_UNORM_SRGB = 99,
        DXGI_FORMAT_AYUV = 100,
        DXGI_FORMAT_Y410 = 101,
        DXGI_FORMAT_Y416 = 102,
        DXGI_FORMAT_NV12 = 103,
        DXGI_FORMAT_P010 = 104,
        DXGI_FORMAT_P016 = 105,
        DXGI_FORMAT_420_OPAQUE = 106,
        DXGI_FORMAT_YUY2 = 107,
        DXGI_FORMAT_Y210 = 108,
        DXGI_FORMAT_Y216 = 109,
        DXGI_FORMAT_NV11 = 110,
        DXGI_FORMAT_AI44 = 111,
        DXGI_FORMAT_IA44 = 112,
        DXGI_FORMAT_P8 = 113,
        DXGI_FORMAT_A8P8 = 114,
        DXGI_FORMAT_B4G4R4A4_UNORM = 115,

        DXGI_FORMAT_P208 = 130,
        DXGI_FORMAT_V208 = 131,
        DXGI_FORMAT_V408 = 132,


        DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE = 189,
        DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE = 190,


        DXGI_FORMAT_FORCE_UINT = -1
    }

    enum BC_FORMAT : int
    {
        [Description("Pick Best Fit")]
        DXGI_FORMAT_UNKNOWN = 0,
        [Description("BC1 (RGBA) Low Quality Alpha")]
        DXGI_FORMAT_BC1_UNORM = 71,
        [Description("BC3 (RGBA) Medium Quality")]
        DXGI_FORMAT_BC3_UNORM = 77,
        [Description("BC4 (R8) Single-Channel Gray")]
        DXGI_FORMAT_BC4_UNORM = 80,
        [Description("BC5 (R8G8) Dual-Channel Gray")]
        DXGI_FORMAT_BC5_UNORM = 83,
        [Description("BC6 (UF16) HDR")]
        DXGI_FORMAT_BC6H_UF16 = 95,
        [Description("BC7 (RGBA) High Quality")]
        DXGI_FORMAT_BC7_UNORM = 98,
    }

    enum TextureDimension : int
    {
        [Description("1D Texture")]
        Texture1D,
        [Description("2D Texture")]
        Texture2D,
        [Description("3D Texture")]
        Texture3D,
        [Description("Texture Cube")]
        TextureCube,
    }

    // NOTE: ContentToEngine.hのdxforge::content::texture_flags::flags列挙と同じでなければならない。
    [Flags]
    enum TextureFlags : int
    {
        IsHdr = 0x01,
        HasAlpha = 0x02,
        IsPremultipliedAlpha = 0x04,
        IsImportedAsNormalMap = 0x08,
        IsCubeMap = 0x10,
        IsVolumeMap = 0x20,
        IsSRGB = 0x40,
    }

    class TextureImportSettings : ViewModelBase, IAssetImportSettings
    {
        public ObservableCollection<string> Sources { get; } = new();

        private TextureDimension _dimension = TextureDimension.Texture2D;
        public TextureDimension Dimension
        {
            get => _dimension;
            set
            {
                if (_dimension != value)
                {
                    _dimension = value;
                    OnPropertyChanged(nameof(Dimension));
                }
            }
        }

        private int _mipLevels;
        public int MipLevels
        {
            get => _mipLevels;
            set
            {
                value = Math.Clamp(value, 0, Texture.MaxMipLevels);
                if (_mipLevels != value)
                {
                    _mipLevels = value;
                    OnPropertyChanged(nameof(MipLevels));
                }
            }
        }

        private float _alphaThreshold;
        public float AlphaThreshold
        {
            get => _alphaThreshold;
            set
            {
                value = Math.Clamp(value, 0.0f, 1.0f);
                if (!_alphaThreshold.IsTheSameAs(value))
                {
                    _alphaThreshold = value;
                    OnPropertyChanged(nameof(AlphaThreshold));
                }
            }
        }

        private bool _preferBC7;
        public bool PreferBC7
        {
            get => _preferBC7;
            set
            {
                if (_preferBC7 != value)
                {
                    _preferBC7 = value;
                    OnPropertyChanged(nameof(PreferBC7));
                }
            }
        }

        private int _formatIndex;
        public int FormatIndex
        {
            get => _formatIndex;
            set
            {
                value = Math.Clamp(value, 0, Enum.GetValues<BC_FORMAT>().Length);
                if (_formatIndex != value)
                {
                    _formatIndex = value;
                    OnPropertyChanged(nameof(FormatIndex));
                    OnPropertyChanged(nameof(OutputFormat));
                }
            }
        }

        public DXGI_FORMAT OutputFormat => Compress ? (DXGI_FORMAT)Enum.GetValues<BC_FORMAT>()[FormatIndex] : DXGI_FORMAT.DXGI_FORMAT_UNKNOWN;

        private bool _compress;
        public bool Compress
        {
            get => _compress;
            set
            {
                if (_compress != value)
                {
                    _compress = value;
                    OnPropertyChanged(nameof(Compress));
                }
            }
        }

        private int _cubeMapSize;
        public int CubeMapSize
        {
            get => _cubeMapSize;
            set
            {
                if (_cubeMapSize != value)
                {
                    _cubeMapSize = value;
                    OnPropertyChanged(nameof(CubeMapSize));
                }
            }
        }

        private bool _mirrorCubeMap;
        public bool MirrorCubeMap
        {
            get => _mirrorCubeMap;
            set
            {
                if (_mirrorCubeMap != value)
                {
                    _mirrorCubeMap = value;
                    OnPropertyChanged(nameof(MirrorCubeMap));
                }
            }
        }

        private bool _prefilterCubeMap;
        public bool PrefilterCubeMap
        {
            get => _prefilterCubeMap;
            set
            {
                if (_prefilterCubeMap != value)
                {
                    _prefilterCubeMap = value;
                    OnPropertyChanged(nameof(PrefilterCubeMap));
                }
            }
        }

        public void ToBinary(BinaryWriter writer)
        {
            writer.Write(string.Join(";", Sources.ToArray()));
            writer.Write((int)Dimension);
            writer.Write(MipLevels);
            writer.Write(AlphaThreshold);
            writer.Write(PreferBC7);
            writer.Write(FormatIndex);
            writer.Write(Compress);
            writer.Write(CubeMapSize);
            writer.Write(MirrorCubeMap);
            writer.Write(PrefilterCubeMap);
        }

        public void FromBinary(BinaryReader reader) => FromBinary(reader, true);

        /// <param name="hasCubeMapSettings">
        /// falseなら、キューブマップの3項目がない古い形式(#38より前)として読み、3項目には既定値を入れる。
        /// </param>
        public void FromBinary(BinaryReader reader, bool hasCubeMapSettings)
        {
            Sources.Clear();
            reader.ReadString().Split(";").Where(x => !string.IsNullOrEmpty(x)).ToList().ForEach(Sources.Add);
            Dimension = (TextureDimension)reader.ReadInt32();
            MipLevels = reader.ReadInt32();
            AlphaThreshold = reader.ReadSingle();
            PreferBC7 = reader.ReadBoolean();
            FormatIndex = reader.ReadInt32();
            Compress = reader.ReadBoolean();
            if (hasCubeMapSettings)
            {
                CubeMapSize = reader.ReadInt32();
                MirrorCubeMap = reader.ReadBoolean();
                PrefilterCubeMap = reader.ReadBoolean();
            }
            else
            {
                SetDefaultCubeMapSettings();
            }
        }

        private void SetDefaultCubeMapSettings()
        {
            CubeMapSize = 256;
            MirrorCubeMap = true;
            PrefilterCubeMap = true;
        }

        public TextureImportSettings()
        {
            MipLevels = 0;
            AlphaThreshold = 0.5f;
            PreferBC7 = true;
            FormatIndex = 0;
            Compress = true;
            SetDefaultCubeMapSettings();
        }
    }

    class Slice
    {
        public int Width { get; set; }
        public int Height { get; set; }
        public int RowPitch { get; set; }
        public int SlicePitch { get; set; }
        public byte[] RawContent { get; set; } = Array.Empty<byte>();
    }

    class TextureMetadata : AssetMetadata
    {
        public int Width { get; init; }
        public int Height { get; init; }
        public int DepthOrArraySize { get; init; }
        public int MipLevels { get; init; }
        public DXGI_FORMAT Format { get; init; }
        public TextureDimension Dimension { get; init; }
    }

    class Texture : Asset
    {
        public static int MaxMipLevels => 14;

        public static int MaxArraySize => 2048;
        public static int Max3DSize => 2048;

        // .assetの形式のバージョン(#63)。
        // 0: #38より前。バージョン番号もキューブマップの設定もない。
        // 1: #38以降。インポート設定の末尾にキューブマップの3項目が増えたが、バージョン番号はない。
        // 2: アセットのヘッダの直後に FileFormatTag とバージョン番号を書く。
        // 3: Format の直後に、対になるIBLのテクスチャ(IBLPair)のGUIDを文字列で書く(#35)。ないときは Guid.Empty。
        // 0と1にはタグがないので、ReadFileFormatVersion() でバイト配置から判別する。
        private const int FileFormatVersion = 3;
        private const int FirstVersionWithCubeMapSettings = 1;
        private const int FirstVersionWithFileFormatTag = 2;
        private const int FirstVersionWithIBLPair = 3;
        // 拡散光のIBLのテクスチャのファイル名に付ける接尾辞(Primal Day 073と同じ)。
        private const string DiffuseIBLFileSuffix = "_diffuse_ibl";
        // バイト列は "TEX\0"。0と1のファイルでは、この位置からインポート設定の Sources(BinaryWriterの
        // 文字列: 7ビット符号化の長さ + UTF-8)が始まる。これをタグとして読むと、長さ84の文字列の
        // 3バイト目がNULになるが、ファイルパスはNULを含まないので、0と1のファイルがタグと一致することはない。
        private const int FileFormatTag = 0x00584554;

        public TextureImportSettings ImportSettings { get; } = new();

        // array ( mip ( サブリソース (スライス) ) )
        // https://learn.microsoft.com/en-us/windows/win32/direct3d12/subresources を参照
        // 配列内のスライスの順番
        private SliceArray3D _slices = new();
        public SliceArray3D Slices
        {
            get => _slices;
            private set
            {
                if (_slices != value)
                {
                    _slices = value;
                    OnPropertyChanged(nameof(Slices));
                }
            }
        }

        private int _width;
        public int Width
        {
            get => _width;
            set
            {
                if (_width != value)
                {
                    _width = value;
                    OnPropertyChanged(nameof(Width));
                }
            }
        }

        private int _height;
        public int Height
        {
            get => _height;
            set
            {
                if (_height != value)
                {
                    _height = value;
                    OnPropertyChanged(nameof(Height));
                }
            }
        }

        private int _arraySize;
        public int ArraySize
        {
            get => _arraySize;
            set
            {
                if (_arraySize != value)
                {
                    // もしこれが立方体マップなら、配列のサイズは6の倍数でなければならない。
                    Debug.Assert(!(IsCubeMap && (value % 6) != 0));
                    _arraySize = value;
                    OnPropertyChanged(nameof(ArraySize));
                }
            }
        }

        private TextureFlags _flags;
        public TextureFlags Flags
        {
            get => _flags;
            set
            {
                if (_flags != value)
                {
                    _flags = value;
                    OnPropertyChanged(nameof(IsHDR));
                    OnPropertyChanged(nameof(HasAlpha));
                    OnPropertyChanged(nameof(IsPremultipliedAlpha));
                    OnPropertyChanged(nameof(IsNormalMap));
                    OnPropertyChanged(nameof(IsCubeMap));
                    OnPropertyChanged(nameof(IsVolumeMap));
                    OnPropertyChanged(nameof(IsSRGB));
                }
            }
        }

        public bool IsHDR => Flags.HasFlag(TextureFlags.IsHdr);
        public bool HasAlpha => Flags.HasFlag(TextureFlags.HasAlpha);
        public bool IsPremultipliedAlpha => Flags.HasFlag(TextureFlags.IsPremultipliedAlpha);
        public bool IsNormalMap => Flags.HasFlag(TextureFlags.IsImportedAsNormalMap);
        public bool IsCubeMap => Flags.HasFlag(TextureFlags.IsCubeMap);
        public bool IsVolumeMap => Flags.HasFlag(TextureFlags.IsVolumeMap);
        public bool IsSRGB => Flags.HasFlag(TextureFlags.IsSRGB);

        private int _mipLevels;
        public int MipLevels
        {
            get => _mipLevels;
            set
            {
                // NOTE: ロードされたテクスチャの実際のミップ数を表示したいので、ここでは値はクランプされません。
                if (_mipLevels != value)
                {
                    _mipLevels = value;
                    OnPropertyChanged(nameof(MipLevels));
                }
            }
        }

        private DXGI_FORMAT _format;
        public DXGI_FORMAT Format
        {
            get => _format;
            set
            {
                if (_format != value)
                {
                    _format = value;
                    OnPropertyChanged(nameof(Format));
                    OnPropertyChanged(nameof(FormatName));
                }
            }
        }

        public string FormatName => ImportSettings.Compress && !IsSRGB ? ((BC_FORMAT)Format).GetDescription() : Format.GetDescription();

        // 事前フィルタしたキューブマップの組(Primal Day 073)。元のキューブマップ(ソース画像あり)と、
        // それから作った拡散光のIBLのキューブマップ(irradiance map, ソース画像なし)が、互いを IBLPair として持つ。
        // 元のキューブマップは、鏡面反射のIBL用にラフネスごとのミップへ事前フィルタしたもの(Primal Day 074, #36)。
        private Texture? _iblPair;
        public Texture? IBLPair
        {
            get => _iblPair;
            private set
            {
                if (_iblPair != value)
                {
                    _iblPair = value;
                    OnPropertyChanged(nameof(IBLPair));
                    OnPropertyChanged(nameof(IsPrefilteredIBL));
                    OnPropertyChanged(nameof(IsDiffuseIBL));
                }
            }
        }

        public bool IsPrefilteredIBL => IBLPair != null;
        // 拡散光のIBLのテクスチャはソース画像を持たない(再インポートできない)。
        public bool IsDiffuseIBL => IsPrefilteredIBL && !ImportSettings.Sources.Any();
        // ソース画像を持つか。拡散光のIBLや、ContentToolsで作った既定アセット(BRDF LUTなど)は持たない(再インポートできない)。
        public bool HasSourceImages => ImportSettings.Sources.Any();

        private bool _isSaving;
        // 再インポートで事前フィルタしなくなったときに、保存時に削除する古い拡散光のIBLのファイル。
        private string? _obsoleteIBLPairFile;

        private static bool HasValidDimensions(int width, int height, int arrayOrDepth, bool is3D, string file)
        {
            bool result = true;

            if (width > (1 << MaxMipLevels) || height > (1 << MaxMipLevels))
            {
                Logger.Log(MessageType.Error, $"{1 << MaxMipLevels}を超える画像寸法 ! ({file})");
                result = false;
            }

            if (width % 4 != 0 || height % 4 != 0)
            {
                Logger.Log(MessageType.Error, $"画像寸法が4の倍数でない！ ({file})");
                result = false;
            }

            if (is3D && (width > Max3DSize || height > Max3DSize) || arrayOrDepth > Max3DSize)
            {
                Logger.Log(MessageType.Error, $"{Max3DSize}を超える3Dテクスチャ寸法! ({file})");
                result = false;
            }
            else if (arrayOrDepth > MaxArraySize)
            {
                Logger.Log(MessageType.Error, $"2Dテクスチャ配列のサイズが{MaxArraySize}より大きい！({file})");
                result = false;
            }

            if (width != height)
            {
                Logger.Log(MessageType.Warning, $"正方形でない画像（幅と高さが等しくない）！ ({file})");
            }

            if (!MathUtil.IsPow2(width) || !MathUtil.IsPow2(height))
            {
                Logger.Log(MessageType.Warning, $"画像寸法が2のべき乗でない！ ({file})");
            }

            return result;
        }

        /// <summary>
        /// インポートの結果を設定する。
        /// </summary>
        /// <param name="iblPair">対になるIBLのテクスチャ。事前フィルタしていなければ null。</param>
        public bool SetData(SliceArray3D slices, Slice? icon, Texture? iblPair, string file)
        {
            Debug.Assert(slices.Any() && slices.First().Any() && slices.First().First().Any());

            if (slices.Any() && slices.First().Any() && slices.First().First().Any())
            {
                Slices = slices;
            }
            else
            {
                return false;
            }

            var firstMip = Slices[0][0][0];
            if (!HasValidDimensions(firstMip.Width, firstMip.Height, ArraySize, IsVolumeMap, file))
            {
                return false;
            }

            if (icon == null)
            {
                Debug.Assert(!ImportSettings.Compress);
                icon = firstMip;
            }

            // 圧縮したテクスチャのアイコンは解凍した画像なので、フォーマットはこのテクスチャのものではない。
            var iconFormat = ImportSettings.Compress ? DXGI_FORMAT.DXGI_FORMAT_UNKNOWN : Format;
            var iconImage = BitmapHelper.ImageFromSlice(icon, iconFormat, IsNormalMap) ?? throw new InvalidDataException("アイコン画像を生成できませんでした。");
            Icon = BitmapHelper.CreateThumbnail(iconImage, ContentInfo.IconWidth, ContentInfo.IconWidth);

            if (iblPair == null && IBLPair != null && !string.IsNullOrEmpty(IBLPair.FullPath))
            {
                _obsoleteIBLPairFile = IBLPair.FullPath;
            }
            IBLPair = iblPair;

            return true;
        }

        public override bool Import(string file)
        {
            Debug.Assert(File.Exists(file));

            try
            {
                Logger.Log(MessageType.Info, $"画像ファイルのインポート: {file}");
                // NOTE: Sources はインポート設定の画面(TextureImportSettingsConfigurator.Import)で設定済みで、
                //       再インポートでは .asset から読んだものを使う。ここで file を足すと同じ画像が重複し、
                //       再インポートでは .asset のパスが画像として渡ってしまう(#82, Primal Day 067 と同じ)。
                Debug.Assert(ImportSettings.Sources.Any());

                (var slices, var icon, var diffuseIBL) = ContentToolsAPI.Import(this);
                if (slices == null) throw new InvalidDataException("ContentToolsからテクスチャデータを取得できませんでした。");

                return SetData(slices, icon, diffuseIBL, file);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                var msg = $"インポート用のファイルの読み込みに失敗しました: {file}";
                Debug.WriteLine(msg);
                Logger.Log(MessageType.Error, msg);
            }

            return false;
        }

        public override bool Load(string file)
        {
            Debug.Assert(File.Exists(file));
            Debug.Assert(Path.GetExtension(file).ToLower() == AssetFileExtension);

            try
            {
                using var reader = new BinaryReader(File.Open(file, FileMode.Open, FileAccess.Read));

                ReadAssetFileHeader(reader);
                var version = ReadFileFormatVersion(reader);
                ImportSettings.FromBinary(reader, version >= FirstVersionWithCubeMapSettings);

                Width = reader.ReadInt32();
                Height = reader.ReadInt32();
                ArraySize = reader.ReadInt32();
                Flags = (TextureFlags)reader.ReadInt32();
                MipLevels = reader.ReadInt32();
                Format = (DXGI_FORMAT)reader.ReadInt32();
                // バージョン3より前の形式には IBLPair がない(事前フィルタしていないものとして読む)。
                var iblPairGuid = version >= FirstVersionWithIBLPair ? new Guid(reader.ReadString()) : Guid.Empty;
                var compressedLength = reader.ReadInt32();
                Debug.Assert(compressedLength > 0);
                var compressed = reader.ReadBytes(compressedLength);
                DecompressContent(compressed);

                HasValidDimensions(Width, Height, ArraySize, IsVolumeMap, file);
                FullPath = file;

                if (iblPairGuid != Guid.Empty && IBLPair == null)
                {
                    LoadIBLPair(iblPairGuid, file);
                }

                // For Testing. Remove later.
                WriteForEngineTest("texture.img", PackForEngine);
                // For Testing. Remove later.

                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"テクスチャアセットをロードできませんでした: {file} ");
            }

            return false;
        }

        /// <summary>
        /// 対になるIBLのテクスチャを読み込む。読み込めなくても、このテクスチャ自体の読み込みは失敗にしない。
        /// </summary>
        private void LoadIBLPair(Guid iblPairGuid, string file)
        {
            var pairFile = AssetRegistry.GetAssetInfo(iblPairGuid)?.FullPath;
            if (string.IsNullOrEmpty(pairFile))
            {
                // アセットがまだ登録されていない場合に備えて、Save() と同じ規則のファイル名(番号付きを含む)も探す。
                pairFile = GetConventionalIBLPairFiles(file).FirstOrDefault(x => TryGetAssetInfo(x)?.Guid == iblPairGuid);
            }

            if (string.IsNullOrEmpty(pairFile) || !File.Exists(pairFile))
            {
                Logger.Log(MessageType.Warning, $"対になるIBLのテクスチャが見つかりません: {file}");
                return;
            }

            var pair = new Texture() { IBLPair = this };
            if (pair.Load(pairFile) && pair.Guid == iblPairGuid)
            {
                IBLPair = pair;
            }
            else
            {
                Logger.Log(MessageType.Warning, $"対になるIBLのテクスチャを読み込めませんでした: {pairFile}");
            }
        }

        /// <summary>
        /// Save() の規則でペアに付く名前の候補。元のキューブマップからは &lt;名前&gt;_diffuse_ibl[番号].asset、
        /// 拡散光のIBLからは名前の末尾の _diffuse_ibl[番号] を除いた名前。
        /// </summary>
        private IEnumerable<string> GetConventionalIBLPairFiles(string file)
        {
            var directory = Path.GetDirectoryName(file) ?? string.Empty;
            var name = Path.GetFileNameWithoutExtension(file);
            if (ImportSettings.Sources.Any())
            {
                var pairName = $"{name}{DiffuseIBLFileSuffix}";
                yield return Path.Combine(directory, $"{pairName}{AssetFileExtension}");
                for (var i = 1; File.Exists(Path.Combine(directory, $"{pairName}{i}{AssetFileExtension}")); ++i)
                {
                    yield return Path.Combine(directory, $"{pairName}{i}{AssetFileExtension}");
                }
            }
            else
            {
                var index = name.LastIndexOf(DiffuseIBLFileSuffix, StringComparison.Ordinal);
                if (index >= 0 && name[(index + DiffuseIBLFileSuffix.Length)..].All(char.IsAsciiDigit))
                {
                    yield return Path.Combine(directory, $"{name[..index]}{AssetFileExtension}");
                }
            }
        }

        private static int ReadFileFormatVersion(BinaryReader reader)
        {
            var stream = reader.BaseStream;
            var start = stream.Position;

            if (stream.Length - start >= sizeof(int) && reader.ReadInt32() == FileFormatTag)
            {
                var version = reader.ReadInt32();
                if (version < FirstVersionWithFileFormatTag || version > FileFormatVersion)
                {
                    throw new InvalidDataException($"未対応のテクスチャアセットの形式です(バージョン{version})。");
                }
                return version;
            }

            // タグのない古い形式(0か1)。どちらもインポート設定のあとに
            // Width, Height, ArraySize, Flags, MipLevels, Format, 圧縮データの長さ(各int32)と圧縮データが
            // ファイルの終わりまで続く。1は0よりインポート設定が6バイト(int32 + bool×2)長いので、
            // それぞれの形式で読んだときに、圧縮データがちょうどファイルの終わりで終わる方を選ぶ。
            for (var version = FirstVersionWithCubeMapSettings; version >= 0; --version)
            {
                stream.Position = start;
                var matches = EndsAtEndOfStream(reader, version);
                stream.Position = start;
                if (matches) return version;
            }

            throw new InvalidDataException("テクスチャアセットの形式を判別できませんでした。");
        }

        private static bool EndsAtEndOfStream(BinaryReader reader, int version)
        {
            try
            {
                new TextureImportSettings().FromBinary(reader, version >= FirstVersionWithCubeMapSettings);
                reader.BaseStream.Position += 6 * sizeof(int);
                var compressedLength = reader.ReadInt32();
                return compressedLength > 0 && reader.BaseStream.Position + compressedLength == reader.BaseStream.Length;
            }
            catch (EndOfStreamException)
            {
                return false;
            }
        }

        /// <summary>
        /// テクスチャを、エンジンで使用できるバイト配列にパックする。
        /// </summary>
        /// <returns>
        /// Returns a byte array that contains
        /// struct
        /// {
        ///     u32 width, height, array_size (or depth), flags, mip_levels, format,
        ///     struct
        ///     {
        ///         u32 row_pitch, slice_pitch,
        ///         u8 image[mip_level][slice_pitch * depth_per_mip],
        ///     } images[]
        /// } texture
        /// </returns>
        ///
        public override byte[] PackForEngine()
        {
            using var writer = new BinaryWriter(new MemoryStream());
            writer.Write(Width);
            writer.Write(Height);
            writer.Write(ArraySize);
            writer.Write((int)Flags);
            writer.Write(MipLevels);
            writer.Write((int)Format);

            Debug.Assert(Slices?.Any() == true);
            foreach (var arraySlice in Slices)
            {
                foreach (var mipLevel in arraySlice)
                {
                    writer.Write(mipLevel[0].RowPitch);
                    writer.Write(mipLevel[0].SlicePitch);
                    foreach (var slice in mipLevel)
                    {
                        writer.Write(slice.RawContent);
                    }
                }
            }

            writer.Flush();
            var data = (writer.BaseStream as MemoryStream)?.ToArray();
            Debug.Assert(data?.Length > 0);

            return data;
        }

        public override IEnumerable<string> Save(string file)
        {
            _isSaving = true;

            try
            {
                if (TryGetAssetInfo(file) is AssetInfo info && info.Type == Type)
                {
                    Guid = info.Guid;
                }

                var savedFiles = new List<string>();

                // 対になる拡散光のIBLのテクスチャも保存する(互いのGUIDを書くので、このテクスチャのGUIDを決めた後で保存する)。
                // ペアを保存できなければ、存在しない・古いペアを参照しないよう、このテクスチャも保存しない。
                string? pairFile = null;
                IBLPairBackup? pairBackup = null;
                if (IBLPair != null && !IBLPair._isSaving && !IsDiffuseIBL)
                {
                    pairFile = string.IsNullOrEmpty(IBLPair.FullPath) ? GetNewIBLPairFile(file) : IBLPair.FullPath;
                    pairBackup = new IBLPairBackup(IBLPair, pairFile);
                    var savedPairFiles = IBLPair.Save(pairFile).ToList();
                    if (!savedPairFiles.Any())
                    {
                        throw new IOException($"対になる拡散光のIBLのテクスチャを保存できませんでした: {pairFile}");
                    }
                    savedFiles.AddRange(savedPairFiles);
                }

                try
                {
                    WriteAssetFile(file);
                }
                catch
                {
                    // このテクスチャを書けなければ、ペアも保存前の内容に戻し、ディスク上の2つの内容をそろえる。
                    pairBackup?.Restore();
                    throw;
                }

                FullPath = file;
                Logger.Log(MessageType.Info, $"テクスチャを保存: {file}");

                // 事前フィルタをやめたときの古いペアのファイルを消す。ペアを付け直して同じファイルへ保存した場合は消さない。
                if (!string.IsNullOrEmpty(_obsoleteIBLPairFile))
                {
                    if (!IsSameFile(_obsoleteIBLPairFile, pairFile) && File.Exists(_obsoleteIBLPairFile))
                    {
                        File.Delete(_obsoleteIBLPairFile);
                        Logger.Log(MessageType.Info, $"不要になった拡散光のIBLのテクスチャを削除: {_obsoleteIBLPairFile}");
                    }
                    _obsoleteIBLPairFile = null;
                }

                savedFiles.Insert(0, file);
                return savedFiles;

            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"テクスチャの保存に失敗しました: {file}");
            }
            finally
            {
                _isSaving = false;
            }

            return new List<string>();
        }

        /// <summary>
        /// アセットのファイルを書く。書き終えるまで元のファイルを壊さないよう、同じフォルダの一時ファイルに書いてから置き換える
        /// (元のファイルが読み取り専用などで置き換えられなければ例外になり、元のファイルはそのまま残る)。
        /// </summary>
        private void WriteAssetFile(string file)
        {
            var compressed = CompressContent();
            Debug.Assert(compressed?.Length > 0);
            Hash = ContentHelper.ComputeHash(compressed);

            var tempFile = $"{file}.tmp";
            try
            {
                using (var writer = new BinaryWriter(File.Open(tempFile, FileMode.Create, FileAccess.Write)))
                {
                    WriteAssetFileHeader(writer);
                    writer.Write(FileFormatTag);
                    writer.Write(FileFormatVersion);
                    ImportSettings.ToBinary(writer);

                    writer.Write(Width);
                    writer.Write(Height);
                    writer.Write(ArraySize);
                    writer.Write((int)Flags);
                    writer.Write(MipLevels);
                    writer.Write((int)Format);
                    writer.Write((IBLPair?.Guid ?? Guid.Empty).ToString());
                    writer.Write(compressed.Length);
                    writer.Write(compressed);
                }

                File.Move(tempFile, file, true);
            }
            finally
            {
                if (File.Exists(tempFile))
                {
                    File.Delete(tempFile);
                }
            }
        }

        /// <summary>
        /// 拡散光のIBLのテクスチャを保存する前のファイルの内容と保存先。元のキューブマップを保存できなかったときに戻すために使う。
        /// </summary>
        private sealed class IBLPairBackup
        {
            private readonly Texture _pair;
            private readonly string _file;
            private readonly byte[]? _content;
            private readonly string _fullPath;
            private readonly Guid _guid;

            public IBLPairBackup(Texture pair, string file)
            {
                _pair = pair;
                _file = file;
                _content = File.Exists(file) ? File.ReadAllBytes(file) : null;
                _fullPath = pair.FullPath;
                _guid = pair.Guid;
            }

            public void Restore()
            {
                try
                {
                    if (_content != null)
                    {
                        File.WriteAllBytes(_file, _content);
                    }
                    else if (File.Exists(_file))
                    {
                        File.Delete(_file);
                    }
                    _pair.FullPath = _fullPath;
                    _pair.Guid = _guid;
                }
                catch (Exception ex)
                {
                    Debug.WriteLine(ex.Message);
                    Logger.Log(MessageType.Error, $"拡散光のIBLのテクスチャを保存前の内容に戻せませんでした: {_file}");
                }
            }
        }

        /// <summary>
        /// まだ保存していない拡散光のIBLのテクスチャの保存先を決める。規則のファイル名(&lt;名前&gt;_diffuse_ibl.asset)に
        /// このテクスチャのペアではない別のアセットがあるときは、上書きしないよう番号を付けた名前にする。
        /// </summary>
        private string GetNewIBLPairFile(string file)
        {
            var directory = Path.GetDirectoryName(file) ?? string.Empty;
            var name = $"{Path.GetFileNameWithoutExtension(file)}{DiffuseIBLFileSuffix}";
            var pairFile = Path.Combine(directory, $"{name}{AssetFileExtension}");
            for (var i = 1; File.Exists(pairFile) && !IsOwnIBLPairFile(pairFile); ++i)
            {
                pairFile = Path.Combine(directory, $"{name}{i}{AssetFileExtension}");
            }

            if (!IsSameFile(pairFile, Path.Combine(directory, $"{name}{AssetFileExtension}")))
            {
                Logger.Log(MessageType.Warning, $"同じ名前の別のアセットがあるため、拡散光のIBLのテクスチャを別の名前で保存します: {pairFile}");
            }
            return pairFile;
        }

        /// <summary>
        /// ファイルが、このテクスチャを IBLPair として参照するテクスチャアセット(前に保存したペア)かどうか。
        /// </summary>
        private bool IsOwnIBLPairFile(string file)
        {
            if (Guid == Guid.Empty || TryGetAssetInfo(file)?.Type != Type) return false;

            try
            {
                using var reader = new BinaryReader(File.Open(file, FileMode.Open, FileAccess.Read));

                new Texture().ReadAssetFileHeader(reader);
                var version = ReadFileFormatVersion(reader);
                if (version < FirstVersionWithIBLPair) return false;

                new TextureImportSettings().FromBinary(reader, version >= FirstVersionWithCubeMapSettings);
                reader.BaseStream.Position += 6 * sizeof(int);
                return new Guid(reader.ReadString()) == Guid;
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }

            return false;
        }

        private static bool IsSameFile(string file, string? other) =>
            !string.IsNullOrEmpty(other) &&
            string.Equals(Path.GetFullPath(file), Path.GetFullPath(other), StringComparison.OrdinalIgnoreCase);

        private byte[] CompressContent()
        {
            Debug.Assert(Slices.First().Any() && Slices.First().Count == MipLevels);
            var data = ContentToolsAPI.SlicesToBinary(Slices);
            Debug.Assert(data?.Length > 0);

            return CompressionHelper.Compress(data);
        }

        private void DecompressContent(byte[] compressed)
        {
            var decompressed = CompressionHelper.Decompress(compressed);
            Slices = ContentToolsAPI.SlicesFromBinary(decompressed, ArraySize, MipLevels, IsVolumeMap);
        }

        public override TextureMetadata GetMetadata()
        {
            return new()
            {
                Width = Width,
                Height = Height,
                DepthOrArraySize = ArraySize,
                Format = Format,
                MipLevels = MipLevels,
                Dimension = ImportSettings.Dimension,
            };
        }

        public Texture() : base(AssetType.Texture) { }

        public Texture(IAssetImportSettings importSettings) : this()
        {
            Debug.Assert(importSettings is TextureImportSettings);
            ImportSettings = (TextureImportSettings)importSettings;
        }

        /// <summary>
        /// アセットのファイルから読む(エンジンへのアップロード用。Primal Day 077)。
        /// </summary>
        public Texture(AssetInfo assetInfo) : this()
        {
            Debug.Assert(assetInfo != null && assetInfo.Guid != Guid.Empty);
            Debug.Assert(File.Exists(assetInfo.FullPath) && assetInfo.Type == Type);
            Load(assetInfo.FullPath);
        }

    }
}
