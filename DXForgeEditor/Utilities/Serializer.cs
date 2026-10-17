using DXForgeEditor.GameProject;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.Serialization;
using System.Text;

namespace DXForgeEditor.Utilities
{
    public static class Serializer
    {
        public static void ToFile<T>(T instance, string path)
        {
            try
            {
                using var fs = new FileStream(path, FileMode.Create);
                var serializer = new DataContractSerializer(typeof(T));
                serializer.WriteObject(fs, instance);
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"デシリアライズに失敗: {instance} 場所: {path}");
                throw;
            }
        }

        public static T FromFile<T>(string path)
        {

            try
            {
                using var fs = new FileStream(path, FileMode.Open);
                var serializer = new DataContractSerializer(typeof(T));
                // ルート要素がnilの場合は従来どおりnullを返す(呼び出し元はnullを失敗として扱う)
                T instance = (T)serializer.ReadObject(fs)!;
                return instance;
            }
            catch (Exception ex)
            {
                Debug.WriteLine(ex.Message);
                Logger.Log(MessageType.Error, $"デシリアライズに失敗: {path}");
                throw;
            }
        }
    }
}