import sys, time
import os; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench import Tester, DUT

dut = DUT()
t = Tester("COM3").open()

print("MAX6675 emulator -> S3 TOT (reads every 250 ms, 6-sample avg)\n")
print("%-7s %-9s %-9s" % ("set C", "read C", "healthy"))
rows = []
for T in (100, 300, 600, 900):
    t.set_tot(T)
    time.sleep(2.2)
    d = dut.data()
    g, h = d.get("tot"), d.get("tot_healthy")
    rows.append((T, g, h))
    print("%-7d %-9s %-9s" % (T, g, h))

t.set_tot("open")
time.sleep(2.2)
d = dut.data()
print("open    %-9s %-9s  (expect unhealthy)" % (d.get("tot"), d.get("tot_healthy")))

t.set_tot("off")
t.close()

errs = [abs(g - T) for T, g, h in rows if isinstance(g, (int, float)) and h]
if errs:
    print("\nmax error %.1f C across sweep" % max(errs))
dut.ensure_mode_standby()
