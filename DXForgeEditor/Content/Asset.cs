using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;

namespace DXForgeEditor.Content
{
    enum AssetType
    {
        Unknown,
        Animation,
        Audio,
        Material,
        Mesh,
        Skeleton,
        Texture
    }

    interface IAssetImportSettings
    {
        void ToBinary(BinaryWriter writer);
        void FromBinary(BinaryReader reader);

        static void CopyImportSettings(IAssetImportSettings fromSettings, IAssetImportSettings toSettings)
        {
            if (fromSettings == null || toSettings == null)
            {
                throw new ArgumentNullException("Arguments should not be null.");
            }
            else if (fromSettings.GetType() != toSettings.GetType())
            {
                throw new ArgumentException("Arguments should be of the same type.");
            }

            using BinaryWriter writer = new(new MemoryStream());
            fromSettings.ToBinary(writer);
            writer.Flush();
            var bytes = ((MemoryStream)writer.BaseStream).ToArray();

            using BinaryReader reader = new(new MemoryStream(bytes));
            toSettings.FromBinary(reader);
        }
    }

    sealed class AssetInfo
    {
        public AssetType Type { get; set; }
        public byte[] Icon { get; set; } = Array.Empty<byte>();
        public string FullPath { get; set; } = string.Empty;
        public string FullName => Path.GetFileNameWithoutExtension(FullPath);
        public DateTime RegisterTime { get; set; }
        public DateTime ImportDate { get; set; }
        public Guid Guid { get; set; }
        public byte[]? Hash { get; set; }
    }

    /// <summary>
    /// エンジンにアップロードしたアセットについて、エディタが覚えておく情報(Primal Day 077)。
    /// </summary>
    abstract class AssetMetadata { }

    abstract class Asset : ViewModelBase
    {
        public static string AssetFileExtension => ".asset";
        public AssetType Type { get; }
        public byte[]? Icon { get; protected set; }

        private string _fullPath = string.Empty;
        public string FullPath
        {
            get => _fullPath;
            set
            {
                if (_fullPath != value)
                {
                    _fullPath = value;
                    OnPropertyChanged(nameof(FullPath));
                    OnPropertyChanged(nameof(FileName));
                }
            }
        }

        public string FileName => Path.GetFileNameWithoutExtension(FullPath);
        public Guid Guid { get; protected set; } = Guid.NewGuid();
        public DateTime ImportDate { get; protected set; }
        public byte[]? Hash { get; protected set; }

        public abstract AssetMetadata GetMetadata();
        public abstract bool Import(string file);
        public abstract bool Load(string file);
        public abstract IEnumerable<string> Save(string file);
        public abstract byte[] PackForEngine();
        /// <summary>
        /// このアセットが使うアセット(マテリアルの入力のテクスチャなど)。エンジンには先にアップロードする。
        /// </summary>
        public virtual List<AssetInfo> GetReferencedAssets() => [];

        public AssetInfo GetAssetInfo()
            => new()
            {
                Type = Type,
                Icon = Icon ?? [],
                FullPath = FullPath,
                RegisterTime = AssetRegistry.GetAssetInfo(Guid)?.RegisterTime ?? default,
                ImportDate = ImportDate,
                Guid = Guid,
                Hash = Hash,
            };

        // For Testing. Remove later!
        /// <summary>
        /// EngineTest で使うための試験用の書き出し(リポジトリの x64 フォルダに fileName の名前で書く)。
        /// 書き出し先は作業フォルダではなく実行ファイルの場所(x64\構成名\)から決める(Issue #91)。
        /// 書き出しに失敗しても、呼び出し元(アセットの読み込み)は失敗にしない。
        /// </summary>
        protected static void WriteForEngineTest(string fileName, Func<byte[]> pack)
        {
            try
            {
                var dir = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, @"..\..\x64"));
                if (!Directory.Exists(dir))
                {
                    // リポジトリの x64\構成名\ 以外に置いた実行ファイルから起動した場合は書き出さない。
                    Debug.WriteLine($"Skipped writing {fileName} for EngineTest: {dir} does not exist.");
                    return;
                }

                File.WriteAllBytes(Path.Combine(dir, fileName), pack());
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Warning, $"試験用のファイルを書き出せませんでした: {fileName}");
            }
        }
        // For Testing. Remove later!

        private static AssetInfo GetAssetInfo(BinaryReader reader)
        {
            reader.BaseStream.Position = 0;
            var info = new AssetInfo();

            info.Type = (AssetType)reader.ReadInt32();
            var idSize = reader.ReadInt32();
            info.Guid = new Guid(reader.ReadBytes(idSize));
            info.ImportDate = DateTime.FromBinary(reader.ReadInt64());
            var hashSize = reader.ReadInt32();
            if (hashSize > 0)
            {
                info.Hash = reader.ReadBytes(hashSize);
            }
            var iconSize = reader.ReadInt32();
            info.Icon = reader.ReadBytes(iconSize);

            return info;
        }

        public static AssetInfo? TryGetAssetInfo(string file) =>
            File.Exists(file) && Path.GetExtension(file) == AssetFileExtension ? AssetRegistry.GetAssetInfo(file) ?? GetAssetInfo(file) : null;

        public static AssetInfo? GetAssetInfo(string file)
        {
            Debug.Assert(File.Exists(file) && Path.GetExtension(file) == AssetFileExtension);
            try
            {
                using var reader = new BinaryReader(File.Open(file, FileMode.Open, FileAccess.Read));
                var info = GetAssetInfo(reader);
                info.FullPath = file;
                return info;
            }
            catch (Exception ex) { Debug.WriteLine(ex.Message); }

            return null;
        }

        protected void WriteAssetFileHeader(BinaryWriter writer)
        {
            var id = Guid.ToByteArray();
            var importDate = DateTime.Now.ToBinary();

            writer.BaseStream.Position = 0;

            writer.Write((int)Type);
            writer.Write(id.Length);
            writer.Write(id);
            writer.Write(importDate);
            // asset hash is optional
            if (Hash?.Length > 0)
            {
                writer.Write(Hash.Length);
                writer.Write(Hash);
            }
            else
            {
                writer.Write(0);
            }

            // アイコンは派生クラスがインポート/読み込み時に必ず設定する
            Debug.Assert(Icon != null);
            writer.Write(Icon.Length);
            writer.Write(Icon);
        }

        protected void ReadAssetFileHeader(BinaryReader reader)
        {
            var info = GetAssetInfo(reader);

            Debug.Assert(Type == info.Type);
            Guid = info.Guid;
            ImportDate = info.ImportDate;
            Hash = info.Hash;
            Icon = info.Icon;
        }

        public Asset(AssetType type)
        {
            Debug.Assert(type != AssetType.Unknown);
            Type = type;
        }
    }

    /// <summary>
    /// エンジンにアップロードしたアセット(Primal Day 077)。同じアセット(GUID)は1回だけアップロードし、参照数を数える。
    /// アセットが使うアセット(GetReferencedAssets())も一緒にアップロードし、参照数を増やす。
    /// NOTE: エンジンの初期化(Primal Day 078, #31)がまだないので、エディタからは呼ばれない。
    /// </summary>
    class UploadedAsset
    {
        public int ContentId { get; private set; } = ID.INVALID_ID;
        public int ReferenceCount { get; private set; }
        public AssetInfo AssetInfo { get; private set; }
        public AssetMetadata Metadata { get; private set; }
        private List<UploadedAsset> _referencedAssets = [];

        private static readonly Dictionary<Guid, UploadedAsset> _uploadedAssets = [];

        private static UploadedAsset? UploadAssetToEngine(AssetInfo assetInfo, Asset? asset = null)
        {
            Debug.Assert(assetInfo != null);

            // NOTE: マテリアルは AppliedMaterial を渡す(マテリアルのアセットのファイルからは作らない)
            asset ??= assetInfo.Type switch
            {
                AssetType.Mesh => new Geometry(assetInfo),
                AssetType.Texture => new Texture(assetInfo),
                _ => null
            };

            Debug.Assert(asset != null);
            if (asset == null) return null;

            Debug.Assert(asset.Guid == assetInfo.Guid);
            var referencedAssets = new List<UploadedAsset>();
            foreach (var referencedAsset in asset.GetReferencedAssets())
            {
                var uploadedAsset = AddToScene(referencedAsset);
                if (uploadedAsset == null)
                {
                    // NOTE: Primal は null をそのまま入れる。ここでは、ここまでに増やした参照を戻して失敗にする
                    referencedAssets.ForEach(RemoveFromScene);
                    return null;
                }
                referencedAssets.Add(uploadedAsset);
            }

            var data = asset.PackForEngine();
            var contentId = data.Length > 0 ? EngineAPI.CreateResource(data, assetInfo.Type) : ID.INVALID_ID;
            if (!ID.IsValid(contentId))
            {
                referencedAssets.ForEach(RemoveFromScene);
                return null;
            }

            return new(assetInfo, asset.GetMetadata())
            {
                ContentId = contentId,
                ReferenceCount = 1,
                _referencedAssets = referencedAssets,
            };
        }

        private static void UnloadAssetFromEngine(UploadedAsset uploadedAsset)
        {
            Debug.Assert(uploadedAsset?.AssetInfo != null && ID.IsValid(uploadedAsset.ContentId));
            EngineAPI.DestroyResource(uploadedAsset.ContentId, uploadedAsset.AssetInfo.Type);
        }

        /// <summary>
        /// アセットをエンジンにアップロードする(アップロード済みなら参照数を増やす)。失敗したら null。
        /// asset を渡さないときは、assetInfo のファイルから読む。
        /// </summary>
        public static UploadedAsset? AddToScene(AssetInfo assetInfo, Asset? asset = null)
        {
            Debug.Assert(assetInfo != null && assetInfo.Guid != Guid.Empty);
            var key = assetInfo.Guid;

            if (_uploadedAssets.TryGetValue(key, out var value))
            {
                ++value.ReferenceCount;
                value._referencedAssets.ForEach(x => ++x.ReferenceCount);
                return value;
            }

            var uploadedAsset = UploadAssetToEngine(assetInfo, asset);
            if (uploadedAsset == null)
            {
                Logger.Log(MessageType.Error, $"アセット {assetInfo.FullName} をエンジンにアップロードできませんでした。");
                return null;
            }

            _uploadedAssets[key] = uploadedAsset;
            return uploadedAsset;
        }

        public static void RemoveFromScene(UploadedAsset uploadedAsset)
        {
            Debug.Assert(uploadedAsset != null && _uploadedAssets.ContainsKey(uploadedAsset.AssetInfo.Guid));

            uploadedAsset._referencedAssets.ForEach(RemoveFromScene);
            --uploadedAsset.ReferenceCount;
            if (uploadedAsset.ReferenceCount == 0)
            {
                UnloadAssetFromEngine(uploadedAsset);
                _uploadedAssets.Remove(uploadedAsset.AssetInfo.Guid);
                uploadedAsset.ContentId = ID.INVALID_ID;
            }
        }

        public static int GetContentId(Guid id)
        {
            Debug.Assert(id != Guid.Empty);
            return _uploadedAssets.TryGetValue(id, out var uploadedAsset) ? uploadedAsset.ContentId : ID.INVALID_ID;
        }

        // NOTE: UploadedAsset.AddToScene() からだけ作る
        private UploadedAsset(AssetInfo assetInfo, AssetMetadata metadata)
        {
            AssetInfo = assetInfo;
            Metadata = metadata;
        }
    }
}
