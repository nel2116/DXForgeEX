using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Numerics;
using System.Runtime.Serialization;
using System.Text.Json.Nodes;

namespace DXForgeEditor.Automation
{
    /// <summary>操作をすべて実行した結果。</summary>
    class SceneOpRunResult
    {
        /// <summary>操作ごとの結果。失敗したら、失敗した操作までが入る。</summary>
        public JsonArray Results { get; } = [];
        /// <summary>値を変えた操作があったか。失敗したときは何も反映しないので false。</summary>
        public bool Changed { get; set; }
        public SceneOpException? Error { get; set; }
    }

    /// <summary>
    /// シーンを操作する本体(Issue #183)。読み込んだ Project をメモリの上で直接変える(undo は積まない)。保存はしない。
    /// エンジンには触らない: ヘッドレスではエンティティはアクティブにならず、IsActive を変える既存の入口(Scene.AddGameEntity、
    /// GameEntity.RemoveComponent)は使わない。
    /// </summary>
    class SceneOpRunner(Project project, string projectFile, SceneOpAssetStaging assets)
    {
        public const string DefaultCubeAsset = "default:cube";

        private readonly Project _project = project;
        private readonly string _contentFolder = Path.Combine(Path.GetDirectoryName(projectFile) ?? string.Empty, "Content");
        private readonly SceneOpAssetStaging _assets = assets;
        private readonly Dictionary<string, GameEntity> _aliases = [];
        private Dictionary<Guid, AssetInfo>? _assetsByGuid;

        public SceneOpRunResult Run(IEnumerable<SceneOp> ops)
        {
            var result = new SceneOpRunResult();
            foreach (var op in ops)
            {
                var opResult = new JsonObject { ["index"] = op.Index, ["op"] = op.Name };
                result.Results.Add(opResult);
                try
                {
                    var warnings = new JsonArray();
                    var changed = Execute(op, opResult, warnings);
                    opResult["ok"] = true;
                    opResult["changed"] = changed;
                    opResult["warnings"] = warnings;
                    result.Changed |= changed;
                }
                catch (SceneOpException ex)
                {
                    opResult["ok"] = false;
                    opResult["error"] = new JsonObject { ["code"] = ex.Code, ["message"] = ex.Message };
                    result.Error = new SceneOpException(ex.Kind, ex.Code, $"ops[{op.Index}] {op.Name}: {ex.Message}", op.Index, ex.Key);
                    result.Changed = false;
                    return result;
                }
            }
            return result;
        }

        private bool Execute(SceneOp op, JsonObject result, JsonArray warnings)
        {
            switch (op.Name)
            {
                case "list-scenes":
                    result["scenes"] = new JsonArray(_project.Scenes.Select(x => (JsonNode)ToJson(x)).ToArray());
                    return false;
                case "list-entities":
                    {
                        var scenes = op.Scene != null ? [FindScene(op.Scene)] : _project.Scenes.ToList();
                        result["entities"] = new JsonArray(scenes.SelectMany(x => x.GameEntities).Select(x => (JsonNode)ToJson(x)).ToArray());
                        return false;
                    }
                case "get-entity":
                    result["entity"] = ToJson(Resolve(op));
                    return false;
                case "create-entity":
                    {
                        var scene = op.Scene != null ? FindScene(op.Scene) : _project.ActiveScene
                            ?? throw new SceneOpException(SceneOpErrorKind.NotFound, "scene_not_found", "アクティブなシーンがありません(scene で指定してください)。", key: "scene");
                        var entity = new GameEntity(scene) { Name = op.EntityName ?? "Empty Entity" };
                        scene.AddEntityWithoutEngine(entity);
                        if (op.Alias != null) _aliases[op.Alias] = entity;
                        result["entity"] = ToJson(entity);
                        return true;
                    }
                case "remove-entity":
                    {
                        var entity = Resolve(op);
                        result["entity"] = ToJson(entity);
                        entity.ParentScene.RemoveEntityWithoutEngine(entity);
                        return true;
                    }
                case "rename":
                    {
                        var entity = Resolve(op);
                        var changed = entity.Name != op.EntityName;
                        entity.Name = op.EntityName!;
                        result["entity"] = ToJson(entity);
                        return changed;
                    }
                case "set-enabled":
                    {
                        var entity = Resolve(op);
                        var changed = entity.IsEnabled != op.Enabled;
                        entity.IsEnabled = op.Enabled!.Value;
                        result["entity"] = ToJson(entity);
                        return changed;
                    }
                case "set-transform":
                    return SetTransform(op, result);
                case "add-component":
                    return AddComponent(op, result, warnings);
                case "remove-component":
                    return RemoveComponent(op, result);
                case "set-geometry":
                    return SetGeometry(op, result);
                case "create-scene":
                    return CreateScene(op, result);
                case "remove-scene":
                    return RemoveScene(op, result);
                case "set-active-scene":
                    return SetActiveScene(op, result);
                case "import-asset":
                    _assets.Import(op, result, warnings);
                    return true;
                default:
                    throw new InvalidOperationException($"Unknown op: {op.Name}"); // SceneOpDocument が断っている
            }
        }

        private bool SetTransform(SceneOp op, JsonObject result)
        {
            var entity = Resolve(op);
            var transform = entity.GetComponent<Transform>()
                ?? throw new SceneOpException(SceneOpErrorKind.OperationFailed, "component_not_found", $"エンティティ {entity.Name} に Transform がありません。");
            var position = op.Position ?? transform.Position;
            var rotation = op.RotationDeg ?? transform.RotationEulerHint;
            var scale = op.Scale ?? transform.Scale;
            var changed = position != transform.Position || rotation != transform.RotationEulerHint || scale != transform.Scale;
            transform.Position = position;
            // 度数法の手がかりとクォータニオンの両方を書く
            if (op.RotationDeg != null) transform.RotationEulerHint = rotation;
            transform.Scale = scale;
            result["entity"] = ToJson(entity);
            return changed;
        }

        private bool AddComponent(SceneOp op, JsonObject result, JsonArray warnings)
        {
            var entity = Resolve(op);
            var kind = Enum.Parse<ComponentType>(op.ComponentType!, ignoreCase: true);
            if (entity.Components.Any(x => x.ToEnumType() == kind))
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "component_exists", $"エンティティ {entity.Name} はすでに {op.ComponentType} を持っています。", key: "type");
            }

            object data = kind switch
            {
                ComponentType.Script => op.EntityName!,
                ComponentType.Geometry => ResolveMesh(op.Asset!),
                _ => new object(),
            };
            entity.AddComponent(ComponentFactory.GetCreationFunction(kind)(entity, data));
            if (kind == ComponentType.Script)
            {
                // ゲームのコードの DLL を読まないので、登録されているスクリプトかは分からない
                warnings.Add((JsonNode?)$"スクリプト {op.EntityName} が登録されているかは確かめていません(未確認)。");
            }
            result["entity"] = ToJson(entity);
            return true;
        }

        private bool RemoveComponent(SceneOp op, JsonObject result)
        {
            var entity = Resolve(op);
            var kind = Enum.Parse<ComponentType>(op.ComponentType!, ignoreCase: true);
            if (kind == ComponentType.Transform)
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "transform_not_removable", "Transform は削除できません。", key: "type");
            }
            var component = entity.Components.FirstOrDefault(x => x.ToEnumType() == kind)
                ?? throw new SceneOpException(SceneOpErrorKind.OperationFailed, "component_not_found", $"エンティティ {entity.Name} は {op.ComponentType} を持っていません。", key: "type");
            entity.RemoveComponentWithoutEngine(component);
            result["entity"] = ToJson(entity);
            return true;
        }

        private bool SetGeometry(SceneOp op, JsonObject result)
        {
            var entity = Resolve(op);
            var geometry = entity.GetComponent<Components.Geometry>()
                ?? throw new SceneOpException(SceneOpErrorKind.OperationFailed, "component_not_found", $"エンティティ {entity.Name} は geometry を持っていません(先に add-component)。");
            var asset = ResolveMesh(op.Asset!);
            var changed = geometry.GeometryGuid != asset.Guid;
            geometry.SetGeometry(asset.Guid);
            result["entity"] = ToJson(entity);
            return changed;
        }

        private bool CreateScene(SceneOp op, JsonObject result)
        {
            // NOTE: 同じ名前のシーンは FindScene が 1 つに決められず、以後 scene で指せなくなる
            if (_project.Scenes.Any(x => x.Name == op.EntityName))
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "scene_exists", $"シーン {op.EntityName} はすでにあります。", key: "name");
            }
            _project.AddScene(op.EntityName!);
            result["scene"] = ToJson(_project.Scenes.Last());
            return true;
        }

        private bool RemoveScene(SceneOp op, JsonObject result)
        {
            var scene = FindScene(op.Scene!);
            if (_project.Scenes.Count == 1)
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "last_scene", $"最後のシーンは削除できません: {scene.Name}", key: "scene");
            }
            // エディタの RemoveSceneCommand も、アクティブなシーンの削除を禁じている
            if (scene.IsActive || scene == _project.ActiveScene)
            {
                throw new SceneOpException(SceneOpErrorKind.OperationFailed, "scene_active", $"アクティブなシーンは削除できません(先に set-active-scene で別のシーンをアクティブにしてください): {scene.Name}", key: "scene");
            }
            result["scene"] = ToJson(scene);
            _project.RemoveScene(scene);
            return true;
        }

        private bool SetActiveScene(SceneOp op, JsonObject result)
        {
            var target = FindScene(op.Scene!);
            var changed = false;
            foreach (var scene in _project.Scenes)
            {
                var active = scene == target;
                changed |= scene.IsActive != active;
                scene.IsActive = active;
            }
            _project.ActiveScene = target;
            result["scene"] = ToJson(target);
            return changed;
        }

        private Scene FindScene(string name)
        {
            var matches = _project.Scenes.Where(x => x.Name == name).ToList();
            if (matches.Count == 0) throw new SceneOpException(SceneOpErrorKind.NotFound, "scene_not_found", $"シーン {name} がありません(シーン: {string.Join(", ", _project.Scenes.Select(x => x.Name))})", key: "scene");
            // NOTE: シーンの名前の重複は禁じられていない。先頭のシーンを選ぶと、別のシーンのエンティティを変えてしまう
            if (matches.Count > 1) throw new SceneOpException(SceneOpErrorKind.NotFound, "ambiguous_target", $"シーン {name} が {matches.Count} 個あるので 1 つに決まりません(エンティティは guid: や name: でも指定できます)。", key: "scene");
            return matches[0];
        }

        private GameEntity Resolve(SceneOp op)
        {
            var selector = op.Target!;
            if (selector.StartsWith('$'))
            {
                var entity = _aliases[selector[1..]]; // 未定義の別名は SceneOpDocument が断っている
                // シーンごと消したときは、シーンの一覧にまだエンティティが残っている
                if (!_project.Scenes.Contains(entity.ParentScene) || !entity.ParentScene.GameEntities.Contains(entity))
                {
                    throw NotFound($"別名 {selector} のエンティティは、この ops の中で削除されています。");
                }
                return entity;
            }
            if (selector.StartsWith("index:", StringComparison.Ordinal))
            {
                SceneOpDocument.TryParseIndexSelector(selector, out var sceneName, out var index);
                var scene = FindScene(sceneName);
                if (index >= scene.GameEntities.Count) throw NotFound($"シーン {sceneName} に {index} 番のエンティティはありません({scene.GameEntities.Count} 個)。");
                return scene.GameEntities[index];
            }

            var all = _project.Scenes.SelectMany(x => x.GameEntities);
            var matches = selector.StartsWith("guid:", StringComparison.Ordinal)
                ? all.Where(x => x.EntityGuid == Guid.Parse(selector["guid:".Length..])).ToList()
                : all.Where(x => x.Name == selector["name:".Length..]).ToList();
            if (matches.Count == 0) throw NotFound($"エンティティが見つかりません: {selector}");
            if (matches.Count > 1)
            {
                throw new SceneOpException(SceneOpErrorKind.NotFound, "ambiguous_target",
                    $"{selector} に {matches.Count} 個のエンティティが一致します(guid: で指定してください): {string.Join(", ", matches.Select(x => x.EntityGuid))}", key: "target");
            }
            return matches[0];
        }

        private static SceneOpException NotFound(string message) => new(SceneOpErrorKind.NotFound, "target_not_found", message, key: "target");

        private AssetInfo ResolveMesh(string asset)
        {
            AssetInfo? info;
            if (asset == DefaultCubeAsset)
            {
                info = DefaultCube();
            }
            else if (asset.StartsWith("guid:", StringComparison.Ordinal))
            {
                var guid = Guid.Parse(asset["guid:".Length..]);
                info = guid == DefaultAssets.DefaultGeometryGuid ? DefaultCube() : FindAssetByGuid(guid) ?? _assets.FindStaged(guid);
            }
            else
            {
                var relative = asset["path:".Length..].Replace('/', '\\');
                info = Content.Asset.TryGetAssetInfo(Path.Combine(_contentFolder, relative)) ?? _assets.FindStaged(relative);
            }

            if (info == null) throw new SceneOpException(SceneOpErrorKind.NotFound, "asset_not_found", $"アセットが見つかりません: {asset}({_contentFolder})", key: "asset");
            if (info.Type != AssetType.Mesh) throw new SceneOpException(SceneOpErrorKind.OperationFailed, "asset_not_mesh", $"アセットがジオメトリではありません: {asset}({info.Type})", key: "asset");
            return info;
        }

        // NOTE: 既定のキューブのファイルはエディタの Resources にあり、エンジンのパスが決まるまで作られない。保存するのは GUID だけなので、ファイルは見ない
        private static AssetInfo DefaultCube() => new() { Type = AssetType.Mesh, Guid = DefaultAssets.DefaultGeometryGuid };

        /// <summary>
        /// Content フォルダの .asset のヘッダーを読んで探す。.dxproj に保存された Path は古いことがあるので(Project.ContentPath ではなく)
        /// 渡された .dxproj のフォルダを見る。AssetRegistry は ContentWatcher と結びついているので使わない。
        /// </summary>
        private AssetInfo? FindAssetByGuid(Guid guid)
        {
            if (_assetsByGuid == null)
            {
                _assetsByGuid = [];
                if (Directory.Exists(_contentFolder))
                {
                    foreach (var file in Directory.EnumerateFiles(_contentFolder, "*" + Content.Asset.AssetFileExtension, SearchOption.AllDirectories))
                    {
                        if (Content.Asset.TryGetAssetInfo(file) is AssetInfo info) _assetsByGuid.TryAdd(info.Guid, info);
                    }
                }
            }
            return _assetsByGuid.GetValueOrDefault(guid);
        }

        private static JsonObject ToJson(Scene scene) => new()
        {
            ["name"] = scene.Name,
            ["active"] = scene.IsActive,
            ["entity_count"] = scene.GameEntities.Count,
        };

        /// <summary>
        /// エンティティの結果の形(get-entity など)。回転は度数法のオイラー角(rotation_deg、Transform の手がかり)と、
        /// エンジンに渡すクォータニオン(rotation_quat、x, y, z, w)。ラジアンは出さない(#205)。
        /// </summary>
        private static JsonObject ToJson(GameEntity entity)
        {
            var components = new JsonArray();
            foreach (var component in entity.Components)
            {
                var json = new JsonObject { ["type"] = component.ToEnumType().ToString().ToLowerInvariant() };
                switch (component)
                {
                    case Transform transform:
                        json["position"] = ToJson(transform.Position);
                        json["rotation_deg"] = ToJson(transform.RotationEulerHint);
                        json["rotation_quat"] = new JsonArray(Number(transform.RotationQuaternion.X), Number(transform.RotationQuaternion.Y),
                            Number(transform.RotationQuaternion.Z), Number(transform.RotationQuaternion.W));
                        json["scale"] = ToJson(transform.Scale);
                        break;
                    case Script script:
                        json["name"] = script.Name;
                        break;
                    case Components.Geometry geometry:
                        json["geometry"] = geometry.GeometryGuid.ToString();
                        json["materials"] = geometry.GetMaterialsData().Count;
                        break;
                }
                components.Add(json);
            }
            return new JsonObject
            {
                ["guid"] = entity.EntityGuid.ToString(),
                ["name"] = entity.Name,
                ["enabled"] = entity.IsEnabled,
                ["scene"] = entity.ParentScene.Name,
                ["index"] = entity.ParentScene.GameEntities.IndexOf(entity),
                ["components"] = components,
            };
        }

        private static JsonArray ToJson(Vector3 v) => new(Number(v.X), Number(v.Y), Number(v.Z));

        // float を double にすると 0.1 が 0.10000000149011612 になるので、float の最短の表記を保つ
        private static JsonNode Number(float value) => JsonValue.Create(double.Parse(value.ToString("R", CultureInfo.InvariantCulture), CultureInfo.InvariantCulture));

    }

    /// <summary>プロジェクトをファイルに保存する(Issue #183)。</summary>
    static class SceneOpProjectWriter
    {
        /// <summary>
        /// 実行ごとに別の名前にする。同じ名前だと、並行で実行したほかの dxforge が書いた内容を置き換えてしまう。
        /// File.Move が同じボリュームで済むように、同じフォルダに作る。
        /// </summary>
        internal static string TemporaryFile(string file) => $"{file}.{Guid.NewGuid():N}.dxforge-tmp";

        /// <summary>
        /// 一時ファイルに書いてから置き換える。途中で落ちても元の .dxproj は壊れない。失敗したら元の .dxproj は変わらない。
        /// 保存先は渡されたファイルで、Project.FullPath(.dxproj に保存された Path は古いことがある)は使わない。
        /// NOTE: 同じプロジェクトに並行で実行すると、あとから保存したものが勝つ(先の変更は消える)。これを防ぐ排他はしない。
        /// </summary>
        public static void Save(Project project, string file)
        {
            var temp = TemporaryFile(file);
            try
            {
                Serializer.ToFile(project, temp);
                File.Move(temp, file, overwrite: true);
            }
            catch (Exception ex) when (ex is IOException || ex is UnauthorizedAccessException || ex is SerializationException)
            {
                try
                {
                    if (File.Exists(temp)) File.Delete(temp);
                }
                catch (Exception deleteError) when (deleteError is IOException || deleteError is UnauthorizedAccessException)
                {
                    Logger.Log(MessageType.Warning, $"一時ファイルを削除できませんでした: {temp}({deleteError.Message})");
                }
                throw new SceneOpException(SceneOpErrorKind.SaveFailed, "save_failed", $".dxproj を保存できません: {file}({ex.Message})");
            }
        }
    }
}
