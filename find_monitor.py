import subprocess

out = subprocess.run(
    [r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe', '-NoProfile', '-Command',
     "Get-CimInstance Win32_Process -Filter \"Name='python.exe'\" | ForEach-Object { $_.ProcessId.ToString() + ' :: ' + $_.CommandLine }"],
    capture_output=True, text=True, errors='replace',
)
print(out.stdout)
print(out.stderr, file=__import__('sys').stderr)
