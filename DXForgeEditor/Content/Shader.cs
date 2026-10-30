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
        // 同じシェーダーグループを何度もエンジンに登録しないための参照カウント。
        // 同じかどうかは、種類・キー・ハッシュの並び(Signature)で決める。
        // NOTE: Primalはハッシュだけで決めるため、同じバイトコードでキーが違うグループに同じIDを返してしまい、
        //       エンジンが2つ目のキーのシェーダーを探すと見つからない(Codexの指摘, #29)。
        private class UploadedShaderGroup
        {
            public int ContentId { get; private set; } = ID.INVALID_ID;
            public byte[] Signature { get; private set; } = [];
            public int ReferenceCount { get; private set; }

            private static readonly Dictionary<string, UploadedShaderGroup> _uploadedShaders = [];
            private static readonly Dictionary<int, UploadedShaderGroup> _uploadedShaderIds = [];

            public static UploadedShaderGroup? UploadToEngine(ShaderGroup shaderGroup)
            {
                // NOTE: null を返すときは、このインスタンスが前に登録したグループの参照を必ず解放しておく
                //       (呼び出し側は ContentId を無効にするので、ここで解放しないと参照数が残る。Codexの指摘, #29)。
                if (!shaderGroup.IsCompiled)
                {
                    // 再コンパイルに失敗した場合など。内容が変わったときと同じく、前の登録を解放する
                    if (ID.IsValid(shaderGroup.ContentId) && _uploadedShaderIds.ContainsKey(shaderGroup.ContentId))
                    {
                        UnloadFromEngine(shaderGroup.ContentId);
                    }

                    return null;
                }

                var signature = GetSignature(shaderGroup);

                if (ID.IsValid(shaderGroup.ContentId) && _uploadedShaderIds.TryGetValue(shaderGroup.ContentId, out var uploadedShader))
                {
                    if (uploadedShader.Signature.SequenceEqual(signature))
                    {
                        // NOTE: このインスタンスは既にこのグループを参照しているので、参照数は増やさない。
                        //       Primalはここで増やすため、同じインスタンスで2回登録すると、1回の UnloadFromEngine では
                        //       参照数が0にならず、エンジンから解放されない(Codexの指摘, #29)。
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

                var hashString = Convert.ToBase64String(signature);

                if (_uploadedShaders.TryGetValue(hashString, out var identicalShader))
                {
                    ++identicalShader.ReferenceCount;
                    return identicalShader;
                }

                var contentId = AddShaderGroupToEngine(shaderGroup);
                if (!ID.IsValid(contentId)) return null;

                var newUploadedShader = new UploadedShaderGroup()
                {
                    ContentId = contentId,
                    Signature = signature,
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
                        RemoveShaderGroupFromEngine(uploadedShader.ContentId);
                        var hashString = Convert.ToBase64String(uploadedShader.Signature);
                        Debug.Assert(_uploadedShaders.ContainsKey(hashString));
                        _uploadedShaders.Remove(hashString);
                        _uploadedShaderIds.Remove(uploadedShader.ContentId);
                    }
                }
            }

            private static byte[] GetSignature(ShaderGroup shaderGroup)
            {
                using var stream = new MemoryStream();
                using (var writer = new BinaryWriter(stream))
                {
                    writer.Write((int)shaderGroup.Type);
                    shaderGroup.Keys.ForEach(writer.Write);
                    shaderGroup.Hash.ForEach(writer.Write);
                }

                return stream.ToArray();
            }

            private UploadedShaderGroup() { }
        }

        public static readonly int HashSize = 16;

        // NOTE: エンジンを初期化できない(#31 まで)ヘッドレスのテストで、マテリアルのアップロードの流れを確かめるため、
        //       エンジンへの登録と削除を差し替えられるようにしておく(material_asset_test.ps1)。
        internal static Func<ShaderGroup, int> AddShaderGroupToEngine { get; set; } = EngineAPI.AddShaderGroup;
        internal static Action<int> RemoveShaderGroupFromEngine { get; set; } = EngineAPI.RemoveShaderGroup;

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
                // 登録できなかったときは、前に登録したもの(コンパイルの失敗・内容の変更のどちらでも)は
                // UploadedShaderGroup.UploadToEngine が解放済みなので、二重に解放しないよう無効にする
                ContentId = ID.INVALID_ID;
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
