"""通过公开命令行及文件描述符读取边界验证两字节小端协议。

普通文件只提供协议样本，不模拟 I2C 芯片，不能代替板端验收。
用法：python3 tests/test_cli.py /path/to/native/lightap_test
"""

import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


BINARY = os.path.abspath(sys.argv.pop(1))


class SensorCliContract(unittest.TestCase):
    def run_frame(self, data):
        # 使用真实文件描述符承载独立定义的协议样本，调用实际可执行文件。
        with tempfile.NamedTemporaryFile() as sample:
            sample.write(data)
            sample.flush()
            return subprocess.run(
                [BINARY, "-d", sample.name, "-n", "1"],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
                timeout=3,
            )

    def test_little_endian_frame(self):
        # 期望值是协议给定的固定样本，不从被测实现反算。
        cases = [(b"\x34\x12", 4660), (b"\x00\x00", 0),
                 (b"\xff\xff", 65535)]
        for frame, expected in cases:
            with self.subTest(frame=frame):
                result = self.run_frame(frame)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("ALS_RAW={}\n".format(expected), result.stdout)

    def test_incomplete_frame_never_reports_a_sample(self):
        for frame in (b"", b"\x34"):
            with self.subTest(frame=frame):
                result = self.run_frame(frame)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("ALS_RAW=", result.stdout)

    def test_missing_device_reports_failure(self):
        with tempfile.TemporaryDirectory() as empty:
            result = subprocess.run(
                [BINARY, "-d", str(pathlib.Path(empty) / "missing"), "-n", "1"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                universal_newlines=True, timeout=3,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("ALS_RAW=", result.stdout)


if __name__ == "__main__":
    unittest.main()
