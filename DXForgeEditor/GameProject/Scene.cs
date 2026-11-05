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
        public ICommand AddGameEntityCommand { get; private set; } = null!;
        public ICommand RemoveGameEntityCommand { get; private set; } = null!;

        private void AddGameEntity(GameEntity entity, int index = -1)
        {
            Debug.Assert(!_gameEntities.Contains(entity));
            entity.IsActive = IsActive;
            if (index == -1)
            {
                _gameEntities.Add(entity);
            }
            else
            {
                _gameEntities.Insert(index, entity);
            }
        }

        private void RemoveGameEntity(GameEntity entity)
        {
            Debug.Assert(_gameEntities.Contains(entity));
            entity.IsActive = false;
            _gameEntities.Remove(entity);
        }

        /// <summary>
        /// ドロップしたファイルのうち、このプロジェクトのジオメトリのアセットごとに、その名前のエンティティを Geometry 付きで作る
        /// (Primal Day 081)。エンティティごとに undo の履歴が 1 件増える。作ったエンティティを返す。
        /// NOTE: Primal は Task.Run で IsActive = true と AddComponent をするが、DXForgeEX はアセットとエンジンの呼び出しに
        ///       ロックがない(#171)ので、UI スレッドで作る。エンジンに作るのは AddGameEntity の IsActive = IsActive に任せる。
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
                AddGameEntityCommand.Execute(entity);
                entities.Add(entity);
            }
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

            AddGameEntityCommand = new RelayCommand<GameEntity>(x =>
            {
                AddGameEntity(x);
                var entityIndex = _gameEntities.Count - 1;
                Project.UndoRedo.Add(new UndoRedoAction(
                    () => RemoveGameEntity(x),
                    () => AddGameEntity(x, entityIndex),
                    $"Add {x.Name} to {Name}"));
            });

            RemoveGameEntityCommand = new RelayCommand<GameEntity>(x =>
            {
                var entityIndex = _gameEntities.IndexOf(x);
                RemoveGameEntity(x);
                Project.UndoRedo.Add(new UndoRedoAction(
                    () => AddGameEntity(x, entityIndex),
                    () => RemoveGameEntity(x),
                    $"Remove {x.Name}"));
            });

        }


        public Scene(Project project, string name)
        {
            Debug.Assert(project != null);
            Project = project;
            Name = name;
            OnDeserialized(new StreamingContext());
        }
    }
}
