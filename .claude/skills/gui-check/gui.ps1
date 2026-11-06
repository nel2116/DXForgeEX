<#
.SYNOPSIS
	GUI 確認の操作の入口(#195)。エディタの起動、プロジェクトの作成と切り替え、クリック、ドラッグ、撮影、UI ツリーの読み取りをする。

.DESCRIPTION
	要素は操作のたびに UI Automation で読み直す(orca computer の番号のように古くならない)。子ウィンドウ(ダイアログ)も読める。
	クリックとドラッグは実際のマウス操作にする(Project Browser のタブなど、Invoke では動かないものがあるため)。
	座標は per-monitor DPI aware の物理座標で、ウィンドウの左上からの相対。

	対象(-Target、-To)の書き方:
	  <名前>             部品の中の名前。一覧は -Action names
	  x,y                ウィンドウの左上からの座標(ウィンドウは -Title。省略時は一番大きいもの)
	  el:<種類>:<正規表現>[#n]  種類(Button、MenuItem、Text など。クラス名でもよい)と名前で探す。n は一致したものの番号(0 から)
	                     Text と ListItem は左端の近くを押す(Hierarchy の行は右に Remove があるため)。ほかは中央

	前面に出す操作(ALT を押して離す)は、対象がすでに前面なら行わない。ALT で開いたドロップダウンが閉じるため。
	-NoFocus を付けると、前面でなくても行わない。

	Action:
	  names           名前付きの対象と領域の一覧
	  open            エディタを起動し、-Project を開く。最後の行が PID <n>
	  create          Empty Project から -Project(GuiCheck で始まる名前)を作る。-ProcId を渡すと File > New Project から
	  switch          開いているエディタ(-ProcId)で File > Open Project から -Project に切り替える
	  close           WM_CLOSE で閉じる。Project Browser が残ったら続けて閉じ、終了を確かめる。ゲームや VS の PID も渡せる
	  click           -Target を押す([-Clicks n])
	  drag            -Target から -To へドラッグする
	  keys            -Keys を SendKeys で送る(-ProcId を渡すと先に前面に出す)
	  shot            撮影する。-Region(名前か x,y,w,h)か -Target の要素の範囲。省略はウィンドウ全体。幅は -MaxWidth(既定 1000、0 で縮小なし)まで
	  tree            UI ツリーをテキストで出す(-Pattern で絞る。判定はまずこれで行う)
	  wait            -Target か -Title が現れるまで待つ
	  move            ウィンドウを -X -Y -W -H に動かす
	  prepare         GUI なしで中身を作る。-Import のファイルを DXForgeImport で Content に取り込み、-Ops か -Op を dxforge で実行する
	  remove-project  確認用のプロジェクト(GuiCheck で始まる名前)を消す
	  diff            -Dir の base-*.png と pr-*.png の画素の差を出す(pixdiff.py)

	終了コード: 0 成功、1 失敗(見つからない、時間切れなど)、2 引数の誤り。ツールの終了コードがあるときはそれを返す(prepare)。

.EXAMPLE
	$p = ./.claude/skills/gui-check/gui.ps1 -Action create -Project GuiCheck200
	./.claude/skills/gui-check/gui.ps1 -Action tree -ProcId 1234 -Pattern 'Hierarchy|MenuItem'
	./.claude/skills/gui-check/gui.ps1 -Action click -ProcId 1234 -Target AddEntity
	./.claude/skills/gui-check/gui.ps1 -Action shot -ProcId 1234 -Region Hierarchy -OutFile $env:TEMP\gui-check-pr200\pr-hierarchy.png
	./.claude/skills/gui-check/gui.ps1 -Action close -ProcId 1234
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)]
	[ValidateSet("names", "open", "create", "switch", "close", "click", "drag", "keys", "shot", "tree", "wait", "move", "prepare", "remove-project", "diff")]
	[string]$Action,

	[int]$ProcId,
	[string]$Exe,
	[string]$Project,
	[string]$Title,
	[object]$Target,
	[object]$To,
	[int]$Clicks = 1,
	[switch]$NoFocus,
	[string]$Keys,
	[string]$OutFile,
	[object]$Region,
	[int]$MaxWidth = 1000,
	[string]$Pattern,
	[int]$MaxLines = 150,
	[switch]$All,
	[int]$TimeoutSeconds = 60,
	[int]$X = -1,
	[int]$Y = -1,
	[int]$W = 0,
	[int]$H = 0,
	[string[]]$Import,
	[string]$Ops,
	[string]$Op,
	[string]$ToolDir,
	[string]$ProjectsRoot,
	[string]$Dir
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
# 引用符なしの 22,165 は配列で届く
$Target = @($Target) -join ","
$To = @($To) -join ","
$Region = @($Region) -join ","
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path

$EditorTitle = "DXForge Editor"
$BrowserTitle = "Project Browser"

# 表示倍率 100%、既定の大きさ(1280x720)のウィンドウでの位置。ウィンドウの DPI に合わせて変換する(WPF は倍率に合わせて拡大される)。
# open と create は既定の大きさのまま開く。大きさを変えたら el: を使う
$Named = [ordered]@{
	File             = @{ Window = $EditorTitle; Point = 21, 40 }
	OpenTab          = @{ Window = $BrowserTitle; Element = "Button:^Open Project$" }
	CreateTab        = @{ Window = $BrowserTitle; Element = "Button:^Create Project$" }
	NewScript        = @{ Window = $EditorTitle; Element = "Button:^New Script$" }
	Build            = @{ Window = $EditorTitle; Element = "Button:^Build$" }
	PrimitiveMesh    = @{ Window = $EditorTitle; Element = "Button:^Primitive mesh$" }
	AddEntity        = @{ Window = $EditorTitle; Element = "Button:^\+$" }
	# 1 つ目のシーンの最初の行。左端を押す(右に Remove があり、中央や右を押すと消える)
	FirstEntity      = @{ Window = $EditorTitle; Point = 22, 165 }
	# 1 つ目のシーンの ListBox(MinHeight 48)の中央。エンティティがなくても落とせる範囲はここだけ
	EntityDrop       = @{ Window = $EditorTitle; Point = 117, 174 }
	Viewport         = @{ Window = $EditorTitle; Point = 603, 279 }
	ContentBrowser   = @{ Window = $EditorTitle; Point = 55, 484 }
	LoggerView       = @{ Window = $EditorTitle; Point = 142, 484 }
	UndoRedo         = @{ Window = $EditorTitle; Point = 215, 484 }
	ContentList      = @{ Window = $EditorTitle; Point = 300, 640 }
}
$Regions = [ordered]@{
	Menu      = @{ Window = $EditorTitle; Rect = 6, 25, 500, 70 }
	Hierarchy = @{ Window = $EditorTitle; Rect = 6, 95, 222, 368 }
	Viewport  = @{ Window = $EditorTitle; Rect = 233, 95, 740, 368 }
	Inspector = @{ Window = $EditorTitle; Rect = 978, 95, 296, 368 }
	Bottom    = @{ Window = $EditorTitle; Rect = 6, 468, 1268, 246 }
}

function Stop-Usage([string]$Message) {
	Write-Host $Message
	exit 2
}

function Test-GuiCheckName([string]$Name) { return $Name -cmatch '^GuiCheck[A-Za-z0-9_]*$' }

function Get-ProjectsRoot {
	if ($ProjectsRoot) { return $ProjectsRoot }
	# エディタ(NewProject.GetDefaultProjectPath、ProjectsPath.Resolve)と同じく、プロセスの値ではなくユーザー環境変数を読む(読むだけ)
	$value = [Environment]::GetEnvironmentVariable("DXFORGE_PROJECTS", "User")
	if ($value -and $value.Trim()) {
		$path = [Environment]::ExpandEnvironmentVariables($value.Trim())
		try {
			if ($path.IndexOfAny([System.IO.Path]::GetInvalidPathChars()) -lt 0 -and [System.IO.Path]::IsPathFullyQualified($path)) {
				$driveRoot = [System.IO.Path]::GetPathRoot($path)
				$invalidNameChars = [char[]]@('"', '<', '>', '|', '*', '?', ':')
				if ($path.Substring($driveRoot.Length).IndexOfAny($invalidNameChars) -lt 0 -and [System.IO.Directory]::Exists($driveRoot)) { return $path }
			}
		}
		catch { Write-Verbose $_.Exception.Message }
	}
	return (Join-Path $env:USERPROFILE "DXForgeProjects")
}

function Resolve-ProjectFile([string]$NameOrPath) {
	if ($NameOrPath -match '\.dxproj$') { return $NameOrPath }
	return (Join-Path (Get-ProjectsRoot) "$NameOrPath\$NameOrPath.dxproj")
}

function Initialize-Native {
	if (-not ("DXForgeGui" -as [type])) {
		Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class DXForgeGui {
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
	[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
	[DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool r);
	[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
	[DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
	[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
	[DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
	[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
	public static List<IntPtr> Find(uint target) {
		var list = new List<IntPtr>();
		EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == target && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
		return list;
	}
	public static string Text(IntPtr h) { var s = new StringBuilder(512); GetWindowText(h, s, 512); return s.ToString(); }
}
"@
	}
	Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing, System.Windows.Forms
	[void][DXForgeGui]::SetProcessDpiAwarenessContext([IntPtr]::new(-4))
	[void][DXForgeGui]::SetThreadDpiAwarenessContext([IntPtr]::new(-4))
}

function Get-Rect([IntPtr]$Handle) {
	$rect = New-Object DXForgeGui+RECT
	[void][DXForgeGui]::GetWindowRect($Handle, [ref]$rect)
	return $rect
}

function Get-Windows([int]$Id, [string]$TitlePart) {
	return @([DXForgeGui]::Find([uint32]$Id) | Where-Object { -not $TitlePart -or [DXForgeGui]::Text($_).Contains($TitlePart) })
}

# 一番大きいウィンドウ。タイトルなしのポップアップやツールチップを選ばないため
function Get-Window([int]$Id, [string]$TitlePart) {
	$found = @(Get-Windows $Id $TitlePart | Sort-Object { $r = Get-Rect $_; ($r.R - $r.L) * ($r.B - $r.T) } -Descending)
	if ($found.Count -eq 0) { throw "ウィンドウが見つかりません(PID $Id, タイトル '$TitlePart')。今あるもの: $(Get-WindowSummary $Id)" }
	# 最小化されていると矩形が画面の外になる(SW_RESTORE)
	if ([DXForgeGui]::IsIconic($found[0])) {
		[void][DXForgeGui]::ShowWindow($found[0], 9)
		Start-Sleep -Milliseconds 500
	}
	return $found[0]
}

# 名前付きの座標(倍率 100% の値)を、ウィンドウの DPI の物理座標にする
function Get-DpiScaled([IntPtr]$Handle, [int[]]$Values) {
	$dpi = [DXForgeGui]::GetDpiForWindow($Handle)
	$scale = if ($dpi -gt 0) { $dpi / 96.0 } else { 1.0 }
	return @($Values | ForEach-Object { [int][Math]::Round($_ * $scale) })
}

function Get-WindowSummary([int]$Id) {
	if (-not (Get-Process -Id $Id -ErrorAction SilentlyContinue)) { return "(プロセスが終了しています)" }
	$names = @([DXForgeGui]::Find([uint32]$Id) | ForEach-Object { "'" + [DXForgeGui]::Text($_) + "'" })
	if ($names.Count -eq 0) { return "(なし)" }
	return $names -join ", "
}

# 対象がすでに前面なら何もしない(ALT で開いたドロップダウンが閉じるため)
function Set-Front([IntPtr]$Handle) {
	# タイトルのないウィンドウはメニューのポップアップ。前面にしなくても押せて、ALT を送ると閉じる
	if ($NoFocus -or [DXForgeGui]::GetForegroundWindow() -eq $Handle -or -not [DXForgeGui]::Text($Handle)) { return }
	[DXForgeGui]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
	[DXForgeGui]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
	[void][DXForgeGui]::SetForegroundWindow($Handle)
	Start-Sleep -Milliseconds 300
}

# プロセスのウィンドウの内側にある要素を読む。Project Browser は隠れたタブのビューも画面の外に持っているので除く
function Get-Elements([int]$Id, [string]$TitlePart) {
	$automation = [System.Windows.Automation.AutomationElement]
	$scope = [System.Windows.Automation.TreeScope]::Descendants
	$handles = @(Get-Windows $Id $TitlePart)
	$bounds = @([DXForgeGui]::Find([uint32]$Id) | ForEach-Object { Get-Rect $_ })
	foreach ($handle in $handles) {
		$rootElement = $automation::FromHandle($handle)
		foreach ($element in $rootElement.FindAll($scope, [System.Windows.Automation.Condition]::TrueCondition)) {
			$current = $element.Current
			$rect = $current.BoundingRectangle
			if ($rect.IsEmpty -or [double]::IsInfinity($rect.Width) -or $current.IsOffscreen) { continue }
			$centerX = $rect.X + $rect.Width / 2
			$centerY = $rect.Y + $rect.Height / 2
			$inside = $false
			foreach ($b in $bounds) { if ($centerX -ge $b.L -and $centerX -lt $b.R -and $centerY -ge $b.T -and $centerY -lt $b.B) { $inside = $true; break } }
			if (-not $inside) { continue }
			[pscustomobject]@{
				Element = $element
				Type    = $current.ControlType.ProgrammaticName -replace '^ControlType\.', ''
				Name    = $current.Name
				Class   = $current.ClassName
				Rect    = $rect
				Window  = $handle
			}
		}
	}
}

function Find-Element([int]$Id, [string]$TitlePart, [string]$Spec) {
	$match = [regex]::Match($Spec, '^(?<type>[^:]*):(?<name>.*?)(?:#(?<index>\d+))?$')
	if (-not $match.Success) { throw "要素の指定は <種類>:<正規表現>[#n] です: $Spec" }
	$type = $match.Groups["type"].Value
	$name = $match.Groups["name"].Value
	$index = if ($match.Groups["index"].Success) { [int]$match.Groups["index"].Value } else { 0 }
	$hits = @(Get-Elements $Id $TitlePart | Where-Object { (-not $type -or $_.Type -eq $type -or $_.Class -eq $type) -and $_.Name -match $name })
	if ($hits.Count -le $index) { return $null }
	return $hits[$index]
}

# 対象を画面の座標(と要素の矩形)にする。呼ぶたびに読み直す
function Resolve-Target([int]$Id, [string]$Spec) {
	$titlePart = $Title
	if ($Named.Contains($Spec)) {
		$entry = $Named[$Spec]
		if (-not $titlePart) { $titlePart = $entry.Window }
		if ($entry.Contains("Element")) { $Spec = "el:" + $entry.Element }
		else {
			$scaled = Get-DpiScaled (Get-Window $Id $titlePart) $entry.Point
			$Spec = "$($scaled[0]),$($scaled[1])"
		}
	}
	if ($Spec -match '^(-?\d+),(-?\d+)$') {
		$handle = Get-Window $Id $titlePart
		$r = Get-Rect $handle
		return [pscustomobject]@{ X = $r.L + [int]$Matches[1]; Y = $r.T + [int]$Matches[2]; Window = $handle; Rect = $null }
	}
	if ($Spec -notmatch '^el:') { throw "対象が分かりません: $Spec(-Action names で一覧、x,y、el:<種類>:<正規表現>[#n])" }
	$found = Find-Element $Id $titlePart $Spec.Substring(3)
	if (-not $found) { throw "要素が見つかりません: $Spec(-Action tree で確かめてください)" }
	$rect = $found.Rect
	# Text と ListItem は左端の近く(行の右に Remove があるものを誤って押さない)
	$px = if ($found.Type -in "Text", "ListItem", "DataItem") { $rect.X + [Math]::Min(8, $rect.Width / 2) } else { $rect.X + $rect.Width / 2 }
	return [pscustomobject]@{ X = [int]$px; Y = [int]($rect.Y + $rect.Height / 2); Window = $found.Window; Rect = $rect }
}

function Invoke-MouseClick($Point, [int]$Count = 1) {
	Set-Front $Point.Window
	[void][DXForgeGui]::SetCursorPos($Point.X, $Point.Y)
	Start-Sleep -Milliseconds 150
	foreach ($i in 1..$Count) {
		[DXForgeGui]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
		[DXForgeGui]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
		Start-Sleep -Milliseconds 60
	}
	Start-Sleep -Milliseconds 300
}

function Invoke-MouseDrag($From, $Dest) {
	Set-Front $From.Window
	[void][DXForgeGui]::SetCursorPos($From.X, $From.Y)
	Start-Sleep -Milliseconds 150
	[DXForgeGui]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds 150
	foreach ($i in 1..12) {
		[void][DXForgeGui]::SetCursorPos($From.X + ($Dest.X - $From.X) * $i / 12, $From.Y + ($Dest.Y - $From.Y) * $i / 12)
		Start-Sleep -Milliseconds 40
	}
	Start-Sleep -Milliseconds 200
	[DXForgeGui]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
	Start-Sleep -Milliseconds 500
}

function Wait-Until([scriptblock]$Condition, [int]$Seconds, [string]$What, [int]$Id) {
	$deadline = (Get-Date).AddSeconds($Seconds)
	while ((Get-Date) -lt $deadline) {
		$result = & $Condition
		if ($result) { return $result }
		Start-Sleep -Milliseconds 300
	}
	throw "時間切れ($Seconds 秒): $What。今あるウィンドウ: $(Get-WindowSummary $Id)"
}

function Start-Editor {
	$path = if ($Exe) { $Exe } else { Join-Path $RepoRoot "x64\DebugEditor\DXForgeEditor.exe" }
	if (-not (Test-Path -LiteralPath $path)) { throw "エディタがありません: $path(./build.ps1 -Configuration DebugEditor -Target all)" }
	return (Start-Process -FilePath $path -PassThru).Id
}

function Wait-Browser([int]$Id) {
	[void](Wait-Until { @(Get-Windows $Id $BrowserTitle).Count -gt 0 } 30 "Project Browser が開く" $Id)
	Start-Sleep -Milliseconds 800
}

# Project Browser が閉じ、メインウィンドウが出るまで待つ。読み込みに失敗すると Project Browser に残る(ダイアログが出る)
function Wait-Loaded([int]$Id) {
	[void](Wait-Until {
			if (-not (Get-Process -Id $Id -ErrorAction SilentlyContinue)) { throw "エディタが終了しました" }
			@(Get-Windows $Id $BrowserTitle).Count -eq 0 -and @(Get-Windows $Id $EditorTitle).Count -gt 0
		} $TimeoutSeconds "プロジェクトの読み込み" $Id)
	# ウィンドウが出たあとに一覧が埋まるので、シーンの行が出るまで待つ
	[void](Wait-Until { Find-Element $Id $EditorTitle "DataItem:GameProject\.Scene$" } $TimeoutSeconds "シーンの表示" $Id)
	Start-Sleep -Milliseconds 500
}

function Invoke-Element([int]$Id, [string]$Window, [string]$Spec) {
	$point = Wait-Until { $script:Title = $Window; try { Resolve-Target $Id "el:$Spec" } catch { $null } } 10 "要素 $Spec" $Id
	Invoke-MouseClick $point
}

# Project Browser で開く。一覧の行は実際のマウスで選ぶ
function Open-InBrowser([int]$Id, [string]$Name) {
	$savedTitle = $script:Title
	try {
		$script:Title = $BrowserTitle
		Invoke-MouseClick (Resolve-Target $Id "OpenTab")
		Start-Sleep -Milliseconds 500
		Invoke-Element $Id $BrowserTitle "Text:^$([regex]::Escape($Name))$"
		Invoke-Element $Id $BrowserTitle "Button:^Open$"
	}
	finally { $script:Title = $savedTitle }
	Wait-Loaded $Id
}

# タブは実際のマウスでないと切り替わらない。名前は ValuePattern で入れる(PropertyChanged で反映される)
function Create-InBrowser([int]$Id, [string]$Name) {
	$savedTitle = $script:Title
	try {
		$script:Title = $BrowserTitle
		Invoke-MouseClick (Resolve-Target $Id "CreateTab")
		Start-Sleep -Milliseconds 500
		Invoke-Element $Id $BrowserTitle "Text:^Empty Project$"
		$edits = @(Get-Elements $Id $BrowserTitle | Where-Object { $_.Type -eq "Edit" })
		if ($edits.Count -lt 2) { throw "Create Project の入力欄が見つかりません" }
		$valuePattern = [System.Windows.Automation.ValuePattern]::Pattern
		$edits[0].Element.GetCurrentPattern($valuePattern).SetValue($Name)
		$folder = $edits[1].Element.GetCurrentPattern($valuePattern).Current.Value
		Start-Sleep -Milliseconds 800
		$create = Find-Element $Id $BrowserTitle "Button:^Create$"
		if (-not $create -or -not $create.Element.Current.IsEnabled) { throw "Create を押せません(同じ名前のプロジェクトがあるか、名前が使えません): $Name" }
		Invoke-Element $Id $BrowserTitle "Button:^Create$"
	}
	finally { $script:Title = $savedTitle }
	Wait-Loaded $Id
	return (Join-Path $folder "$Name\$Name.dxproj")
}

# File メニューの項目を押す。開いたあとは ALT を送らない(Set-Front は前面なら何もしない)
function Invoke-FileMenu([int]$Id, [string]$Item) {
	$savedTitle = $script:Title
	try {
		$script:Title = $EditorTitle
		Invoke-MouseClick (Resolve-Target $Id "File")
		Invoke-Element $Id "" "MenuItem:^$([regex]::Escape($Item))"
	}
	finally { $script:Title = $savedTitle }
}

function Close-Editor([int]$Id) {
	$process = Get-Process -Id $Id -ErrorAction SilentlyContinue
	if (-not $process) { Write-Output "すでに終了しています: PID $Id"; return }
	$sent = $false
	foreach ($titlePart in $EditorTitle, $BrowserTitle) {
		$handles = @(Get-Windows $Id $titlePart)
		if ($handles.Count -eq 0) { continue }
		$sent = $true
		foreach ($handle in $handles) { [void][DXForgeGui]::PostMessage($handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
		if ($process.WaitForExit(8000)) { Write-Output "終了しました: PID $Id"; return }
	}
	# エディタでないプロセス(ゲーム、VS)は、見えているウィンドウをすべて閉じる。
	# エディタのときは行わない(assert のダイアログを閉じると、止まった理由が分からなくなる)
	if (-not $sent) {
		foreach ($handle in Get-Windows $Id "") { [void][DXForgeGui]::PostMessage($handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
		if ($process.WaitForExit(8000)) { Write-Output "終了しました: PID $Id"; return }
	}
	throw "終了しません: PID $Id。今あるウィンドウ: $(Get-WindowSummary $Id)(assert のダイアログなら内容を報告してください)"
}

# この呼び出しで起動したエディタを、失敗のあとに閉じる。閉じられなくても元のエラーを隠さない(PID を出す)
function Stop-StartedEditor([int]$Id) {
	try { Close-Editor $Id | Out-Null }
	catch { Write-Host "起動したエディタを閉じられませんでした。手で閉じてください: PID $Id($($_.Exception.Message))" }
}

function Save-Shot([System.Drawing.Rectangle]$Area, [string]$Path) {
	$Path = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
	New-Item -ItemType Directory -Force (Split-Path -Parent $Path) | Out-Null
	$bitmap = New-Object System.Drawing.Bitmap $Area.Width, $Area.Height
	$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
	$output = $bitmap
	try {
		$graphics.CopyFromScreen($Area.X, $Area.Y, 0, 0, $bitmap.Size)
		if ($MaxWidth -gt 0 -and $Area.Width -gt $MaxWidth) {
			$output = New-Object System.Drawing.Bitmap $MaxWidth, ([int][Math]::Max(1, $Area.Height * $MaxWidth / $Area.Width))
			$scaler = [System.Drawing.Graphics]::FromImage($output)
			$scaler.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
			$scaler.DrawImage($bitmap, 0, 0, $output.Width, $output.Height)
			$scaler.Dispose()
		}
		$output.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
		Write-Output "saved $Path ($($output.Width)x$($output.Height))"
	}
	finally {
		$graphics.Dispose()
		if ($output -ne $bitmap) { $output.Dispose() }
		$bitmap.Dispose()
	}
}

function Get-TreeLines([int]$Id) {
	$previous = $null
	foreach ($e in Get-Elements $Id $Title) {
		$r = Get-Rect $e.Window
		if (-not $All) {
			# 飾りと、ボタンの中の同じ名前のテキストは出さない
			if ($e.Type -in "Image", "Thumb", "ScrollBar", "Separator" -or ($e.Type -eq "Text" -and -not $e.Name)) { continue }
			if ($e.Type -eq "Text" -and $previous -eq $e.Name) { continue }
		}
		if ($e.Name) { $previous = $e.Name }
		$label = if ($e.Name) { "'$($e.Name)'" } else { "<$($e.Class)>" }
		"{0} {1} {2},{3},{4},{5}" -f $e.Type, $label, [int]($e.Rect.X - $r.L), [int]($e.Rect.Y - $r.T), [int]$e.Rect.Width, [int]$e.Rect.Height
	}
}

function Invoke-Prepare {
	if (-not $Project) { Stop-Usage "prepare には -Project が要ります。" }
	if ($Ops -and $Op) { Stop-Usage "-Ops と -Op は一緒には使えません。" }
	if (-not $Import -and -not $Ops -and -not $Op) { Stop-Usage "prepare には -Import、-Ops、-Op のどれかが要ります。" }
	$file = Resolve-ProjectFile $Project
	if (-not (Test-Path -LiteralPath $file)) { Stop-Usage "プロジェクトがありません: $file(先に -Action create で作り、閉じてください)" }
	$tools = if ($ToolDir) { $ToolDir } else { Join-Path $RepoRoot "x64\DebugEditor" }

	foreach ($source in $Import) {
		$tool = Join-Path $tools "DXForgeImport.exe"
		if (-not (Test-Path -LiteralPath $tool)) { Stop-Usage "ツールがありません: $tool" }
		$content = Join-Path (Split-Path -Parent $file) "Content"
		$text = (& $tool --input $source --output $content --json 2>$null) -join ""
		$code = $LASTEXITCODE
		Write-Output "import $(Split-Path -Leaf $source) exit=$code"
		if ($code -ne 0) { Write-Output $text; exit $code }
	}
	if ($Ops -or $Op) {
		$tool = Join-Path $tools "dxforge.exe"
		if (-not (Test-Path -LiteralPath $tool)) { Stop-Usage "ツールがありません: $tool" }
		$selector = if ($Ops) { @("--ops", $Ops) } else { @("--op", $Op) }
		$text = (& $tool --load-project $file @selector --json 2>$null) -join ""
		$code = $LASTEXITCODE
		# 読みやすい 1 行に。結果の全体は -Op の read 系で読める
		$summary = [regex]::Match($text, '"changed":\w+,"saved":\w+').Value
		Write-Output "ops exit=$code $summary"
		if ($code -ne 0) { Write-Output $text; exit $code }
		if ($Op -and $text -notmatch '"changed":true') { Write-Output $text }
	}
}

function Invoke-RemoveProject {
	if (-not $Project) { Stop-Usage "remove-project には -Project が要ります。" }
	if (-not (Test-GuiCheckName $Project)) { Stop-Usage "GuiCheck で始まる名前だけを消せます: $Project" }
	$folder = Join-Path (Get-ProjectsRoot) $Project
	if (-not (Test-Path -LiteralPath $folder)) { Write-Output "ありません: $folder"; return }
	$lock = Join-Path $folder ".DXForge\editor.lock"
	if (Test-Path -LiteralPath $lock) {
		try { [System.IO.File]::Open($lock, "Open", "ReadWrite", "None").Dispose() }
		catch { throw "エディタがまだ開いています(先に -Action close): $folder" }
	}
	Remove-Item -LiteralPath $folder -Recurse -Force
	Write-Output "消しました: $folder"
}

# ---- 引数の検査(ウィンドウに触る前)----
switch ($Action) {
	"names" {
		foreach ($name in $Named.Keys) {
			$entry = $Named[$name]
			$value = if ($entry.Contains("Element")) { "el:" + $entry.Element } else { $entry.Point -join "," }
			Write-Output "$name [$($entry.Window)] $value"
		}
		foreach ($name in $Regions.Keys) { Write-Output "region $name [$($Regions[$name].Window)] $($Regions[$name].Rect -join ',')" }
		exit 0
	}
	"prepare" { Invoke-Prepare; exit 0 }
	"remove-project" {
		try { Invoke-RemoveProject; exit 0 }
		catch { Write-Host $_.Exception.Message; exit 1 }
	}
	"diff" {
		if (-not $Dir) { Stop-Usage "diff には -Dir が要ります。" }
		& python (Join-Path $PSScriptRoot "pixdiff.py") $Dir
		exit $LASTEXITCODE
	}
	"open" { if (-not $Project) { Stop-Usage "open には -Project が要ります。" } }
	"create" {
		if (-not (Test-GuiCheckName $Project)) { Stop-Usage "create の -Project は GuiCheck で始まる名前にしてください(英数字と _。エディタの規則と同じ): '$Project'" }
	}
	"switch" { if (-not $Project -or -not $ProcId) { Stop-Usage "switch には -ProcId と -Project が要ります。" } }
	"keys" { if (-not $Keys) { Stop-Usage "keys には -Keys が要ります。" } }
	"click" { if (-not $ProcId -or -not $Target) { Stop-Usage "click には -ProcId と -Target が要ります。" } }
	"drag" { if (-not $ProcId -or -not $Target -or -not $To) { Stop-Usage "drag には -ProcId、-Target、-To が要ります。" } }
	"shot" { if (-not $ProcId -or -not $OutFile) { Stop-Usage "shot には -ProcId と -OutFile が要ります。" } }
	"wait" { if (-not $ProcId -or (-not $Target -and -not $Title)) { Stop-Usage "wait には -ProcId と、-Target か -Title が要ります。" } }
	"move" { if (-not $ProcId -or $X -lt 0 -or $Y -lt 0 -or $W -le 0 -or $H -le 0) { Stop-Usage "move には -ProcId、-X、-Y、-W、-H が要ります。" } }
	{ $_ -in "close", "tree" } { if (-not $ProcId) { Stop-Usage "$Action には -ProcId が要ります。" } }
}

try {
	Initialize-Native
	switch ($Action) {
		"open" {
			$id = Start-Editor
			try {
				Wait-Browser $id
				Open-InBrowser $id $Project
			}
			catch { Stop-StartedEditor $id; throw }
			Write-Output "PID $id"
		}
		"create" {
			# ProcId があるときのエディタは呼び出した側のもの。閉じない
			$started = -not $ProcId
			if ($ProcId) { $id = $ProcId; Invoke-FileMenu $id "New Project" }
			else { $id = Start-Editor }
			try {
				Wait-Browser $id
				$file = Create-InBrowser $id $Project
			}
			catch { if ($started) { Stop-StartedEditor $id }; throw }
			Write-Output "Project $file"
			Write-Output "PID $id"
		}
		"switch" {
			Invoke-FileMenu $ProcId "Open Project"
			Wait-Browser $ProcId
			Open-InBrowser $ProcId $Project
			Write-Output "PID $ProcId"
		}
		"close" { Close-Editor $ProcId }
		"click" {
			$point = Resolve-Target $ProcId $Target
			Invoke-MouseClick $point $Clicks
			Write-Output "clicked $Target at $($point.X),$($point.Y)"
		}
		"drag" {
			$from = Resolve-Target $ProcId $Target
			$dest = Resolve-Target $ProcId $To
			Invoke-MouseDrag $from $dest
			Write-Output "dragged $Target -> $To"
		}
		"keys" {
			if ($ProcId) { Set-Front (Get-Window $ProcId $Title) }
			[System.Windows.Forms.SendKeys]::SendWait($Keys)
			Start-Sleep -Milliseconds 300
			Write-Output "sent"
		}
		"shot" {
			if ($Region -and $Regions.Contains($Region)) {
				$entry = $Regions[$Region]
				$handle = Get-Window $ProcId $(if ($Title) { $Title } else { $entry.Window })
				$rect = Get-DpiScaled $handle $entry.Rect
			}
			elseif ($Region) {
				if ($Region -notmatch '^-?\d+,-?\d+,\d+,\d+$') { Stop-Usage "領域は名前か x,y,w,h です: $Region" }
				$handle = Get-Window $ProcId $Title
				$rect = @($Region -split "," | ForEach-Object { [int]$_ })
			}
			elseif ($Target) {
				$point = Resolve-Target $ProcId $Target
				if (-not $point.Rect) { Stop-Usage "shot の -Target は要素(el: か要素の名前)だけです: $Target" }
				$handle = $point.Window
				$base = Get-Rect $handle
				$rect = @(([int]$point.Rect.X - $base.L), ([int]$point.Rect.Y - $base.T), [int]$point.Rect.Width, [int]$point.Rect.Height)
			}
			else {
				$handle = Get-Window $ProcId $Title
				$base = Get-Rect $handle
				$rect = @(0, 0, ($base.R - $base.L), ($base.B - $base.T))
			}
			Set-Front $handle
			$base = Get-Rect $handle
			Save-Shot ([System.Drawing.Rectangle]::new($base.L + $rect[0], $base.T + $rect[1], $rect[2], $rect[3])) $OutFile
		}
		"tree" {
			$lines = @(Get-TreeLines $ProcId)
			if ($Pattern) { $lines = @($lines | Where-Object { $_ -match $Pattern }) }
			$lines | Select-Object -First $MaxLines
			if ($lines.Count -gt $MaxLines) { Write-Output "...(ほか $($lines.Count - $MaxLines) 行。-Pattern で絞ってください)" }
		}
		"wait" {
			if ($Target) { $found = Wait-Until { try { Resolve-Target $ProcId $Target } catch { $null } } $TimeoutSeconds "対象 $Target" $ProcId }
			else { [void](Wait-Until { @(Get-Windows $ProcId $Title).Count -gt 0 } $TimeoutSeconds "ウィンドウ '$Title'" $ProcId) }
			Write-Output "found"
		}
		"move" {
			$handle = Get-Window $ProcId $Title
			[void][DXForgeGui]::MoveWindow($handle, $X, $Y, $W, $H, $true)
			Write-Output "moved"
		}
	}
	exit 0
}
catch {
	Write-Host $_.Exception.Message
	exit 1
}
