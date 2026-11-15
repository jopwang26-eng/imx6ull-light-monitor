"""在Ubuntu使用原生应用检验观察器进程/命令/退出路径，不中断网络。"""
import os
import subprocess
import sys
import tempfile

observer, application = map(os.path.abspath, sys.argv[1:3])
with tempfile.TemporaryDirectory() as folder:
    sensor = os.path.join(folder, 'sensor')
    buzzer = os.path.join(folder, 'buzzer')
    with open(sensor, 'wb') as stream:
        stream.write(b'\x34\x12')
    with open(buzzer, 'wb') as stream:
        stream.write(b'\x00')
    report = os.path.join(folder, 'report')
    ready = os.path.join(folder, 'NFS_READY')
    environment = os.environ.copy()
    environment['LM_NFS_READY_FILE'] = ready
    result = subprocess.run([observer, application, os.path.join(folder, 'logs'),
                             '4', report, sensor, buzzer, 'none'],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            timeout=10, env=environment)
    assert result.returncode == 0, result.stderr
    assert b'OK command=STOP' in result.stdout
    assert b'OK command=START' in result.stdout
    assert b'OBSERVER_SIGINT' in result.stdout
    assert b'OBSERVER_ARMED' in result.stdout and b'error=0' in result.stdout
    assert b'OBSERVER_EXIT child_done=1 child_code=0' in result.stdout
    with open(report, 'rb') as stream:
        assert stream.read() == result.stdout
    with open(buzzer, 'rb') as stream:
        assert stream.read() == b'\x00'
    with open(ready, 'rb') as stream:
        assert stream.read() == b'ready\n'
print('PASS: observer captures STATUS/STOP/START, SIGINT exit and exact RAM report')
