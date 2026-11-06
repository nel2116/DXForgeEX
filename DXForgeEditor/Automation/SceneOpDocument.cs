using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace DXForgeEditor.Automation
{
    /// <summary>ops の失敗の分類。dxforge は、これを終了コード 40〜44 にする(Issue #183)。</summary>
    enum SceneOpErrorKind
    {
        InvalidOps,
        NotFound,
        OperationFailed,
        SaveFailed,
        EditorOpen,
    }

    class SceneOpException(SceneOpErrorKind kind, string code, string message, int? opIndex = null, string? key = null) : Exception(message)
    {
        public SceneOpErrorKind Kind { get; } = kind;
        public string Code { get; } = code;
        public int? OpIndex { get; } = opIndex;
        public string? Key { get; } = key;
    }

    /// <summary>1 つの操作(検査したあとのもの)。</summary>
    class SceneOp
    {
        public int Index { get; init; }
        public string Name { get; init; } = string.Empty;
        public string? Target { get; init; }
        public string? Scene { get; init; }
        public string? EntityName { get; init; }
        public string? Alias { get; init; }
        public bool? Enabled { get; init; }
        public Vector3? Position { get; init; }
        public Vector3? RotationRad { get; init; }
        public Vector3? Scale { get; init; }
        public string? ComponentType { get; init; }
        public string? Asset { get; init; }
        public string? Source { get; init; }
        public string? Folder { get; init; }
        /// <summary>import-asset の settings を、DXForgeImport の --set の形(Name=Value)にしたもの。</summary>
        public IReadOnlyList<string>? ImportAssignments { get; init; }
    }

    /// <summary>
    /// 操作の JSON を読んで検査する。実行の前に全体を検査するので、形式の誤りがあれば何も実行しない。
    /// 形の定義は DXForgeCli/Schema/ops.schema.json と同じ(一致していることを DXForgeCli/Tests/ops_cli_test.ps1 が確かめる)。
    /// </summary>
    static class SceneOpDocument
    {
        /// <summary>操作の名前と、受け付けるキー(op を除く)。</summary>
        internal static readonly IReadOnlyDictionary<string, string[]> AllowedKeys = new Dictionary<string, string[]>
        {
            ["list-scenes"] = [],
            ["list-entities"] = ["scene"],
            ["get-entity"] = ["target"],
            ["create-entity"] = ["as", "name", "scene"],
            ["remove-entity"] = ["target"],
            ["rename"] = ["name", "target"],
            ["set-enabled"] = ["enabled", "target"],
            ["set-transform"] = ["position", "rotation_deg", "rotation_rad", "scale", "target"],
            ["add-component"] = ["asset", "name", "target", "type"],
            ["remove-component"] = ["target", "type"],
            ["set-geometry"] = ["asset", "target"],
            ["create-scene"] = ["name"],
            ["remove-scene"] = ["scene"],
            ["set-active-scene"] = ["scene"],
            ["import-asset"] = ["folder", "settings", "source"],
        };

        /// <summary>操作の名前と、必須のキー(op を除く)。</summary>
        internal static readonly IReadOnlyDictionary<string, string[]> RequiredKeys = new Dictionary<string, string[]>
        {
            ["list-scenes"] = [],
            ["list-entities"] = [],
            ["get-entity"] = ["target"],
            ["create-entity"] = [],
            ["remove-entity"] = ["target"],
            ["rename"] = ["name", "target"],
            ["set-enabled"] = ["enabled", "target"],
            ["set-transform"] = ["target"],
            ["add-component"] = ["target", "type"],
            ["remove-component"] = ["target", "type"],
            ["set-geometry"] = ["asset", "target"],
            ["create-scene"] = ["name"],
            ["remove-scene"] = ["scene"],
            ["set-active-scene"] = ["scene"],
            ["import-asset"] = ["source"],
        };

        private static readonly HashSet<string> _readOnlyOps = ["list-scenes", "list-entities", "get-entity"];
        private static readonly Regex _aliasPattern = new("^[A-Za-z0-9_-]+$", RegexOptions.CultureInvariant);
        private static readonly Regex _importSettingPattern = new("^[A-Za-z0-9_]+$", RegexOptions.CultureInvariant);

        public static bool ChangesProject(SceneOp op) => !_readOnlyOps.Contains(op.Name);

        /// <summary>--ops のファイルの中身({"ops":[...]})を読む。</summary>
        public static List<SceneOp> ParseDocument(string json)
        {
            using var document = ParseJson(json);
            var root = document.RootElement;
            if (root.ValueKind != JsonValueKind.Object) throw Invalid("最上位は {\"ops\":[...]} のオブジェクトです。");
            CheckDuplicateKeys(root, null);
            foreach (var property in root.EnumerateObject())
            {
                if (property.Name == "$schema" && property.Value.ValueKind == JsonValueKind.String) continue;
                if (property.Name != "ops") throw Invalid($"{property.Name}: 知らないキーです。", null, property.Name);
            }
            if (!root.TryGetProperty("ops", out var ops)) throw Invalid("ops がありません。", null, "ops");
            if (ops.ValueKind != JsonValueKind.Array) throw Invalid("ops は配列です。", null, "ops");

            var aliases = new HashSet<string>();
            return ops.EnumerateArray().Select((x, i) => ParseOp(x, i, aliases)).ToList();
        }

        /// <summary>--op の 1 つの操作を読む。</summary>
        public static List<SceneOp> ParseSingle(string json)
        {
            using var document = ParseJson(json);
            return [ParseOp(document.RootElement, 0, [])];
        }

        private static JsonDocument ParseJson(string json)
        {
            try
            {
                return JsonDocument.Parse(json);
            }
            catch (JsonException ex)
            {
                throw Invalid($"JSON として読めません: {ex.Message}");
            }
        }

        private static SceneOp ParseOp(JsonElement element, int index, HashSet<string> aliases)
        {
            if (element.ValueKind != JsonValueKind.Object) throw Invalid($"ops[{index}]: 操作はオブジェクトです。", index);
            CheckDuplicateKeys(element, index);
            if (!element.TryGetProperty("op", out var opElement) || opElement.ValueKind != JsonValueKind.String)
            {
                throw Invalid($"ops[{index}].op: 操作の名前(文字列)がありません。", index, "op");
            }
            var name = opElement.GetString()!;
            if (!AllowedKeys.TryGetValue(name, out var allowed))
            {
                throw Invalid($"ops[{index}].op: 知らない操作です: {name}(使えるもの: {string.Join(", ", AllowedKeys.Keys)})", index, "op");
            }
            foreach (var property in element.EnumerateObject())
            {
                if (property.Name != "op" && !allowed.Contains(property.Name))
                {
                    throw Invalid($"ops[{index}].{property.Name}: {name} では使えないキーです(使えるもの: {string.Join(", ", allowed)})", index, property.Name);
                }
            }
            foreach (var key in RequiredKeys[name])
            {
                if (!element.TryGetProperty(key, out _)) throw Invalid($"ops[{index}].{key}: {name} に必要なキーがありません。", index, key);
            }

            var reader = new OpReader(element, index);
            var target = reader.Selector("target", aliases);
            var op = new SceneOp
            {
                Index = index,
                Name = name,
                Target = target,
                Scene = reader.NonEmptyString("scene"),
                EntityName = reader.NonEmptyString("name"),
                Alias = reader.NonEmptyString("as"),
                Enabled = reader.Bool("enabled"),
                Position = reader.Vector("position"),
                RotationRad = reader.Rotation(),
                Scale = reader.Vector("scale"),
                ComponentType = reader.ComponentType("type"),
                Asset = reader.Asset("asset"),
                Source = reader.AbsolutePath("source"),
                Folder = reader.ContentFolder("folder"),
                ImportAssignments = reader.ImportSettings("settings"),
            };

            switch (name)
            {
                case "create-entity":
                    if (op.Alias != null)
                    {
                        if (!_aliasPattern.IsMatch(op.Alias)) throw Invalid($"ops[{index}].as: 別名に使えるのは英数字と _ と - です: {op.Alias}", index, "as");
                        if (!aliases.Add(op.Alias)) throw Invalid($"ops[{index}].as: 別名が重複しています: {op.Alias}", index, "as");
                    }
                    break;
                case "set-transform":
                    if (op.Position == null && op.RotationRad == null && op.Scale == null)
                    {
                        throw Invalid($"ops[{index}]: set-transform には position, rotation_deg, rotation_rad, scale のどれかが必要です。", index);
                    }
                    break;
                case "add-component":
                    CheckAddComponent(op, element, index);
                    break;
            }
            return op;
        }

        private static void CheckAddComponent(SceneOp op, JsonElement element, int index)
        {
            var hasName = element.TryGetProperty("name", out _);
            var hasAsset = element.TryGetProperty("asset", out _);
            switch (op.ComponentType)
            {
                case "transform":
                    if (hasName) throw Invalid($"ops[{index}].name: transform には使えません。", index, "name");
                    if (hasAsset) throw Invalid($"ops[{index}].asset: transform には使えません。", index, "asset");
                    break;
                case "script":
                    if (!hasName) throw Invalid($"ops[{index}].name: script にはスクリプトの名前が必要です。", index, "name");
                    if (hasAsset) throw Invalid($"ops[{index}].asset: script には使えません。", index, "asset");
                    break;
                case "geometry":
                    if (!hasAsset) throw Invalid($"ops[{index}].asset: geometry にはアセットが必要です。", index, "asset");
                    if (hasName) throw Invalid($"ops[{index}].name: geometry には使えません。", index, "name");
                    break;
            }
        }

        // JsonDocument は重複したキーをそのまま受け付けるので、後ろの値が黙って使われないように、ここで断る
        private static void CheckDuplicateKeys(JsonElement element, int? index)
        {
            var seen = new HashSet<string>();
            foreach (var property in element.EnumerateObject())
            {
                if (!seen.Add(property.Name))
                {
                    var where = index == null ? property.Name : $"ops[{index}].{property.Name}";
                    throw Invalid($"{where}: キーが重複しています。", index, property.Name);
                }
            }
        }

        private static SceneOpException Invalid(string message, int? index = null, string? key = null) =>
            new(SceneOpErrorKind.InvalidOps, "ops_invalid", message, index, key);

        /// <summary>1 つの操作のキーの値を、型を確かめながら読む。</summary>
        private readonly struct OpReader(JsonElement element, int index)
        {
            private readonly JsonElement _element = element;
            private readonly int _index = index;

            private SceneOpException Error(string key, string message) => Invalid($"ops[{_index}].{key}: {message}", _index, key);

            public string? NonEmptyString(string key)
            {
                if (!_element.TryGetProperty(key, out var value)) return null;
                if (value.ValueKind != JsonValueKind.String) throw Error(key, "文字列です。");
                var text = value.GetString()!;
                if (string.IsNullOrWhiteSpace(text)) throw Error(key, "空にはできません。");
                return text;
            }

            public bool? Bool(string key)
            {
                if (!_element.TryGetProperty(key, out var value)) return null;
                if (value.ValueKind != JsonValueKind.True && value.ValueKind != JsonValueKind.False) throw Error(key, "true か false です。");
                return value.GetBoolean();
            }

            public Vector3? Vector(string key, Func<double, double>? convert = null)
            {
                if (!_element.TryGetProperty(key, out var value)) return null;
                if (value.ValueKind != JsonValueKind.Array || value.GetArrayLength() != 3 || value.EnumerateArray().Any(x => x.ValueKind != JsonValueKind.Number))
                {
                    throw Error(key, "3 つの数の配列です。");
                }
                var numbers = value.EnumerateArray().Select(x => (float)(convert ?? (y => y))(x.GetDouble())).ToArray();
                // float に収まらない値(1e39 など)は無限大になる
                if (numbers.Any(x => !float.IsFinite(x))) throw Error(key, "float の範囲に収まらない数があります。");
                return new Vector3(numbers[0], numbers[1], numbers[2]);
            }

            public Vector3? Rotation()
            {
                var degrees = Vector("rotation_deg", x => x * Math.PI / 180.0);
                var radians = Vector("rotation_rad");
                if (degrees != null && radians != null) throw Error("rotation_deg", "rotation_deg と rotation_rad は、どちらか一方だけです。");
                return degrees ?? radians;
            }

            public string? ComponentType(string key)
            {
                var text = NonEmptyString(key);
                if (text != null && text != "transform" && text != "script" && text != "geometry") throw Error(key, $"transform, script, geometry のどれかです: {text}");
                return text;
            }

            public string? Asset(string key)
            {
                var text = NonEmptyString(key);
                if (text == null || text == SceneOpRunner.DefaultCubeAsset) return text;
                if (text.StartsWith("guid:", StringComparison.Ordinal))
                {
                    if (!Guid.TryParse(text["guid:".Length..], out _)) throw Error(key, $"GUID として読めません: {text}");
                    return text;
                }
                if (text.StartsWith("path:", StringComparison.Ordinal))
                {
                    var path = text["path:".Length..].Replace('/', '\\');
                    if (path.Length == 0 || Path.IsPathRooted(path) || path.Split('\\').Contains(".."))
                    {
                        throw Error(key, $"Content フォルダからの相対パスです(.. や絶対パスは使えません): {text}");
                    }
                    return text;
                }
                throw Error(key, $"guid:<GUID>, path:<Content からの相対パス>, {SceneOpRunner.DefaultCubeAsset} のどれかです: {text}");
            }

            // NOTE: 相対パスを受け付けると、同じ JSON が現在のフォルダで別の意味になる(エディタの経路 #184 と共有できない)
            public string? AbsolutePath(string key)
            {
                var text = NonEmptyString(key);
                if (text != null && !Path.IsPathFullyQualified(text)) throw Error(key, $"絶対パスです: {text}");
                return text;
            }

            public string? ContentFolder(string key)
            {
                var text = NonEmptyString(key);
                if (text == null) return null;
                var path = text.Replace('/', '\\');
                if (Path.IsPathRooted(path) || path.Contains(':') || path.IndexOfAny(Path.GetInvalidPathChars()) >= 0 || path.Split('\\').Contains(".."))
                {
                    throw Error(key, $"Content フォルダからの相対パスです(.. や絶対パスは使えません): {text}");
                }
                return path;
            }

            public IReadOnlyList<string>? ImportSettings(string key)
            {
                if (!_element.TryGetProperty(key, out var value)) return null;
                if (value.ValueKind != JsonValueKind.Object) throw Error(key, "項目名と値のオブジェクトです。");
                CheckDuplicateKeys(value, _index);
                var assignments = new List<string>();
                foreach (var property in value.EnumerateObject())
                {
                    if (!_importSettingPattern.IsMatch(property.Name)) throw Error($"{key}.{property.Name}", "項目名に使えるのは英数字と _ です。");
                    assignments.Add(property.Value.ValueKind switch
                    {
                        JsonValueKind.String => $"{property.Name}={property.Value.GetString()}",
                        JsonValueKind.Number or JsonValueKind.True or JsonValueKind.False => $"{property.Name}={property.Value.GetRawText()}",
                        _ => throw Error($"{key}.{property.Name}", "値は文字列、数、true か false です。"),
                    });
                }
                return assignments;
            }

            public string? Selector(string key, HashSet<string> aliases)
            {
                var text = NonEmptyString(key);
                if (text == null) return null;
                if (text.StartsWith("guid:", StringComparison.Ordinal))
                {
                    if (!Guid.TryParse(text["guid:".Length..], out _)) throw Error(key, $"GUID として読めません: {text}");
                }
                else if (text.StartsWith("name:", StringComparison.Ordinal))
                {
                    if (text.Length == "name:".Length) throw Error(key, "name: のあとに名前が必要です。");
                }
                else if (text.StartsWith("index:", StringComparison.Ordinal))
                {
                    if (!TryParseIndexSelector(text, out _, out _)) throw Error(key, $"index:<シーン名>/<番号> です: {text}");
                }
                else if (text.StartsWith('$'))
                {
                    if (!aliases.Contains(text[1..])) throw Error(key, $"この操作より前の create-entity で定義されていない別名です: {text}");
                }
                else
                {
                    throw Error(key, $"guid:<GUID>, name:<名前>, index:<シーン名>/<番号>, $<別名> のどれかです: {text}");
                }
                return text;
            }
        }

        /// <summary>index:&lt;シーン名&gt;/&lt;番号&gt; を読む。シーン名に / があってもよいように、最後の / で区切る。</summary>
        internal static bool TryParseIndexSelector(string selector, out string scene, out int index)
        {
            var body = selector["index:".Length..];
            var slash = body.LastIndexOf('/');
            scene = slash > 0 ? body[..slash] : string.Empty;
            index = -1;
            return slash > 0 && int.TryParse(body[(slash + 1)..], NumberStyles.None, CultureInfo.InvariantCulture, out index);
        }
    }
}
