import os
import subprocess
import time

PROJECT = r'D:\SiteSurvey Pro'
VENV_SCRIPTS = r'C:\Users\joeky\.espressif\python_env\idf6.1_py3.13_env\Scripts'

env = dict(os.environ)
env.pop('MSYSTEM', None)
env['PATH'] = VENV_SCRIPTS + ';' + env.get('PATH', '')
env['IDF_TOOLS_PATH'] = r'C:\Users\joeky\.espressif'

inner = r'C:\esp\v6.1\esp-idf\export.bat >nul 2>&1 && idf.py -p COM3 monitor'

proc = subprocess.Popen(
    ['cmd', '/c', inner],
    env=env,
    cwd=PROJECT,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    errors='replace',
    bufsize=1,
)

start = time.time()
path = os.path.join(PROJECT, 'boot_log2.txt')
out = open(path, 'w', encoding='utf-8', errors='replace')
try:
    for line in proc.stdout:
        out.write(line)
        out.flush()
        if time.time() - start > 40:
            break
finally:
    out.close()
    # Kill the whole cmd -> idf.py -> idf_monitor tree so COM3 is released
    subprocess.run(['taskkill', '/PID', str(proc.pid), '/T', '/F'],
                   capture_output=True)
    time.sleep(1)

print("captured 40s to boot_log2.txt; monitor tree killed")
