<#
.SYNOPSIS
	デバッガをつながずに、OutputDebugString の出力(DBWIN)をファイルに取り込むスクリプト(GUI 確認用)。

.DESCRIPTION
	EngineTest やエディタのデバッグ出力(D3D12 のデバッグレイヤーのメッセージ、ReportLiveDeviceObjects の警告など)を、
	Visual Studio を起動せずに読むためのもの。各行の先頭に [PID] を付けるので、ほかのプロセスの出力と分けられる。
	デバッガがつながっているプロセスの出力は取り込めない。同時に 2 つ実行しない(DBWIN のバッファは 1 つしかない)。

	OutputDebugStringW の文字列は ANSI(このマシンでは CP932)に変換されて届くので、Windows PowerShell 5.1
	(powershell.exe)で実行する。確かめたい操作の前に別のプロセスとして起動し、終わったら止める。

.EXAMPLE
	$listener = Start-Process powershell.exe -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ".claude\skills\gui-check\capture-debug-output.ps1", "-OutFile", "$env:TEMP\gui\pr-debug.log", "-Seconds", "300") -WindowStyle Hidden -PassThru
	# ...確かめる操作...
	Stop-Process -Id $listener.Id
#>
param(
	[Parameter(Mandatory)][string]$OutFile,
	[int]$Seconds = 300
)

$ErrorActionPreference = "Stop"
if ($PSVersionTable.PSEdition -ne "Desktop") { throw "Windows PowerShell 5.1(powershell.exe)で実行してください(ANSI の文字列を読むため)" }

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Runtime.InteropServices;
public static class GuiCheckDbWin {
	[DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
	static extern IntPtr CreateFileMapping(IntPtr file, IntPtr attributes, uint protect, uint high, uint low, string name);
	[DllImport("kernel32.dll", SetLastError = true)]
	static extern IntPtr MapViewOfFile(IntPtr mapping, uint access, uint high, uint low, UIntPtr size);
	public static void Run(string outFile, int seconds) {
		var bufferReady = new EventWaitHandle(false, EventResetMode.AutoReset, "DBWIN_BUFFER_READY");
		var dataReady = new EventWaitHandle(false, EventResetMode.AutoReset, "DBWIN_DATA_READY");
		IntPtr mapping = CreateFileMapping(new IntPtr(-1), IntPtr.Zero, 4, 0, 4096, "DBWIN_BUFFER");
		if (mapping == IntPtr.Zero) throw new Exception("CreateFileMapping failed: " + Marshal.GetLastWin32Error());
		IntPtr view = MapViewOfFile(mapping, 4, 0, 0, new UIntPtr(4096));
		using (var writer = new StreamWriter(outFile, false, new UTF8Encoding(false))) {
			writer.AutoFlush = true;
			var deadline = DateTime.UtcNow.AddSeconds(seconds);
			bufferReady.Set();
			while (DateTime.UtcNow < deadline) {
				if (!dataReady.WaitOne(500)) continue;
				int pid = Marshal.ReadInt32(view);
				var bytes = new byte[4092];
				Marshal.Copy(view + 4, bytes, 0, bytes.Length);
				int length = Array.IndexOf(bytes, (byte)0);
				if (length < 0) length = bytes.Length;
				string text = Encoding.Default.GetString(bytes, 0, length);
				writer.Write("[" + pid + "] " + text);
				if (!text.EndsWith("\n")) writer.WriteLine();
				bufferReady.Set();
			}
		}
	}
}
"@

# StreamWriter はプロセスの作業フォルダを基準にするので、PowerShell の現在位置を基準に絶対パスにする
$OutFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutFile)
New-Item -ItemType Directory -Force (Split-Path -Parent $OutFile) | Out-Null
[GuiCheckDbWin]::Run($OutFile, $Seconds)
