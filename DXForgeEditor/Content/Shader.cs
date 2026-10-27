using DXForgeEditor.DllWrappers;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace DXForgeEditor.Content
{
    [Flags]
    enum ShaderFlags : int
    {
        None = 0x0,
        Vertex = 0x01,
        Hull = 0x02,
        Domain = 0x04,
        Geometry = 0x08,
        Pixel = 0x10,
        Compute = 0x20,
        Amplification = 0x40,
        Mesh = 0x80,
    }

    // NOTE: エンジンの graphics::shader_type(Graphics/Renderer.h)と同じ値
    enum ShaderType : int
    {
        Vertex = 0,
        Hull,
        Domain,
        Geometry,
        Pixel,
        Compute,
        Amplification,
        Mesh,
    }

    /// <summary>
    /// 1つのソース・関数から、引数(ExtraArgs)の組ごとにコンパイルしたシェーダーの集まり(Primal Day 076)。
    /// エンジンには、キー(Keys。頂点シェーダーなら頂点要素の種類)ごとに1つのシェーダーグループとして登録する。
    /// </summary>
    class ShaderGroup
    {
        // 同じシェーダー(ハッシュが同じもの)を何度もエンジンに登録しないための参照カウント
        private class UploadedShaderGroup
        {
            public int ContentId { get; private set; } = ID.INVALID_ID;
            public byte[] CombinedHashes { get; private set; } = [];
            public int ReferenceCount { get; private set; }

            private static readonly Dictionary<string, UploadedShaderGroup> _uploadedShaders = [];
            private static readonly Dictionary<int, UploadedShaderGroup> _uploadedShaderIds = [];

            public static UploadedShaderGroup? UploadToEngine(ShaderGroup shaderGroup)
            {
                if (!shaderGroup.IsCompiled) return null;

                var combinedHashes = shaderGroup.Hash.SelectMany(x => x).ToArray();

                if (ID.IsValid(shaderGroup.ContentId) && _uploadedShaderIds.TryGetValue(shaderGroup.ContentId, out var uploadedShader))
                {
                    if (uploadedShader.CombinedHashes.SequenceEqual(combinedHashes))
                    {
                        ++uploadedShader.ReferenceCount;
                        return uploadedShader;
                    }
                    else
                    {
                        UnloadFromEngine(uploadedShader.ContentId);
                    }
                }
                else
                {
                    Debug.Assert(!ID.IsValid(shaderGroup.ContentId));
                }

                var hashString = Convert.ToBase64String(combinedHashes);

                if (_uploadedShaders.TryGetValue(hashString, out var identicalShader))
                {
                    ++identicalShader.ReferenceCount;
                    return identicalShader;
                }

                var contentId = EngineAPI.AddShaderGroup(shaderGroup);
                if (!ID.IsValid(contentId)) return null;

                var newUploadedShader = new UploadedShaderGroup()
                {
                    ContentId = contentId,
                    CombinedHashes = combinedHashes,
                    ReferenceCount = 1
                };

                _uploadedShaders.Add(hashString, newUploadedShader);
                _uploadedShaderIds.Add(newUploadedShader.ContentId, newUploadedShader);

                return newUploadedShader;
            }

            public static void UnloadFromEngine(int id)
            {
                Debug.Assert(ID.IsValid(id) && _uploadedShaderIds.ContainsKey(id));

                if (ID.IsValid(id) && _uploadedShaderIds.TryGetValue(id, out var uploadedShader))
                {
                    Debug.Assert(uploadedShader.ReferenceCount > 0);
                    --uploadedShader.ReferenceCount;

                    if (uploadedShader.ReferenceCount == 0)
                    {
                        EngineAPI.RemoveShaderGroup(uploadedShader.ContentId);
                        var hashString = Convert.ToBase64String(uploadedShader.CombinedHashes);
                        Debug.Assert(_uploadedShaders.ContainsKey(hashString));
                        _uploadedShaders.Remove(hashString);
                        _uploadedShaderIds.Remove(uploadedShader.ContentId);
                    }
                }
            }

            private UploadedShaderGroup() { }
        }

        public static readonly int HashSize = 16;

        public ShaderType Type { get; set; }
        public string Code { get; set; } = string.Empty;
        public string FunctionName { get; set; } = string.Empty;
        public List<List<string>> ExtraArgs { get; set; } = [];
        public List<uint> Keys { get; set; } = [];
        public List<byte[]> ByteCode { get; set; } = [];
        public List<string> Errors { get; set; } = [];
        public List<string> Assembly { get; set; } = [];
        public List<byte[]> Hash { get; set; } = [];

        public int ContentId { get; private set; } = ID.INVALID_ID;

        public int Count
        {
            get
            {
                Debug.Assert(new int[] { ExtraArgs.Count, Keys.Count, ByteCode.Count, Errors.Count, Assembly.Count, Hash.Count }.Distinct().Count() == 1);
                return Keys.Count;
            }
        }

        /// <summary>
        /// すべての変種がコンパイルできている(エンジンに登録できる)かどうか。
        /// </summary>
        public bool IsCompiled => Keys.Count > 0 && ByteCode.Count == Keys.Count && Hash.Count == Keys.Count &&
            ByteCode.All(x => x.Length > 0) && Hash.All(x => x.Length == HashSize);

        public void ToBinary(BinaryWriter writer)
        {
            writer.Write((int)Type);
            writer.Write(Code);
            writer.Write(FunctionName);
            writer.Write(Count);

            ExtraArgs.ForEach(args => writer.Write(string.Join(";", args)));
            PackForEngine(writer);
            Errors.ForEach(writer.Write);
            Assembly.ForEach(writer.Write);
        }

        public void FromBinary(BinaryReader reader)
        {
            ExtraArgs.Clear();
            Keys.Clear();
            ByteCode.Clear();
            Errors.Clear();
            Assembly.Clear();
            Hash.Clear();

            Type = (ShaderType)reader.ReadInt32();
            Code = reader.ReadString();
            FunctionName = reader.ReadString();
            var count = reader.ReadInt32();

            ExtraArgs.AddRange(Enumerable.Range(0, count).Select(_ => reader.ReadString().Split(';', StringSplitOptions.RemoveEmptyEntries).ToList()));
            Keys.AddRange(Enumerable.Range(0, count).Select(_ => reader.ReadUInt32()));

            for (int i = 0; i < count; i++)
            {
                // NOTE: バイトコードの長さは64ビットの値
                var byteCodeLength = reader.ReadInt64();
                if (byteCodeLength > 0)
                {
                    Hash.Add(reader.ReadBytes(HashSize));
                    ByteCode.Add(reader.ReadBytes((int)byteCodeLength));
                }
                else
                {
                    // NOTE: Primalはここで何も追加しないため、コンパイルに失敗した変種があると Count の各リストの数がずれる
                    Hash.Add([]);
                    ByteCode.Add([]);
                }
            }

            Errors.AddRange(Enumerable.Range(0, count).Select(_ => reader.ReadString()));
            Assembly.AddRange(Enumerable.Range(0, count).Select(_ => reader.ReadString()));
        }

        private void PackForEngine(BinaryWriter writer)
        {
            Keys.ForEach(writer.Write);

            for (int i = 0; i < Count; i++)
            {
                // NOTE: バイトコードの長さは64ビットの値
                var byteCodeLength = ByteCode[i].LongLength;
                writer.Write(byteCodeLength);
                if (byteCodeLength > 0)
                {
                    writer.Write(Hash[i]);
                    writer.Write(ByteCode[i]);
                }
            }
        }

        /// <summary>
        /// エンジンの AddShaderGroup に渡す形式(keys[count], { u64 バイトコードの長さ, ハッシュ, バイトコード }[count])に詰める。
        /// コンパイルできていない変種があれば null。
        /// </summary>
        public byte[]? PackForEngine()
        {
            if (!IsCompiled) return null;

            using var stream = new MemoryStream();
            using (var writer = new BinaryWriter(stream))
            {
                PackForEngine(writer);
            }

            return stream.ToArray();
        }

        public int UploadToEngine()
        {
            var uploadedShader = UploadedShaderGroup.UploadToEngine(this);

            if (uploadedShader == null || !ID.IsValid(uploadedShader.ContentId))
            {
                Logger.Log(MessageType.Error, $"シェーダー {FunctionName} をエンジンに登録できませんでした。");
                return ID.INVALID_ID;
            }

            ContentId = uploadedShader.ContentId;

            return ContentId;
        }

        public void UnloadFromEngine()
        {
            if (ID.IsValid(ContentId))
            {
                UploadedShaderGroup.UnloadFromEngine(ContentId);
                ContentId = ID.INVALID_ID;
            }
        }
    }
}
