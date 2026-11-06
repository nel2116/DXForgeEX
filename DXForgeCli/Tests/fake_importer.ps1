<#
.SYNOPSIS
    ops_cli_test.ps1 が使う偽の DXForgeImport(Issue #183)。FBX SDK のない CI でも import-asset の経路を確かめるためのもの。

.DESCRIPTION
    --input のファイルを、本当の入力ではなく指示の JSON として読む。実物と同じ引数(--input, --output, --sources-output, --set, --json)を受ける。
      assets    出力ファイル名(--output からの相対)→ コピー元のテンプレート。.asset の末尾には --sources-output の値を追記する
      mkdir     先に作るフォルダの配列(コミットの途中の失敗を起こすために使う)
      sleep     終了までに待つ秒数(タイムアウトの確認に使う)
      args_log  受け取った引数を 1 行ずつ書くファイル
      errors, warnings  --json の結果に入れる配列
      no_json   true なら JSON を出さない(クラッシュの代わり)
      exit      終了コード(既定は 0)
#>
$ErrorActionPreference = "Stop"

$values = @{}
for ($i = 0; $i -lt $args.Count; ++$i)
{
	switch ($args[$i])
	{
		"--input" { $values.Input = $args[++$i] }
		"--output" { $values.Output = $args[++$i] }
		"--sources-output" { $values.SourcesOutput = $args[++$i] }
	}
}

$spec = Get-Content -Raw -Encoding utf8 -LiteralPath $values.Input | ConvertFrom-Json

if ($spec.args_log) { [System.IO.File]::WriteAllLines($spec.args_log, [string[]]$args) }
foreach ($folder in @($spec.mkdir)) { if ($folder) { $null = New-Item -ItemType Directory -Force $folder } }
if ($spec.sleep) { Start-Sleep -Seconds $spec.sleep }

if ($spec.assets)
{
	foreach ($asset in $spec.assets.PSObject.Properties)
	{
		$destination = Join-Path $values.Output $asset.Name
		$null = New-Item -ItemType Directory -Force (Split-Path $destination)
		[System.IO.File]::Copy($asset.Value, $destination)
		if ($destination.EndsWith(".asset") -and $values.SourcesOutput)
		{
			[System.IO.File]::AppendAllText($destination, $values.SourcesOutput, [System.Text.UTF8Encoding]::new($false))
		}
	}
}

if (-not $spec.no_json)
{
	@{ errors = @($spec.errors | Where-Object { $_ }); warnings = @($spec.warnings | Where-Object { $_ }) } | ConvertTo-Json -Compress | Write-Output
}
exit $(if ($spec.exit) { [int]$spec.exit } else { 0 })
