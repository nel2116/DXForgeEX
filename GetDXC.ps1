# Courtesy of Devaniti (https://github.com/Devaniti/GetDXC)
# usage: powershell -ExecutionPolicy Bypass -File GetDXC.ps1 PathToFolder
#
# 環境変数 GITHUB_TOKEN か GH_TOKEN があれば、GitHub API を認証付きで呼ぶ(#84)。
# 認証なしの上限はIPごとに1時間60回で、CIのランナーはIPを共有しているため、
# キャッシュが外れたときにレート制限に当たることがある。トークンはログに出さない。
# トークンが無ければ従来どおり認証なしで呼ぶ。
# API の呼び出しとダウンロードは、失敗したら間をおいて再試行する。
# -ApiUrl / -RetryDelaySeconds は動作確認用(既定値のままで使う)。
param(
	[string]$SaveFolder,
	[string]$ApiUrl = "https://api.github.com/repos/microsoft/DirectXShaderCompiler/releases/latest",
	[int[]]$RetryDelaySeconds = @(5, 15, 30)
)

if (-not $SaveFolder)
{
	Write-Host "Missing save folder parameter"
	exit 1
}

if(Test-Path $SaveFolder\*){
	Write-Host "Target path is not empty. Delete it to redownload the latest DXC."
	exit 0
}

# 例外から HTTP ステータスと応答ヘッダを取り出す。
# Windows PowerShell 5.1(HttpWebResponse)と PowerShell 7(HttpResponseMessage)の両方に対応する。
function Get-ResponseHeader($Response, [string]$Name)
{
	try
	{
		if ($Response.Headers -is [System.Net.WebHeaderCollection]) { return $Response.Headers[$Name] }
		$values = $null
		if ($Response.Headers.TryGetValues($Name, [ref]$values)) { return @($values)[0] }
	}
	catch {}
	return $null
}

function Get-FailureInfo($ErrorRecord)
{
	$response = $ErrorRecord.Exception.Response
	$status = $null
	if ($response) { try { $status = [int]$response.StatusCode } catch {} }
	$body = "$($ErrorRecord.ErrorDetails.Message)"
	$remaining = Get-ResponseHeader $response "X-RateLimit-Remaining"
	$isRateLimit = ($status -eq 403 -or $status -eq 429) -and ($remaining -eq "0" -or $body -match "rate limit")
	$reset = $null
	$resetHeader = Get-ResponseHeader $response "X-RateLimit-Reset"
	if ($resetHeader) { $reset = [DateTimeOffset]::FromUnixTimeSeconds([long]$resetHeader).LocalDateTime }
	[pscustomobject]@{ Status = $status; IsRateLimit = $isRateLimit; Reset = $reset; Message = $ErrorRecord.Exception.Message }
}

$Token = $env:GITHUB_TOKEN
if (-not $Token) { $Token = $env:GH_TOKEN }

# GitHub API を呼び、失敗したら再試行する。成功すれば応答の本文を返し、すべて失敗したら $null を返す。
function Invoke-GitHubApi([string]$Uri)
{
	$useToken = [bool]$Token
	if ($useToken) { Write-Host "Querying GitHub API (authenticated)" }
	else { Write-Host "Querying GitHub API (unauthenticated; set GITHUB_TOKEN or GH_TOKEN to raise the rate limit)" }

	$failure = $null
	for ($attempt = 0; $attempt -le $RetryDelaySeconds.Count; $attempt++)
	{
		if ($attempt -gt 0)
		{
			$delay = $RetryDelaySeconds[$attempt - 1]
			Write-Host "Retrying in $delay s (attempt $($attempt + 1) of $($RetryDelaySeconds.Count + 1))"
			Start-Sleep -Seconds $delay
		}

		$headers = @{ "Accept" = "application/vnd.github+json" }
		if ($useToken) { $headers["Authorization"] = "Bearer $Token" }
		try
		{
			return (Invoke-WebRequest -UseBasicParsing -Uri $Uri -Headers $headers).Content
		}
		catch
		{
			$failure = Get-FailureInfo $_
		}

		if ($failure.IsRateLimit) { Write-Host "GitHub API rate limit exceeded (HTTP $($failure.Status))" }
		else { Write-Host "GitHub API request failed: $($failure.Message)" }

		# トークンが無効なら、認証なしに切り替えて続ける(ローカルの古いトークンなど)
		if ($failure.Status -eq 401 -and $useToken)
		{
			Write-Host "The token was rejected (HTTP 401). Continuing without authentication."
			$useToken = $false
		}
	}

	if ($failure.IsRateLimit)
	{
		$resetText = if ($failure.Reset) { " The limit resets at $($failure.Reset)." } else { "" }
		$hint = if ($useToken) { "" } else { " Set GITHUB_TOKEN or GH_TOKEN to use the authenticated limit." }
		Write-Host "ERROR: GitHub API rate limit exceeded; could not look up the latest DXC release.$resetText$hint"
	}
	else
	{
		Write-Host "ERROR: Could not query $Uri after $($RetryDelaySeconds.Count + 1) attempts: $($failure.Message)"
	}
	return $null
}

$JSON = Invoke-GitHubApi $ApiUrl
if ($null -eq $JSON) { exit 1 }

New-Item -ItemType Directory -Force -Path $SaveFolder | Out-Null

$ParsedJSON = ConvertFrom-Json -InputObject $JSON
$Assets = Select-Object -InputObject $ParsedJSON -ExpandProperty assets
Foreach ($Asset IN $Assets)
{
	if ($Asset.name -match 'dxc.*.zip')
	{
		# リリースのアセットは API の上限の対象外なので、認証ヘッダは付けない
		$DownloadURL = $Asset.browser_download_url
		$ZIPPath = Join-Path -Path $SaveFolder -ChildPath "dxc.zip"
		for ($attempt = 0; $attempt -le $RetryDelaySeconds.Count; $attempt++)
		{
			try
			{
				Invoke-WebRequest -UseBasicParsing -Uri $DownloadURL -OutFile $ZIPPath
				Expand-Archive -Path $ZIPPath -DestinationPath $SaveFolder -Force
				Remove-Item -Path $ZIPPath
				Write-Host "Sucessfully downloaded DXC $($Asset.name)"
				exit 0
			}
			catch
			{
				Write-Host "Downloading $($Asset.name) failed: $($_.Exception.Message)"
				# 途中まで書かれたファイルが残ると、次回「空ではない」として取得が飛ばされるため消す
				# (開始時に空だったことを確かめているので、中身はすべてこのスクリプトが作ったもの)
				Remove-Item -Path (Join-Path $SaveFolder "*") -Recurse -Force -ErrorAction SilentlyContinue
				if ($attempt -lt $RetryDelaySeconds.Count)
				{
					$delay = $RetryDelaySeconds[$attempt]
					Write-Host "Retrying in $delay s"
					Start-Sleep -Seconds $delay
				}
			}
		}
		Write-Host "ERROR: Could not download DXC $($Asset.name)"
		exit 1
	}
}

Write-Host "Something went wrong, couldn't download DXC"
exit 1
