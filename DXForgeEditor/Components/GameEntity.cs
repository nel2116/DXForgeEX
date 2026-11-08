using DXForgeEditor.DllWrappers;
using DXForgeEditor.GameProject;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Input;

namespace DXForgeEditor.Components
{
    [DataContract]
    [KnownType(typeof(Transform))]
    [KnownType(typeof(Script))]
    [KnownType(typeof(Geometry))]
    class GameEntity : ViewModelBase
    {
        private int _entityId = ID.INVALID_ID;
        public int EntityId
        {
            get => _entityId;
            private set
            {
                if (_entityId != value)
                {
                    _entityId = value;
                    OnPropertyChanged(nameof(EntityId));
                }
            }
        }

        private bool _isActive;
        public bool IsActive
        {
            get => _isActive;
            set
            {
                if (_isActive != value)
                {
                    _isActive = value;
                    if (_isActive)
                    {
                        // エンジンに作る前に、コンポーネントが使うアセットをアップロードする(Primal Day 079)
                        _components.ToList().ForEach(x => x.Load());
                        EntityId = EngineAPI.EntityAPI.CreateGameEntity(this);
                        Debug.Assert(ID.IsValid(_entityId));
                    }
                    else if (ID.IsValid(EntityId))
                    {
                        EngineAPI.EntityAPI.RemoveGameEntity(this);
                        _components.ToList().ForEach(x => x.Unload());
                        EntityId = ID.INVALID_ID;
                    }
                    OnPropertyChanged(nameof(IsActive));
                }
            }
        }

        private bool _isEnabled = true;
        [DataMember]
        public bool IsEnabled
        {
            get => _isEnabled;
            set
            {
                if (_isEnabled != value)
                {
                    _isEnabled = value;
                    OnPropertyChanged(nameof(IsEnabled));
                }
            }
        }

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
        public Scene ParentScene { get; private set; }

        /// <summary>
        /// .dxproj に保存される、エンティティの変わらない ID(dxforge のセレクター guid:、Issue #183)。名前は重複し、EntityId は
        /// エンジンでの番号で保存されない。game.bin には書かない。
        /// NOTE: Primal にはない。要素がない .dxproj は読むときに作るので、保存するまでは読むたびに変わる。
        /// </summary>
        [DataMember(Name = "Guid")]
        public Guid EntityGuid { get; private set; }

        [DataMember(Name = "Components")]
        private readonly ObservableCollection<Component> _components = new();
        // OnDeserialized() で設定される (コンストラクタからも呼び出される)
        public ReadOnlyObservableCollection<Component> Components { get; private set; } = null!;

        public Component? GetComponent(Type type) => Components.FirstOrDefault(c => c.GetType() == type);

        public T? GetComponent<T>() where T : Component => GetComponent(typeof(T)) as T;

        public bool AddComponent(Component component)
        {
            Debug.Assert(component != null);
            if (!Components.Any(x => x.GetType() == component.GetType()))
            {
                // NOTE: Primal Day 084 は常にエンジンのコンポーネントを更新する。エンジンにないエンティティ(非アクティブ、
                //       ヘッドレスの dxforge の操作)では足すだけにする(アクティブにするときに IsActive が読み込む)
                if (!IsActive)
                {
                    _components.Add(component);
                    return true;
                }

                // エンティティを作り直さず、エンジンのコンポーネントだけを更新する(Primal Day 084)
                component.Load();
                _components.Add(component);
                EngineAPI.EntityAPI.UpdateComponent(this, component.ToEnumType());
                // NOTE: Primal はここで UpdateScene を呼ばないので、次に呼ばれるまで足したジオメトリが描かれない
                if (component is Geometry) ParentScene.Project.UpdateScene();
                return true;
            }
            Logger.Log(MessageType.Warning, $"Entity {Name} はすでに {component.GetType().Name} Componentを持っています。");
            return false;
        }

        public void RemoveComponent(Component component)
        {
            Debug.Assert(component != null);
            if (component is Transform || !_components.Contains(component)) return; // TransformComponentは削除できない

            // NOTE: エンジンにないエンティティ(非アクティブ)では外すだけにする(Primal Day 084 は常にエンジンを更新する)
            if (!IsActive)
            {
                _components.Remove(component);
                return;
            }

            // エンティティを作り直さず、エンジンのコンポーネントだけを更新する(Primal Day 084)
            // NOTE: ジオメトリは、エンジンから消す前にビューポートから外す(#214。Primal はしないので、消した ID が描かれる)
            var enableList = component is Geometry ? ParentScene.DisableAndUpdate([this]) : null;
            _components.Remove(component);
            EngineAPI.EntityAPI.UpdateComponent(this, component.ToEnumType());
            component.Unload();
            if (enableList != null) ParentScene.EnableAndUpdate(enableList);
        }

        /// <summary>
        /// エンジンに触らずにコンポーネントを外す(dxforge の操作, #183)。非アクティブなエンティティでは RemoveComponent と同じだが、
        /// エンジンに触らないことを呼び出し側で明示するために残す。
        /// </summary>
        internal void RemoveComponentWithoutEngine(Component component)
        {
            Debug.Assert(!IsActive && component is not Transform && _components.Contains(component));
            _components.Remove(component);
        }

        // DataContractのデシリアライズではコンストラクタもフィールド初期化子も実行されないため、ここで既定値を入れておく(#165)。
        // NOTE: Primal は初期化子だけなので、読み込んだエンティティの EntityId が 0(有効な ID)になる。
        //       IsEnabled は保存されていればこのあとその値で上書きされる(要素がない古いファイルでも有効にする)。
        [OnDeserializing]
        private void OnDeserializing(StreamingContext context)
        {
            _entityId = ID.INVALID_ID;
            _isEnabled = true;
        }

        [OnDeserialized]
        void OnDeserialized(StreamingContext context)
        {
            if (EntityGuid == Guid.Empty) EntityGuid = Guid.NewGuid();
            if (_components != null)
            {
                Components = new ReadOnlyObservableCollection<Component>(_components);
                OnPropertyChanged(nameof(Components));
                foreach (var component in _components) component.OnOwnerDeserialized();
            }
        }

        public GameEntity(Scene scene)
        {
            Debug.Assert(scene != null);
            ParentScene = scene;
            _components.Add(new Transform(this));
            OnDeserialized(new StreamingContext());
        }

    }
    abstract class MSEntity : ViewModelBase
    {
        private bool _enableUpdates = true;
        private bool? _isEnabled;
        public bool? IsEnabled
        {
            get => _isEnabled;
            set
            {
                if (_isEnabled != value)
                {
                    _isEnabled = value;
                    OnPropertyChanged(nameof(IsEnabled));
                }
            }
        }

        private string? _name;
        public string? Name
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

        private readonly ObservableCollection<IMSComponent> _components = new();
        public ReadOnlyObservableCollection<IMSComponent> Components { get; private set; }

        public T? GetMSComponent<T>() where T : IMSComponent
        {
            return (T?)Components.FirstOrDefault(x => x.GetType() == typeof(T));
        }

        public List<GameEntity> SelectedEntities { get; }

        private void MakeComponentList()
        {
            _components.Clear();
            var firstEntity = SelectedEntities.FirstOrDefault();
            if (firstEntity == null) return;

            foreach (var component in firstEntity.Components)
            {
                var type = component.GetType();
                if (!SelectedEntities.Skip(1).Any(entity => entity.GetComponent(type) == null))
                {
                    Debug.Assert(Components.FirstOrDefault(x => x.GetType() == type) == null);
                    _components.Add(component.GetMultiselectionComponent(this));
                }
            }
        }

        public static int? GetMixedValue<T>(List<T> objects, Func<T, int> getProperty)
        {
            var value = getProperty(objects.First());
            return objects.Skip(1).Any(x => value != getProperty(x)) ? null : value;
        }
        public static float? GetMixedValue<T>(List<T> objects, Func<T, float> getProperty)
        {
            var value = getProperty(objects.First());
            return objects.Skip(1).Any(x => !getProperty(x).IsTheSameAs(value)) ? (float?)null : value;
        }
        public static bool? GetMixedValue<T>(List<T> objects, Func<T, bool> getProperty)
        {
            var value = getProperty(objects.First());
            return objects.Skip(1).Any(x => value != getProperty(x)) ? (bool?)null : value;
        }
        public static string? GetMixedValue<T>(List<T> objects, Func<T, string> getProperty)
        {
            var value = getProperty(objects.First());
            return objects.Skip(1).Any(x => value != getProperty(x)) ? null : value;
        }
        protected virtual bool UpdateGameEntities(string? propertyName)
        {
            switch (propertyName)
            {
                case nameof(IsEnabled): Debug.Assert(IsEnabled.HasValue); SelectedEntities.ForEach(x => x.IsEnabled = IsEnabled.Value); return true;
                case nameof(Name): Debug.Assert(Name != null); SelectedEntities.ForEach(x => x.Name = Name); return true;
            }
            return false;
        }
        protected virtual bool UpdateGameEntities()
        {
            IsEnabled = GetMixedValue(SelectedEntities, new Func<GameEntity, bool>(x => x.IsEnabled));
            Name = GetMixedValue(SelectedEntities, new Func<GameEntity, string>(x => x.Name));
            return true;
        }
        public void Refresh()
        {
            _enableUpdates = false;
            UpdateGameEntities();
            MakeComponentList();
            _enableUpdates = true;
        }

        public MSEntity(List<GameEntity> entities)
        {
            Debug.Assert(entities?.Any() == true);
            Components = new ReadOnlyObservableCollection<IMSComponent>(_components);
            SelectedEntities = entities;
            PropertyChanged += (s, e) => { if (_enableUpdates) UpdateGameEntities(e.PropertyName); };
        }
    }

    class MSGameEntity : MSEntity
    {
        public MSGameEntity(List<GameEntity> entities) : base(entities)
        {
            Refresh();
        }
    }
}
