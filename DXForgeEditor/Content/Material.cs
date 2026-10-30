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

        // NOTE: Primal は入力の数を書かないので、AddInput() や RemoveInput() のあとに保存すると、読み込みで足りなくなるか
        //       例外になる(Codexの指摘, #145)。ここでは数を書き、読むときはその数の入力に作り直す。
        public void FromBinary(BinaryReader reader)
        {
            var count = reader.ReadInt32();
            _inputs.Clear();
            for (int i = 0; i < count; ++i)
            {
                _inputs.Add(new(reader.ReadString()));
            }
        }

        public void ToBinary(BinaryWriter writer)
        {
            writer.Write(_inputs.Count);
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
        // 読み込んだマテリアルと、そのシェーダーをエンジンに登録している適用マテリアルの数
        private class RefCountedMaterial
        {
            public int ReferenceCount { get; set; }
            public required Material Material { get; init; }
        }

        private static readonly Dictionary<string, UploadedAsset> _packedMaterials = [];
        private static readonly Dictionary<int, string> _packedMaterialIds = [];
        private static readonly Dictionary<Guid, RefCountedMaterial> _loadedMaterials = [];

        // NOTE: 同じマテリアルの適用マテリアルは、_loadedMaterials の同じ RefCountedMaterial を共有する。
        //       表から外れたあと(Material を保存し直したときや、参照数が0になったとき)も、作ったときのものを使い続ける。
        private readonly RefCountedMaterial _loadedMaterial;
        private Material _material => _loadedMaterial.Material;
        private readonly ObservableCollection<AppliedMaterialInput> _inputs = [];
        public ReadOnlyObservableCollection<AppliedMaterialInput> Inputs { get; }
        public MaterialSurface MaterialSurface { get; init; }

        // ---- アップロードの状態 ----
        // このインスタンスが持ち続けるもの(アップロードしている間だけ。外すと全部戻す):
        //   UploadedAsset      アップロードしたマテリアル。その参照を1つ持つ(マテリアルは入力のアセットの参照を持つ)
        //   シェーダーの参照   _loadedMaterial.ReferenceCount のうちの1つ
        //   _shaderIds         そのとき登録していたシェーダーのID(PackForEngine() が書く)
        //   _uploadedData      アップロードしたデータ(PackForEngine() の結果)。変わったかどうかを比べる
        // UploadToEngine() の中で一時的に増やして、抜けるときに必ず戻すもの:
        //   入力のアセットの参照(PackForEngine() がそのIDを書くため)、シェーダーの参照(登録したままパックするため)
        //
        // | 操作                                   | 成功したあと                               | 失敗したあと                 |
        // |----------------------------------------|--------------------------------------------|------------------------------|
        // | アップロードしていないときに Upload    | 新しいマテリアル(同じ内容があれば共有)を持つ | 何も持たない(呼ぶ前のまま)   |
        // | 同じ内容で Upload                      | 変わらない(true を返す)                    | -(失敗しない)              |
        // | 内容を変えて Upload                    | 新しいマテリアルを持ち、前のものを戻す     | 前のものを持ったまま         |
        // | Unload                                 | 何も持たない                               | -                            |
        // | アップロードしていないときに Unload    | 何もしない                                 | -                            |
        // 同じ内容(データが同じ)の適用マテリアルは、エンジンのマテリアルを共有し、それぞれが参照を1つ持つ。
        // シェーダーは Material ごとに参照数を数え、0 から 1 で登録し、1 から 0 で外す。
        // material_asset_test.ps1 の 8 節が、この表の操作を組み合わせて確かめる。
        public UploadedAsset? UploadedAsset { get; private set; }
        private List<int> _shaderIds = [];
        private byte[] _uploadedData = [];

        public override List<AssetInfo> GetReferencedAssets() =>
            Inputs.Where(x => x.Asset != null && x.Asset.Guid != Guid.Empty).Select(x => x.Asset!).ToList();

        // NOTE: Primal は入力のアセットを複製しない(Codexの指摘, #145)
        public AppliedMaterial Clone()
        {
            var clone = new AppliedMaterial(_loadedMaterial) { MaterialSurface = MaterialSurface.Clone() };
            Debug.Assert(clone._inputs.Count == _inputs.Count);
            for (int i = 0; i < Math.Min(clone._inputs.Count, _inputs.Count); ++i)
            {
                clone._inputs[i].Asset = _inputs[i].Asset;
            }
            clone.Icon = Icon;
            return clone;
        }

        /// <summary>
        /// マテリアルを保存したら、そのあとに作る適用マテリアルが新しい内容を読むように、表から外す。
        /// NOTE: Primal は GUID だけで古い Material を使い続ける(Codexの指摘, #145)
        /// </summary>
        public static void OnMaterialSaved(Guid materialGuid) => _loadedMaterials.Remove(materialGuid);

        // NOTE: Primalは適用マテリアルごとに ShaderGroup.UploadToEngine() と UnloadFromEngine() を呼ぶ。
        //       DXForgeEXの ShaderGroup は、同じインスタンスで2回登録しても参照数を増やさない(#29)ので、
        //       同じ Material を共有する適用マテリアルの1つを外すと、ほかの適用マテリアルが使うシェーダーまで
        //       エンジンから外れてしまう。そこで、シェーダーの登録は Material ごとに参照数を数え、
        //       0 から 1 になったときに登録し、1 から 0 になったときに外す。
        //       シェーダーグループを1つでも登録できなければ、登録できたものを外して失敗にする
        //       (Primal は失敗を無視し、そのシェーダーのない(IDが無効な)マテリアルをアップロードする。Codexの指摘, #145)。
        /// <summary>
        /// マテリアルのシェーダーの参照を1つ増やし(0 から 1 なら登録し)、そのIDを返す。失敗したら参照を増やさない。
        /// </summary>
        private bool AcquireShaders(out List<int> shaderIds)
        {
            shaderIds = [];
            var loadedMaterial = _loadedMaterial;
            if (loadedMaterial.ReferenceCount == 0)
            {
                var shaderGroups = Enum.GetValues<ShaderType>().Select(_material.GetShaderGroup).OfType<ShaderGroup>().ToList();
                if (shaderGroups.Count == 0)
                {
                    Logger.Log(MessageType.Error, $"マテリアル {_material.FileName} にシェーダーがないため、エンジンにアップロードできません。");
                    return false;
                }

                var uploaded = new List<ShaderGroup>();
                foreach (var shaderGroup in shaderGroups)
                {
                    if (!ID.IsValid(shaderGroup.UploadToEngine()))
                    {
                        uploaded.ForEach(x => x.UnloadFromEngine());
                        return false;
                    }
                    uploaded.Add(shaderGroup);
                }
            }

            ++loadedMaterial.ReferenceCount;
            shaderIds = Enum.GetValues<ShaderType>().Select(x => _material.GetShaderGroup(x)?.ContentId ?? ID.INVALID_ID).ToList();
            return true;
        }

        /// <summary>
        /// AcquireShaders() で増やした参照を1つ戻す(1 から 0 なら外す)。
        /// </summary>
        private void ReleaseShaders()
        {
            var loadedMaterial = _loadedMaterial;
            Debug.Assert(loadedMaterial.ReferenceCount > 0);
            if (loadedMaterial.ReferenceCount <= 0) return;

            --loadedMaterial.ReferenceCount;
            if (loadedMaterial.ReferenceCount == 0)
            {
                foreach (var shaderType in Enum.GetValues<ShaderType>())
                {
                    _material.GetShaderGroup(shaderType)?.UnloadFromEngine();
                }

                // 使う適用マテリアルがなくなったら表から外す(次に作るときにファイルから読み直す)
                var key = _loadedMaterials.FirstOrDefault(x => x.Value == loadedMaterial).Key;
                if (key != Guid.Empty)
                {
                    _loadedMaterials.Remove(key);
                }
            }
        }

        /// <summary>
        /// アップロードしたマテリアルの参照を1つ戻す。参照数が0になったら、同じ内容を探す表からも外す。
        /// </summary>
        private static void ReleaseUploadedMaterial(UploadedAsset uploadedAsset)
        {
            // NOTE: RemoveFromScene() は参照数が0になると ContentId を無効にするので、先に取っておく
            //       (Primal はそのあとの ContentId で _packedMaterialIds から外すため、外れずに残る)
            var contentId = uploadedAsset.ContentId;
            UploadedAsset.RemoveFromScene(uploadedAsset);
            if (uploadedAsset.ReferenceCount == 0 && _packedMaterialIds.Remove(contentId, out var dataString))
            {
                _packedMaterials.Remove(dataString);
            }
        }

        /// <summary>
        /// 適用マテリアルをエンジンにアップロードする。前にアップロードしてから変わっていなければ何もしない。
        /// 失敗したときは、呼ぶ前の状態のまま(上の状態の表)。
        /// NOTE: エンジンの初期化(Primal Day 078, #31)がまだないので、エディタからは呼ばれない。
        /// </summary>
        public bool UploadToEngine()
        {
            // 一時的な参照: 入力のアセット(PackForEngine() がそのIDを書くため)。最後に必ず戻す。
            // NOTE: Primal は先にアップロードしないので、入力のアセットがまだアップロードされていないと失敗する(Codexの指摘, #145)
            var inputs = new List<UploadedAsset>();
            try
            {
                foreach (var assetInfo in GetReferencedAssets())
                {
                    var input = UploadedAsset.AddToScene(assetInfo);
                    if (input == null) return false;
                    inputs.Add(input);
                }

                // 一時的な参照: シェーダー(登録したままパックするため)。最後に必ず戻す
                if (!AcquireShaders(out var shaderIds)) return false;
                try
                {
                    return UploadMaterialToEngine(shaderIds);
                }
                finally
                {
                    ReleaseShaders();
                }
            }
            finally
            {
                inputs.ForEach(UploadedAsset.RemoveFromScene);
            }
        }

        // NOTE: Primal は、内容が変わったときに「前の内容をアップロード済み」であることを Debug.Assert で前提にして GUID を
        //       作り直すが、同じ内容を共有していた別の適用マテリアルが残っていて、自分は外し済みのときに外れる(Codexの指摘, #145)。
        //       ここでは、新しい内容は常に新しいGUIDでアップロードし、同じ内容のものがあればそのGUIDを使う。
        //       また、同じ内容でアップロードし直したときに、持ち続けるもの(_shaderIds など)を変えない(Codexの指摘3回目, #145)。
        private bool UploadMaterialToEngine(List<int> shaderIds)
        {
            var packedData = Pack(shaderIds);
            if (packedData.Length == 0) return false;

            // 同じ内容なら何も変えない
            // NOTE: SequenceEqual() は遅いが、データは小さく、頻繁には行わない
            if (UploadedAsset != null && packedData.SequenceEqual(_uploadedData)) return true;

            // 同じ内容のマテリアルがアップロードされていれば、それを使う(参照数が増える)。なければ新しいGUIDでアップロードする。
            // NOTE: AddToScene() は this.PackForEngine() と GetMetadata() を呼ぶので、その間だけ新しい値にし、失敗したら戻す
            var dataString = Convert.ToBase64String(packedData);
            var previous = (Guid, _shaderIds, _uploadedData);
            UploadedAsset? uploadedAsset;

            if (_packedMaterials.TryGetValue(dataString, out var sameAsset))
            {
                uploadedAsset = UploadedAsset.AddToScene(sameAsset.AssetInfo);
            }
            else
            {
                Guid = Guid.NewGuid();
                _shaderIds = shaderIds;
                _uploadedData = packedData;
                uploadedAsset = UploadedAsset.AddToScene(GetAssetInfo(), this);
                if (uploadedAsset != null)
                {
                    _packedMaterials[dataString] = uploadedAsset;
                    _packedMaterialIds[uploadedAsset.ContentId] = dataString;
                }
            }

            if (uploadedAsset == null)
            {
                (Guid, _shaderIds, _uploadedData) = previous;
                return false;
            }

            // 持ち続けるシェーダーの参照を増やす(一時的な参照を持っているので、登録はしない)
            var acquired = AcquireShaders(out _);
            Debug.Assert(acquired);

            // 前のものを戻す
            if (UploadedAsset != null)
            {
                ReleaseUploadedMaterial(UploadedAsset);
                ReleaseShaders();
            }

            UploadedAsset = uploadedAsset;
            Guid = uploadedAsset.AssetInfo.Guid;
            _shaderIds = shaderIds;
            _uploadedData = packedData;

            return true;
        }

        /// <summary>
        /// アップロードした適用マテリアルを外し、持っているものを全部戻す。アップロードしていなければ何もしない。
        /// </summary>
        public void UnloadFromEngine()
        {
            if (UploadedAsset == null) return;

            ReleaseUploadedMaterial(UploadedAsset);
            ReleaseShaders();

            UploadedAsset = null;
            _shaderIds = [];
            _uploadedData = [];
        }

        public override MaterialMetadata GetMetadata() => new() { PackedData = _uploadedData };

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
        // NOTE: アップロードしている間(と UploadToEngine() の中)だけデータを返す。アップロードしていなければ空
        public override byte[] PackForEngine() => Pack(_shaderIds);

        private byte[] Pack(List<int> shaderIds)
        {
            if (shaderIds.Count != Enum.GetValues<ShaderType>().Length) return [];

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
            shaderIds.ForEach(writer.Write);

            writer.Flush();
            return ((MemoryStream)writer.BaseStream).ToArray();
        }

        public AppliedMaterial(AssetInfo materialAssetInfo) : this(GetLoadedMaterial(materialAssetInfo)) { }

        private static RefCountedMaterial GetLoadedMaterial(AssetInfo materialAssetInfo)
        {
            Debug.Assert(materialAssetInfo != null && materialAssetInfo.Guid != Guid.Empty);
            // NOTE: 参照数は、適用マテリアルをアップロードするときに増やす
            if (_loadedMaterials.TryGetValue(materialAssetInfo.Guid, out var loadedMaterial)) return loadedMaterial;

            // 読めなければ空のマテリアル(シェーダーがないのでアップロードできない)を使い、表には入れない
            var material = Material.FromAssetInfo(materialAssetInfo);
            if (material == null) return new() { Material = new() };

            loadedMaterial = new() { Material = material };
            _loadedMaterials[materialAssetInfo.Guid] = loadedMaterial;
            return loadedMaterial;
        }

        private AppliedMaterial(RefCountedMaterial loadedMaterial) : base(AssetType.Material)
        {
            _loadedMaterial = loadedMaterial;
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
            // NOTE: ファイルがない・種類が違うときは失敗にする(ファイルが消えたり置き換えられたりすると実際に起こる)
            if (!File.Exists(file) || Path.GetExtension(file).ToLower() != AssetFileExtension || GetAssetInfo(file)?.Type != Type)
            {
                Logger.Log(MessageType.Error, $"マテリアルを読み込めませんでした: {file}");
                return false;
            }

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
                    if (_shaders.ContainsKey(shaderGroup.Type)) throw new InvalidDataException($"duplicate {shaderGroup.Type} shader group");
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
                AppliedMaterial.OnMaterialSaved(Guid);
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

        /// <summary>
        /// マテリアルのアセットのファイルから読む。すべて読めたときだけ返し、読めなければ null。
        /// NOTE: Primal はコンストラクタで読み、途中で失敗しても(読めたところまでの)オブジェクトを返す(Codexの指摘, #145)
        /// </summary>
        public static Material? FromAssetInfo(AssetInfo assetInfo)
        {
            if (!CanLoad(assetInfo, AssetType.Material)) return null;

            var material = new Material();
            return material.Load(assetInfo.FullPath) ? material : null;
        }
    }
}
