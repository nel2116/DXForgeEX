using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.Components
{
    enum ComponentType
    {
        Transform,
        Script,
    }

    class ComponentFactory
    {
        private static readonly Func<GameEntity, object, Component>[] _function =
            new Func<GameEntity, object, Component>[]
            {
                (entity, data) => new Transform(entity),
                (entity, data) => new Script(entity){ Name = (string)data},
            };

        public static Func<GameEntity, object, Component> GetCreationFunction(ComponentType conponentType)
        {
            Debug.Assert((int)conponentType < _function.Length);
            return _function[(int)conponentType];
        }
    }
}
