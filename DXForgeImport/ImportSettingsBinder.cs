using System.IO;
using System.Globalization;
using System.Reflection;
using System.Text.Json;
using DXForgeEditor.Content;

namespace DXForgeImport
{
    class InvalidSettingsException : Exception
    {
        public InvalidSettingsException(string message) : base(message) { }
    }

    /// <summary>
    /// インポート設定(JSONファイルと --set Name=Value)を、エディタのインポート設定のクラス
    /// (GeometryImportSettings / TextureImportSettings)に設定する。
    /// 項目はそのクラスの公開プロパティ(読み書きできるもの)をリフレクションで引くので、項目の一覧を二重に持たない。
    /// 指定しなかった項目は、エディタの既定値(各クラスのコンストラクタで設定される値)のまま。
    /// </summary>
    static class ImportSettingsBinder
    {
        public const string GeometrySection = "geometry";
        public const string TextureSection = "texture";

        public static IEnumerable<PropertyInfo> GetSettableProperties(IAssetImportSettings settings) =>
            settings.GetType().GetProperties(BindingFlags.Public | BindingFlags.Instance)
                .Where(p => p.CanRead && p.SetMethod?.IsPublic == true && p.GetIndexParameters().Length == 0)
                .Where(p => p.PropertyType == typeof(bool) || p.PropertyType == typeof(int) || p.PropertyType == typeof(float) || p.PropertyType.IsEnum);

        /// <summary>結果のJSONに書く、設定の全項目の値。</summary>
        public static Dictionary<string, object?> ToDictionary(IAssetImportSettings settings)
        {
            var values = new Dictionary<string, object?>();
            foreach (var property in GetSettableProperties(settings))
            {
                var value = property.GetValue(settings);
                values[property.Name] = property.PropertyType.IsEnum ? value?.ToString() : value;
            }

            if (settings is TextureImportSettings texture)
            {
                values[nameof(TextureImportSettings.Sources)] = texture.Sources.ToArray();
            }

            return values;
        }

        /// <summary>
        /// 設定ファイル(JSON)を読む。形式: { "geometry": { "項目名": 値, ... }, "texture": { ... } }(どちらも省略可)。
        /// 項目名は大文字・小文字を区別しない。どちらの節も、知らない項目や型の合わない値はエラーにする。
        /// </summary>
        public static void ApplyFile(string file, GeometryImportSettings geometry, TextureImportSettings texture)
        {
            JsonDocument document;
            try
            {
                document = JsonDocument.Parse(File.ReadAllText(file), new JsonDocumentOptions { CommentHandling = JsonCommentHandling.Skip, AllowTrailingCommas = true });
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException)
            {
                throw new InvalidSettingsException($"設定ファイルを読めません: {file} ({ex.Message})");
            }
            catch (JsonException ex)
            {
                throw new InvalidSettingsException($"設定ファイルがJSONとして正しくありません: {file} ({ex.Message})");
            }

            using (document)
            {
                if (document.RootElement.ValueKind != JsonValueKind.Object)
                {
                    throw new InvalidSettingsException($"設定ファイルの最上位はオブジェクトでなければなりません: {file}");
                }

                foreach (var section in document.RootElement.EnumerateObject())
                {
                    IAssetImportSettings target = section.Name.ToLowerInvariant() switch
                    {
                        GeometrySection => geometry,
                        TextureSection => texture,
                        _ => throw new InvalidSettingsException($"設定ファイルに知らない節があります: \"{section.Name}\"(\"{GeometrySection}\" か \"{TextureSection}\")"),
                    };

                    if (section.Value.ValueKind != JsonValueKind.Object)
                    {
                        throw new InvalidSettingsException($"設定ファイルの \"{section.Name}\" はオブジェクトでなければなりません。");
                    }

                    foreach (var item in section.Value.EnumerateObject())
                    {
                        Apply(target, section.Name, item.Name, item.Value);
                    }
                }
            }
        }

        /// <summary>引数の --set Name=Value を設定する。</summary>
        public static void ApplyAssignment(IAssetImportSettings settings, string sectionName, string assignment)
        {
            var index = assignment.IndexOf('=');
            if (index <= 0)
            {
                throw new InvalidSettingsException($"--set は Name=Value の形で指定してください: \"{assignment}\"");
            }

            var name = assignment[..index].Trim();
            var text = assignment[(index + 1)..].Trim();
            var property = FindProperty(settings, sectionName, name);
            object? value = null;
            var type = property.PropertyType;

            if (type == typeof(bool) && bool.TryParse(text, out var b)) value = b;
            else if (type == typeof(int) && int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var i)) value = i;
            else if (type == typeof(float) && float.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var f) && float.IsFinite(f)) value = f;
            else if (type.IsEnum) value = ParseEnum(type, text);

            if (value == null)
            {
                throw new InvalidSettingsException($"{sectionName}.{property.Name} の値が正しくありません: \"{text}\"({DescribeType(type)})");
            }

            property.SetValue(settings, value);
        }

        private static void Apply(IAssetImportSettings settings, string sectionName, string name, JsonElement element)
        {
            var property = FindProperty(settings, sectionName, name);
            object? value = null;
            var type = property.PropertyType;

            if (type == typeof(bool) && (element.ValueKind == JsonValueKind.True || element.ValueKind == JsonValueKind.False)) value = element.GetBoolean();
            else if (type == typeof(int) && element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out var i)) value = i;
            else if (type == typeof(float) && element.ValueKind == JsonValueKind.Number && element.TryGetSingle(out var f) && float.IsFinite(f)) value = f;
            else if (type.IsEnum && element.ValueKind == JsonValueKind.String) value = ParseEnum(type, element.GetString() ?? string.Empty);
            else if (type.IsEnum && element.ValueKind == JsonValueKind.Number && element.TryGetInt32(out var e)) value = ParseEnum(type, e.ToString(CultureInfo.InvariantCulture));

            if (value == null)
            {
                throw new InvalidSettingsException($"{sectionName}.{property.Name} の値が正しくありません: {element.GetRawText()}({DescribeType(type)})");
            }

            property.SetValue(settings, value);
        }

        private static PropertyInfo FindProperty(IAssetImportSettings settings, string sectionName, string name)
        {
            var properties = GetSettableProperties(settings).ToList();
            return properties.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase))
                ?? throw new InvalidSettingsException($"{sectionName} の設定に \"{name}\" という項目はありません。指定できる項目: {string.Join(", ", properties.Select(p => p.Name))}");
        }

        private static object? ParseEnum(Type type, string text)
        {
            // Enum.TryParse は定義されていない数値も受け付けるので、定義済みの値か確かめる。
            return Enum.TryParse(type, text, true, out var value) && Enum.IsDefined(type, value!) ? value : null;
        }

        private static string DescribeType(Type type) =>
            type == typeof(bool) ? "true か false" :
            type == typeof(int) ? "整数" :
            type == typeof(float) ? "数値" :
            type.IsEnum ? string.Join(", ", Enum.GetNames(type)) + " のどれか" :
            type.Name;
    }
}
