using System;

namespace DXForgeEditor.Content
{
    /// <summary>
    /// エディタの外(プロジェクトを開いていない状態)でアセットをインポートするときに、エディタの画面やプロジェクトに
    /// 依存する処理を差し替えるための設定(コマンドラインのインポーター DXForgeImport, Issue #10)。
    /// 既定値(null)のときは、エディタのこれまでの処理をそのまま使う。エディタはこれを設定しない。
    /// </summary>
    static class ContentImportEnvironment
    {
        /// <summary>
        /// FBXをインポートするときにコピーを置く一時フォルダ(末尾は区切り文字)。null ならプロジェクトの一時フォルダ(Project.Current.TempFolder)。
        /// </summary>
        public static string? TempFolder { get; set; }

        /// <summary>
        /// FBXの埋め込みテクスチャ(引数: 画像ファイルの一覧, 出力先のフォルダ)を取り込む処理。
        /// null ならエディタのインポート(ConfigureImportSettings.Import、非同期)を使う。
        /// </summary>
        public static Action<string[], string>? ImportEmbeddedFiles { get; set; }
    }
}
