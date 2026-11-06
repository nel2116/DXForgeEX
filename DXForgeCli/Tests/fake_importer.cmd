@pwsh -NoProfile -NonInteractive -File "%~dp0fake_importer.ps1" %*
@exit /b %errorlevel%
