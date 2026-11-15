"""运行真实原生程序验证线程、协议和恢复，不模拟I2C硬件。兼容Python3.5。"""
import os
import re
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

BINARY = os.path.abspath(sys.argv.pop(1))


class Session:
    def __init__(self, path, interval=None, extra=None, env=None, preexec_fn=None):
        buzzer = os.path.join(os.path.dirname(path), 'buzzer')
        if not os.path.exists(buzzer):
            with open(buzzer, 'wb') as stream:
                stream.write(b'\x00')
        args = [BINARY, '-d', path, '-l', os.path.join(os.path.dirname(path), 'logs'),
                '-b', buzzer, '-k', 'none']
        if interval is not None:
            args += ['-i', str(interval)]
        args += extra or []
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        env=env, preexec_fn=preexec_fn)
        self.pending = b''

    def send(self, text):
        self.process.stdin.write(text.encode('ascii'))
        self.process.stdin.flush()

    def until(self, predicate, timeout=4):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                line = line.decode('ascii')
                if predicate(line):
                    return line
                continue
            ready, _, _ = select.select([self.process.stdout], [], [],
                                         max(0, end - time.monotonic()))
            if ready:
                data = os.read(self.process.stdout.fileno(), 4096)
                if not data:
                    raise AssertionError('unexpected EOF: ' +
                                         self.process.stderr.read().decode('ascii'))
                self.pending += data
        raise AssertionError('timeout waiting for expected process output')

    def close(self):
        # 即使测试失败也恢复SIGSTOP，再退出并回收进程，避免残留测试程序。
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGCONT)
            self.process.send_signal(signal.SIGINT)
        try:
            out, err = self.process.communicate(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.communicate()
            raise
        return self.process.returncode, self.pending + out, err


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


class ProcessContract(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.folder.name, 'sensor')
        self.frame(b'\x34\x12')
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            if session.process.poll() is None:
                session.close()
        self.folder.cleanup()

    def frame(self, data):
        # 临时文件+rename确保每轮只能看到完整旧帧或完整新帧。
        temporary = self.path + '.new'
        with open(temporary, 'wb') as stream:
            stream.write(data)
        os.replace(temporary, self.path)

    def start(self, interval=100):
        session = Session(self.path, interval)
        self.sessions.append(session)
        session.until(lambda line: line.startswith('READY'))
        return session

    def status(self, session):
        session.send('STATUS\n')
        return fields(session.until(lambda line: line.startswith('STATUS')))

    def test_default_period_partial_input_and_status(self):
        session = self.start(None)
        first = fields(session.until(lambda line: line.startswith('SAMPLE')))
        session.send('STA')
        second = fields(session.until(lambda line: line.startswith('SAMPLE')))
        delta = int(second['elapsed_ms']) - int(first['elapsed_ms'])
        self.assertGreaterEqual(delta, 900)
        self.assertLess(delta, 2000)
        session.send('TUS\n')
        state = fields(session.until(lambda line: line.startswith('STATUS')))
        self.assertEqual(state['ALS_RAW'], '4660')
        self.assertEqual(state['interval_ms'], '1000')
        self.assertEqual(state['alarm'], 'DISABLED')
        code, out, err = session.close()
        self.assertEqual(code, 0, err)
        self.assertIn(b'EXIT state=STOPPED', out)

    def test_failure_invalid_short_frame_and_recovery(self):
        session = self.start()
        session.until(lambda line: 'valid=1 ALS_RAW=4660' in line)
        os.unlink(self.path)
        failure = session.until(lambda line: line.startswith('SAMPLE') and 'valid=0' in line)
        self.assertIn('ALS_RAW=NA', failure)
        state = self.status(session)
        self.assertEqual(state['valid'], '0')
        self.assertEqual(state['ALS_RAW'], 'NA')
        self.assertGreater(int(state['errors']), 0)
        self.frame(b'\x34')
        session.until(lambda line: 'valid=0' in line and 'errno=5 ' in line)
        self.frame(b'\xff\xff')
        session.until(lambda line: 'valid=1 ALS_RAW=65535' in line)
        state = self.status(session)
        self.assertEqual(state['valid'], '1')
        self.assertEqual(state['last_errno'], '0')
        self.assertGreater(int(state['errors']), 0)

    def test_long_unknown_and_embedded_nul_lines(self):
        session = self.start()
        for text in ['STATUS extra\n', 'START extra\n']:
            session.send(text)
            session.until(lambda line: line.startswith('ERR invalid_arguments'))
        session.send('BOGUS\n')
        session.until(lambda line: line.startswith('ERR unknown_command'))
        for text in ['x' * 200 + '\n', 'STATUS\0extra\n']:
            session.send(text)
            session.until(lambda line: line.startswith('ERR invalid_or_too_long'))
        session.send(' \tSTATUS\r\n')
        session.until(lambda line: line.startswith('STATUS state=RUNNING'))

    def test_missed_periods_skip_without_burst(self):
        session = self.start(200)
        before = fields(session.until(lambda line: line.startswith('SAMPLE')))
        session.process.send_signal(signal.SIGSTOP)
        # waitpid确认进程已停止，暂停期间单调时钟继续前进。
        os.waitpid(session.process.pid, os.WUNTRACED)
        time.sleep(1.2)
        session.process.send_signal(signal.SIGCONT)
        state = fields(session.until(lambda line: line.startswith('SAMPLE') and
                                     int(fields(line).get('missed', '0')) >= 4))
        self.assertLessEqual(int(state['attempt']) - int(before['attempt']), 2)
        after = self.status(session)
        self.assertGreaterEqual(int(after['missed']), 4)

    def test_stop_wakes_long_interval_and_eof(self):
        session = self.start(60000)
        session.until(lambda line: line.startswith('SAMPLE'))
        started = time.monotonic()
        code, out, err = session.close()
        self.assertEqual(code, 0, err)
        self.assertLess(time.monotonic() - started, 1)
        session = self.start()
        session.process.stdin.close()
        session.process.stdin = None
        # 先等待EOF自行结束，不能由close里的SIGINT替代EOF路径。
        session.process.wait(timeout=2)
        code, out, err = session.close()
        self.assertEqual(code, 0, err)

    def test_invalid_intervals(self):
        for interval in ['99', '60001', '-1', 'abc', ' 100', '1' * 80]:
            result = subprocess.run([BINARY, '-i', interval], stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=2)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
