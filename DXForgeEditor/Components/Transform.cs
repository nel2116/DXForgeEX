using DXForgeEditor.DllWrappers;
using DXForgeEditor.EngineAPIStructs;
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

        // ====== C# の値の変更とエンジンへの送信(Primal Day 086、#244、docs/design/transform-editing.md) ======
        // NOTE: インスペクターの編集と Undo/Redo はどちらもここを通る。C# の値が正で、エンジンには C# -> エンジンの向きにだけ送る
        //       (エンジンから読み戻さない)。ローカル/ワールドの差分も C# で絶対の値にしてから送る(TransformSpace.Absolute)。

        /// <summary>位置を設定し、エンジンにあるエンティティにまとめて送る。</summary>
        public static void ApplyPositions(IReadOnlyList<(Transform transform, Vector3 position)> values)
        {
            foreach (var (transform, position) in values) transform.Position = position;
            SendToEngine(values, x => x.transform, x => x.position,
                (ids, v) => EngineAPI.EntityAPI.SetPosition(ids, v, TransformSpace.Absolute), "位置");
        }

        /// <summary>回転(クォータニオンとその手がかり)を設定し、エンジンにあるエンティティにまとめて送る。</summary>
        public static void ApplyRotations(IReadOnlyList<(Transform transform, Quaternion rotation, Vector3 eulerHintDegrees)> values)
        {
            foreach (var (transform, rotation, hint) in values) transform.SetRotation(rotation, hint);
            // SetRotation が正規化したクォータニオンを送る
            SendToEngine(values, x => x.transform, x => x.transform.RotationQuaternion,
                (ids, v) => EngineAPI.EntityAPI.SetRotation(ids, v, TransformSpace.Absolute), "回転");
        }

        /// <summary>拡大率を設定し、エンジンにあるエンティティにまとめて送る。</summary>
        public static void ApplyScales(IReadOnlyList<(Transform transform, Vector3 scale)> values)
        {
            foreach (var (transform, scale) in values) transform.Scale = scale;
            SendToEngine(values, x => x.transform, x => x.scale, EngineAPI.EntityAPI.SetScale, "拡大率");
        }

        // エンジンにエンティティがあるもの(アクティブなシーン)だけを 1 回の呼び出しで送る。ひとつもなければ DLL を呼ばない
        // (アクティブでないシーンや、ヘッドレスの dxforge は C# の値だけを変える)。
        // NOTE: 失敗(削除済みの ID など)は警告だけにする。インスペクターや Undo のコマンドから呼ばれるので例外を投げない
        private static void SendToEngine<TItem, TValue>(IReadOnlyList<TItem> items, Func<TItem, Transform> transformOf,
            Func<TItem, TValue> valueOf, Func<int[], TValue[], bool> send, string what)
        {
            var targets = items.Where(x => ID.IsValid(transformOf(x).Owner.EntityId)).ToList();
            if (targets.Count == 0) return;
            var ids = targets.Select(x => transformOf(x).Owner.EntityId).ToArray();
            var values = targets.Select(valueOf).ToArray();
            if (!send(ids, values))
            {
                Logger.Log(MessageType.Warning, $"エンジンのエンティティの{what}を設定できないものがありました({string.Join(", ", targets.Select(x => transformOf(x).Owner.Name))})。");
            }
        }

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

        // ====== オフセット(Primal Day 086 の Local Offset と Rotation Offset) ======
        // NOTE: オフセットは選択したときと確定したとき(TransformView が ResetOffsets() を呼ぶ)に 0 に戻り、前回の値との差を
        //       差分として各エンティティに足す。差分は C# で絶対の値にしてから送る(docs/design/transform-editing.md)

        /// <summary>true なら回転のオフセットをエンティティのローカル軸について、false ならワールド軸について回す(Primal と同じく全体で共有)。</summary>
        private static bool _isLocalRotation = true;
        public bool IsLocalRotation
        {
            get => _isLocalRotation;
            set
            {
                if (_isLocalRotation != value)
                {
                    _isLocalRotation = value;
                    OnPropertyChanged(nameof(IsLocalRotation));
                }
            }
        }

        /// <summary>true なら拡大率の 1 つの成分を変えると 3 つとも同じ値にする(Primal と同じく全体で共有)。</summary>
        private static bool _isUniformScale = true;
        public bool IsUniformScale
        {
            get => _isUniformScale;
            set
            {
                if (_isUniformScale != value)
                {
                    _isUniformScale = value;
                    OnPropertyChanged(nameof(IsUniformScale));
                }
            }
        }

        private Vector3 _localPos;
        private Vector3 _previousLocalPos;
        public float LocalPosX { get => _localPos.X; set => SetOffset(ref _localPos.X, value, nameof(LocalPosX)); }
        public float LocalPosY { get => _localPos.Y; set => SetOffset(ref _localPos.Y, value, nameof(LocalPosY)); }
        public float LocalPosZ { get => _localPos.Z; set => SetOffset(ref _localPos.Z, value, nameof(LocalPosZ)); }

        // 度数法(x = ピッチ、y = ヨー、z = ロール。EulerRotation と同じ)
        private Vector3 _rotOffset;
        private Vector3 _previousRotOffset;
        public float RotOffsetX { get => _rotOffset.X; set => SetOffset(ref _rotOffset.X, value, nameof(RotOffsetX)); }
        public float RotOffsetY { get => _rotOffset.Y; set => SetOffset(ref _rotOffset.Y, value, nameof(RotOffsetY)); }
        public float RotOffsetZ { get => _rotOffset.Z; set => SetOffset(ref _rotOffset.Z, value, nameof(RotOffsetZ)); }

        private void SetOffset(ref float field, float value, string propertyName)
        {
            // 数として読めない値ではエンティティを動かさない
            if (!float.IsFinite(value) || field == value) return;
            field = value;
            OnPropertyChanged(propertyName);
        }

        private bool _resettingOffsets;

        /// <summary>オフセットの表示を 0 に戻す。エンティティは動かさない。</summary>
        public void ResetOffsets()
        {
            _resettingOffsets = true;
            try
            {
                _localPos = _previousLocalPos = Vector3.Zero;
                _rotOffset = _previousRotOffset = Vector3.Zero;
                OnPropertyChanged(nameof(LocalPosX)); OnPropertyChanged(nameof(LocalPosY)); OnPropertyChanged(nameof(LocalPosZ));
                OnPropertyChanged(nameof(RotOffsetX)); OnPropertyChanged(nameof(RotOffsetY)); OnPropertyChanged(nameof(RotOffsetZ));
            }
            finally { _resettingOffsets = false; }
        }

        /// <summary>
        /// 回転 rotation に、度数法のオイラー角の差分 deltaDegrees を足す。local ならエンティティのローカル軸について
        /// (エンジンの XMQuaternionMultiply(delta, q) と同じ)、そうでなければワールド軸について(XMQuaternionMultiply(q, delta))回す。
        /// </summary>
        // NOTE: Quaternion.Concatenate(a, b) は「a のあとに b」で、XMQuaternionMultiply(a, b) と同じ順序
        internal static Quaternion AddRotation(Quaternion rotation, Vector3 deltaDegrees, bool local)
        {
            var delta = EulerRotation.FromDegrees(deltaDegrees);
            return Quaternion.Normalize(local ? Quaternion.Concatenate(delta, rotation) : Quaternion.Concatenate(rotation, delta));
        }

        /// <summary>位置 position を、回転 rotation のローカル軸(right/up/front)に沿って delta だけ動かす。</summary>
        internal static Vector3 AddLocalPosition(Vector3 position, Quaternion rotation, Vector3 delta) => position + Vector3.Transform(delta, rotation);

        protected override bool UpdateComponents(string? propertyName)
        {
            switch (propertyName)
            {
                case nameof(PosX):
                case nameof(PosY):
                case nameof(PosZ):
                    Transform.ApplyPositions(SelectedComponents.Select(c => (c, new Vector3(_posX ?? c.Position.X, _posY ?? c.Position.Y, _posZ ?? c.Position.Z))).ToList());
                    return true;

                case nameof(RotX):
                case nameof(RotY):
                case nameof(RotZ):
                    {
                        // 度数法のオイラー角(手がかり)を変える。クォータニオンも変わる。数として読めない値(NaN など)では回転を変えない
                        var values = SelectedComponents
                            .Select(c => (c, hint: new Vector3(_rotX ?? c.RotationEulerHint.X, _rotY ?? c.RotationEulerHint.Y, _rotZ ?? c.RotationEulerHint.Z)))
                            .Where(x => float.IsFinite(x.hint.X) && float.IsFinite(x.hint.Y) && float.IsFinite(x.hint.Z))
                            .Select(x => (x.c, EulerRotation.FromDegrees(x.hint), x.hint))
                            .ToList();
                        Transform.ApplyRotations(values);
                    }
                    return true;

                case nameof(ScaleX):
                case nameof(ScaleY):
                case nameof(ScaleZ):
                    {
                        // 一様なら、変えた成分の値を 3 つともに使う(Primal と同じ)
                        var changed = propertyName == nameof(ScaleX) ? _scaleX : propertyName == nameof(ScaleY) ? _scaleY : _scaleZ;
                        if (IsUniformScale && changed is float uniform)
                        {
                            Transform.ApplyScales(SelectedComponents.Select(c => (c, new Vector3(uniform))).ToList());
                            Refresh();
                            return true;
                        }
                        Transform.ApplyScales(SelectedComponents.Select(c => (c, new Vector3(_scaleX ?? c.Scale.X, _scaleY ?? c.Scale.Y, _scaleZ ?? c.Scale.Z))).ToList());
                    }
                    return true;

                case nameof(LocalPosX):
                case nameof(LocalPosY):
                case nameof(LocalPosZ):
                    {
                        if (_resettingOffsets) return true;
                        var delta = _localPos - _previousLocalPos;
                        _previousLocalPos = _localPos;
                        Transform.ApplyPositions(SelectedComponents.Select(c => (c, AddLocalPosition(c.Position, c.RotationQuaternion, delta))).ToList());
                        // Position の欄を新しい値にする(Refresh はオフセットを変えない)
                        Refresh();
                    }
                    return true;

                case nameof(RotOffsetX):
                case nameof(RotOffsetY):
                case nameof(RotOffsetZ):
                    {
                        if (_resettingOffsets) return true;
                        var delta = _rotOffset - _previousRotOffset;
                        _previousRotOffset = _rotOffset;
                        // 手がかりは、差分を足したあとのクォータニオンから作り直す(食い違わないように)
                        Transform.ApplyRotations(SelectedComponents.Select(c =>
                        {
                            var rotation = AddRotation(c.RotationQuaternion, delta, IsLocalRotation);
                            return (c, rotation, EulerRotation.ToDegrees(rotation));
                        }).ToList());
                        Refresh();
                    }
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
