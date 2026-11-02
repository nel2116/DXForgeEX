namespace DXForgeEditor.GameProject
{
    /// <summary>
    /// エディタの外(コマンドラインの dxforge, Issue #11)で .dxproj を読み込むときに、読み込みの副作用を止めるための設定。
    /// 既定値(false)のときは、エディタのこれまでの処理をそのまま使う。エディタはこれを設定しない(true にするのは dxforge だけ)。
    /// </summary>
    static class ProjectLoadEnvironment
    {
        /// <summary>
        /// true のとき、デシリアライズで次のことをしない。
        /// - Project: ゲームのコードの DLL のビルドと読み込み(Visual Studio の COM と DXForgeEngineDLL を使う)
        /// - Scene: アクティブなシーンのエンティティをエンジン(DXForgeEngineDLL)に作ること
        /// </summary>
        public static bool Headless { get; set; }
    }
}
