using DXForgeEditor.DllWrappers;
using System;
using System.Numerics;

namespace DXForgeEditor.Utilities
{
    // NOTE: Primal Day 085 から、値が変わるとエンジンのビューポートのカメラに設定する(EngineAPI.UpdateEditorCamera など)。
    //       角度はこのクラスの中でだけラジアンを使い、エンジンの回転の単位(#208)とは関係しない。
    //       ChangePosition と Update は render thread から、そのほかは UI スレッドから呼ばれる。Primal と同じくロックはない。
    //       競合すると 1 フレームだけ新旧の混ざった値を使ったり、片方の変更が上書きされたりするが、値はどれも有限で、次の Seek で収束する。
    //       SetSurfaceId の前(_surfaceId が無効)はエンジンを呼ばない(Primal は無効な ID で呼んで DLL の assert に当たる)。
    class EditorCamera
    {
        private static readonly float _minNearZ = 0.001f; // 1 mm
        private static readonly float _minDiffNearZFarZ = 0.001f;
        private static readonly float _minFov = 0.01f;
        // NOTE: Primal は 180 度まで許すが、180 度ではエンジンの射影(XMMatrixPerspectiveFovRH に pi)の縦横の倍率がほぼ 0 になり、描画が潰れる
        private static readonly float _maxFov = 179f;

        private int _surfaceId = ID.INVALID_ID;
        private bool _updatePosition;
        private bool _updateRotation;
        private float _orbitRadius;
        private float _acceleration = 0f;
        private Vector3 _position;
        private Vector3 _rotation;
        private Vector3 _target;
        private Vector3 _desiredPosition;
        private Vector3 _desiredRotation;

        public float OrbitRadius => _orbitRadius;

        private float _speed = 5f;
        public float Speed
        {
            get => _speed;
            set
            {
                value = Math.Clamp(value, 1f, 10f);
                if (!_speed.IsTheSameAs(value))
                {
                    _speed = value;
                }
            }
        }

        private float _fov = 45f;
        public float FoV
        {
            get => _fov;
            set
            {
                value = Math.Clamp(value, _minFov, _maxFov);
                if (!_fov.IsTheSameAs(value))
                {
                    _fov = value;
                    if (ID.IsValid(_surfaceId)) EngineAPI.SetCameraFoV(_surfaceId, _fov);
                }
            }
        }

        private float _nearZ = 0.1f;
        public float NearZ
        {
            get => _nearZ;
            set
            {
                value = Math.Clamp(value, _minNearZ, _farZ - _minDiffNearZFarZ);
                if (!_nearZ.IsTheSameAs(value))
                {
                    _nearZ = value;
                    if (ID.IsValid(_surfaceId)) EngineAPI.SetCameraRange(_surfaceId, _nearZ, FarZ);
                }
            }
        }

        private float _farZ = 100f;
        public float FarZ
        {
            get => _farZ;
            set
            {
                value = Math.Max(value, _nearZ + _minDiffNearZFarZ);
                if (!_farZ.IsTheSameAs(value))
                {
                    _farZ = value;
                    if (ID.IsValid(_surfaceId)) EngineAPI.SetCameraRange(_surfaceId, NearZ, _farZ);
                }
            }
        }

        public Vector3 Target => _target;

        public void Goto(Vector3 desiredPosition)
        {
            _target = desiredPosition;
            Orbit(0, 0, 0, true);
        }

        private void Orbit(double dx, double dy, int dz, bool slide)
        {
            var theta = _desiredRotation.X + (float)dy * 0.005f;
            theta = Math.Clamp(theta, 0.0001f - MathUtil.HalfPi, MathUtil.HalfPi - 0.0001f);
            var phi = _desiredRotation.Y - (float)dx * 0.005f;
            _orbitRadius *= 1f - (0.1f * dz); // dz は -1、0、+1 のどれか

            var rotationMatrix = Matrix4x4.CreateFromYawPitchRoll(phi, theta, 0);
            var v = Vector3.TransformNormal(new(0, 0, 1), rotationMatrix);
            v = Vector3.Normalize(v);
            v *= _orbitRadius;

            _desiredPosition = _target - v;
            _desiredRotation.X = theta;
            _desiredRotation.Y = phi;

            if (!slide)
            {
                _position = _desiredPosition;
                _rotation = _desiredRotation;

                if (ID.IsValid(_surfaceId)) EngineAPI.UpdateEditorCamera(_surfaceId, _position, _rotation);
            }
            else
            {
                _updatePosition = true;
                _updateRotation = true;
            }
        }

        public void Orbit(double dx, double dy, int dz) => Orbit(dx, dy, dz, false);

        public void ChangePosition(Vector3 direction, float dt)
        {
            if (direction.LengthSquared() <= MathUtil.Epsilon) return;

            var theta = _desiredRotation.X;
            var phi = _desiredRotation.Y;

            var rotationMatrix = Matrix4x4.CreateFromYawPitchRoll(phi, theta, 0);

            var dtScale = dt * 60;

            if (_acceleration < 1f)
            {
                _acceleration += 0.02f * dtScale;
            }
            // NOTE: render thread から呼ばれる。Keyboard.Modifiers は UI スレッドでしか使えないので、キーの状態を直接読む(Primal Day 085)
            var step = Speed * dtScale * (KeyboardHelper.GetAsyncKeyState(KeyboardHelper.VKey.Shift) < 0 ? 0.1f : 0.01f);
            var v = Vector3.Transform(direction * step, rotationMatrix) * _acceleration;

            _desiredPosition += v;
            _target += v;
            _updatePosition = true;
        }

        public void ChangeDirection(double dx, double dy)
        {
            var theta = _desiredRotation.X + (float)dy * 0.005f;
            theta = Math.Clamp(theta, 0.0001f - MathUtil.HalfPi, MathUtil.HalfPi - 0.0001f);
            var phi = _desiredRotation.Y - (float)dx * 0.005f;

            var rotationMatrix = Matrix4x4.CreateFromYawPitchRoll(phi, theta, 0);
            var v = Vector3.TransformNormal(new(0, 0, 1), rotationMatrix);
            v = Vector3.Normalize(v);
            v *= _orbitRadius;

            _target = _desiredPosition + v;
            _desiredRotation.X = theta;
            _desiredRotation.Y = phi;

            _updateRotation = true;
        }

        private void Seek(float dt)
        {
            var dtScale = 0.2f * dt * 60f;

            if (_updatePosition)
            {
                var p = _desiredPosition - _position;
                _updatePosition = p.LengthSquared() > 1e-8f;

                if (_updatePosition)
                {
                    _position += p * dtScale;
                }
                else
                {
                    _position = _desiredPosition;
                    _acceleration = 0f;
                }
            }

            if (_updateRotation)
            {
                var o = _desiredRotation - _rotation;
                _updateRotation = o.LengthSquared() > 1e-8f;
                _rotation = _updateRotation ? _rotation + o * dtScale : _desiredRotation;
            }

            EngineAPI.UpdateEditorCamera(_surfaceId, _position, _rotation);
        }

        public void SetSurfaceId(int surfaceId)
        {
            _surfaceId = surfaceId;
            if (ID.IsValid(_surfaceId))
            {
                EngineAPI.UpdateEditorCamera(_surfaceId, _position, _rotation);
                // NOTE: Primal は位置と回転だけを送る。プロジェクトを開き直すと同じ EditorCamera が新しいビューポートにつながるので、
                //       画面に出している FoV と範囲もここで送り、エンジンのカメラ(既定は 45 度、0.01〜1000)と合わせる。
                EngineAPI.SetCameraFoV(_surfaceId, _fov);
                EngineAPI.SetCameraRange(_surfaceId, _nearZ, _farZ);
            }
        }

        public void Update(float dt)
        {
            if ((_updatePosition || _updateRotation) && ID.IsValid(_surfaceId))
            {
                Seek(dt);
            }
        }

        public EditorCamera()
        {
            _orbitRadius = 3f;
            _speed = 5f;
            _position = _desiredPosition = new(0, 1, 10);
            _rotation = _desiredRotation = new(0, -MathUtil.Pi, 0);
            _target = new(0, 1, _position.Z - _orbitRadius);
        }
    }
}
