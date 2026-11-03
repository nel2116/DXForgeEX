using System;
using System.Collections.Generic;

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
        /// FBXの埋め込みテクスチャ(引数: 出力先の &lt;FBXのアセット名&gt;.fbm へのコピーの結果(ContentHelper.CopyEmbeddedMedia), 出力先のフォルダ)を
        /// 取り込む処理。コピーはエディタと共通で、呼ぶ前に済んでいる。
        /// null ならエディタのインポート(コピーできた画像を ConfigureImportSettings.Import、非同期)を使う。
        /// </summary>
        public static Action<IReadOnlyList<EmbeddedMediaCopy>, string>? ImportEmbeddedFiles { get; set; }
    }
}
