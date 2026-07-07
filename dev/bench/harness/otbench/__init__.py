"""OTBench — PC-side harness for the OpenTurbine hardware-in-the-loop bench rig.

The PC is the brain: it drives the OTBench tester (classic ESP32) over USB
serial and the OpenTurbine DUT (ESP32-S3) over its Wi-Fi web API, closing the
loop to verify sensor-input paths, actuator outputs, sequence timing and safety.
"""

__version__ = "0.1"

from .pinmap import PinMap
from .dut import DUT
from .tester import Tester, TesterError, TesterTimeout

__all__ = ["PinMap", "DUT", "Tester", "TesterError", "TesterTimeout", "__version__"]
