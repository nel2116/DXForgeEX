using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net.Http.Headers;
using System.Numerics;
using System.Runtime.Serialization;
using System.Text;
using System.Threading.Tasks;

namespace DXForgeEditor.Components
{
    [DataContract]
    class Transform : Component
    {
        private Vector3 _position;
        [DataMember]
        public Vector3 Position
        {
            get => _position;
            set
            {
                if (_position != value)
                {
                    _position = value;
                    OnPropertyChanged(nameof(Position));
                }
            }
        }

        // NOTE: 回転の正はクォータニオン。度数法のオイラー角はインスペクターに出すための手がかり(docs/design/rotation-units.md)。
        //       Primal は度数法のオイラー角の Rotation を持つが、単位と順序を取り違えないよう DXForgeEX はクォータニオンを持つ(#205)。
        [DataMember(Name = nameof(RotationQuaternion))]
        private Quaternion _rotationQuaternion = Quaternion.Identity;
        /// <summary>回転(正規化したクォータニオン)。エンジンにはこれを渡す。</summary>
        public Quaternion RotationQuaternion => _rotationQuaternion;

        [DataMember(Name = nameof(RotationEulerHint))]
        private Vector3 _rotationEulerHint;
        /// <summary>
        /// 回転の度数法のオイラー角(x = ピッチ、y = ヨー、z = ロール。EulerRotation を参照)。入力された値をそのまま残し、表示に使う。
        /// 設定すると RotationQuaternion も変わる。
        /// </summary>
        public Vector3 RotationEulerHint
        {
            get => _rotationEulerHint;
            set
            {
                // 数として読めない入力(NaN など)では回転を変えない
                if (IsFinite(value)) SetRotation(EulerRotation.FromDegrees(value), value);
            }
        }

        // 古い .dxproj の Rotation(単位の決まっていないオイラー角)。読み込んだら捨てる(移行しない。rotation-units.md の決定 5)
        [DataMember(Name = "Rotation", EmitDefaultValue = false)]
        private Vector3? _legacyRotation;

        /// <summary>回転を、クォータニオンとその手がかりの組で設定する(元に戻す・やり直しが両方を戻すのに使う)。</summary>
        public void SetRotation(Quaternion rotation, Vector3 eulerHintDegrees)
        {
            Debug.Assert(EulerRotation.IsValid(rotation));
            rotation = Quaternion.Normalize(rotation);
            var quaternionChanged = _rotationQuaternion != rotation;
            var hintChanged = _rotationEulerHint != eulerHintDegrees;
            _rotationQuaternion = rotation;
            _rotationEulerHint = eulerHintDegrees;
            if (quaternionChanged) OnPropertyChanged(nameof(RotationQuaternion));
            if (hintChanged) OnPropertyChanged(nameof(RotationEulerHint));
        }

        // 新しく作るTransformのスケールは1 (0だと何も描画されない)
        private Vector3 _scale = Vector3.One;
        [DataMember]
        public Vector3 Scale
        {
            get => _scale;
            set
            {
                if (_scale != value)
                {
                    _scale = value;
                    OnPropertyChanged(nameof(Scale));
                }
            }
        }
        public override IMSComponent GetMultiselectionComponent(MSEntity msEntity) => new MSTransform(msEntity);

        public override void WriteToBinary(BinaryWriter bw)
        {
            // 位置、回転、スケールをバイナリに書き込む
            bw.Write(_position.X); bw.Write(_position.Y); bw.Write(_position.Z);
            // game.bin の回転はクォータニオン(x, y, z, w)。手がかりは表示用なので書かない(docs/design/rotation-units.md の決定 4)
            bw.Write(_rotationQuaternion.X); bw.Write(_rotationQuaternion.Y); bw.Write(_rotationQuaternion.Z); bw.Write(_rotationQuaternion.W);
            bw.Write(_scale.X); bw.Write(_scale.Y); bw.Write(_scale.Z);
        }

        // DataContractのデシリアライズではコンストラクタもフィールド初期化子も実行されないため、
        // ここで既定値を入れておく。保存されているScaleがあれば、このあとその値で上書きされる。
        [OnDeserializing]
        private void OnDeserializing(StreamingContext context)
        {
            _scale = Vector3.One;
            _rotationQuaternion = Quaternion.Identity;
            _rotationEulerHint = Vector3.Zero;
            _legacyRotation = null;
        }

        // 読み込みで回転を捨てたときの警告。エンティティの名前はこのあとで読まれるので、OnOwnerDeserialized() で出す
        private string? _loadWarning;

        [OnDeserialized]
        private void OnDeserialized(StreamingContext context)
        {
            _loadWarning = null;
            if (_legacyRotation != null)
            {
                _loadWarning = "古い形式の回転(Rotation)なので、読み込まずに回転なしにしました。";
                _legacyRotation = null;
                _rotationQuaternion = Quaternion.Identity;
                _rotationEulerHint = Vector3.Zero;
                return;
            }
            if (!EulerRotation.IsValid(_rotationQuaternion))
            {
                _loadWarning = "回転のクォータニオンが不正なので、回転なしにしました。";
                _rotationQuaternion = Quaternion.Identity;
                _rotationEulerHint = Vector3.Zero;
                return;
            }
            // クォータニオンが正。手がかりが同じ回転にならなければ(手で書き換えたなど)、クォータニオンから作り直す
            _rotationQuaternion = Quaternion.Normalize(_rotationQuaternion);
            if (!IsFinite(_rotationEulerHint) || !EulerRotation.IsSameRotation(EulerRotation.FromDegrees(_rotationEulerHint), _rotationQuaternion))
            {
                _rotationEulerHint = EulerRotation.ToDegrees(_rotationQuaternion);
            }
        }

        // NOTE: Project.Load は OpenProjectView や async void のコマンドから呼ばれるので、例外を投げずに警告だけを出す
        public override void OnOwnerDeserialized()
        {
            if (_loadWarning == null) return;
            Logger.Log(MessageType.Warning, $"エンティティ {Owner.Name} の Transform は{_loadWarning}");
            _loadWarning = null;
        }

        private static bool IsFinite(Vector3 v) => float.IsFinite(v.X) && float.IsFinite(v.Y) && float.IsFinite(v.Z);

        public Transform(GameEntity owner) : base(owner)
        {
        }
    }

    sealed class MSTransform : MSComponent<Transform>
    {
        private float? _posX;
        public float? PosX
        {
            get => _posX;
            set
            {
                if (!_posX.IsTheSameAs(value))
                {
                    _posX = value;
                    OnPropertyChanged(nameof(PosX));
                }
            }
        }

        private float? _posY;
        public float? PosY
        {
            get => _posY;
            set
            {
                if (!_posY.IsTheSameAs(value))
                {
                    _posY = value;
                    OnPropertyChanged(nameof(PosY));
                }
            }
        }

        private float? _posZ;
        public float? PosZ
        {
            get => _posZ;
            set
            {
                if (!_posZ.IsTheSameAs(value))
                {
                    _posZ = value;
                    OnPropertyChanged(nameof(PosZ));
                }
            }
        }

        private float? _rotX;
        public float? RotX
        {
            get => _rotX;
            set
            {
                if (!_rotX.IsTheSameAs(value))
                {
                    _rotX = value;
                    OnPropertyChanged(nameof(RotX));
                }
            }
        }

        private float? _rotY;
        public float? RotY
        {
            get => _rotY;
            set
            {
                if (!_rotY.IsTheSameAs(value))
                {
                    _rotY = value;
                    OnPropertyChanged(nameof(RotY));
                }
            }
        }

        private float? _rotZ;
        public float? RotZ
        {
            get => _rotZ;
            set
            {
                if (!_rotZ.IsTheSameAs(value))
                {
                    _rotZ = value;
                    OnPropertyChanged(nameof(RotZ));
                }
            }
        }

        private float? _scaleX;
        public float? ScaleX
        {
            get => _scaleX;
            set
            {
                if (!_scaleX.IsTheSameAs(value))
                {
                    _scaleX = value;
                    OnPropertyChanged(nameof(ScaleX));
                }
            }
        }

        private float? _scaleY;
        public float? ScaleY
        {
            get => _scaleY;
            set
            {
                if (!_scaleY.IsTheSameAs(value))
                {
                    _scaleY = value;
                    OnPropertyChanged(nameof(ScaleY));
                }
            }
        }

        private float? _scaleZ;
        public float? ScaleZ
        {
            get => _scaleZ;
            set
            {
                if (!_scaleZ.IsTheSameAs(value))
                {
                    _scaleZ = value;
                    OnPropertyChanged(nameof(ScaleZ));
                }
            }
        }

        protected override bool UpdateComponents(string? propertyName)
        {
            switch (propertyName)
            {
                case nameof(PosX):
                case nameof(PosY):
                case nameof(PosZ):
                    SelectedComponents.ForEach(c => c.Position = new Vector3(_posX ?? c.Position.X, _posY ?? c.Position.Y, _posZ ?? c.Position.Z));
                    return true;

                case nameof(RotX):
                case nameof(RotY):
                case nameof(RotZ):
                    // 度数法のオイラー角(手がかり)を変える。クォータニオンも変わる
                    SelectedComponents.ForEach(c => c.RotationEulerHint = new Vector3(_rotX ?? c.RotationEulerHint.X, _rotY ?? c.RotationEulerHint.Y, _rotZ ?? c.RotationEulerHint.Z));
                    return true;

                case nameof(ScaleX):
                case nameof(ScaleY):
                case nameof(ScaleZ):
                    SelectedComponents.ForEach(c => c.Scale = new Vector3(_scaleX ?? c.Scale.X, _scaleY ?? c.Scale.Y, _scaleZ ?? c.Scale.Z));
                    return true;
            }
            return false;
        }
        protected override bool UpdateMSComponent()
        {
            PosX = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Position.X));
            PosY = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Position.Y));
            PosZ = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Position.Z));

            RotX = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.RotationEulerHint.X));
            RotY = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.RotationEulerHint.Y));
            RotZ = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.RotationEulerHint.Z));

            ScaleX = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Scale.X));
            ScaleY = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Scale.Y));
            ScaleZ = MSEntity.GetMixedValue(SelectedComponents, new Func<Transform, float>(x => x.Scale.Z));
            return true;
        }

        public MSTransform(MSEntity msEntity) : base(msEntity)
        {
            Refresh();
        }
    }
}
