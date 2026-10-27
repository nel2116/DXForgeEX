using System.Runtime.CompilerServices;
using System.Windows;

[assembly: ThemeInfo(
    ResourceDictionaryLocation.None,            //where theme specific resource dictionaries are located
                                                //(used if a resource is not found in the page,
                                                // or application resource dictionaries)
    ResourceDictionaryLocation.SourceAssembly   //where the generic resource dictionary is located
                                                //(used if a resource is not found in the page,
                                                // app, or any theme specific resource dictionaries)
)]

// コマンドラインのインポーター(DXForgeImport, Issue #10)が、エディタと同じ .asset の書き出し(Content の internal な型)を使うため。
[assembly: InternalsVisibleTo("DXForgeImport")]
