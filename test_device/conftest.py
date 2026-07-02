import os
import sys

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
from device_harness import DeviceHarness, find_port  # noqa: E402


def pytest_configure(config):
    config.addinivalue_line("markers", "device: requires the physical device on serial")


@pytest.fixture(scope="session")
def harness():
    """A live DeviceHarness, or skip the whole suite if no device is attached.

    Assumes examples/08_selftest_console is flashed (it exposes the TEST protocol).
    """
    if not find_port():
        pytest.skip("no solide device on serial")
    h = DeviceHarness()
    yield h
    h.close()
