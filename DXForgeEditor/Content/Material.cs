using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace DXForgeEditor.Content
{
    // NOTE: エンジンの graphics::material_type(Graphics/Renderer.h)と同じ値
    enum MaterialType : int
    {
        Opaque = 0,
    }

    enum MaterialMode : int
    {
        NoInput,
        Default,
        Node,
        Code,
    }

    class MaterialMetadata : AssetMetadata
    {
        public byte[] PackedData { get; init; } = [];
    }

    class MaterialInput : ViewModelBase
    {
        private string _name = string.Empty;
        public string Name
        {
            get => _name;
            set
            {
                if (_name != value)
                {
                    _name = value;
                    OnPropertyChanged(nameof(Name));
                }
            }
        }

        public MaterialInput(string name)
        {
            Name = name;
        }
    }

    /// <summary>
    /// 適用マテリアルの入力。入力に使うアセット(テクスチャなど)を持つ。
    /// </summary>
    class AppliedMaterialInput(MaterialInput input) : MaterialInput(input.Name)
    {
        private AssetInfo? _asset;
        public AssetInfo? Asset
        {
            get => _asset;
            set
            {
                if (_asset != value)
                {
                    _asset = value;
                    OnPropertyChanged(nameof(Asset));
                }
            }
        }
    }

    /// <summary>
    /// PBRのマテリアルの値。エンジンの graphics::material_surface(Graphics/Renderer.h)と同じ並びで書き出す。
    /// </summary>
    class MaterialSurface : ViewModelBase
    {
        private Color _baseColor = Color.FromScRgb(1f, 0.7f, 0.7f, 0.7f);
        public Color BaseColor
        {
            get => _baseColor;
            set
            {
                if (_baseColor != value)
                {
                    _baseColor = value;
                    OnPropertyChanged(nameof(BaseColor));
                }
            }
        }

        private Color _emissiveColor = Color.FromScRgb(1f, 0f, 0f, 0f);
        public Color EmissiveColor
        {
            get => _emissiveColor;
            set
            {
                if (_emissiveColor != value)
                {
                    _emissiveColor = value;
                    OnPropertyChanged(nameof(EmissiveColor));
                }
            }
        }

        private float _emissiveIntensity = 1f;
        public float EmissiveIntensity
        {
            get => _emissiveIntensity;
            set
            {
                if (!_emissiveIntensity.IsTheSameAs(value))
                {
                    _emissiveIntensity = value;
                    OnPropertyChanged(nameof(EmissiveIntensity));
                }
            }
        }

        private float _metallic = 0f;
        public float Metallic
        {
            get => _metallic;
            set
            {
                if (!_metallic.IsTheSameAs(value))
                {
                    _metallic = value;
                    OnPropertyChanged(nameof(Metallic));
                }
            }
        }

        private float _roughness = 0.9f;
        public float Roughness
        {
            get => _roughness;
            set
            {
                if (!_roughness.IsTheSameAs(value))
                {
                    _roughness = value;
                    OnPropertyChanged(nameof(Roughness));
                }
            }
        }

        public void FromBinary(BinaryReader reader)
        {
            _baseColor.ScR = reader.ReadSingle(); _baseColor.ScG = reader.ReadSingle(); _baseColor.ScB = reader.ReadSingle(); _baseColor.ScA = reader.ReadSingle();
            _emissiveColor.ScR = reader.ReadSingle(); _emissiveColor.ScG = reader.ReadSingle(); _emissiveColor.ScB = reader.ReadSingle();
            _emissiveIntensity = reader.ReadSingle();
            _metallic = reader.ReadSingle();
            _roughness = reader.ReadSingle();
        }

        public void ToBinary(BinaryWriter writer)
        {
            writer.Write(_baseColor.ScR); writer.Write(_baseColor.ScG); writer.Write(_baseColor.ScB); writer.Write(_baseColor.ScA);
            writer.Write(_emissiveColor.ScR); writer.Write(_emissiveColor.ScG); writer.Write(_emissiveColor.ScB);
            writer.Write(_emissiveIntensity);
            writer.Write(_metallic);
            writer.Write(_roughness);
        }

        public MaterialSurface Clone()
            => new()
            {
                BaseColor = BaseColor,
                EmissiveColor = EmissiveColor,
                EmissiveIntensity = EmissiveIntensity,
                Metallic = Metallic,
                Roughness = Roughness,
            };
    }

    class DefaultMaterialInputs : ViewModelBase
    {
        private readonly List<MaterialInput> _inputs;

        public List<MaterialInput> GetInputs() => _inputs;

        public void AddInput(MaterialInput input)
        {
            if (!_inputs.Any(x => x.Name == input.Name))
            {
                _inputs.Add(input);
            }
        }

        public void RemoveInput(string name)
        {
            _inputs.RemoveAll(x => x.Name == name);
        }

        public void FromBinary(BinaryReader reader)
        {
            foreach (var input in _inputs)
            {
                input.Name = reader.ReadString();
            }
        }

        public void ToBinary(BinaryWriter writer)
        {
            foreach (var input in _inputs)
            {
                writer.Write(input.Name);
            }
        }

        public DefaultMaterialInputs()
        {
            _inputs = [new("Base Color"), new("Emissive Color"), new("Normal Map"), new("Metallic and Roughness"), new("Ambient Occlusion"),];
        }
    }

    class NodeMaterial : ViewModelBase
    {
        private readonly List<MaterialInput> _inputs = [];

        public List<MaterialInput> GetInputs() => _inputs;
    }

    class CodeMaterial : ViewModelBase
    {
        private readonly List<MaterialInput> _inputs = [];

        public List<MaterialInput> GetInputs() => _inputs;
    }

    /// <summary>
    /// マテリアル(Material)に値と入力のアセットを与えたもの。エンジンにはこれをマテリアルとしてアップロードする(Primal Day 077)。
    /// 同じ内容の適用マテリアルは、エンジンでは1つのマテリアルを共有する。
    /// </summary>
    class AppliedMaterial : Asset
    {
        // マテリアルのシェーダーをエンジンに登録している適用マテリアルの数
        private class RefCountedMaterial
        {
            public int ReferenceCount { get; set; }
            public required Material Material { get; init; }
        }

        private static readonly Dictionary<string, UploadedAsset> _packedMaterials = [];
        private static readonly Dictionary<int, string> _packedMaterialIds = [];
        private static readonly Dictionary<Guid, RefCountedMaterial> _loadedMaterials = [];

        private readonly Material _material;
        private readonly ObservableCollection<AppliedMaterialInput> _inputs = [];
        public ReadOnlyObservableCollection<AppliedMaterialInput> Inputs { get; }
        private readonly List<int> _shaderIds = [];
        public MaterialSurface MaterialSurface { get; init; }
        public UploadedAsset? UploadedAsset { get; private set; }

        private byte[] _packedData = [];
        private byte[] _previousPackedData = [];

        public override List<AssetInfo> GetReferencedAssets() =>
            Inputs.Where(x => x.Asset != null && x.Asset.Guid != Guid.Empty).Select(x => x.Asset!).ToList();

        public AppliedMaterial Clone()
        {
            var clone = new AppliedMaterial(_material.GetAssetInfo()) { MaterialSurface = MaterialSurface.Clone() };
            clone.Icon = Icon;
            return clone;
        }

        // NOTE: Primalは適用マテリアルごとに ShaderGroup.UploadToEngine() と UnloadFromEngine() を呼ぶ。
        //       DXForgeEXの ShaderGroup は、同じインスタンスで2回登録しても参照数を増やさない(#29)ので、
        //       同じ Material を共有する適用マテリアルの1つを外すと、ほかの適用マテリアルが使うシェーダーまで
        //       エンジンから外れてしまう。そこで、シェーダーの登録は Material ごとに参照数を数え、
        //       0 から 1 になったときに登録し、1 から 0 になったときに外す。
        private void UploadShaders()
        {
            if (!_loadedMaterials.TryGetValue(_material.Guid, out var loadedMaterial))
            {
                loadedMaterial = new() { Material = _material };
                _loadedMaterials.Add(_material.Guid, loadedMaterial);
            }

            if (loadedMaterial.ReferenceCount == 0)
            {
                foreach (var shaderType in Enum.GetValues<ShaderType>())
                {
                    _material.GetShaderGroup(shaderType)?.UploadToEngine();
                }
            }

            ++loadedMaterial.ReferenceCount;

            _shaderIds.Clear();
            foreach (var shaderType in Enum.GetValues<ShaderType>())
            {
                _shaderIds.Add(_material.GetShaderGroup(shaderType)?.ContentId ?? ID.INVALID_ID);
            }
        }

        private void UnloadShaders()
        {
            _shaderIds.Clear();

            Debug.Assert(_loadedMaterials.ContainsKey(_material.Guid));
            if (_loadedMaterials.TryGetValue(_material.Guid, out var loadedMaterial))
            {
                Debug.Assert(loadedMaterial.ReferenceCount > 0);
                --loadedMaterial.ReferenceCount;
                if (loadedMaterial.ReferenceCount == 0)
                {
                    foreach (var shaderType in Enum.GetValues<ShaderType>())
                    {
                        _material.GetShaderGroup(shaderType)?.UnloadFromEngine();
                    }
                }
            }
        }

        /// <summary>
        /// 適用マテリアルをエンジンにアップロードする。前にアップロードしてから変わっていなければ何もしない。
        /// NOTE: エンジンの初期化(Primal Day 078, #31)がまだないので、エディタからは呼ばれない。
        /// </summary>
        public bool UploadToEngine()
        {
            UploadShaders();
            var packedData = PackForEngine();

            if (packedData.Length == 0)
            {
                UnloadShaders();
                return false;
            }

            _packedData = packedData;

            // 前にアップロードしてから変わっていなければ、アップロードし直さない
            // NOTE: SequenceEqual() は遅いが、データは小さく、頻繁には行わない
            if (UploadedAsset != null && _packedData.SequenceEqual(_previousPackedData))
            {
                Debug.Assert(UploadedAsset.GetContentId(Guid) == UploadedAsset.ContentId);
                UnloadShaders();
                return true;
            }

            // 変わったか、まだアップロードしていない。同じ内容のマテリアルがアップロードされていれば、それを使う
            var dataString = Convert.ToBase64String(_packedData);
            UploadedAsset? uploadedAsset;

            if (_packedMaterials.TryGetValue(dataString, out var sameAsset))
            {
                Debug.Assert(_packedMaterialIds.ContainsKey(sameAsset.ContentId));
                Guid = sameAsset.AssetInfo.Guid;
                // 参照数が増える
                uploadedAsset = UploadedAsset.AddToScene(GetAssetInfo(), this);
                Debug.Assert(uploadedAsset == sameAsset);
            }
            else
            {
                // 前の内容をアップロード済みなら、新しい内容には別のGUIDを使う
                if (ID.IsValid(UploadedAsset.GetContentId(Guid)))
                {
                    Debug.Assert(_previousPackedData.Length > 0 && UploadedAsset != null);
                    Guid = Guid.NewGuid();
                }

                uploadedAsset = UploadedAsset.AddToScene(GetAssetInfo(), this);
                if (uploadedAsset != null)
                {
                    Debug.Assert(!_packedMaterialIds.ContainsKey(uploadedAsset.ContentId));
                    _packedMaterials.Add(dataString, uploadedAsset);
                    _packedMaterialIds.Add(uploadedAsset.ContentId, dataString);
                }
            }

            if (uploadedAsset == null)
            {
                UnloadShaders();
                return false;
            }

            // 前の内容をアンロードする
            if (UploadedAsset != null)
            {
                Debug.Assert(UploadedAsset.ContentId != uploadedAsset.ContentId);
                UnloadFromEngine();
            }

            _previousPackedData = _packedData;
            UploadedAsset = uploadedAsset;

            return true;
        }

        public void UnloadFromEngine()
        {
            Debug.Assert(UploadedAsset != null && _packedMaterialIds.ContainsKey(UploadedAsset.ContentId));
            if (UploadedAsset == null) return;

            // NOTE: RemoveFromScene() は参照数が0になると ContentId を無効にするので、先に取っておく
            //       (Primal はそのあとの ContentId で _packedMaterialIds から外すため、外れずに残る)
            var contentId = UploadedAsset.ContentId;
            if (_packedMaterialIds.TryGetValue(contentId, out var dataString) &&
                _packedMaterials.TryGetValue(dataString, out var uploadedAsset))
            {
                Debug.Assert(UploadedAsset == uploadedAsset);
                UploadedAsset.RemoveFromScene(uploadedAsset);
                UnloadShaders();

                if (uploadedAsset.ReferenceCount == 0)
                {
                    _packedMaterialIds.Remove(contentId);
                    _packedMaterials.Remove(dataString);
                }
            }

            _previousPackedData = [];
            UploadedAsset = null;
        }

        public override MaterialMetadata GetMetadata() => new() { PackedData = _packedData };

        // NOTE: Primal は以下の3つで NotImplementedException を投げる。適用マテリアルはファイルにしないので、失敗を返す。
        public override bool Import(string file) => false;

        public override bool Load(string file) => false;

        public override IEnumerable<string> Save(string file) => [];

        /// <summary>
        /// エンジンの CreateResource に渡す形式に詰める。入力のアセットがアップロードされていなければ空。
        /// </summary>
        /// <returns>
        /// struct {
        ///  u32                 texture_count,
        ///  id::id_type         texture_ids[texture_count];
        ///  id::id_type*        texture_ids;       // 0。エンジン(DLL)が上の配列を指すようにする
        ///  material_surface    surface;
        ///  material_type::type type,
        ///  u32                 texture_count,
        ///  id::id_type         shader_ids[shader_type::count],
        /// }
        /// NOTE: 並びは DXForgeEngineDLL/ResourceUpload.h と合わせる
        /// </returns>
        public override byte[] PackForEngine()
        {
            Debug.Assert(_shaderIds.Count == Enum.GetValues<ShaderType>().Length, "UploadShaders() must be called first.");
            if (_shaderIds.Count != Enum.GetValues<ShaderType>().Length) return [];

            using var writer = new BinaryWriter(new MemoryStream());
            var referencedAssets = GetReferencedAssets();

            writer.Write(referencedAssets.Count);

            foreach (var input in referencedAssets)
            {
                var contentId = UploadedAsset.GetContentId(input.Guid);
                if (!ID.IsValid(contentId))
                {
                    Logger.Log(MessageType.Error, $"マテリアルの入力 {input.FullName} がエンジンにアップロードされていません。");
                    return [];
                }

                writer.Write(contentId);
            }

            // テクスチャのIDへのポインタの場所(8バイト)。テクスチャがなければ null のまま。
            writer.Write(0L);
            MaterialSurface.ToBinary(writer);
            writer.Write((int)_material.MaterialType);
            writer.Write(referencedAssets.Count);
            _shaderIds.ForEach(writer.Write);

            writer.Flush();
            return ((MemoryStream)writer.BaseStream).ToArray();
        }

        public AppliedMaterial(AssetInfo materialAssetInfo) : base(AssetType.Material)
        {
            Debug.Assert(materialAssetInfo != null && materialAssetInfo.Guid != Guid.Empty);
            if (_loadedMaterials.TryGetValue(materialAssetInfo.Guid, out var loadedMaterial))
            {
                // NOTE: 参照数は、適用マテリアルをアップロードするときに増やす
                _material = loadedMaterial.Material;
            }
            else
            {
                _material = new(materialAssetInfo);
                _loadedMaterials.Add(_material.Guid, new() { Material = _material });
            }

            MaterialSurface = _material.MaterialSurface.Clone();
            _material.GetInputs().ForEach(x => _inputs.Add(new(x)));
            Icon = _material.Icon;
            Inputs = new(_inputs);
        }
    }

    /// <summary>
    /// マテリアルのアセット(Primal Day 077)。シェーダーグループ(Shader.cs)と既定の値、入力の名前を持つ。
    /// </summary>
    class Material : Asset
    {
        public static AssetInfo? Default => DefaultAssets.DefaultMaterial;

        private readonly Dictionary<ShaderType, ShaderGroup> _shaders = [];

        private MaterialType _materialType;
        public MaterialType MaterialType
        {
            get => _materialType;
            set
            {
                if (_materialType != value)
                {
                    _materialType = value;
                    OnPropertyChanged(nameof(MaterialType));
                }
            }
        }

        private MaterialMode _materialMode;
        public MaterialMode MaterialMode
        {
            get => _materialMode;
            set
            {
                if (_materialMode != value)
                {
                    _materialMode = value;
                    OnPropertyChanged(nameof(MaterialMode));
                }
            }
        }

        public MaterialSurface MaterialSurface { get; } = new();

        public DefaultMaterialInputs DefaultMaterialInputs { get; } = new();
        public NodeMaterial NodeMaterial { get; } = new();
        public CodeMaterial CodeMaterial { get; } = new();

        public List<MaterialInput> GetInputs() =>
            _materialMode switch
            {
                MaterialMode.Default => DefaultMaterialInputs.GetInputs(),
                MaterialMode.Node => NodeMaterial.GetInputs(),
                MaterialMode.Code => CodeMaterial.GetInputs(),
                // NOTE: Primal は未知の値で NotImplementedException を投げる
                _ => [],
            };

        public override bool Import(string file) => false;

        public override bool Load(string file)
        {
            Debug.Assert(File.Exists(file));
            Debug.Assert(Path.GetExtension(file).ToLower() == AssetFileExtension);

            if (!File.Exists(file)) return false;

            try
            {
                using var reader = new BinaryReader(File.Open(file, FileMode.Open, FileAccess.Read));

                ReadAssetFileHeader(reader);

                var shaderGroupCount = reader.ReadInt32();

                _shaders.Clear();

                for (int i = 0; i < shaderGroupCount; ++i)
                {
                    var shaderGroup = new ShaderGroup();
                    shaderGroup.FromBinary(reader);
                    Debug.Assert(!_shaders.ContainsKey(shaderGroup.Type));
                    _shaders[shaderGroup.Type] = shaderGroup;
                }

                MaterialType = (MaterialType)reader.ReadInt32();
                MaterialMode = (MaterialMode)reader.ReadInt32();

                MaterialSurface.FromBinary(reader);
                DefaultMaterialInputs.FromBinary(reader);
                // TODO: NodeMaterial と CodeMaterial(Primal でもまだない)

                FullPath = file;

                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"マテリアルを読み込めませんでした: {file}");
            }

            return false;
        }

        // NOTE: エンジンにアップロードするのは適用マテリアル(AppliedMaterial)。Primal は NotImplementedException を投げる。
        public override byte[] PackForEngine() => [];

        public override IEnumerable<string> Save(string file)
        {
            try
            {
                if (TryGetAssetInfo(file) is AssetInfo info && info.Type == Type)
                {
                    Guid = info.Guid;
                }

                var bmp = new BitmapImage();
                bmp.BeginInit();
                // NOTE: Primal は pack://application:,,,/ で探すため、エディタ以外から呼ぶと見つからない
                bmp.UriSource = ContentHelper.GetPackUri("Resources/TextureEditor/Checker64.png", typeof(Material));
                bmp.DecodePixelWidth = ContentInfo.IconWidth;
                bmp.EndInit();
                Icon = BitmapHelper.CreateThumbnail(bmp, ContentInfo.IconWidth, ContentInfo.IconWidth);

                using var writer = new BinaryWriter(File.Open(file, FileMode.Create, FileAccess.Write));

                WriteAssetFileHeader(writer);

                writer.Write(_shaders.Count);

                foreach (var (_, shaderGroup) in _shaders)
                {
                    shaderGroup.ToBinary(writer);
                }

                writer.Write((int)MaterialType);
                writer.Write((int)MaterialMode);

                MaterialSurface.ToBinary(writer);
                DefaultMaterialInputs.ToBinary(writer);

                FullPath = file;
                Logger.Log(MessageType.Info, $"マテリアルを保存しました: {file}");

                return [file];
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"マテリアルを保存できませんでした: {file}");
                return [];
            }
        }

        public bool AddShaderGroup(ShaderGroup shaderGroup)
        {
            Debug.Assert(shaderGroup != null && !_shaders.ContainsKey(shaderGroup.Type));
            return _shaders.TryAdd(shaderGroup.Type, shaderGroup);
        }

        public ShaderGroup? GetShaderGroup(ShaderType shaderType)
        {
            _shaders.TryGetValue(shaderType, out var shaderGroup);
            return shaderGroup;
        }

        // NOTE: Primal は NotImplementedException を投げる
        public override AssetMetadata GetMetadata() => new MaterialMetadata();

        public Material() : base(AssetType.Material) { }

        public Material(AssetInfo assetInfo) : this()
        {
            Debug.Assert(assetInfo != null && assetInfo.Guid != Guid.Empty);
            Debug.Assert(File.Exists(assetInfo.FullPath) && assetInfo.Type == Type);
            Load(assetInfo.FullPath);
        }
    }
}
