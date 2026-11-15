"""测试真实应用命令、按键FIFO协议、滞回以及受控适配层故障。非板测。"""
import csv
import os
import tempfile
import time
import unittest
from test_process import Session, fields, BINARY


class ControlContract(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.folder.name, 'sensor')
        self.buzzer = os.path.join(self.folder.name, 'buzzer')
        self.sessions = []
        self.holders = []
        self.frame(15)

    def tearDown(self):
        for name in ['fail-off', 'read-gate']:
            path = os.path.join(self.folder.name, name)
            if os.path.exists(path):
                os.unlink(path)
        for session in self.sessions:
            if session.process.poll() is None:
                session.close()
        for fd in self.holders:
            os.close(fd)
        self.folder.cleanup()

    def frame(self, raw):
        path = self.path + '.new'
        with open(path, 'wb') as stream:
            stream.write(bytes([raw & 255, raw >> 8]))
        os.replace(path, self.path)

    def start(self, extra=None, env=None, interval=100):
        session = Session(self.path, interval, extra, env)
        self.sessions.append(session)
        session.until(lambda line: line.startswith('READY'))
        return session

    def command(self, session, text):
        session.send(text + '\n')
        return session.until(lambda line: line.startswith(('OK ', 'ERR ')))

    def status(self, session):
        session.send('STATUS\n')
        return fields(session.until(lambda line: line.startswith('STATUS')))

    def wait_state(self, session, condition, timeout=3):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            state = self.status(session)
            if condition(state):
                return state
            time.sleep(0.03)
        self.fail('state timeout: ' + str(state))

    def fixture_buzzer(self):
        with open(self.buzzer, 'rb') as stream:
            return stream.read()

    def finish(self, session):
        code, output, error = session.close()
        final = fields(next(line for line in output.decode('ascii').splitlines()
                            if line.startswith('EXIT')))
        return code, final

    def environment(self):
        environment = os.environ.copy()
        environment['LD_PRELOAD'] = os.path.join(os.path.dirname(BINARY), 'control_faults.so')
        environment['LM_TEST_BUZZER_PATH'] = self.buzzer
        environment['LM_TEST_SENSOR_PATH'] = self.path
        return environment

    def test_hysteresis_both_boundaries_csv_and_exit_off(self):
        session = self.start()
        self.wait_state(session, lambda s: s['valid'] == '1')
        self.assertEqual(self.status(session)['alarm'], 'DISABLED')
        self.assertTrue(self.command(session, 'SET_THRESHOLD 10 20').startswith('OK'))
        # 中间及等于边界保持解除；低于下限开启后，中间和边界保持开启。
        for raw, alarm in [(15, 'OFF'), (10, 'OFF'), (20, 'OFF'), (9, 'ON'),
                           (10, 'ON'), (15, 'ON'), (20, 'ON'), (21, 'OFF'), (9, 'ON')]:
            self.frame(raw)
            state = self.wait_state(session, lambda s: s['ALS_RAW'] == str(raw) and s['alarm'] == alarm)
            self.assertEqual(state['buzzer'], alarm)
            self.assertEqual(self.fixture_buzzer(), b'\x01' if alarm == 'ON' else b'\x00')
        code, final = self.finish(session)
        self.assertEqual(code, 0)
        self.assertEqual(final['buzzer'], 'OFF')
        self.assertEqual(self.fixture_buzzer(), b'\x00')
        with open(os.path.join(self.folder.name, 'logs', 'light_monitor.csv')) as stream:
            rows = list(csv.DictReader(stream))
        self.assertTrue(any(r['alarm'] == 'ON' and r['als_raw'] == '9' for r in rows))
        self.assertTrue(any(r['alarm'] == 'OFF' and r['als_raw'] == '21' for r in rows))

    def test_pause_resume_interval_bounds_and_invalid_no_mutation(self):
        session = self.start()
        self.assertIn('state=PAUSED', self.command(session, 'STOP'))
        before = self.status(session)
        time.sleep(0.25)
        self.assertEqual(self.status(session)['attempts'], before['attempts'])
        self.assertEqual(before['valid'], '0')
        for value in ['100', '60000']:
            self.assertTrue(self.command(session, 'SET_INTERVAL ' + value).startswith('OK'))
            self.assertEqual(self.status(session)['interval_ms'], value)
        for command in ['SET_INTERVAL 99', 'SET_INTERVAL 60001', 'SET_INTERVAL -1',
                        'SET_INTERVAL +100', 'SET_INTERVAL 1x', 'SET_INTERVAL ' + '9' * 80,
                        'SET_THRESHOLD 20 10', 'SET_THRESHOLD 10 10', 'SET_THRESHOLD 0 65536',
                        'START extra', 'STOP extra', 'SET_THRESHOLD 1 2 3']:
            self.assertEqual(self.command(session, command), 'ERR invalid_arguments')
            state = self.status(session)
            self.assertEqual(state['state'], 'PAUSED')
            self.assertEqual(state['interval_ms'], '60000')
            self.assertEqual(state['threshold_enabled'], '0')
        self.assertTrue(self.command(session, 'SET_THRESHOLD 0 65535').startswith('OK'))
        self.assertTrue(self.command(session, 'SET_INTERVAL 100').startswith('OK'))
        self.assertIn('state=RUNNING', self.command(session, 'START'))
        self.wait_state(session, lambda s: int(s['attempts']) > int(before['attempts']))
        self.assertEqual(self.status(session)['missed'], '0')

    def test_sensor_failure_closes_and_recovery_recomputes_alarm(self):
        session = self.start()
        self.command(session, 'SET_THRESHOLD 10 20')
        self.frame(5)
        self.wait_state(session, lambda s: s['alarm'] == 'ON')
        os.unlink(self.path)
        state = self.wait_state(session, lambda s: s['valid'] == '0' and int(s['errors']) > 0)
        self.assertEqual(state['ALS_RAW'], 'NA')
        self.assertEqual(state['alarm'], 'OFF')
        self.assertEqual(state['buzzer'], 'OFF')
        self.frame(15)
        self.wait_state(session, lambda s: s['valid'] == '1' and s['ALS_RAW'] == '15')
        self.assertEqual(self.status(session)['alarm'], 'OFF')

    def test_key_fifo_and_command_share_toggle_control(self):
        path = os.path.join(self.folder.name, 'key')
        os.mkfifo(path)
        holder = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        self.holders.append(holder)
        session = self.start(['-k', path])
        self.wait_state(session, lambda s: s['key_enabled'] == '1' and s['key_errno'] == '0')
        os.write(holder, b'\x01')
        session.until(lambda line: line.startswith('KEY action=TOGGLE state=PAUSED'))
        before = self.status(session)
        time.sleep(0.2)
        self.assertEqual(self.status(session)['attempts'], before['attempts'])
        os.write(holder, b'\x01')
        session.until(lambda line: line.startswith('KEY action=TOGGLE state=RUNNING'))
        self.wait_state(session, lambda s: int(s['attempts']) > int(before['attempts']))
        self.assertIn('state=PAUSED', self.command(session, 'STOP'))
        os.write(holder, b'\x01')
        session.until(lambda line: line.startswith('KEY action=TOGGLE state=RUNNING'))

    def test_close_failure_visible_and_paused_retry_recovers(self):
        gate = os.path.join(self.folder.name, 'fail-off')
        environment = self.environment()
        environment['LM_TEST_FAIL_OFF'] = gate
        session = self.start(env=environment)
        self.command(session, 'SET_THRESHOLD 10 20')
        self.frame(5)
        self.wait_state(session, lambda s: s['alarm'] == 'ON')
        with open(gate, 'w'):
            pass
        reply = self.command(session, 'STOP')
        self.assertIn('ERR control_failed', reply)
        self.assertIn('applied=1 state=PAUSED', reply)
        self.assertIn('buzzer=UNKNOWN control_errno=5', reply)
        state = self.status(session)
        self.assertGreater(int(state['control_errors']), 0)
        os.unlink(gate)
        self.wait_state(session, lambda s: s['buzzer'] == 'OFF' and s['control_errno'] == '0')
        code, final = self.finish(session)
        self.assertEqual(code, 1)
        self.assertEqual(final['buzzer'], 'OFF')

    def test_stop_during_inflight_read_cannot_reenable_alarm(self):
        self.frame(5)
        gate = os.path.join(self.folder.name, 'read-gate')
        marker = os.path.join(self.folder.name, 'read-ready')
        with open(gate, 'w'):
            pass
        environment = self.environment()
        environment['LM_TEST_READ_GATE'] = gate
        environment['LM_TEST_READ_READY'] = marker
        session = self.start(env=environment)
        end = time.monotonic() + 2
        while not os.path.exists(marker) and time.monotonic() < end:
            time.sleep(0.01)
        self.assertTrue(os.path.exists(marker))
        self.command(session, 'SET_THRESHOLD 10 20')
        self.assertIn('state=PAUSED', self.command(session, 'STOP'))
        os.unlink(gate)
        state = self.wait_state(session, lambda s: int(s['superseded']) >= 1)
        self.assertEqual(state['attempts'], '0')
        self.assertEqual(state['alarm'], 'OFF')
        self.assertEqual(state['buzzer'], 'OFF')
        self.command(session, 'START')
        self.wait_state(session, lambda s: s['alarm'] == 'ON')

    def test_restart_resets_threshold_and_default_interval(self):
        session = self.start()
        self.command(session, 'SET_THRESHOLD 10 20')
        self.command(session, 'SET_INTERVAL 200')
        self.assertEqual(self.finish(session)[0], 0)
        session = self.start(interval=None)
        state = self.status(session)
        self.assertEqual(state['interval_ms'], '1000')
        self.assertEqual(state['threshold_enabled'], '0')
        self.assertEqual(state['alarm'], 'DISABLED')
        self.assertEqual(state['state'], 'RUNNING')


if __name__ == '__main__':
    unittest.main()
