# Canonical ESP-IDF environment for SiteSurvey Pro
# Usage: powershell -File tools\idfenv.ps1 idf.py build
param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Cmd)
$env:IDF_PYTHON_ENV_PATH = 'C:\Users\joeky\.espressif\python_env\idf6.0_py3.14_env'
. 'C:\esp\v6.0.1\esp-idf\export.ps1' | Out-Null
Set-Location 'D:\SiteSurvey Pro'
& $Cmd[0] @($Cmd | Select-Object -Skip 1)
