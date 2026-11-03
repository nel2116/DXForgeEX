using DXForgeEditor.Content;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.Components
{
    enum ComponentType
    {
        Transform,
        Script,
        // NOTE: Geometry は game.bin に書かない(Component.CanWriteToBinary)ので、この値はファイルに出ない
        Geometry,
    }

    static class ComponentFactory
    {
        private static readonly Func<GameEntity, object, Component>[] _function =
            new Func<GameEntity, object, Component>[]
            {
                (entity, data) => new Transform(entity),
                (entity, data) => new Script(entity){ Name = (string)data},
                (entity, data) => new Geometry(entity, (AssetInfo)data),
            };

        public static Func<GameEntity, object, Component> GetCreationFunction(ComponentType conponentType)
        {
            Debug.Assert((int)conponentType < _function.Length);
            return _function[(int)conponentType];
        }

        public static ComponentType ToEnumType(this Component component)
        {
            return component switch
            {
                Transform _ => ComponentType.Transform,
                Script _ => ComponentType.Script,
                Geometry _ => ComponentType.Geometry,
                _ => throw new ArgumentException("Unknown component type"),
            };
        }
    }
}
