"""Web-page config backup/restore round-trip: GET /api/ecu_config (the portable
engine file), change config+hardware, POST it back (multipart, like the web page's
restore), verify everything reverts."""
import sys, os, time, json, subprocess, urllib.request
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "harness"))
from otbench.dut import DUT

BASE = "http://192.168.4.1"
d = DUT(); d.ensure_mode_standby(); d.ensure_dev_mode(True)

def reboot_wait():
    for _ in range(30):
        try: d.status(); time.sleep(0.4)
        except Exception: break
    for _ in range(60):
        if os.name == "nt":
            subprocess.run(
                'netsh wlan connect name="OpenTurbine" ssid="OpenTurbine" interface="Wi-Fi"',
                shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                timeout=8, check=False)
        try: d.status(); time.sleep(2.0); return
        except Exception: time.sleep(1)
    raise RuntimeError("DUT did not return after reboot")

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
hardware0 = bdoc["hardware"]
print("backup: %d bytes; rpm_limit=%s n1_rpm.enabled=%s" % (
    len(backup),
    bdoc.get("settings", {}).get("engine", {}).get("rpm_limit") if "settings" in bdoc else bdoc.get("engine", {}).get("rpm_limit"),
    (bdoc.get("devices") or bdoc.get("hardware") or {}).get("sensors", {}).get("n1_rpm", {}).get("enabled")))
rpm0 = d.config()["engine"]["rpm_limit"]
desc0 = d.hardware().get("profile_desc", "")
desc_target = (desc0[:40] + " [restore test]")[:63]
print("live before change: rpm_limit=%s profile_desc=%r" % (rpm0, desc0))

# ── 2. change config + hardware ─────────────────────────────────────
d.patch("/api/config", {"engine": {"rpm_limit": 61234}}); time.sleep(0.4)
hw = d.hardware(); hw["profile_desc"] = desc_target
code, hw_resp = d._post("/api/hardware", hw)
if code == 200: reboot_wait()
d.ensure_dev_mode(True)
rpm1 = d.config()["engine"]["rpm_limit"]; desc1 = d.hardware().get("profile_desc", "")
change_ok = code == 200 and rpm1 == 61234 and desc1 == desc_target
print("after change: rpm_limit=%s profile_desc=%r response=%r (changed OK: %s)" %
      (rpm1, desc1, hw_resp, change_ok))

# ── 3. restore the backup via the web-page endpoint ─────────────────
d.ensure_mode_standby()
code, resp = multipart_post("/api/ecu_config", backup)
print("restore POST: %s %s" % (code, resp[:80]))
reboot_wait()
d.ensure_dev_mode(True)
rpm2 = d.config()["engine"]["rpm_limit"]; desc2 = d.hardware().get("profile_desc", "")
hardware2 = d.hardware()
print("after restore: rpm_limit=%s profile_desc=%r" % (rpm2, desc2))

def registry_signature(hw):
    registry = hw.get("channel_registry", {})
    return {
        "inputs": [(c.get("id"), c.get("purpose"), c.get("driver"), c.get("pin"))
                   for c in registry.get("inputs", [])],
        "outputs": [(c.get("id"), c.get("purpose"), c.get("driver"), c.get("pin"))
                    for c in registry.get("outputs", [])],
        "bindings": [(b.get("key"), b.get("channel"))
                     for b in registry.get("bindings", [])],
    }

registry0 = registry_signature(hardware0)
registry2 = registry_signature(hardware2)
if registry2 != registry0:
    print("registry before restore:", registry0)
    print("registry after restore: ", registry2)

print("\nRESULT:")
checks = [
    ("backup downloaded", len(backup) > 500),
    ("change applied", change_ok),
    ("restore reverted config (rpm_limit)", rpm2 == rpm0),
    ("restore reverted hardware (profile description)", desc2 == desc0),
    ("restore preserves every hardware field", hardware2 == hardware0),
    ("restore preserves channel IDs, purposes, drivers and pins", registry2 == registry0),
]
for name, ok in checks:
    print("  %s: %s" % (name, ok))
raise SystemExit(0 if all(ok for _, ok in checks) else 1)
