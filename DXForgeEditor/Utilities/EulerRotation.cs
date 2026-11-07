using System;
using System.Numerics;

namespace DXForgeEditor.Utilities
{
    /// <summary>
    /// エンティティの回転の、度数法のオイラー角とクォータニオンの変換(docs/design/rotation-units.md)。
    /// 順序はエンジンの XMQuaternionRotationRollPitchYaw と同じ: x = ピッチ、y = ヨー、z = ロールで、Z → X → Y の順に回す。
    /// System.Numerics の Quaternion.CreateFromYawPitchRoll は同じ順序である(DXForgeCli/Tests/ops_cli_test.ps1 がエンジンと比べる)。
    /// </summary>
    static class EulerRotation
    {
        // 同じ回転とみなす |内積| の下限(角度の差でおよそ 0.16 度)。float の丸めの誤差(1e-7 ほど)より十分大きくする
        private const double SameRotationDot = 1.0 - 1e-6;

        public static Quaternion FromDegrees(Vector3 degrees)
        {
            return Quaternion.Normalize(Quaternion.CreateFromYawPitchRoll(
                ToRadians(degrees.Y), ToRadians(degrees.X), ToRadians(degrees.Z)));
        }

        /// <summary>FromDegrees の逆。ピッチは -90〜90 度、ヨーとロールは -180〜180 度になる。</summary>
        public static Vector3 ToDegrees(Quaternion q)
        {
            q = Quaternion.Normalize(q);
            double x = q.X, y = q.Y, z = q.Z, w = q.W;
            // 回転行列(Rz、Rx、Ry の順にかける)の成分から読む
            var sinPitch = Math.Clamp(2.0 * (w * x - y * z), -1.0, 1.0);
            var pitch = Math.Asin(sinPitch);
            double yaw, roll;
            if (Math.Abs(sinPitch) > 0.9999999)
            {
                // ジンバルロック: ヨーとロールを分けられないので、ロールを 0 にしてヨーにまとめる
                roll = 0.0;
                yaw = Math.Atan2(-2.0 * (x * z - w * y), 1.0 - 2.0 * (y * y + z * z));
            }
            else
            {
                yaw = Math.Atan2(2.0 * (w * y + x * z), 1.0 - 2.0 * (x * x + y * y));
                roll = Math.Atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (x * x + z * z));
            }
            return new Vector3(ToDegrees(pitch), ToDegrees(yaw), ToDegrees(roll));
        }

        /// <summary>同じ回転か(q と -q は同じ回転)。</summary>
        public static bool IsSameRotation(Quaternion a, Quaternion b)
        {
            return Math.Abs(Quaternion.Dot(Quaternion.Normalize(a), Quaternion.Normalize(b))) >= SameRotationDot;
        }

        /// <summary>回転として使えるか(有限で、長さが 0 でない)。</summary>
        public static bool IsValid(Quaternion q)
        {
            return float.IsFinite(q.X) && float.IsFinite(q.Y) && float.IsFinite(q.Z) && float.IsFinite(q.W) && q.LengthSquared() > 1e-12f;
        }

        public static float ToRadians(float degrees) => (float)(degrees * Math.PI / 180.0);
        private static float ToDegrees(double radians) => (float)(radians * 180.0 / Math.PI);
    }
}
