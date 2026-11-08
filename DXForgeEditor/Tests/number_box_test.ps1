<#
.SYNOPSIS
    NumberBox と VectorBox(Primal Day 086、Issue #244)の値の範囲とイベントを確かめる回帰テスト。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、コントロールを表示せずに作って、ドラッグと入力の確定
    (NumberBox の BeginDrag/DragTo/EndDrag と CommitText。マウスとキーのハンドラーが呼ぶもの)を直接呼ぶ。
    GPU もエンジンも ContentTools.dll も使わないので、CI でも動く。

    確かめること:
      1. ClampOrWrap: 端で止める。回り込みは Minimum が 0 でなくても正しい(-180〜180 で 190 は -170)。
      2. プログラムやバインディングで Value を変えても ValueChanging も ValueChanged も出ない。
      3. 入力の確定で ValueChanging と ValueChanged が 1 回ずつ出て、ValueChanged は前と後の値を持つ。範囲外は範囲に収め、
         数として読めない入力は受け付けない。整数の NumberBox は丸める。
      4. ドラッグでは 1 歩ごとに ValueChanging が出て、ValueChanged は離したときに 1 回だけ出る。動かさなければ何も出ない。
      5. 複数の値(Value が null)は空の文字列で表示し、そこから入力やドラッグをすると ValueChanged の前の値は NaN。
      6. VectorBox: 中の NumberBox の確定で、VectorBox の ValueChanged がベクトルの前と後の値で 1 回出て、X/Y/Z が変わる。
         Minimum/Maximum/WrapAround は中の NumberBox に渡る。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/number_box_test.ps1
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
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
[System.Windows.Application].GetField("_resourceAssembly", [System.Reflection.BindingFlags]"NonPublic,Static").SetValue($null, $editor)
$null = [System.Windows.Application]::new()
foreach ($name in @("EditorColors", "ControlTemplates"))
{
	$dictionary = [System.Windows.Application]::LoadComponent([Uri]::new("/DXForgeEditor;component/Dictionaries/$name.xaml", [UriKind]::Relative))
	[System.Windows.Application]::Current.Resources.MergedDictionaries.Add($dictionary)
}
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
$numberBoxType = $editor.GetType("DXForgeEditor.Utilities.Controls.NumberBox", $true)
$vectorBoxType = $editor.GetType("DXForgeEditor.Utilities.Controls.VectorBox", $true)
$clampOrWrap = $numberBoxType.GetMethod("ClampOrWrap", $flags)
$commitText = $numberBoxType.GetMethod("CommitText", $flags)
$beginDrag = $numberBoxType.GetMethod("BeginDrag", $flags)
$dragTo = $numberBoxType.GetMethod("DragTo", $flags)
$endDrag = $numberBoxType.GetMethod("EndDrag", $flags)
$formatValue = $numberBoxType.GetMethod("FormatValue", $flags)
$none = [System.Windows.Input.ModifierKeys]::None

function Near([double]$a, [double]$b) { return [Math]::Abs($a - $b) -lt 1e-6 }
function Clamp-Or-Wrap([double]$value, [double]$min, [double]$max, [bool]$wrap) { return [double]$clampOrWrap.Invoke($null, [object[]]@($value, $min, $max, $wrap)) }

# NumberBox を作り、イベントを数える
function New-NumberBox
{
	$box = [Activator]::CreateInstance($numberBoxType)
	$log = [pscustomobject]@{ Changing = 0; Changed = [System.Collections.Generic.List[object]]::new() }
	$box.AddHandler($numberBoxType.GetField("ValueChangingEvent").GetValue($null), [System.Windows.RoutedEventHandler]{ param($s, $e) $log.Changing++ }.GetNewClosure())
	$box.AddHandler($numberBoxType.GetField("ValueChangedEvent").GetValue($null), [System.Windows.RoutedPropertyChangedEventHandler[double]]{ param($s, $e) $log.Changed.Add(@($e.OldValue, $e.NewValue)) }.GetNewClosure())
	return [pscustomobject]@{ Box = $box; Log = $log }
}
function Type-Text($box, [string]$text) { return [bool]$commitText.Invoke($box, [object[]]@($text)) }
function Drag($box, [double[]]$xs)
{
	$null = $beginDrag.Invoke($box, [object[]]@([double]0))
	foreach ($x in $xs) { $null = $dragTo.Invoke($box, [object[]]@($x, $none)) }
	return [bool]$endDrag.Invoke($box, @())
}

Write-Host "1. ClampOrWrap"
Check ((Near (Clamp-Or-Wrap 5 0 3 $false) 3) -and (Near (Clamp-Or-Wrap -5 0 3 $false) 0) -and (Near (Clamp-Or-Wrap 2 0 3 $false) 2)) "clamp: 5 -> 3, -5 -> 0, 2 -> 2"
Check ((Near (Clamp-Or-Wrap 370 0 360 $true) 10) -and (Near (Clamp-Or-Wrap -10 0 360 $true) 350) -and (Near (Clamp-Or-Wrap 360 0 360 $true) 0)) "wrap 0..360: 370 -> 10, -10 -> 350, 360 -> 0"
$wrapped = @((Clamp-Or-Wrap 190 -180 180 $true), (Clamp-Or-Wrap -190 -180 180 $true), (Clamp-Or-Wrap 540 -180 180 $true), (Clamp-Or-Wrap 45 -180 180 $true))
Check ((Near $wrapped[0] -170) -and (Near $wrapped[1] 170) -and (Near $wrapped[2] -180) -and (Near $wrapped[3] 45)) "wrap -180..180 (min != 0): 190 -> -170, -190 -> 170, 540 -> -180, 45 -> 45 ($($wrapped -join ', '))"
Check ((Near (Clamp-Or-Wrap 7 2 2 $true) 2) -and [double]::IsNaN((Clamp-Or-Wrap ([double]::NaN) 0 1 $false))) "an empty range clamps to it; NaN stays NaN"

Write-Host "2. programmatic changes raise no events"
$t = New-NumberBox
$t.Box.Value = 4.5
$t.Box.Value = $null
$t.Box.Value = 1
Check ($t.Log.Changing -eq 0 -and $t.Log.Changed.Count -eq 0) "setting Value from code: no ValueChanging/ValueChanged ($($t.Log.Changing), $($t.Log.Changed.Count))"
$t.Box.Minimum = 2
Check ((Near $t.Box.Value 2) -and $t.Log.Changed.Count -eq 0) "raising Minimum coerces Value without events ($($t.Box.Value))"
$t.Box.Maximum = 0
Check ((Near $t.Box.Maximum 2) -and (Near $t.Box.Value 2)) "Maximum below Minimum becomes Minimum ($($t.Box.Maximum))"

Write-Host "3. typing"
$t = New-NumberBox
$t.Box.Value = 1
$ok = Type-Text $t.Box "2.5"
Check ($ok -and (Near $t.Box.Value 2.5) -and $t.Log.Changing -eq 1 -and $t.Log.Changed.Count -eq 1 -and (Near $t.Log.Changed[0][0] 1) -and (Near $t.Log.Changed[0][1] 2.5)) "commit 2.5: one ValueChanging, one ValueChanged 1 -> 2.5 ($($t.Log.Changing), $($t.Log.Changed | ForEach-Object { $_ -join '->' }))"
$ok = Type-Text $t.Box "2.5"
Check ($ok -and $t.Log.Changing -eq 1 -and $t.Log.Changed.Count -eq 1) "committing the same value raises nothing"
$ok = Type-Text $t.Box "abc"
$ok2 = Type-Text $t.Box "NaN"
$ok3 = Type-Text $t.Box ""
Check (-not $ok -and -not $ok2 -and -not $ok3 -and (Near $t.Box.Value 2.5) -and $t.Log.Changed.Count -eq 1) "not a number, NaN and empty text are rejected and the value stays 2.5 ($($t.Box.Value))"
$t.Box.Minimum = -1; $t.Box.Maximum = 10
$null = Type-Text $t.Box "20"
Check ((Near $t.Box.Value 10) -and (Near $t.Log.Changed[-1][1] 10)) "commit 20 with Maximum 10 -> 10 ($($t.Box.Value))"
$t.Box.Minimum = -180; $t.Box.Maximum = 180; $t.Box.WrapAround = $true
$null = Type-Text $t.Box "190"
Check (Near $t.Box.Value -170) "commit 190 with WrapAround -180..180 -> -170 ($($t.Box.Value))"
$i = New-NumberBox
$i.Box.IsInteger = $true
$null = Type-Text $i.Box "2.6"
Check ((Near $i.Box.Value 3) -and [string]$formatValue.Invoke($i.Box, @()) -eq "3") "IsInteger: 2.6 -> 3, shown as 3 ($($i.Box.Value))"
$i.Box.Minimum = 0; $i.Box.Maximum = 3.7
$null = Type-Text $i.Box "4"
Check (Near $i.Box.Value 3) "IsInteger with Maximum 3.7: 4 -> 3, not 4 (rounding stays in range) ($($i.Box.Value))"
$i.Box.Maximum = 360; $i.Box.WrapAround = $true
$null = Type-Text $i.Box "359.6"
Check (Near $i.Box.Value 0) "IsInteger with WrapAround 0..360: 359.6 -> 0, not 360 ($($i.Box.Value))"
$i.Box.Minimum = 0.8; $i.Box.Maximum = 3.7
$i.Box.Value = 3.6
Check (Near $i.Box.Value 1) "IsInteger with WrapAround 0.8..3.7 (integers 1, 2, 3): 3.6 -> 4 -> 1, an integer ($($i.Box.Value))"
$null = Type-Text $i.Box "-0.2"
Check (Near $i.Box.Value 3) "IsInteger with WrapAround 0.8..3.7: -0.2 -> 0 -> 3 ($($i.Box.Value))"
$i.Box.WrapAround = $false; $i.Box.Minimum = 0.2; $i.Box.Maximum = 0.8
$null = Type-Text $i.Box "0.5"
Check (Near $i.Box.Value 0.5) "IsInteger with no integer in 0.2..0.8: keeps 0.5 in range ($($i.Box.Value))"

Write-Host "4. dragging"
$t = New-NumberBox
$t.Box.Value = 0
$dragged = Drag $t.Box @(50, 100)
# 0.01 / px、Multiplier 1、感度 1: 100 px で 1
Check ($dragged -and (Near $t.Box.Value 1) -and $t.Log.Changing -eq 2 -and $t.Log.Changed.Count -eq 1 -and (Near $t.Log.Changed[0][0] 0) -and (Near $t.Log.Changed[0][1] 1)) "drag 100 px in two steps: Value 1, two ValueChanging, one ValueChanged 0 -> 1 ($($t.Box.Value), $($t.Log.Changing), $($t.Log.Changed.Count))"
$dragged = Drag $t.Box @(1)
Check (-not $dragged -and (Near $t.Box.Value 1) -and $t.Log.Changing -eq 2 -and $t.Log.Changed.Count -eq 1) "a 1 px move is a click: no change, no events"
$t.Box.Minimum = 0; $t.Box.Maximum = 1.5
$null = Drag $t.Box @(100)
Check ((Near $t.Box.Value 1.5) -and $t.Log.Changed.Count -eq 2) "dragging past Maximum stops at 1.5 ($($t.Box.Value))"

Write-Host "5. mixed values"
$t = New-NumberBox
$t.Box.Value = $null
Check ([string]$formatValue.Invoke($t.Box, @()) -eq "") "null (mixed) is shown as empty text"
$null = Type-Text $t.Box "3"
Check ((Near $t.Box.Value 3) -and $t.Log.Changed.Count -eq 1 -and [double]::IsNaN($t.Log.Changed[0][0]) -and (Near $t.Log.Changed[0][1] 3)) "typing into a mixed value: ValueChanged NaN -> 3"
$t.Box.Value = $null
$null = Drag $t.Box @(100)
Check ((Near $t.Box.Value 1) -and $t.Log.Changed.Count -eq 2 -and [double]::IsNaN($t.Log.Changed[1][0])) "dragging a mixed value starts at 0 (same as before) and reports NaN as the old value ($($t.Box.Value))"

Write-Host "6. VectorBox"
$vector = [Activator]::CreateInstance($vectorBoxType)
$vector.X = 1; $vector.Y = $null; $vector.Z = 3
$vector.Minimum = -180; $vector.Maximum = 180; $vector.WrapAround = $true
# 表示しないので、スタイル(ControlTemplates.xaml)を自分で付ける
$vector.Style = [System.Windows.Application]::Current.FindResource($vectorBoxType)
$null = $vector.ApplyTemplate()
$nbX = $vector.Template.FindName("nbX", $vector)
$nbY = $vector.Template.FindName("nbY", $vector)
Check ($null -ne $nbX -and (Near $nbX.Value 1) -and $null -eq $nbY.Value -and (Near $nbX.Minimum -180) -and (Near $nbX.Maximum 180) -and $nbX.WrapAround) "the inner NumberBoxes show X/Y/Z and get Minimum/Maximum/WrapAround ($($nbX.Value), $($nbY.Value), $($nbX.Minimum)..$($nbX.Maximum))"
$vlog = [pscustomobject]@{ Changing = 0; Changed = [System.Collections.Generic.List[object]]::new(); Inner = 0 }
$vector.AddHandler($vectorBoxType.GetField("ValueChangingEvent").GetValue($null), [System.Windows.RoutedEventHandler]{ param($s, $e) $vlog.Changing++ }.GetNewClosure())
$vector.AddHandler($vectorBoxType.GetField("ValueChangedEvent").GetValue($null), [System.Windows.RoutedPropertyChangedEventHandler[System.Numerics.Vector4]]{ param($s, $e) $vlog.Changed.Add(@($e.OldValue, $e.NewValue)) }.GetNewClosure())
$vector.AddHandler($numberBoxType.GetField("ValueChangedEvent").GetValue($null), [System.Windows.RoutedPropertyChangedEventHandler[double]]{ param($s, $e) $vlog.Inner++ }.GetNewClosure())
$null = Type-Text $nbX "190"
$old = if ($vlog.Changed.Count -eq 1) { $vlog.Changed[0][0] } else { $null }
$new = if ($vlog.Changed.Count -eq 1) { $vlog.Changed[0][1] } else { $null }
Check ((Near $vector.X -170) -and $vlog.Changing -eq 1 -and $vlog.Changed.Count -eq 1 -and $vlog.Inner -eq 0) "typing 190 into X: X wraps to -170, one VectorBox ValueChanging and ValueChanged, the inner event is handled ($($vector.X), $($vlog.Changing), $($vlog.Changed.Count), inner $($vlog.Inner))"
Check ($null -ne $old -and (Near $old.X 1) -and (Near $new.X -170) -and [double]::IsNaN($old.Y) -and [double]::IsNaN($new.Y) -and (Near $old.Z 3) -and (Near $new.Z 3)) "ValueChanged: old (1, NaN, 3) -> new (-170, NaN, 3) ($old -> $new)"

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
