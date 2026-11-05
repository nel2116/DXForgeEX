<#
.SYNOPSIS
    Project Layout のビュー(Primal Day 081, Issue #185)を確かめる回帰テスト。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、ProjectLayoutView を画面の外のウィンドウに置いて、ヘッドレスで読み込んだ
    プロジェクト(DXForgeCli/Tests/Projects/TransformOnly)を表示する。エンジンも GPU も使わない。

    確かめること:
      1. アクティブなシーンのリストが読み込まれると先頭のエンティティが選ばれ、インスペクターに出る。スクロールバーを持たない。
      2. 選択の undo/redo。選択したエンティティを削除・追加したあとで、履歴の最初まで undo し、最後まで redo しても落ちない。
         選択したエンティティより前のものを消して番号がずれても、同じエンティティに戻る。
      3. シーンごとの Rename ボタンで、名前の入力欄が開く。
      4. 空のシーンのプロジェクトに切り替えると、インスペクターが空になる(#178)。
      5. 共通にした RemoveButtonStyle を使う、インポート設定のビューを作れる。
      6. VisualStudio.IsDebugging と CanStart は、ビルドの lock が取られていても待たずに false を返す(コマンドの CanExecute で UI スレッドを止めない)。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/project_layout_view_test.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$failures = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }

Add-Type -AssemblyName PresentationFramework
$app = [System.Windows.Application]::new()
$app.ShutdownMode = "OnExplicitShutdown"
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
foreach ($name in @("EditorColors", "ControlTemplates"))
{
	$app.Resources.MergedDictionaries.Add([System.Windows.Application]::LoadComponent([Uri]::new("/DXForgeEditor;component/Dictionaries/$name.xaml", [UriKind]::Relative)))
}

function Pump { 1..3 | ForEach-Object { $app.Dispatcher.Invoke([Action] {}, [System.Windows.Threading.DispatcherPriority]::ApplicationIdle) } }

function Find($root, [type]$type)
{
	$found = @()
	$count = [System.Windows.Media.VisualTreeHelper]::GetChildrenCount($root)
	for ($i = 0; $i -lt $count; ++$i)
	{
		$child = [System.Windows.Media.VisualTreeHelper]::GetChild($root, $i)
		if ($child -is $type) { $found += $child }
		$found += @(Find $child $type)
	}
	return $found
}

$editor.GetType("DXForgeEditor.GameProject.ProjectLoadEnvironment", $true).GetProperty("Headless", $flags).SetValue($null, $true)
$projectType = $editor.GetType("DXForgeEditor.GameProject.Project", $true)
$load = $projectType.GetMethod("Load", $flags)
$fixture = Join-Path $RepoRoot "DXForgeCli\Tests\Projects\TransformOnly\TransformOnly.dxproj"
$undo = $projectType.GetProperty("UndoRedo", $flags).GetValue($null)
$work = Join-Path ([System.IO.Path]::GetTempPath()) "dxforge_layout_test_$([Guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Force $work
$win = $null

try
{
	$project = $load.Invoke($null, [object[]]@([string]$fixture))
	$inspector = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Editors.GameEntityView", $true))
	$view = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Editors.ProjectLayoutView", $true))
	$view.DataContext = $project.PSObject.BaseObject
	$win = [System.Windows.Window]::new()
	$win.WindowStartupLocation = "Manual"
	$win.Left = -30000; $win.Top = -30000; $win.Width = 600; $win.Height = 600
	$win.ShowInTaskbar = $false; $win.ShowActivated = $false
	$win.Content = $view
	$win.Show()
	Pump

	Write-Host "1. Loaded"
	$active = @(Find $view ([System.Windows.Controls.ListBox]) | Where-Object { $_.IsEnabled })
	Check ($active.Count -eq 1 -and $active[0].SelectedIndex -eq 0) "the first entity of the active scene is selected (active lists $($active.Count))"
	Check ($inspector.DataContext -and $inspector.DataContext.Name -eq "Origin") "the inspector shows Origin ($($inspector.DataContext.Name))"
	$list = $active[0]
	$scene = $list.DataContext
	Check ($undo.UndoList.Count -eq 1) "one undo entry after loading ($($undo.UndoList.Count))"
	Check (@(Find $list ([System.Windows.Controls.ScrollViewer])).Count -eq 0) "the list has no ScrollViewer"
	Check (@(Find $list ([System.Windows.Controls.Button])).Count -ge 1) "an item has the remove button"

	Write-Host "2. selection undo/redo"
	$list.SelectedIndex = 2; Pump
	Check ($inspector.DataContext.Name -eq "Scaled") "select Scaled"
	$scene.RemoveGameEntityCommand.Execute($scene.GameEntities[2]); Pump
	Check ($list.SelectedIndex -eq -1 -and $scene.GameEntities.Count -eq 2 -and $null -eq $inspector.DataContext) "removing the selected entity empties the selection and the inspector"
	$list.SelectedIndex = 1; Pump
	$added = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Components.GameEntity", $true), $flags, $null, [object[]]@($scene), $null)
	$scene.AddGameEntityCommand.Execute($added); Pump

	# Undo の最初の 2 件は「エンティティの追加」(選択は Moved のまま)と「Moved の選択」
	$undo.Undo(); Pump
	Check ($scene.GameEntities.Count -eq 2 -and (@($list.SelectedItems | ForEach-Object { $_.Name }) -join ",") -eq "Moved") "undoing the add keeps Moved selected"
	$undo.Undo(); Pump
	Check ($list.SelectedItems.Count -eq 0) "undoing the selection empties it"
	$undo.Undo(); Pump
	Check ($scene.GameEntities.Count -eq 3 -and $scene.GameEntities[2].Name -eq "Scaled") "undoing the remove brings Scaled back"
	$undo.Undo(); Pump
	Check ((@($list.SelectedItems | ForEach-Object { $_.Name }) -join ",") -eq "Scaled") "after that, undo selects Scaled again"
	while ($undo.UndoList.Count -gt 0) { $undo.Undo(); Pump }
	Check ($list.SelectedItems.Count -eq 0) "undoing everything does not throw and empties the selection"
	while ($undo.RedoList.Count -gt 0) { $undo.Redo(); Pump }
	Check ($scene.GameEntities.Count -eq 3 -and (@($list.SelectedItems | ForEach-Object { $_.Name }) -join ",") -eq "Moved") "redoing everything restores the entities and the selection"

	# 選択したままその前のエンティティを消すと番号がずれる(SelectionChanged は来ない)。それでも同じエンティティに戻る(Codex の指摘)
	$list.SelectedIndex = 2; Pump
	$selected = $scene.GameEntities[2]
	$scene.RemoveGameEntityCommand.Execute($scene.GameEntities[0]); Pump
	$list.SelectedIndex = 0; Pump
	$undo.Undo(); Pump
	Check ($list.SelectedItems.Count -eq 1 -and [object]::ReferenceEquals($list.SelectedItems[0], $selected.PSObject.BaseObject)) "undo selects the same entity after an earlier entity was removed"
	$undo.Undo(); Pump
	Check ($scene.GameEntities.Count -eq 3 -and [object]::ReferenceEquals($scene.GameEntities[2], $selected.PSObject.BaseObject)) "undoing the remove restores the order"

	Write-Host "3. Rename"
	$rename = @(Find $view ([System.Windows.Controls.Button]) | Where-Object { $_.Content -eq "Rename" })
	Check ($rename.Count -eq 2) "one Rename button per scene ($($rename.Count))"
	$box = $rename[0].Tag
	Check ($box -is [System.Windows.Controls.TextBox] -and $box.Visibility -eq "Collapsed") "the rename box starts collapsed"
	$rename[0].RaiseEvent([System.Windows.RoutedEventArgs]::new([System.Windows.Controls.Primitives.ButtonBase]::ClickEvent)); Pump
	Check ($box.Visibility -eq "Visible") "Rename shows the box"
	$box.Visibility = "Collapsed"

	Write-Host "4. switching projects (#178)"
	$xml = [System.IO.File]::ReadAllText($fixture)
	$emptied = [regex]::Replace($xml, '(?s)<Scene z:Id="i2"><GameEntities([^>]*)>.*?</GameEntities>', '<Scene z:Id="i2"><GameEntities$1></GameEntities>')
	Check ($emptied -ne $xml) "the fixture was edited to have an empty active scene"
	$emptyFile = Join-Path $work "Empty.dxproj"
	[System.IO.File]::WriteAllText($emptyFile, $emptied)
	$emptyProject = $load.Invoke($null, [object[]]@([string]$emptyFile))
	$list.SelectedIndex = 0; Pump
	Check ($null -ne $inspector.DataContext) "before the switch the inspector shows an entity"
	$view.DataContext = $null; Pump
	$view.DataContext = $emptyProject.PSObject.BaseObject; Pump
	$after = @(Find $view ([System.Windows.Controls.ListBox]) | Where-Object { $_.IsEnabled })
	Check ($after.Count -eq 1 -and $after[0].Items.Count -eq 0) "the new active list is empty"
	Check ($null -eq $inspector.DataContext) "the inspector is empty after the switch"

	Write-Host "5. import settings views"
	foreach ($type in @("ConfigureGeometryImportSettingsView", "ConfigureTextureImportSettingsView"))
	{
		Check ($null -ne [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Content.$type", $true))) "$type can be created"
	}

	Write-Host "6. VisualStudio.IsDebugging and CanStart do not wait for a running build"
	$visualStudio = $editor.GetType("DXForgeEditor.GameDev.VisualStudio", $true)
	$isDebugging = $visualStudio.GetMethod("IsDebugging", $flags)
	$canStart = $visualStudio.GetMethod("CanStart", $flags)
	Check (-not $isDebugging.Invoke($null, @()) -and $canStart.Invoke($null, @())) "with no build and no Visual Studio: not debugging, can start"
	$buildLock = $visualStudio.GetField("_lock", $flags).GetValue($null)
	Add-Type -TypeDefinition @"
namespace DXForgeLayoutTest
{
	public static class LockHolder
	{
		// 別のスレッドで lock を持ち続ける(ビルドの代わり)。テストが止まらないよう、10 秒で放す
		public static System.Threading.Thread Hold(object target, System.Threading.ManualResetEventSlim held, System.Threading.ManualResetEventSlim release)
		{
			var thread = new System.Threading.Thread(() => { lock (target) { held.Set(); release.Wait(10000); } }) { IsBackground = true };
			thread.Start();
			return thread;
		}
	}
}
"@
	$held = [System.Threading.ManualResetEventSlim]::new($false)
	$release = [System.Threading.ManualResetEventSlim]::new($false)
	$holder = [DXForgeLayoutTest.LockHolder]::Hold($buildLock, $held, $release)
	Check ($held.Wait(5000)) "another thread holds the build lock"
	$watch = [System.Diagnostics.Stopwatch]::StartNew()
	$debuggingWhileBusy = $isDebugging.Invoke($null, @())
	$canStartWhileBusy = $canStart.Invoke($null, @())
	$watch.Stop()
	$release.Set()
	$null = $holder.Join(11000)
	Check ($watch.Elapsed.TotalSeconds -lt 5 -and -not $debuggingWhileBusy -and -not $canStartWhileBusy) "while the lock is held both return false without waiting ($($watch.ElapsedMilliseconds) ms)"
	Check ($canStart.Invoke($null, @())) "after the lock is released, can start again"
}
finally
{
	if ($win) { $win.Close() }
	$undo.Reset()
	Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
	$app.Shutdown()
}

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "FAILED: $($failures.Count)"
	$failures | ForEach-Object { Write-Host "  $_" }
	exit 1
}
Write-Host "ALL PASSED"
exit 0
