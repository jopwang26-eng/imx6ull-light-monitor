"""普通文件验证用户工具字节协议；不代替 GPIO 和声音实测。"""
import os
import subprocess
import sys
import tempfile

binary = os.path.abspath(sys.argv[1])

def run(*args):
    return subprocess.run([binary] + list(args), stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, universal_newlines=True)

with tempfile.TemporaryDirectory() as folder:
    path = os.path.join(folder, 'frame')
    for command, value in [('on', 1), ('off', 0)]:
        with open(path, 'wb') as stream:
            stream.write(b'\x00')
        result = run(command, path)
        assert result.returncode == 0, result.stderr
        with open(path, 'rb') as stream:
            assert stream.read() == bytes([value])
        assert 'GPIO_LOGICAL={}'.format(value) in result.stdout
    for frame, success in [(b'\x00', True), (b'\x01', True),
                           (b'\x02', False), (b'', False)]:
        with open(path, 'wb') as stream:
            stream.write(frame)
        result = run('status', path)
        assert (result.returncode == 0) == success
        if not success:
            assert 'GPIO_LOGICAL=' not in result.stdout
    assert run('status', os.path.join(folder, 'missing')).returncode != 0
    assert run('invalid').returncode == 2
print('PASS: CLI byte protocol and error cases')
