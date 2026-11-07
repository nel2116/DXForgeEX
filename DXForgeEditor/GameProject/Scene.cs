using DXForgeEditor.Components;
using DXForgeEditor.Content;
using DXForgeEditor.Utilities;
using System;
using System.CodeDom;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Configuration;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Windows.Input;

namespace DXForgeEditor.GameProject
{
    [DataContract]
    class Scene : ViewModelBase
    {
        private string _name = string.Empty;
        [DataMember]
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
        [DataMember]
        public Project Project { get; private set; }

        private bool _isActive;

        [DataMember]
        public bool IsActive
        {
            get => _isActive;
            set
            {
                if (_isActive != value)
                {
                    _isActive = value;
                    OnPropertyChanged(nameof(IsActive));
                }
            }
        }
        [DataMember(Name = "GameEntities")]
        private readonly ObservableCollection<GameEntity> _gameEntities = new();
        // Initialized synchronously by OnDeserialized during construction/loading.
        public ReadOnlyObservableCollection<GameEntity> GameEntities { get; private set; } = null!;

        // The commands are initialized synchronously by OnDeserialized.
        public ICommand RenameCommand { get; private set; } = null!;

        private void AddGameEntities_Internal(List<(GameEntity Entity, bool IsEnabled, int Index)> entities)
        {
            if (entities.Count == 0) return;

            // NOTE: Primal Day 084 は渡した順に入れる。複数を消したあとの undo では、消す前の位置の昇順に入れないと位置がずれる
            //       (足すときの位置は -1 なので順番は変わらない)
            foreach (var (entity, isEnabled, index) in entities.OrderBy(x => x.Index))
            {
                Debug.Assert(!_gameEntities.Contains(entity));

                entity.IsEnabled = isEnabled;
                entity.IsActive = IsActive;

                if (index == -1 || index >= _gameEntities.Count)
                {
                    _gameEntities.Add(entity);
                }
                else
                {
                    _gameEntities.Insert(index, entity);
                }
            }

            Project.UpdateScene();
        }

        /// <summary>エンティティを足す(Primal Day 084)。undo の履歴は 1 件。</summary>
        public void AddGameEntities(List<GameEntity> entities, int index = -1)
        {
            if (entities.Count == 0) return;

            var enableList = entities.Select(entity => (entity, entity.IsEnabled, index)).ToList();
            AddGameEntities_Internal(enableList);

            Project.UndoRedo.Add(new UndoRedoAction(
                () => RemoveGameEntities_Internal(entities),
                () => AddGameEntities_Internal(enableList),
                entities.Count == 1 ?
                    $"Add {entities[0].Name} to {Name}" :
                    $"Add multiple entities to {Name}"));
        }

        private List<(GameEntity Entity, bool IsEnabled, int Index)> RemoveGameEntities_Internal(List<GameEntity> entities)
        {
            if (entities.Count == 0) return [];

            // 描いているジオメトリを先にビューポートから外してから、エンジンから消す(#214)
            var enableList = DisableAndUpdate(entities);
            var indices = enableList.Select(x => (x.Entity, x.IsEnabled, _gameEntities.IndexOf(x.Entity))).ToList();

            foreach (var entity in entities)
            {
                Debug.Assert(_gameEntities.Contains(entity));
                entity.IsActive = false;
                _gameEntities.Remove(entity);
            }

            return indices;
        }

        /// <summary>エンティティを消す(Primal Day 084)。undo の履歴は 1 件。</summary>
        public void RemoveGameEntities(List<GameEntity> entities)
        {
            if (entities.Count == 0) return;

            var enableList = RemoveGameEntities_Internal(entities);

            Project.UndoRedo.Add(new UndoRedoAction(
                () => AddGameEntities_Internal(enableList),
                () => RemoveGameEntities_Internal(entities),
                entities.Count == 1 ?
                    $"Remove {entities[0].Name} from {Name}" :
                    $"Remove multiple entities from {Name}"));
        }

        /// <summary>
        /// ビューポートに描く、有効でエンジンにあるエンティティのジオメトリのコンポーネントの ID(Primal Day 084)。
        /// NOTE: Primal はすべて有効な ID だと assert する。DXForgeEX はジオメトリを読み込めなかったエンティティ(アセットがないなど)を
        ///       Geometry なしでエンジンに作る(EngineAPI.EntityAPI.CreateGameEntity)ので、その ID は飛ばす
        /// </summary>
        public List<int> GetGeometryComponentIds()
        {
            return GameEntities
                .Where(entity => entity.IsEnabled && entity.IsActive)
                .Select(entity => entity.GetComponent<Components.Geometry>())
                .Where(c => c != null)
                .Select(c => c!.GetComponentId())
                .Where(ID.IsValid)
                .ToList();
        }

        /// <summary>
        /// エンティティを無効にして、ビューポートから外す(Primal Day 084)。エンジンから消すか作り直す前に呼ぶ(#214)。
        /// 元の IsEnabled を返す(EnableAndUpdate で戻す)。
        /// </summary>
        public List<(GameEntity Entity, bool IsEnabled)> DisableAndUpdate(List<GameEntity> entities, bool update = true)
        {
            var enableList = entities.Select(x => (x, x.IsEnabled)).ToList();
            entities.ForEach(x => x.IsEnabled = false);

            if (update) Project.UpdateScene();

            return enableList;
        }

        /// <summary>DisableAndUpdate で無効にしたエンティティの IsEnabled を戻して、ビューポートに描く(Primal Day 084)。</summary>
        public void EnableAndUpdate(List<(GameEntity Entity, bool IsEnabled)> enableList, bool update = true)
        {
            enableList.ForEach(x => x.Entity.IsEnabled = x.IsEnabled);
            if (update) Project.UpdateScene();
        }

        /// <summary>
        /// ドロップしたファイルのうち、このプロジェクトのジオメトリのアセットごとに、その名前のエンティティを Geometry 付きで作る
        /// (Primal Day 081)。undo の履歴は 1 件(Primal Day 084)。作ったエンティティを返す。
        /// NOTE: Primal は Task.Run で IsActive = true と AddComponent をするが、DXForgeEX はアセットとエンジンの呼び出しに
        ///       ロックがない(#171)ので、UI スレッドで作る。エンジンに作るのは AddGameEntities の IsActive = IsActive に任せる。
        /// </summary>
        internal List<GameEntity> AddGeometryEntities(IEnumerable<string> files)
        {
            var assets = files
                .Where(x => string.Equals(Path.GetExtension(x), Asset.AssetFileExtension, StringComparison.OrdinalIgnoreCase))
                .Select(Asset.TryGetAssetInfo)
                .Where(x => x?.Type == AssetType.Mesh)
                .ToList();
            if (assets.Count == 0)
            {
                Logger.Log(MessageType.Warning, "ドロップしたファイルに、ジオメトリのアセットがありません。");
            }

            var entities = new List<GameEntity>();
            foreach (var assetInfo in assets)
            {
                // NOTE: Geometry が使うのはプロジェクトに登録したアセット(GUID で探す)。プロジェクトの外のファイルは使えない
                if (AssetRegistry.GetAssetInfo(assetInfo!.Guid) == null)
                {
                    Logger.Log(MessageType.Warning, $"ジオメトリ {assetInfo.FullName} はこのプロジェクトのアセットではないので、使えません。");
                    continue;
                }

                var entity = new GameEntity(this) { Name = assetInfo.FullName.Trim() };
                entity.AddComponent(new Components.Geometry(entity, assetInfo));
                entities.Add(entity);
            }
            AddGameEntities(entities);
            return entities;
        }

        [OnDeserialized]
        private void OnDeserialized(StreamingContext context)
        {
            if (_gameEntities != null)
            {
                GameEntities = new ReadOnlyObservableCollection<GameEntity>(_gameEntities);
                OnPropertyChanged(nameof(_gameEntities));
            }

            Debug.Assert(_gameEntities != null);
            // NOTE: エンティティをエンジンに作るのは Project.OnDeserialized(ファイル全体を読めたあと)。Primal はここで作る

            RenameCommand = new RelayCommand<string>(x =>
            {
                var oldName = _name;
                Name = x;

                Project.UndoRedo.Add(new UndoRedoAction(nameof(Name), this,
                    oldName, x, $"Rename scene '{oldName}' to '{x}'"));
            }, x => x != _name);
        }


        public Scene(Project project, string name)
        {
            Debug.Assert(project != null);
            Project = project;
            Name = name;
            OnDeserialized(new StreamingContext());
        }

        /// <summary>エンジンに触らず、undo も積まずにエンティティを足す(dxforge の操作, #183)。</summary>
        internal void AddEntityWithoutEngine(GameEntity entity)
        {
            Debug.Assert(!_gameEntities.Contains(entity) && !entity.IsActive);
            _gameEntities.Add(entity);
        }

        /// <summary>エンジンに触らず、undo も積まずにエンティティを消す(dxforge の操作, #183)。</summary>
        internal void RemoveEntityWithoutEngine(GameEntity entity)
        {
            Debug.Assert(_gameEntities.Contains(entity) && !entity.IsActive);
            _gameEntities.Remove(entity);
        }
    }
}
