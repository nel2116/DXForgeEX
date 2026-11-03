using DXForgeEditor.Content;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Windows.Media;

namespace DXForgeEditor.Components
{
    /// <summary>
    /// サブメッシュと、それに適用するマテリアル(Primal Day 079)。
    /// </summary>
    class MeshWithMaterial : ViewModelBase
    {
        public MeshInfo MeshInfo { get; }

        private AppliedMaterial _material;
        public AppliedMaterial Material
        {
            get => _material;
            set
            {
                if (_material != value && value != null)
                {
                    Debug.Assert(ID.IsValid(value.UploadedAsset?.ContentId ?? ID.INVALID_ID));
                    _material.UnloadFromEngine();
                    _material = value;
                    OnPropertyChanged(nameof(Material));
                }
            }
        }

        public MeshWithMaterial(MeshInfo mesh, AppliedMaterial material)
        {
            Debug.Assert(mesh != null && material != null);
            MeshInfo = mesh;
            _material = material;
        }
    }

    class LodWithMaterials(string name, float threshold, List<MeshWithMaterial> meshes)
    {
        public string Name { get; } = name;
        public float Threshold { get; } = threshold;
        public List<MeshWithMaterial> Meshes { get; } = meshes;
    }

    class GeometryWithMaterials(string name, byte[] icon, List<LodWithMaterials> lods)
    {
        public string Name { get; } = name;
        public byte[] Icon { get; } = icon;
        public List<LodWithMaterials> LODs { get; } = lods;
    }

    /// <summary>
    /// サブメッシュごとのマテリアルを .dxproj に保存する形。中身は Primal Day 079 が AppliedMaterial を
    /// [DataContract] にして保存するもの(マテリアルのアセットのGUID、入力のアセットのGUIDと名前、マテリアルの値)と同じ。
    /// NOTE: DXForgeEX の AppliedMaterial は、参照数とアップロードの状態をコンストラクタで作る(#145)ので、
    ///       コンストラクタを呼ばない DataContractSerializer で直接は作らず、Geometry.Load() のときにこれから作る。
    ///       色は System.Windows.Media.Color をそのまま保存せず、scRGB の値で持つ。
    /// </summary>
    [DataContract]
    class AppliedMaterialData
    {
        [DataMember]
        public Guid Material { get; private set; }
        [DataMember]
        public List<Guid> Inputs { get; private set; } = [];
        [DataMember]
        public List<string> InputNames { get; private set; } = [];
        [DataMember]
        public float[] BaseColor { get; private set; } = [];
        [DataMember]
        public float[] EmissiveColor { get; private set; } = [];
        [DataMember]
        public float EmissiveIntensity { get; private set; }
        [DataMember]
        public float Metallic { get; private set; }
        [DataMember]
        public float Roughness { get; private set; }

        private static float[] ToArray(Color color) => [color.ScR, color.ScG, color.ScB, color.ScA];
        private static Color? FromArray(float[]? values) =>
            values?.Length == 4 ? Color.FromScRgb(values[3], values[0], values[1], values[2]) : null;

        public static AppliedMaterialData From(AppliedMaterial material)
        {
            var data = new AppliedMaterialData()
            {
                Material = material.MaterialGuid,
                BaseColor = ToArray(material.MaterialSurface.BaseColor),
                EmissiveColor = ToArray(material.MaterialSurface.EmissiveColor),
                EmissiveIntensity = material.MaterialSurface.EmissiveIntensity,
                Metallic = material.MaterialSurface.Metallic,
                Roughness = material.MaterialSurface.Roughness,
            };
            foreach (var input in material.Inputs.Where(x => x.Asset != null && x.Asset.Guid != Guid.Empty))
            {
                data.Inputs.Add(input.Asset!.Guid);
                data.InputNames.Add(input.Name);
            }
            return data;
        }

        /// <summary>
        /// 保存したマテリアルから適用マテリアルを作る(アップロードはしない)。マテリアルのアセットがなければ既定のマテリアル、
        /// それもなければ null。
        /// </summary>
        public AppliedMaterial? ToAppliedMaterial()
        {
            var assetInfo = AssetRegistry.GetAssetInfo(Material);
            if (assetInfo?.Type != AssetType.Material)
            {
                Logger.Log(MessageType.Warning, $"マテリアル({Material})が見つからないので、既定のマテリアルを使います。");
                assetInfo = Content.Material.Default;
                if (assetInfo == null) return null;
            }

            var surface = new MaterialSurface()
            {
                EmissiveIntensity = EmissiveIntensity,
                Metallic = Metallic,
                Roughness = Roughness,
            };
            if (FromArray(BaseColor) is Color baseColor) surface.BaseColor = baseColor;
            if (FromArray(EmissiveColor) is Color emissiveColor) surface.EmissiveColor = emissiveColor;

            var material = new AppliedMaterial(assetInfo) { MaterialSurface = surface };

            var inputs = Inputs ?? [];
            var inputNames = InputNames ?? [];
            for (int i = 0; i < Math.Min(inputs.Count, inputNames.Count); ++i)
            {
                var input = material.Inputs.FirstOrDefault(x => x.Name == inputNames[i]);
                if (input == null) continue;
                // NOTE: Primal は見つからなければ既定のテクスチャを使うが、DXForgeEX にはまだない(入力なしにする)
                input.Asset = AssetRegistry.GetAssetInfo(inputs[i]);
                if (input.Asset == null)
                {
                    Logger.Log(MessageType.Warning, $"マテリアルの入力 {inputNames[i]} のアセット({inputs[i]})が見つかりません。");
                }
            }
            return material;
        }
    }

    /// <summary>
    /// ジオメトリのアセットを描画するコンポーネント(Primal Day 079)。サブメッシュごとにマテリアルを持つ。
    /// エンティティがアクティブになるとき(Load())にジオメトリとマテリアルをエンジンにアップロードし、
    /// アクティブでなくなるとき(Unload())に外す。
    /// game.bin には書かない(CanWriteToBinary が false。Primal Day 079 も WriteToBinary を実装していない)。
    /// </summary>
    [DataContract]
    class Geometry : Component
    {
        private UploadedAsset? _geometry;

        [DataMember(Name = "Geometry")]
        public Guid GeometryGuid { get; private set; }

        // NOTE: 読み込んだあとの(アップロードしていない間の)サブメッシュごとのマテリアル。アップロードしている間は
        //       GeometryWithMaterials が持ち、Unload() と保存のときにここへ戻す
        [DataMember(Name = "Materials")]
        private List<AppliedMaterialData> _materials = [];

        private GeometryWithMaterials? _geometryWithMaterials;
        public GeometryWithMaterials? GeometryWithMaterials
        {
            get => _geometryWithMaterials;
            private set
            {
                if (_geometryWithMaterials != value)
                {
                    _geometryWithMaterials = value;
                    OnPropertyChanged(nameof(GeometryWithMaterials));
                }
            }
        }

        public List<AppliedMaterial> MaterialsList =>
            GeometryWithMaterials?.LODs.SelectMany(x => x.Meshes, (x, m) => m.Material).ToList() ?? [];

        public int ContentId => _geometry?.ContentId ?? ID.INVALID_ID;

        public override bool CanWriteToBinary => false;

        /// <summary>
        /// 適用マテリアルを作ってエンジンにアップロードする。失敗したら null(作ったものは戻す)。
        /// </summary>
        private static AppliedMaterial? CreateAndUploadAppliedMaterial(AppliedMaterialData? data)
        {
            AppliedMaterial? material;
            if (data != null)
            {
                material = data.ToAppliedMaterial();
            }
            else
            {
                var defaultMaterial = Content.Material.Default;
                material = defaultMaterial != null ? new AppliedMaterial(defaultMaterial) : null;
            }

            if (material == null)
            {
                Logger.Log(MessageType.Error, "既定のマテリアルがないので、ジオメトリにマテリアルを適用できません。");
                return null;
            }

            if (!material.UploadToEngine()) return null;
            Debug.Assert(material.UploadedAsset != null);
            return material;
        }

        private void Load(AssetInfo geometry)
        {
            Debug.Assert(_geometry == null && GeometryWithMaterials == null);

            var uploadedGeometry = UploadedAsset.AddToScene(geometry);
            if (uploadedGeometry == null) return;

            if (uploadedGeometry.Metadata is not GeometryMetadata metadata || metadata.LODs.Count == 0 || metadata.LODs.Any(x => x.Meshes.Count == 0))
            {
                Logger.Log(MessageType.Error, $"ジオメトリ {geometry.FullName} にメッシュがありません。");
                UploadedAsset.RemoveFromScene(uploadedGeometry);
                return;
            }

            // サブメッシュの順に、保存したマテリアル(足りなければ既定のマテリアル)を適用する
            var index = 0;
            var uploadedMaterials = new List<AppliedMaterial>();
            var lods = new List<LodWithMaterials>();
            foreach (var lod in metadata.LODs)
            {
                var meshes = new List<MeshWithMaterial>();
                foreach (var mesh in lod.Meshes)
                {
                    var material = CreateAndUploadAppliedMaterial(index < _materials.Count ? _materials[index] : null);
                    ++index;
                    if (material == null)
                    {
                        Logger.Log(MessageType.Error, $"ジオメトリ {geometry.FullName} のマテリアルをエンジンにアップロードできませんでした。");
                        uploadedMaterials.ForEach(x => x.UnloadFromEngine());
                        UploadedAsset.RemoveFromScene(uploadedGeometry);
                        return;
                    }
                    uploadedMaterials.Add(material);
                    meshes.Add(new MeshWithMaterial(mesh, material));
                }
                lods.Add(new LodWithMaterials(lod.Name, lod.Threshold, meshes));
            }

            _geometry = uploadedGeometry;
            GeometryWithMaterials = new(metadata.Name, geometry.Icon, lods);
        }

        /// <summary>
        /// ジオメトリとサブメッシュごとのマテリアルをエンジンにアップロードする。
        /// 失敗したとき(アセットがない、アップロードできない)は、ログを出して何もアップロードしない(エンティティは Geometry なしで作られる)。
        /// NOTE: Primal は見つからないジオメトリを既定のジオメトリにするが、DXForgeEX にはまだない
        /// </summary>
        public override void Load()
        {
            Debug.Assert(_geometry == null && GeometryWithMaterials == null);
            if (_geometry != null) return;

            var assetInfo = GeometryGuid != Guid.Empty ? AssetRegistry.GetAssetInfo(GeometryGuid) : null;
            if (assetInfo?.Type != AssetType.Mesh)
            {
                Logger.Log(MessageType.Error, $"エンティティ {Owner.Name} のジオメトリ({GeometryGuid})が見つかりません。");
                return;
            }

            Load(assetInfo);
        }

        public override void Unload()
        {
            if (_geometry == null) return;

            var materials = MaterialsList;
            _materials = materials.Select(AppliedMaterialData.From).ToList();
            materials.ForEach(x => x.UnloadFromEngine());
            GeometryWithMaterials = null;
            GeometryGuid = _geometry.AssetInfo.Guid;
            UploadedAsset.RemoveFromScene(_geometry);
            _geometry = null;
        }

        /// <summary>
        /// ジオメトリを変える。マテリアルは新しいジオメトリの既定のマテリアルにする。
        /// </summary>
        public void SetGeometry(Guid guid)
        {
            if (GeometryGuid != guid)
            {
                // エンティティを消して(ジオメトリをエンジンから外して)から、新しいジオメトリで作り直す
                var wasActive = Owner.IsActive;
                Owner.IsActive = false;
                GeometryGuid = guid;
                _materials.Clear();
                Owner.IsActive = wasActive;
            }
        }

        public override IMSComponent GetMultiselectionComponent(MSEntity msEntity) => new MSGeometry(msEntity);

        // NOTE: Primal Day 079 は NotImplementedException を投げる。DXForgeEX では CanWriteToBinary を false にして、
        //       Project.WriteGameBinary が呼ばないようにする(game.bin の書き出しは async void のコマンドから呼ばれる)
        public override void WriteToBinary(BinaryWriter bw) { }

        [OnSerializing]
        private void OnSerializing(StreamingContext context)
        {
            if (GeometryWithMaterials != null)
            {
                _materials = MaterialsList.Select(AppliedMaterialData.From).ToList();
            }
        }

        [OnDeserialized]
        private void OnDeserialized(StreamingContext context)
        {
            // DataContractSerializer はフィールドの初期化子を実行しないので、要素がないときは空にする
            _materials ??= [];
        }

        public Geometry(GameEntity owner, AssetInfo geometry) : base(owner)
        {
            Debug.Assert(geometry?.Type == AssetType.Mesh);
            GeometryGuid = geometry?.Guid ?? Guid.Empty;
        }
    }

    sealed class MSGeometry : MSComponent<Geometry>
    {
        private GeometryWithMaterials? _geometryWithMaterials;
        public GeometryWithMaterials? GeometryWithMaterials
        {
            get => _geometryWithMaterials;
            private set
            {
                if (_geometryWithMaterials != value)
                {
                    _geometryWithMaterials = value;
                    OnPropertyChanged(nameof(GeometryWithMaterials));
                }
            }
        }

        public Guid GeometryGuid => _geometryWithMaterials != null ? SelectedComponents.First().GeometryGuid : Guid.Empty;

        public void SetGeometry(Guid guid)
        {
            SelectedComponents.ForEach(x => x.SetGeometry(guid));
            Refresh();
        }

        protected override bool UpdateComponents(string? propertyName) => false;

        protected override bool UpdateMSComponent()
        {
            var contentId = MSEntity.GetMixedValue(SelectedComponents, new Func<Geometry, int>(x => x.ContentId));
            GeometryWithMaterials = contentId.HasValue ? SelectedComponents.First().GeometryWithMaterials : null;

            return true;
        }

        public MSGeometry(MSEntity msEntity) : base(msEntity)
        {
            Refresh();
        }
    }
}
