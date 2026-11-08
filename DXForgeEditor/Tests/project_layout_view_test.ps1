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
      2c. インスペクターの Transform の欄のドラッグと入力の確定がそれぞれ Undo 1 件になる。複数選択で値がそろっていない欄は空で、
          入力するとすべてが変わり、Undo でそれぞれの値に戻る(Primal Day 086 の NumberBox/VectorBox、#244)。
      3. シーンごとの Rename ボタンで、名前の入力欄が開く。
      4. 空のシーンのプロジェクトに切り替えると、インスペクターが空になる(#178)。
      5. 共通にした RemoveButtonStyle を使う、インポート設定のビューを作れる。
      3b. Hierarchy の既定の幅(約 222px)で、シーンの + と項目の削除ボタンが幅の中に入る。名前が長くても同じ。
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
$entityType = $editor.GetType("DXForgeEditor.Components.GameEntity", $true)
# Scene.AddGameEntities/RemoveGameEntities(Primal Day 084)に渡す List<GameEntity>
function EntityList($items) {
	$list = [Activator]::CreateInstance([System.Collections.Generic.List`1].MakeGenericType($entityType))
	foreach ($item in $items) { [void]$list.Add($item) }
	return ,$list
}
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
	# NOTE: アクティブなシーンでは、追加や Undo が GameEntity.IsActive からエンジンを呼ぶ。ここだけシーンをアクティブでなくして、エンジンを呼ばない
	#       (リストは無効の表示になるが、選択の処理と SelectionChanged は同じ)
	$scene.IsActive = $false
	# アクティブでないシーンの Expander は閉じる(閉じていると、追加した項目の ListBoxItem が作られない)ので、開いておく
	@(Find $view ([System.Windows.Controls.Expander]) | Where-Object { [object]::ReferenceEquals($_.DataContext, $scene.PSObject.BaseObject) })[0].IsExpanded = $true
	Pump
	$list.SelectedIndex = 2; Pump
	Check ($inspector.DataContext.Name -eq "Scaled") "select Scaled"
	$scene.RemoveGameEntities((EntityList @($scene.GameEntities[2]))); Pump
	Check ($list.SelectedIndex -eq -1 -and $scene.GameEntities.Count -eq 2 -and $null -eq $inspector.DataContext) "removing the selected entity empties the selection and the inspector"
	$list.SelectedIndex = 1; Pump
	$added = [Activator]::CreateInstance($entityType, $flags, $null, [object[]]@($scene), $null)
	$scene.AddGameEntities((EntityList @($added)), -1); Pump

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
	$scene.RemoveGameEntities((EntityList @($scene.GameEntities[0]))); Pump
	$list.SelectedIndex = 0; Pump
	$undo.Undo(); Pump
	Check ($list.SelectedItems.Count -eq 1 -and [object]::ReferenceEquals($list.SelectedItems[0], $selected.PSObject.BaseObject)) "undo selects the same entity after an earlier entity was removed"
	$undo.Undo(); Pump
	Check ($scene.GameEntities.Count -eq 3 -and [object]::ReferenceEquals($scene.GameEntities[2], $selected.PSObject.BaseObject)) "undoing the remove restores the order"

	# インスペクターの Name: Enter を押さずにフォーカスを移しても確定し、Undo できる。WPF はキーボードのフォーカスのイベントを
	# Preview → LostKeyboardFocus の順に出すので、その順に発生させる(Codex の指摘。実際のフォーカスの移動は GUI で確かめる)
	Write-Host "2b. renaming in the inspector without pressing Enter"
	$inspectorWindow = [System.Windows.Window]::new()
	$inspectorWindow.WindowStartupLocation = "Manual"
	$inspectorWindow.Left = -30000; $inspectorWindow.Top = -30000; $inspectorWindow.Width = 600; $inspectorWindow.Height = 600
	$inspectorWindow.ShowInTaskbar = $false; $inspectorWindow.ShowActivated = $false
	$inspectorWindow.Content = $inspector
	$inspectorWindow.Show()
	$list.SelectedIndex = 0; Pump
	$renamed = $scene.GameEntities[0]
	$oldName = $renamed.Name
	$nameBox = @(Find $inspector ([System.Windows.Controls.TextBox]) | Where-Object { $_.IsVisible })[0]
	function Raise-Focus($event)
	{
		$focusArgs = [System.Windows.Input.KeyboardFocusChangedEventArgs]::new([System.Windows.Input.Keyboard]::PrimaryDevice, 0, $null, $nameBox)
		$focusArgs.RoutedEvent = $event
		$nameBox.RaiseEvent($focusArgs)
	}
	Raise-Focus ([System.Windows.Input.Keyboard]::GotKeyboardFocusEvent)
	$nameBox.Text = "Typed Name"
	$undoCount = $undo.UndoList.Count
	Raise-Focus ([System.Windows.Input.Keyboard]::PreviewLostKeyboardFocusEvent)
	Raise-Focus ([System.Windows.Input.Keyboard]::LostKeyboardFocusEvent)
	Pump
	Check ($renamed.Name -eq "Typed Name" -and $undo.UndoList.Count -eq $undoCount + 1) "the name is committed and one undo entry is added ($($renamed.Name), $($undo.UndoList.Count - $undoCount))"
	$undo.Undo(); Pump
	Check ($renamed.Name -eq $oldName) "undo restores the name ($($renamed.Name))"

	# インスペクターの Transform の数値の欄(Primal Day 086 の NumberBox/VectorBox、#244): 1 回のドラッグと 1 回の入力の確定が
	# それぞれ Undo 1 件になり、複数選択ではすべてのエンティティが変わり、Undo でそれぞれの値に戻る。マウスの移動は
	# 表示しないウィンドウでは起こせないので、ボタンを押すイベントを出したあと、NumberBox のドラッグのメソッドを直接呼ぶ
	Write-Host "2c. dragging and typing in the inspector's Transform boxes"
	$numberBoxType = $editor.GetType("DXForgeEditor.Utilities.Controls.NumberBox", $true)
	$vectorBoxType = $editor.GetType("DXForgeEditor.Utilities.Controls.VectorBox", $true)
	function Position-Box
	{
		$vector = @(Find $inspector $vectorBoxType)[0]
		return $vector.Template.FindName("nbX", $vector)
	}
	$nbX = Position-Box
	$first = $scene.GameEntities[0]; $second = $scene.GameEntities[1]
	$firstTransform = @($first.Components)[0]; $secondTransform = @($second.Components)[0]
	$x0 = $firstTransform.Position.X; $x1 = $secondTransform.Position.X
	$textBox = $nbX.Template.FindName("PART_textBox", $nbX)
	$press = [System.Windows.Input.MouseButtonEventArgs]::new([System.Windows.Input.Mouse]::PrimaryDevice, 0, [System.Windows.Input.MouseButton]::Left)
	# NOTE: PreviewMouseLeftButtonDown は Direct のイベントで、WPF がトンネルの PreviewMouseDown から要素ごとに出し直す。
	#       PreviewMouseLeftButtonDown を直接出すと TextBox にしか届かないので、PreviewMouseDown を出す
	$press.RoutedEvent = [System.Windows.UIElement]::PreviewMouseDownEvent
	$undoCount = $undo.UndoList.Count
	$textBox.RaiseEvent($press)
	$null = [System.Windows.Input.Mouse]::Capture($null)
	# 押した位置はウィンドウの外の実際のマウスの位置なので、0 から始め直す
	$null = $numberBoxType.GetMethod("BeginDrag", $flags).Invoke($nbX, [object[]]@([double]0))
	$null = $numberBoxType.GetMethod("DragTo", $flags).Invoke($nbX, [object[]]@([double]50, [System.Windows.Input.ModifierKeys]::None))
	$null = $numberBoxType.GetMethod("DragTo", $flags).Invoke($nbX, [object[]]@([double]100, [System.Windows.Input.ModifierKeys]::None))
	$null = $numberBoxType.GetMethod("EndDrag", $flags).Invoke($nbX, @())
	Pump
	Check ([Math]::Abs($firstTransform.Position.X - ($x0 + 1)) -lt 1e-4 -and $undo.UndoList.Count -eq $undoCount + 1) "a drag of 100 px moves Position.X by 1 with one undo entry ($x0 -> $($firstTransform.Position.X), $($undo.UndoList.Count - $undoCount))"
	$undo.Undo(); Pump
	Check ([Math]::Abs($firstTransform.Position.X - $x0) -lt 1e-6 -and (Position-Box).Value -eq [double]$x0) "undo restores Position.X and the box ($($firstTransform.Position.X), $((Position-Box).Value))"
	$undo.Redo(); Pump
	Check ([Math]::Abs($firstTransform.Position.X - ($x0 + 1)) -lt 1e-4) "redo moves it again ($($firstTransform.Position.X))"
	$undo.Undo(); Pump

	# Position に入力し、Enter を押さずに Rotation を押す: WPF は Rotation の PreviewMouseDown を Position の確定
	# (PreviewLostKeyboardFocus)より先に出す。それでも Position の Undo は位置を戻す(行ごとに前の値を覚える。Codex の指摘)
	function Text-Box($numberBox) { return $numberBox.Template.FindName("PART_textBox", $numberBox) }
	$positionText = Text-Box (Position-Box)
	$focusArgs = [System.Windows.Input.KeyboardFocusChangedEventArgs]::new([System.Windows.Input.Keyboard]::PrimaryDevice, 0, $null, $positionText)
	$focusArgs.RoutedEvent = [System.Windows.Input.Keyboard]::GotKeyboardFocusEvent
	$positionText.RaiseEvent($focusArgs)
	$rotationVector = @(Find $inspector $vectorBoxType)[1]
	$rotationText = Text-Box ($rotationVector.Template.FindName("nbX", $rotationVector))
	$press = [System.Windows.Input.MouseButtonEventArgs]::new([System.Windows.Input.Mouse]::PrimaryDevice, 0, [System.Windows.Input.MouseButton]::Left)
	$press.RoutedEvent = [System.Windows.UIElement]::PreviewMouseDownEvent
	$rotationText.RaiseEvent($press)
	$null = [System.Windows.Input.Mouse]::Capture($null)
	$null = $numberBoxType.GetMethod("EndDrag", $flags).Invoke($rotationVector.Template.FindName("nbX", $rotationVector), @())
	$null = $numberBoxType.GetMethod("CommitText", $flags).Invoke((Position-Box), [object[]]@("7"))
	Pump
	Check ($firstTransform.Position.X -eq 7) "typing 7 into Position.X, committed after pressing Rotation.X ($($firstTransform.Position.X))"
	$undo.Undo(); Pump
	Check ([Math]::Abs($firstTransform.Position.X - $x0) -lt 1e-6) "undo still restores Position.X ($($firstTransform.Position.X))"

	$null = $list.SelectedItems.Add($second.PSObject.BaseObject); Pump
	$nbX = Position-Box
	Check ($x0 -ne $x1 -and $null -eq $nbX.Value) "two entities with different X: the box is mixed (null) ($($nbX.Value))"
	$textBox = $nbX.Template.FindName("PART_textBox", $nbX)
	$focusArgs = [System.Windows.Input.KeyboardFocusChangedEventArgs]::new([System.Windows.Input.Keyboard]::PrimaryDevice, 0, $null, $textBox)
	$focusArgs.RoutedEvent = [System.Windows.Input.Keyboard]::GotKeyboardFocusEvent
	$textBox.RaiseEvent($focusArgs)
	$undoCount = $undo.UndoList.Count
	$null = $numberBoxType.GetMethod("CommitText", $flags).Invoke($nbX, [object[]]@("5"))
	Pump
	Check ($firstTransform.Position.X -eq 5 -and $secondTransform.Position.X -eq 5 -and $undo.UndoList.Count -eq $undoCount + 1) "typing 5 sets both X with one undo entry ($($firstTransform.Position.X), $($secondTransform.Position.X), $($undo.UndoList.Count - $undoCount))"
	$undo.Undo(); Pump
	Check ($firstTransform.Position.X -eq $x0 -and $secondTransform.Position.X -eq $x1) "undo restores each entity's own X ($($firstTransform.Position.X), $($secondTransform.Position.X))"
	$list.SelectedIndex = 0; Pump
	$inspectorWindow.Content = $null
	$inspectorWindow.Close()

	$scene.IsActive = $true; Pump

	Write-Host "3. Rename"
	$rename = @(Find $view ([System.Windows.Controls.Button]) | Where-Object { $_.Content -eq "Rename" })
	Check ($rename.Count -eq 2) "one Rename button per scene ($($rename.Count))"
	$box = $rename[0].Tag
	Check ($box -is [System.Windows.Controls.TextBox] -and $box.Visibility -eq "Collapsed") "the rename box starts collapsed"
	$rename[0].RaiseEvent([System.Windows.RoutedEventArgs]::new([System.Windows.Controls.Primitives.ButtonBase]::ClickEvent)); Pump
	Check ($box.Visibility -eq "Visible") "Rename shows the box"
	$box.Visibility = "Collapsed"

	Write-Host "3b. the Hierarchy fits its default width (about 222 px)"
	$view.Width = 222; Pump
	$scroll = @(Find $view ([System.Windows.Controls.ScrollViewer]))[0]
	function Right-Edge($element) { return $element.TransformToAncestor($scroll).Transform([System.Windows.Point]::new($element.ActualWidth, 0)).X }
	function Check-Fits([string]$when)
	{
		$plus = @(Find $view ([System.Windows.Controls.Button]) | Where-Object { $_.Content -eq "+" })
		$activeList = @(Find $view ([System.Windows.Controls.ListBox]) | Where-Object { $_.IsEnabled })[0]
		$removeStyle = $app.FindResource("RemoveButtonStyle")
		$removes = @(Find $activeList ([System.Windows.Controls.Button]) | Where-Object { $_.Style -eq $removeStyle })
		$removes | ForEach-Object { $_.Visibility = "Visible" }
		Pump
		$rights = @($plus + $removes | ForEach-Object { Right-Edge $_ })
		$widest = ($rights | Measure-Object -Maximum).Maximum
		$removes | ForEach-Object { $_.ClearValue([System.Windows.UIElement]::VisibilityProperty) }
		Check ($plus.Count -eq 2 -and $removes.Count -ge 1 -and $scroll.ScrollableWidth -eq 0 -and $widest -le $scroll.ViewportWidth) "$when`: + and the remove buttons are inside the viewport (right edge $widest, viewport $($scroll.ViewportWidth))"
	}
	Check-Fits "default names"
	$sceneName = $scene.Name
	$scene.Name = "A very long scene name that does not fit in the Hierarchy at all, not even close"
	$longEntity = $scene.GameEntities[0]
	$entityName = $longEntity.Name
	$longEntity.Name = "A very long entity name that does not fit in the Hierarchy at all, not even close"
	Pump
	Check-Fits "long names"
	$scene.Name = $sceneName
	$longEntity.Name = $entityName
	$view.ClearValue([System.Windows.FrameworkElement]::WidthProperty); Pump

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

	# エンジンの DLL は最初の P/Invoke で読み込まれる。読み込まれていなければ、このテストはエンジンを呼んでいない
	Check (@([System.Diagnostics.Process]::GetCurrentProcess().Modules | Where-Object { $_.ModuleName -eq "DXForgeEngineDLL.dll" }).Count -eq 0) "the test did not call the engine (DXForgeEngineDLL.dll is not loaded)"
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
