# Canonical ESP-IDF environment for SiteSurvey Pro
# Usage: powershell -File tools\idfenv.ps1 idf.py build
# Updated 2026-09-09 for new computer: ESP-IDF v6.1, python_env idf6.1_py3.13_env, device on COM3
param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Cmd)
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\joeky\.espressif\python_env\idf6.1_py3.13_env'
. 'C:\esp\v6.1\esp-idf\export.ps1' | Out-Null
Set-Location 'D:\SiteSurvey Pro'
& $Cmd[0] @($Cmd | Select-Object -Skip 1)
