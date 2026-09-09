import os
import subprocess
import sys

env = dict(os.environ)
env.pop('MSYSTEM', None)
env['PATH'] = (r'C:\Users\joeky\AppData\Local\Programs\Python\Python313;'
               r'C:\Users\joeky\AppData\Local\Programs\Python\Python313\Scripts;'
               + env.get('PATH', ''))

# Stream output so we can see progress
proc = subprocess.Popen(
    ['cmd', '/c', r'C:\esp\v6.1\esp-idf\install.bat', 'esp32c5'],
    env=env,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    text=True,
    errors='replace',
)
for line in proc.stdout:
    sys.stdout.write(line)
    sys.stdout.flush()
rc = proc.wait()
print(f"\n=== install.bat exit code: {rc} ===")
sys.exit(rc)
