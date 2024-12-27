using DXForgeEditor.ContentToolsAPIStructs;
using DXForgeEditor.Utilities;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Text;

namespace DXForgeEditor.ContentToolsAPIStructs
{
    [StructLayout(LayoutKind.Sequential)]
    class GeometryImportSettings
    {
        public float SmoothingAngle = 178f;
        public byte CalculateNormals = 0;
        public byte CalculateTangents = 1;
        public byte ReverseHandedness = 0;
        public byte ImportEnabededTextures = 1;
        public byte ImportAnimations = 1;

        private byte ToByte(bool value) => value ? (byte)1 : (byte)0;

        public void FromContentSettings(Content.Geometry geometry)
        {
            var settings = geometry.ImportSettings;
            SmoothingAngle = settings.SmoothingAngle;
            CalculateNormals = ToByte(settings.CalculateNormals);
            CalculateTangents = ToByte(settings.CalculateTangents);
            ReverseHandedness = ToByte(settings.ReverseHandedness);
            ImportEnabededTextures = ToByte(settings.ImportEmbeddedTextures);
            ImportAnimations = ToByte(settings.ImportAnimations);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class SceneData : IDisposable
    {
        public IntPtr Data;
        public int DataSize;
        public GeometryImportSettings ImportSettings = new GeometryImportSettings();

        public void Dispose()
        {
            Marshal.FreeCoTaskMem(Data);
            GC.SuppressFinalize(this);
        }

        ~SceneData()
        {
            Dispose();
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    class PrimitiveInitInfo
    {
        public Content.PrimitiveMeshType Type;
        public int SegmentsX = 1;
        public int SegmentsY = 1;
        public int SegmentsZ = 1;
        public Vector3 Size = new Vector3(1f);
        public int LOD = 0;
    }
}

namespace DXForgeEditor.DllWrappers
{
    static class ContentToolsAPI
    {
        private const string _toolsDLL = "ContentTools.dll";

        [DllImport(_toolsDLL)]
        private static extern void CreatePrimitiveMesh([In, Out] SceneData data, PrimitiveInitInfo info);
        public static void CreatePrimitiveMesh(Content.Geometry geometry, PrimitiveInitInfo info)
        {
            Debug.Assert(geometry.Type != null);
            using var sceneData = new SceneData();
            try
            {
                sceneData.ImportSettings.FromContentSettings(geometry);
                CreatePrimitiveMesh(sceneData, info);
                Debug.Assert(sceneData.Data != IntPtr.Zero && sceneData.DataSize > 0);
                var data = new byte[sceneData.DataSize];
                Marshal.Copy(sceneData.Data, data, 0, sceneData.DataSize);
                geometry.FromRawData(data);
            }
            catch (Exception ex)
            {
                Logger.Log(MessageType.Error, $"{info.Type} プリミティブメッシュの作成に失敗しました。");
                Debug.WriteLine(ex.Message);
            }
        }
    }
}
