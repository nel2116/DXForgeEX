using DXForgeEditor.Content;
using DXForgeEditor.DllWrappers;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace DXForgeEditor.Content
{
    enum PrimitiveMeshType
    {
        Plane,
        Cube,
        UvSphere,
        IcoSphere,
        Cylinder,
        Capsule
    }

    enum ElementsType
    {
        Position = 0x00,
        Normals = 0x01,
        TSpace = 0x03,
        Joints = 0x04,
        Colors = 0x08,
    }

    enum PrimitiveTopology
    {
        PointList = 1,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
    }

    class MeshInfo : ViewModelBase
    {
        public string Name { get; init; } = string.Empty;

        private byte[] _icon = [];
        /// <summary>
        /// サブメッシュのアイコン。最初はアセット全体のアイコンで、生成が終わると置き換わる(Primal Day 081)。UI スレッドで設定する。
        /// </summary>
        public byte[] Icon
        {
            get => _icon;
            set
            {
                if (_icon != value)
                {
                    _icon = value;
                    OnPropertyChanged(nameof(Icon));
                }
            }
        }

        public int IndexCount { get; init; }
        public int VertexCount { get; init; }
        public int TriangleCount { get; init; }
    }

    class LodInfo
    {
        public string Name { get; init; } = string.Empty;
        public float Threshold { get; init; }
        public List<MeshInfo> Meshes { get; init; } = [];
    }

    class GeometryMetadata : AssetMetadata
    {
        // NOTE: Primal Day 079 で足された(Primal は値を入れていない)。ここでは LOD グループの名前を入れる
        public string Name { get; init; } = string.Empty;
        public List<LodInfo> LODs { get; init; } = [];
    }

    class Mesh : ViewModelBase
    {
        public static int PositionSize => sizeof(float) * 3;

        private int _elementSize;
        public int ElementSize
        {
            get => _elementSize;
            set
            {
                if (_elementSize != value)
                {
                    _elementSize = value;
                    OnPropertyChanged(nameof(ElementSize));
                }
            }
        }

        private int _vertexCount;
        public int VertexCount
        {
            get => _vertexCount;
            set
            {
                if (_vertexCount != value)
                {
                    _vertexCount = value;
                    OnPropertyChanged(nameof(VertexCount));
                }
            }
        }

        private int _indexSize;
        public int IndexSize
        {
            get => _indexSize;
            set
            {
                if (_indexSize != value)
                {
                    _indexSize = value;
                    OnPropertyChanged(nameof(IndexSize));
                }
            }
        }

        private int _indexCount;
        public int IndexCount
        {
            get => _indexCount;
            set
            {
                if (_indexCount != value)
                {
                    _indexCount = value;
                    OnPropertyChanged(nameof(IndexCount));
                }
            }
        }

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

        public ElementsType ElementsType { get; set; }
        public PrimitiveTopology PrimitiveTopology { get; set; }
        public byte[] Positions { get; set; } = Array.Empty<byte>();
        public byte[] Elements { get; set; } = Array.Empty<byte>();
        public byte[] Indices { get; set; } = Array.Empty<byte>();
    }

    class MeshLOD : ViewModelBase
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

        private float _lodThreshold;
        public float LodThreshold
        {
            get => _lodThreshold;
            set
            {
                if (!_lodThreshold.IsTheSameAs(value))
                {
                    _lodThreshold = value;
                    OnPropertyChanged(nameof(LodThreshold));
                }
            }
        }

        public ObservableCollection<Mesh> Meshes { get; } = new ObservableCollection<Mesh>();
    }

    class LODGroup : ViewModelBase
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

        public ObservableCollection<MeshLOD> LODs { get; } = new ObservableCollection<MeshLOD>();
    }

    class GeometryImportSettings : ViewModelBase, IAssetImportSettings
    {
        private bool _calculateNormals;
        public bool CalculateNormals
        {
            get => _calculateNormals;
            set
            {
                if (_calculateNormals != value)
                {
                    _calculateNormals = value;
                    OnPropertyChanged(nameof(CalculateNormals));
                }
            }
        }

        private bool _calculateTangents;
        public bool CalculateTangents
        {
            get => _calculateTangents;
            set
            {
                if (_calculateTangents != value)
                {
                    _calculateTangents = value;
                    OnPropertyChanged(nameof(CalculateTangents));
                }
            }
        }

        private float _smoothingAngle;
        public float SmoothingAngle
        {
            get => _smoothingAngle;
            set
            {
                if (!_smoothingAngle.IsTheSameAs(value))
                {
                    _smoothingAngle = value;
                    OnPropertyChanged(nameof(SmoothingAngle));
                }
            }
        }

        private bool _reverseHandedness;
        public bool ReverseHandedness
        {
            get => _reverseHandedness;
            set
            {
                if (_reverseHandedness != value)
                {
                    _reverseHandedness = value;
                    OnPropertyChanged(nameof(ReverseHandedness));
                }
            }
        }

        private bool _importEmbeddedTextures;
        public bool ImportEmbeddedTextures
        {
            get => _importEmbeddedTextures;
            set
            {
                if (_importEmbeddedTextures != value)
                {
                    _importEmbeddedTextures = value;
                    OnPropertyChanged(nameof(ImportEmbeddedTextures));
                }
            }
        }

        private bool _importAnimations;
        public bool ImportAnimations
        {
            get => _importAnimations;
            set
            {
                if (_importAnimations != value)
                {
                    _importAnimations = value;
                    OnPropertyChanged(nameof(ImportAnimations));
                }
            }
        }

        private bool _coalesceMeshes;
        public bool CoalesceMeshes
        {
            get => _coalesceMeshes;
            set
            {
                if (_coalesceMeshes != value)
                {
                    _coalesceMeshes = value;
                    OnPropertyChanged(nameof(CoalesceMeshes));
                }
            }
        }

        public GeometryImportSettings()
        {
            CalculateNormals = false;
            CalculateTangents = true;
            SmoothingAngle = 178f;
            ReverseHandedness = false;
            ImportEmbeddedTextures = true;
            ImportAnimations = true;
            CoalesceMeshes = false;
        }

        public void ToBinary(BinaryWriter writer)
        {
            writer.Write(CalculateNormals);
            writer.Write(CalculateTangents);
            writer.Write(SmoothingAngle);
            writer.Write(ReverseHandedness);
            writer.Write(ImportEmbeddedTextures);
            writer.Write(ImportAnimations);
            writer.Write(CoalesceMeshes);
        }

        public void FromBinary(BinaryReader reader)
        {
            CalculateNormals = reader.ReadBoolean();
            CalculateTangents = reader.ReadBoolean();
            SmoothingAngle = reader.ReadSingle();
            ReverseHandedness = reader.ReadBoolean();
            ImportEmbeddedTextures = reader.ReadBoolean();
            ImportAnimations = reader.ReadBoolean();
            CoalesceMeshes = reader.ReadBoolean();
        }
    }

    class Geometry : Asset
    {
        /// <summary>
        /// Save() でこの GUID を使う(null ならファイルごとに決める)。既定のジオメトリ(DefaultAssets.DefaultGeometryGuid)に使う。
        /// NOTE: LOD グループが複数あると、すべてのファイルが同じ GUID になるので、LOD グループが 1 つのものにだけ使う
        /// </summary>
        public Guid? FixedGuid { get; init; }

        private readonly List<LODGroup> _lodGroups = new();
        private readonly object _lock = new();

        public GeometryImportSettings ImportSettings { get; } = new();

        /// <summary>
        /// インポートしたLODグループの数(Save() はLODグループごとに1つのファイルを書く)。コマンドラインのインポーター(DXForgeImport)が、
        /// インポートの失敗(FBXのインポートに失敗しても Import() は true を返すことがある)と、一部だけ保存できた場合を見分けるのに使う。
        /// </summary>
        internal int LODGroupCount => _lodGroups.Count;

        public LODGroup? GetLODGroup(int lodGroup = 0)
        {
            Debug.Assert(lodGroup >= 0 && lodGroup < _lodGroups.Count);
            return (lodGroup < _lodGroups.Count) ? _lodGroups[lodGroup] : null;
        }

        public void FromRawData(byte[] data)
        {
            Debug.Assert(data?.Length > 0);
            _lodGroups.Clear();

            using var reader = new BinaryReader(new MemoryStream(data));
            // skip scene name string
            var s = reader.ReadInt32();
            reader.BaseStream.Position += s;
            // get number of LODs
            var numLODGroups = reader.ReadInt32();
            Debug.Assert(numLODGroups > 0);

            for (int i = 0; i < numLODGroups; ++i)
            {
                // LODグループ名を取得
                s = reader.ReadInt32();
                string lodGroupName;
                if (s > 0)
                {
                    var nameBytes = reader.ReadBytes(s);
                    lodGroupName = Encoding.UTF8.GetString(nameBytes);
                }
                else
                {
                    lodGroupName = $"lod_{ContentHelper.GetRandomString()}";
                }

                // このLODグループのメッシュ数を取得する
                var numMeshes = reader.ReadInt32();
                Debug.Assert(numMeshes > 0);
                var lods = ReadMeshLODs(numMeshes, reader);

                var lodGroup = new LODGroup() { Name = lodGroupName };
                lods.ForEach(lodGroup.LODs.Add);

                _lodGroups.Add(lodGroup);
            }
        }

        private static List<MeshLOD> ReadMeshLODs(int numMeshes, BinaryReader reader)
        {
            var lodIds = new List<int>();
            var lodList = new List<MeshLOD>();
            for (int i = 0; i < numMeshes; ++i)
            {
                ReadMeshes(reader, lodIds, lodList);
            }

            return lodList;
        }

        private static void ReadMeshes(BinaryReader reader, List<int> lodIds, List<MeshLOD> lodList)
        {
            // メッシュの名前を取得
            var s = reader.ReadInt32();
            string meshName;
            if (s > 0)
            {
                var nameBytes = reader.ReadBytes(s);
                meshName = Encoding.UTF8.GetString(nameBytes);
            }
            else
            {
                meshName = $"mesh_{ContentHelper.GetRandomString()}";
            }

            var mesh = new Mesh() { Name = meshName };

            var lodId = reader.ReadInt32();
            mesh.ElementSize = reader.ReadInt32();
            mesh.ElementsType = (ElementsType)reader.ReadInt32();
            mesh.PrimitiveTopology = PrimitiveTopology.TriangleList; // ContentTools currently only support triangle list meshes.
            mesh.VertexCount = reader.ReadInt32();
            mesh.IndexSize = reader.ReadInt32();
            mesh.IndexCount = reader.ReadInt32();
            var lodThreshold = reader.ReadSingle();

            var elementsBufferSize = mesh.ElementSize * mesh.VertexCount;
            var indexBufferSize = mesh.IndexSize * mesh.IndexCount;

            mesh.Positions = reader.ReadBytes(Mesh.PositionSize * mesh.VertexCount);
            mesh.Elements = reader.ReadBytes(elementsBufferSize);
            mesh.Indices = reader.ReadBytes(indexBufferSize);

            MeshLOD lod;
            if (ID.IsValid(lodId) && lodIds.Contains(lodId))
            {
                lod = lodList[lodIds.IndexOf(lodId)];
                Debug.Assert(lod != null);
            }
            else
            {
                lodIds.Add(lodId);
                lod = new MeshLOD() { Name = meshName, LodThreshold = lodThreshold };
                lodList.Add(lod);
            }

            lod.Meshes.Add(mesh);
        }

        public override bool Import(string file)
        {
            Debug.Assert(File.Exists(file));
            Debug.Assert(!string.IsNullOrEmpty(FullPath));
            var ext = Path.GetExtension(file).ToLower();

            if (ext == ".fbx")
            {
                return ImportFbx(file);
            }

            return false;
        }

        private bool ImportFbx(string file)
        {
            Logger.Log(MessageType.Info, $"FBXファイルのインポート: {file}");
            // エディタでは、インポートはプロジェクトを開いている間しか行われない。コマンドラインのインポーターは一時フォルダを差し替える。
            var tempPath = ContentImportEnvironment.TempFolder ?? Application.Current.Dispatcher.Invoke(() => Project.Current!.TempFolder);
            if (string.IsNullOrEmpty(tempPath)) return false;

            lock (_lock)
            {
                if (!Directory.Exists(tempPath)) Directory.CreateDirectory(tempPath);
            }

            var tempFile = $"{tempPath}{ContentHelper.GetRandomString()}.fbx";
            File.Copy(file, tempFile, true);

            bool result = false;
            try
            {
                // 失敗は例外ではなく戻り値で返る(ログには一時ファイルのパスで出るので、下で元のファイルのパスでも出す)。
                result = ContentToolsAPI.ImportFbx(tempFile, this);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
            }

            if (!result)
            {
                var msg = $"FBXファイルのインポートに失敗しました: {file}";
                Debug.WriteLine(msg);
                Logger.Log(MessageType.Error, msg);
            }

            if (ImportSettings.ImportEmbeddedTextures)
            {
                var embeddedMediaDir = $@"{tempPath}{Path.GetFileNameWithoutExtension(tempFile)}.fbm{Path.DirectorySeparatorChar}";
                if (Directory.Exists(embeddedMediaDir))
                {
                    Debug.Assert(!string.IsNullOrEmpty(FullPath));
                    var destinationFolder = Path.GetDirectoryName(FullPath);
                    Debug.Assert(destinationFolder != null);
                    // 一時フォルダはプロジェクトを閉じると消える(Project.Unload)ので、出力先の <FBXのアセット名>.fbm にコピーしてから
                    // そのコピーを取り込む(テクスチャの Sources がコピーを指し、開き直したあとも再インポートできる。#111)。
                    var copies = ContentHelper.CopyEmbeddedMedia(Directory.GetFiles(embeddedMediaDir), file, destinationFolder);
                    if (ContentImportEnvironment.ImportEmbeddedFiles is { } importEmbeddedFiles)
                    {
                        importEmbeddedFiles(copies, destinationFolder);
                    }
                    else
                    {
                        foreach (var copy in copies)
                        {
                            if (copy.Error is { } error) Logger.Log(MessageType.Error, error);
                        }
                        var copied = copies.Where(c => c.Error == null).Select(c => c.Copy).ToArray();
                        if (copied.Length > 0)
                        {
                            new ConfigureImportSettings(copied, destinationFolder).Import();
                        }
                    }
                }
            }

            return result;
        }

        public override bool Load(string file)
        {
            Debug.Assert(File.Exists(file));
            Debug.Assert(Path.GetExtension(file).ToLower() == AssetFileExtension);

            try
            {
                byte[] data;
                using (var reader = new BinaryReader(File.Open(file, FileMode.Open, FileAccess.Read)))
                {
                    ReadAssetFileHeader(reader);
                    ImportSettings.FromBinary(reader);
                    int dataLength = reader.ReadInt32();
                    Debug.Assert(dataLength > 0);
                    data = reader.ReadBytes(dataLength);
                }

                Debug.Assert(data.Length > 0);

                using (var reader = new BinaryReader(new MemoryStream(data)))
                {
                    LODGroup lodGroup = new();
                    lodGroup.Name = reader.ReadString();
                    var lodGroupCount = reader.ReadInt32();

                    for (int i = 0; i < lodGroupCount; ++i)
                    {
                        lodGroup.LODs.Add(BinaryToLOD(reader));
                    }

                    _lodGroups.Clear();
                    _lodGroups.Add(lodGroup);
                }

                return true;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"ファイルからのGeometryAssetのロードに失敗しました: {file}");
            }

            return false;
        }

        public override IEnumerable<string> Save(string file)
        {
            Debug.Assert(_lodGroups.Any());
            var savedFiles = new List<string>();
            if (!_lodGroups.Any()) return savedFiles;

            var path = Path.GetDirectoryName(file) + Path.DirectorySeparatorChar;
            var fileName = Path.GetFileNameWithoutExtension(file);

            try
            {
                foreach (var lodGroup in _lodGroups)
                {
                    Debug.Assert(lodGroup.LODs.Any());
                    // 最も詳細なLODの名前をファイル名に使用する。
                    var meshFileName = ContentHelper.SanitizeFileName(
                        path + fileName + ((_lodGroups.Count > 1) ? "_" + ((lodGroup.LODs.Count > 1) ? lodGroup.Name : lodGroup.LODs[0].Name) : string.Empty)) + AssetFileExtension;
                    // NOTE: 新しいアセットファイルごとに異なるidを作成する必要がありますが、
                    //       同じ名前のジオメトリ・アセットファイルがすでに存在する場合は、代わりにそのguidを使用します。
                    // NOTE: FixedGuid があればそれを使う(既定のジオメトリ。どの配置先でも同じ GUID にするため, #32)
                    Guid = FixedGuid ?? (TryGetAssetInfo(meshFileName) is AssetInfo info && info.Type == Type ? info.Guid : Guid.NewGuid());
                    byte[] data;
                    using (var writer = new BinaryWriter(new MemoryStream()))
                    {
                        writer.Write(lodGroup.Name);
                        writer.Write(lodGroup.LODs.Count);
                        var hashes = new List<byte>();
                        foreach (var lod in lodGroup.LODs)
                        {
                            LODToBinary(lod, writer, out var hash);
                            hashes.AddRange(hash);
                        }

                        Hash = ContentHelper.ComputeHash(hashes.ToArray());
                        data = ((MemoryStream)writer.BaseStream).ToArray();
                        Icon = GenerateIcons(lodGroup.LODs[0])[0];
                    }

                    Debug.Assert(data?.Length > 0);

                    using (var writer = new BinaryWriter(File.Open(meshFileName, FileMode.Create, FileAccess.Write)))
                    {
                        WriteAssetFileHeader(writer);
                        ImportSettings.ToBinary(writer);
                        writer.Write(data.Length);
                        writer.Write(data);
                    }

                    Logger.Log(MessageType.Info, $"Geometryを保存: {meshFileName}");
                    savedFiles.Add(meshFileName);
                }

                FullPath = file;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"GeometryAssetの保存に失敗しました: {file}");
            }

            return savedFiles;
        }

        /// <summary>
        /// ジオメトリをバイト配列にパックし、エンジンで使用できるようにする。
        /// </summary>
        /// <returns>
        /// 以下を含むバイト配列。
        /// struct{
        ///     u32 lod_count,
        ///     struct {
        ///         f32 lod_threshold,
        ///         u32 submesh_count,
        ///         u32 size_of_submeshes,
        ///         struct {
        ///             u32 element_size, u32 vertex_count,
        ///             u32 index_count, u32 elements_type, u32 primitive_topology
        ///             u8 positions[sizeof(f32) * 3 * vertex_count],     // sizeof(positions)は4バイトの倍数でなければならない。 必要であればパディングしてください。
        ///             u8 elements[sizeof(element_size) * vertex_count], // sizeof(elements)は4バイトの倍数でなければならない。 必要であればパディングしてください。
        ///             u8 indices[index_size * index_count]
        ///         } submeshes[submesh_count]
        ///     } mesh_lods[lod_count]
        /// } geometry;
        /// </returns>
        public override byte[] PackForEngine()
        {
            using var writer = new BinaryWriter(new MemoryStream());
            var lodGroup = GetLODGroup();
            Debug.Assert(lodGroup != null);
            writer.Write(lodGroup.LODs.Count);
            foreach (var lod in lodGroup.LODs)
            {
                writer.Write(lod.LodThreshold);
                writer.Write(lod.Meshes.Count);
                var sizeOfSubmeshesPosition = writer.BaseStream.Position;
                writer.Write(0);
                foreach (var mesh in lod.Meshes)
                {
                    writer.Write(mesh.ElementSize);
                    writer.Write(mesh.VertexCount);
                    writer.Write(mesh.IndexCount);
                    writer.Write((int)mesh.ElementsType);
                    writer.Write((int)mesh.PrimitiveTopology);
                    var alignedPositionBuffer = new byte[MathUtil.AlignSizeUp(mesh.Positions.Length, 4)];
                    Array.Copy(mesh.Positions, alignedPositionBuffer, mesh.Positions.Length);
                    var alignedElementBuffer = new byte[MathUtil.AlignSizeUp(mesh.Elements.Length, 4)];
                    Array.Copy(mesh.Elements, alignedElementBuffer, mesh.Elements.Length);
                    writer.Write(alignedPositionBuffer);
                    writer.Write(alignedElementBuffer);
                    writer.Write(mesh.Indices);
                }
                var endOfSubmeshes = writer.BaseStream.Position;
                var sizeOfSubmeshes = (int)(endOfSubmeshes - sizeOfSubmeshesPosition - sizeof(int));
                writer.BaseStream.Position = sizeOfSubmeshesPosition;
                writer.Write(sizeOfSubmeshes);
                writer.BaseStream.Position = endOfSubmeshes;
            }
            writer.Flush();
            var data = ((MemoryStream)writer.BaseStream).ToArray();
            Debug.Assert(data?.Length > 0);
            return data;
        }

        private void LODToBinary(MeshLOD lod, BinaryWriter writer, out byte[] hash)
        {
            writer.Write(lod.Name);
            writer.Write(lod.LodThreshold);
            writer.Write(lod.Meshes.Count);

            var meshDataBegin = writer.BaseStream.Position;

            foreach (var mesh in lod.Meshes)
            {
                writer.Write(mesh.Name);
                writer.Write(mesh.ElementSize);
                writer.Write((int)mesh.ElementsType);
                writer.Write((int)mesh.PrimitiveTopology);
                writer.Write(mesh.VertexCount);
                writer.Write(mesh.IndexSize);
                writer.Write(mesh.IndexCount);
                writer.Write(mesh.Positions);
                writer.Write(mesh.Elements);
                writer.Write(mesh.Indices);
            }

            var meshDataSize = writer.BaseStream.Position - meshDataBegin;
            Debug.Assert(meshDataSize > 0);
            var buffer = ((MemoryStream)writer.BaseStream).ToArray();
            hash = ContentHelper.ComputeHash(buffer, (int)meshDataBegin, (int)meshDataSize)!; // bufferは空でない(meshDataSize > 0)ためnullにならない
        }

        private MeshLOD BinaryToLOD(BinaryReader reader)
        {
            var lod = new MeshLOD();
            lod.Name = reader.ReadString();
            lod.LodThreshold = reader.ReadSingle();
            var meshCount = reader.ReadInt32();

            for (int i = 0; i < meshCount; ++i)
            {
                var mesh = new Mesh()
                {
                    Name = reader.ReadString(),
                    ElementSize = reader.ReadInt32(),
                    ElementsType = (ElementsType)reader.ReadInt32(),
                    PrimitiveTopology = (PrimitiveTopology)reader.ReadInt32(),
                    VertexCount = reader.ReadInt32(),
                    IndexSize = reader.ReadInt32(),
                    IndexCount = reader.ReadInt32()
                };

                mesh.Positions = reader.ReadBytes(Mesh.PositionSize * mesh.VertexCount);
                mesh.Elements = reader.ReadBytes(mesh.ElementSize * mesh.VertexCount);
                mesh.Indices = reader.ReadBytes(mesh.IndexSize * mesh.IndexCount);

                lod.Meshes.Add(mesh);
            }

            return lod;
        }

        /// <summary>
        /// LOD のアイコンを、専用の STA スレッドで描く(Primal Day 081)。終わるまで呼び出し元を止める。
        /// createIconPerSubmesh が false ならすべてのメッシュを描いた 1 枚、true ならサブメッシュごとに 1 枚を返す。
        /// NOTE: ビューも MeshRenderer もこのスレッドで作るので、UI スレッドの Dispatcher を使わず、結果の PNG のバイト列だけを返す。
        ///       描けなければ例外を投げる(呼び出し元が失敗として扱う)。
        /// </summary>
        internal static List<byte[]> GenerateIcons(MeshLOD lod, bool createIconPerSubmesh = false)
        {
            var width = ContentInfo.IconWidth * 4;
            var iconList = new List<byte[]>();
            var color = Application.Current?.TryFindResource("Editor.Window.GrayColor1") is Color resource ? resource : Colors.Black;
            Exception? failure = null;

            var thread = new Thread(() =>
            {
                try
                {
                    // NOTE: ViewModelでWPFコントロール（ビュー）を使用するのは良い習慣ではありません。
                    // しかし、スクリーンショットに使えるグラフィック・レンダラーがない限り、このケースは例外とする必要がある。
                    var view = new Editors.GeometryView()
                    {
                        Background = new SolidColorBrush(color),
                        DataContext = new Editors.MeshRenderer(lod, null),
                        Width = width,
                        Height = width,
                    };

                    for (int i = createIconPerSubmesh ? 0 : -1; i < lod.Meshes.Count; ++i)
                    {
                        view.SetGeometry(i);
                        view.Measure(new Size(width, width));
                        view.Arrange(new Rect(0, 0, width, width));
                        view.UpdateLayout();
                        // 4倍の大きさの画像を作成し、縮小したときにソフトになるようにする。
                        var bmp = new RenderTargetBitmap(width, width, 96, 96, PixelFormats.Default);
                        bmp.Render(view);
                        iconList.Add(BitmapHelper.CreateThumbnail(bmp, ContentInfo.IconWidth, ContentInfo.IconWidth));
                        if (i == -1) break;
                    }
                }
                catch (Exception ex)
                {
                    // NOTE: Primal はここで投げ直すが、このスレッドの未処理の例外はエディタを落とす。呼び出し元に渡す
                    failure = ex;
                }
                finally
                {
                    // このスレッドの Dispatcher(WPF のオブジェクトが作る)を止めて、リソースを残さない
                    Dispatcher.CurrentDispatcher.InvokeShutdown();
                }
            });

            thread.SetApartmentState(ApartmentState.STA);
            thread.IsBackground = true; // 生成の途中でも、エディタの終了を妨げない
            thread.Start();
            thread.Join();

            if (failure != null) throw new InvalidOperationException($"LOD {lod.Name} のアイコンを描けませんでした: {failure.Message}", failure);
            return iconList;
        }

        /// <summary>
        /// サブメッシュごとのアイコンを描いて、MeshInfo に設定する(スレッドプールから呼ぶ。Primal Day 081)。
        /// NOTE: 読む MeshLOD は UploadAssetToEngine が読み込んだアセットのもので、ほかから書き換えられない。結果を書くのは、
        ///       このアップロードのために作った MeshInfo だけ。再インポートや Unload のあとのアップロードは別の MeshInfo を作るので、
        ///       遅れて終わった結果が新しいものを上書きすることはない(使われなくなった MeshInfo に書くだけ)。
        /// </summary>
        private static void GenerateAndSetIcons(List<MeshInfo> meshes, MeshLOD lod)
        {
            try
            {
                var icons = GenerateIcons(lod, true);
                Debug.Assert(icons.Count == meshes.Count);
                // エディタの終了後は Dispatcher に積めない(積んでも実行されない)ので、何もしない
                var dispatcher = Application.Current?.Dispatcher;
                if (dispatcher == null || dispatcher.HasShutdownStarted) return;
                dispatcher.BeginInvoke(() =>
                {
                    for (int i = 0; i < meshes.Count && i < icons.Count; ++i)
                    {
                        meshes[i].Icon = icons[i];
                    }
                });
            }
            catch (Exception ex)
            {
                // 失敗してもアセット全体のアイコンのままで使える。未処理の例外でエディタを落とさない
                Debug.WriteLine(ex.Message);
                // NOTE: Logger.Log は止めた Dispatcher に積むとプロセスを落とすので、終了の途中なら出さない
                var dispatcher = Application.Current?.Dispatcher;
                if (dispatcher != null && !dispatcher.HasShutdownStarted)
                {
                    Logger.Log(MessageType.Warning, ex.Message);
                }
            }
        }

        /// <summary>
        /// LODとメッシュごとの情報。メッシュのアイコンは最初はアセット全体のアイコンで、別のスレッドで描いて置き換える(Primal Day 081)。
        /// NOTE: アイコンの生成はアセットの登録にもエンジンにも触らないので、ロック(#171)がなくても別のスレッドで動かせる。
        /// </summary>
        public override GeometryMetadata GetMetadata()
        {
            var lodGroup = GetLODGroup();
            if (lodGroup == null) return new();

            GeometryMetadata metadata = new() { Name = lodGroup.Name };

            foreach (var lod in lodGroup.LODs)
            {
                LodInfo lodInfo = new() { Name = lod.Name, Threshold = lod.LodThreshold };
                metadata.LODs.Add(lodInfo);

                for (int i = 0; i < lod.Meshes.Count; ++i)
                {
                    var mesh = lod.Meshes[i];
                    lodInfo.Meshes.Add(new()
                    {
                        Name = mesh.Name,
                        Icon = Icon ?? [],
                        IndexCount = mesh.IndexCount,
                        TriangleCount = mesh.IndexCount / 3,
                        VertexCount = mesh.VertexCount
                    });
                }

                _ = Task.Run(() => GenerateAndSetIcons(lodInfo.Meshes, lod));
            }

            return metadata;
        }

        public Geometry() : base(AssetType.Mesh) { }

        public Geometry(IAssetImportSettings importSettings) : this()
        {
            Debug.Assert(importSettings is GeometryImportSettings);
            ImportSettings = (GeometryImportSettings)importSettings;
        }

        /// <summary>
        /// アセットのファイルから読む(エンジンへのアップロード用。Primal Day 077)。読めなければ null。
        /// NOTE: Primal はコンストラクタで読み、読めなくても(中身が空のまま)オブジェクトを返す(Codexの指摘, #145)
        /// </summary>
        public static Geometry? FromAssetInfo(AssetInfo assetInfo)
        {
            if (!CanLoad(assetInfo, AssetType.Mesh)) return null;

            var asset = new Geometry();
            return asset.Load(assetInfo.FullPath) ? asset : null;
        }
    }
}
