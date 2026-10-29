<#
.SYNOPSIS
	プロセスのトップレベルのウィンドウを列挙・撮影・終了させるスクリプト(GUI 確認用)。

.DESCRIPTION
	orca computer で見えない子ウィンドウ(ダイアログ、EngineTest の描画ウィンドウ、assert のダイアログ)も扱える。
	per-monitor DPI aware にしてから物理座標で撮る。撮る前に ALT キーを押して離し、SetForegroundWindow で前面に出す
	(後ろにあると、真っ黒になったり別のウィンドウが写ったりする)。

	-Action list : 見えているウィンドウのハンドル・クラス・タイトル・矩形を表示する
	-Action shot : -Title を含むウィンドウ(省略時は一番大きいもの)を -OutFile に PNG で保存する
	-Action close: すべてのウィンドウ(-Title を指定したときはそれだけ)に WM_CLOSE を送る

.EXAMPLE
	./.claude/skills/gui-check/window.ps1 -Action shot -ProcId 1234 -Title PrimitiveMesh -OutFile $env:TEMP\gui\pr-primitive.png
#>
param(
	[Parameter(Mandatory)][ValidateSet("list", "shot", "close")][string]$Action,
	[Parameter(Mandatory)][int]$ProcId,
	[string]$Title,
	[string]$OutFile
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class GuiCheckWindow {
	public delegate bool EnumProc(IntPtr h, IntPtr l);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
	[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
	[DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
	[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	public static List<IntPtr> Find(uint target) {
		var list = new List<IntPtr>();
		EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == target && IsWindowVisible(h)) list.Add(h); return true; }, IntPtr.Zero);
		return list;
	}
	public static string Text(IntPtr h) { var s = new StringBuilder(512); GetWindowText(h, s, 512); return s.ToString(); }
	public static string ClassOf(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }
}
"@

[GuiCheckWindow]::SetProcessDPIAware() | Out-Null
$windows = @([GuiCheckWindow]::Find([uint32]$ProcId) | Where-Object { -not $Title -or [GuiCheckWindow]::Text($_) -like "*$Title*" })

function Get-Rect([IntPtr]$Handle)
{
	$rect = New-Object GuiCheckWindow+RECT
	[GuiCheckWindow]::GetWindowRect($Handle, [ref]$rect) | Out-Null
	return $rect
}

switch ($Action)
{
	"list"
	{
		foreach ($h in $windows)
		{
			$r = Get-Rect $h
			"{0} class={1} title='{2}' rect={3},{4}-{5},{6}" -f $h, [GuiCheckWindow]::ClassOf($h), [GuiCheckWindow]::Text($h), $r.L, $r.T, $r.R, $r.B
		}
	}
	"shot"
	{
		if (-not $OutFile) { throw "-OutFile を指定してください" }
		# Bitmap.Save はプロセスの作業フォルダを基準にするので、PowerShell の現在位置を基準に絶対パスにする
		$OutFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
		$target = $windows | Sort-Object { $r = Get-Rect $_; ($r.R - $r.L) * ($r.B - $r.T) } -Descending | Select-Object -First 1
		if (-not $target) { throw "ウィンドウが見つかりません(PID $ProcId, Title '$Title')" }
		[GuiCheckWindow]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
		[GuiCheckWindow]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
		[GuiCheckWindow]::SetForegroundWindow($target) | Out-Null
		Start-Sleep -Milliseconds 700
		$r = Get-Rect $target
		$bitmap = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
		$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
		try
		{
			$graphics.CopyFromScreen($r.L, $r.T, 0, 0, $bitmap.Size)
			New-Item -ItemType Directory -Force (Split-Path -Parent $OutFile) | Out-Null
			$bitmap.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
		}
		finally
		{
			$graphics.Dispose()
			$bitmap.Dispose()
		}
		"saved {0} ({1}x{2}) title='{3}'" -f $OutFile, ($r.R - $r.L), ($r.B - $r.T), [GuiCheckWindow]::Text($target)
	}
	"close"
	{
		foreach ($h in $windows)
		{
			[GuiCheckWindow]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
			"WM_CLOSE -> {0} '{1}'" -f $h, [GuiCheckWindow]::Text($h)
		}
	}
}
