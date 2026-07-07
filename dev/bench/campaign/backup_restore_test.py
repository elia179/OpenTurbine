"""Web-page config backup/restore round-trip: GET /api/ecu_config (the portable
engine file), change config+hardware, POST it back (multipart, like the web page's
restore), verify everything reverts."""
import sys, os, time, json, urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.dut import DUT

BASE = "http://192.168.4.1"
d = DUT(); d.ensure_mode_standby(); d.ensure_dev_mode(True)

def reboot_wait():
    for _ in range(30):
        try: d.status(); time.sleep(0.4)
        except Exception: break
    for _ in range(60):
        try: d.status(); time.sleep(2.0); return
        except Exception: time.sleep(1)

def multipart_post(path, content_bytes, filename="ecu_config.json"):
    # The endpoint's handler is onBody (raw body), not onUpload (multipart) — the
    # web page POSTs the raw file content directly (tools.html: body: file text).
    req = urllib.request.Request(BASE + path, data=content_bytes, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")

# ── 1. backup ───────────────────────────────────────────────────────
with urllib.request.urlopen(BASE + "/api/ecu_config", timeout=10) as r:
    backup = r.read()
bdoc = json.loads(backup)
print("backup: %d bytes; rpm_limit=%s n1_rpm.enabled=%s" % (
    len(backup),
    bdoc.get("settings", {}).get("engine", {}).get("rpm_limit") if "settings" in bdoc else bdoc.get("engine", {}).get("rpm_limit"),
    (bdoc.get("devices") or bdoc.get("hardware") or {}).get("sensors", {}).get("n1_rpm", {}).get("enabled")))
rpm0 = d.config()["engine"]["rpm_limit"]
n1en0 = d.hardware()["sensors"]["n1_rpm"]["enabled"]
print("live before change: rpm_limit=%s n1_rpm.enabled=%s" % (rpm0, n1en0))

# ── 2. change config + hardware ─────────────────────────────────────
d.patch("/api/config", {"engine": {"rpm_limit": 61234}}); time.sleep(0.4)
hw = d.hardware(); hw["sensors"]["n1_rpm"]["enabled"] = False
code, _ = d._post("/api/hardware", hw)
if code == 200: reboot_wait()
d.ensure_dev_mode(True)
rpm1 = d.config()["engine"]["rpm_limit"]; n1en1 = d.hardware()["sensors"]["n1_rpm"]["enabled"]
print("after change: rpm_limit=%s n1_rpm.enabled=%s  (changed OK: %s)" % (rpm1, n1en1, rpm1 == 61234 and n1en1 == False))

# ── 3. restore the backup via the web-page endpoint ─────────────────
d.ensure_mode_standby()
code, resp = multipart_post("/api/ecu_config", backup)
print("restore POST: %s %s" % (code, resp[:80]))
reboot_wait()
d.ensure_dev_mode(True)
rpm2 = d.config()["engine"]["rpm_limit"]; n1en2 = d.hardware()["sensors"]["n1_rpm"]["enabled"]
print("after restore: rpm_limit=%s n1_rpm.enabled=%s" % (rpm2, n1en2))

print("\nRESULT:")
print("  backup downloaded:", len(backup) > 500)
print("  change applied:", rpm1 == 61234 and n1en1 == False)
print("  restore reverted config (rpm_limit):", rpm2 == rpm0)
print("  restore reverted hardware (n1 enabled):", n1en2 == n1en0 == True)
