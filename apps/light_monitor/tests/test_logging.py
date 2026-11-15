"""真实进程与文件系统验证CSV、轮换、重启、存储错误、队列满和排空。"""
import csv
import errno
import glob
import os
import resource
import signal
import time
import unittest
from test_process import Session, fields, BINARY
import tempfile


class LoggingContract(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.folder.name, 'sensor')
        self.log_dir = os.path.join(self.folder.name, 'logs')
        with open(self.path, 'wb') as stream:
            stream.write(b'\x34\x12')
        self.sessions = []

    def tearDown(self):
        gate = os.path.join(self.folder.name, 'gate')
        if os.path.exists(gate):
            os.unlink(gate)
        for session in self.sessions:
            if session.process.poll() is None:
                session.close()
        self.folder.cleanup()

    def start(self, extra=None, env=None, setup=None):
        session = Session(self.path, 100, extra, env, setup)
        self.sessions.append(session)
        session.until(lambda line: line.startswith('READY'))
        return session

    def status(self, session):
        session.send('STATUS\n')
        return fields(session.until(lambda line: line.startswith('STATUS')))

    def wait_status(self, session, predicate, timeout=8):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            state = self.status(session)
            if predicate(state):
                return state
            time.sleep(0.05)
        self.fail('status condition timed out: ' + str(state))

    def finish(self, session):
        code, out, err = session.close()
        lines = out.decode('ascii').splitlines()
        final = fields(next(line for line in lines if line.startswith('EXIT')))
        return code, final

    def read_rows(self, path=None):
        with open(path or os.path.join(self.log_dir, 'light_monitor.csv')) as stream:
            return list(csv.DictReader(stream))

    def test_csv_drain_and_restart_preserves_previous_content(self):
        session = self.start()
        self.wait_status(session, lambda s: int(s['successes']) >= 4)
        code, final = self.finish(session)
        self.assertEqual(code, 0)
        self.assertEqual(final['successes'], final['log_saved'])
        rows = self.read_rows()
        self.assertEqual(len(rows), int(final['log_saved']))
        self.assertTrue(all(row['als_raw'] == '4660' and row['alarm'] == 'DISABLED'
                            for row in rows))
        self.assertEqual([int(row['sequence']) for row in rows], list(range(1, len(rows) + 1)))
        logfile = os.path.join(self.log_dir, 'light_monitor.csv')
        with open(logfile, 'rb') as stream:
            before = stream.read()
        session = self.start()
        session.until(lambda line: line.startswith('SAMPLE'))
        code, final = self.finish(session)
        self.assertEqual(code, 0)
        with open(logfile, 'rb') as stream:
            after = stream.read()
        self.assertTrue(after.startswith(before))
        self.assertGreater(len(after), len(before))
        self.assertEqual(after.count(b'sequence,elapsed_ms,als_raw,alarm'), 1)

    def test_rotation_five_files_and_size_limit(self):
        session = self.start(['-r', '128'])
        self.wait_status(session, lambda s: int(s['log_saved']) >= 27)
        code, final = self.finish(session)
        self.assertEqual(code, 0)
        files = glob.glob(os.path.join(self.log_dir, 'light_monitor.csv*'))
        self.assertEqual(len(files), 5)
        combined = []
        for suffix in ['.4', '.3', '.2', '.1', '']:
            path = os.path.join(self.log_dir, 'light_monitor.csv' + suffix)
            self.assertLessEqual(os.path.getsize(path), 128)
            combined += [int(row['sequence']) for row in self.read_rows(path)]
        self.assertEqual(combined, sorted(combined))
        self.assertEqual(combined[-1], int(final['log_saved']))
        self.assertGreater(combined[0], 1)

    def test_missing_directory_failure_five_second_retry_recovery(self):
        parent = os.path.join(self.folder.name, 'missing')
        directory = os.path.join(parent, 'logs')
        session = self.start(['-l', directory])
        bad = self.wait_status(session, lambda s: s['log_failed'] == '1')
        self.assertGreater(int(bad['log_errors']), 0)
        self.assertEqual(int(bad['log_errno']), errno.ENOENT)
        first = int(bad['attempts'])
        os.mkdir(parent)
        time.sleep(0.3)
        bad = self.status(session)
        self.assertEqual(bad['log_failed'], '1')
        self.assertGreater(int(bad['attempts']), first)
        good = self.wait_status(session, lambda s: s['log_failed'] == '0' and
                                int(s['log_saved']) > 0)
        self.assertEqual(good['log_errno'], '0')
        self.assertGreater(int(good['log_dropped']), 0)
        code, final = self.finish(session)
        self.assertEqual(code, 1)  # 存在丢弃就不能以退出0冒充日志完整。
        self.assertEqual(int(final['successes']), int(final['log_saved']) + int(final['log_dropped']))

    def test_real_write_failure_does_not_stop_sampling(self):
        def limit_file():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (100, 100))
        session = self.start(setup=limit_file)
        bad = self.wait_status(session, lambda s: s['log_failed'] == '1')
        self.assertEqual(int(bad['log_errno']), errno.EFBIG)
        before = int(bad['attempts'])
        time.sleep(0.3)
        later = self.status(session)
        self.assertGreater(int(later['attempts']), before)
        code, final = self.finish(session)
        self.assertEqual(code, 1)
        self.assertEqual(int(final['successes']), int(final['log_saved']) + int(final['log_dropped']))
        # 短写回退成功：文件只能含完整的已保存行，不能把失败半行拼进下条。
        self.assertEqual(len(self.read_rows()), int(final['log_saved']))

    def test_blocked_storage_queue_full_drop_new_and_exit_drain(self):
        gate = os.path.join(self.folder.name, 'gate')
        with open(gate, 'w'):
            pass
        environment = os.environ.copy()
        environment['LD_PRELOAD'] = os.path.join(os.path.dirname(BINARY), 'fsync_gate.so')
        environment['LM_TEST_SYNC_GATE'] = gate
        session = self.start(env=environment)
        state = self.wait_status(session, lambda s: int(s['log_dropped']) >= 2, timeout=10)
        self.assertEqual(state['log_queue'], '64')
        self.assertEqual(state['log_errors'], '0')
        self.assertEqual(state['log_saved'], '0')
        os.unlink(gate)
        self.wait_status(session, lambda s: int(s['log_saved']) >= 64)
        code, final = self.finish(session)
        self.assertEqual(code, 1)
        self.assertEqual(int(final['successes']), int(final['log_saved']) + int(final['log_dropped']))
        rows = self.read_rows()
        # 慢存储时保留先入队的64条，丢弃新记录，不覆盖旧队列。
        self.assertEqual([int(row['sequence']) for row in rows[:64]], list(range(1, 65)))


if __name__ == '__main__':
    unittest.main()
