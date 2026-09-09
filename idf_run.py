"""Run idf.py under cmd.exe via export.bat, with MSYSTEM stripped.

Git Bash is unsupported by ESP-IDF v6.1, and export.bat resolves the python
venv from the first python.exe on PATH -- so the idf venv Scripts dir must be
prepended before export.bat runs.
"""
import os
import subprocess
import sys

PROJECT = r'D:\SiteSurvey Pro'
VENV_SCRIPTS = r'C:\Users\joeky\.espressif\python_env\idf6.1_py3.13_env\Scripts'

env = dict(os.environ)
env.pop('MSYSTEM', None)
env['PATH'] = VENV_SCRIPTS + ';' + env.get('PATH', '')
env['IDF_TOOLS_PATH'] = r'C:\Users\joeky\.espressif'

args = sys.argv[1:] or ['build']
inner = r'C:\esp\v6.1\esp-idf\export.bat >nul 2>&1 && idf.py ' + ' '.join(args)

proc = subprocess.Popen(
    ['cmd', '/c', inner],
    env=env,
    cwd=PROJECT,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    errors='replace',
)
for line in proc.stdout:
    sys.stdout.write(line)
    sys.stdout.flush()
rc = proc.wait()
print(f"\n=== idf.py {' '.join(args)} exit code: {rc} ===")
sys.exit(rc)
