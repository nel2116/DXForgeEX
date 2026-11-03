using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.Components
{
    interface IMSComponent
    {
    }

    [DataContract]
    abstract class Component : ViewModelBase
    {

        [DataMember]
        public GameEntity Owner { get; private set; }

        public abstract IMSComponent GetMultiselectionComponent(MSEntity msEntity);
        public abstract void WriteToBinary(BinaryWriter bw);

        /// <summary>
        /// game.bin(ゲームの exe とヘッドレスのランナーが読む)に書けるコンポーネントか。
        /// 書けないもの(Geometry、Primal Day 079 では WriteToBinary がない)は、Project.WriteGameBinary が飛ばす。
        /// </summary>
        public virtual bool CanWriteToBinary => true;

        /// <summary>
        /// エンティティをエンジンに作る前に呼ばれる(GameEntity.IsActive が true になるとき。Primal Day 079)。
        /// エンジンにアップロードするアセットがあれば、ここでアップロードする。
        /// </summary>
        public virtual void Load() { }
        /// <summary>
        /// エンティティをエンジンから消したあとに呼ばれる(GameEntity.IsActive が false になるとき)。Load() でアップロードしたものを外す。
        /// </summary>
        public virtual void Unload() { }

        public Component(GameEntity owner)
        {
            Debug.Assert(owner != null);
            Owner = owner;
        }

    }

    abstract class MSComponent<T> : ViewModelBase, IMSComponent where T : Component
    {
        private bool _enableUpdates = true;
        public List<T> SelectedComponents { get; }

        protected abstract bool UpdateComponents(string? propertyName);
        protected abstract bool UpdateMSComponent();

        public void Refresh()
        {
            _enableUpdates = false;
            UpdateMSComponent();
            _enableUpdates = true;
        }

        public MSComponent(MSEntity msEntity)
        {
            Debug.Assert(msEntity?.SelectedEntities?.Any() == true);
            // MSEntity.MakeComponentList() は全選択エンティティが持つコンポーネントに対してのみ生成するため null にならない
            SelectedComponents = msEntity.SelectedEntities.Select(entity => entity.GetComponent<T>()!).ToList();
            PropertyChanged += (s, e) => { if (_enableUpdates) UpdateComponents(e.PropertyName); };
        }
    }
}
