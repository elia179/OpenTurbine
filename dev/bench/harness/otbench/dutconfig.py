"""DUT reconfiguration helpers for the validation campaign.

Hardware changes (POST /api/hardware) reboot the DUT (~15 s incl. WiFi
reconnect). Config changes (PATCH /api/config) apply live. CRITICAL: every
change is VERIFIED by re-reading it back and retried if it didn't stick — a
config that silently fails to apply otherwise produces false test results.
"""

import time


def _nested_matches(cfg, partial):
    for k, v in partial.items():
        if isinstance(v, dict):
            if not isinstance(cfg.get(k), dict) or not _nested_matches(cfg[k], v):
                return False
        elif isinstance(v, float) or isinstance(cfg.get(k), float):
            if abs((cfg.get(k) or 0.0) - (v or 0.0)) > 1e-4:
                return False
        elif cfg.get(k) != v:
            return False
    return True


class DutConfig:
    def __init__(self, dut):
        self.dut = dut

    # ── low level ────────────────────────────────────────────
    def hw(self):
        return self.dut.hardware()

    def cfg(self):
        return self.dut.config()

    def _wait_reboot(self, n=45):
        # POST returns 200 then schedules restart ~1 s later, so /api/status
        # answers briefly BEFORE the reboot. Confirm it drops then returns,
        # then settle so the reboot-pending preflight flag has cleared.
        time.sleep(1.2)
        for _ in range(20):
            try:
                self.dut.status(); time.sleep(0.3)
            except Exception:
                break
        for _ in range(n):
            try:
                self.dut.status(); time.sleep(2.0); return True
            except Exception:
                time.sleep(1)
        return False

    def edit_hw(self, mutate, check=None, tries=3):
        """GET hardware, apply mutate(hw), POST (reboots), wait, then VERIFY via
        check(hw)->bool. Retries the whole cycle if the change didn't stick."""
        resp = None
        for _ in range(tries):
            hw = self.dut.hardware()
            mutate(hw)
            code, resp = self.dut._post("/api/hardware", hw)
            if code != 200:
                time.sleep(2); continue
            self._wait_reboot()
            if check is None or check(self.dut.hardware()):
                return True, resp
        return False, resp

    def patch_cfg(self, partial, verify=True, tries=4):
        """PATCH a partial (nested) config — applies live — and verify it stuck."""
        code = resp = None
        for _ in range(tries):
            code, resp = self.dut.patch("/api/config", partial)
            if code != 200:
                time.sleep(1); continue
            if not verify or _nested_matches(self.dut.config(), partial):
                return True, resp
            time.sleep(1)
        return False, resp

    # ── verified hardware helpers ────────────────────────────
    def sensor(self, name, **fields):
        return self.edit_hw(lambda hw: hw["sensors"][name].update(fields),
                            check=lambda hw: all(hw["sensors"][name].get(k) == v for k, v in fields.items()))

    def actuator(self, name, **fields):
        return self.edit_hw(lambda hw: hw["actuators"][name].update(fields),
                            check=lambda hw: all(hw["actuators"][name].get(k) == v for k, v in fields.items()))

    def set_safety(self, **flags):
        return self.edit_hw(lambda hw: hw["safety"].update(flags),
                            check=lambda hw: all(hw["safety"].get(k) == v for k, v in flags.items()))

    def set_controllers(self, **flags):
        return self.edit_hw(lambda hw: hw["controllers"].update(flags),
                            check=lambda hw: all(hw["controllers"].get(k) == v for k, v in flags.items()))

    def only_safety(self, *on):
        """Arm exactly the named safeties, disarm all others. Verified."""
        def m(hw):
            for k in hw["safety"]:
                hw["safety"][k] = k in on
        return self.edit_hw(m, check=lambda hw: all((hw["safety"][k] is (k in on)) for k in hw["safety"]))

    def set_sequence(self, startup=None, startup_delays=None, shutdown=None, shutdown_delays=None):
        def m(hw):
            if startup is not None: hw["startup_seq"] = startup
            if startup_delays is not None: hw["startup_delay_ms"] = startup_delays
            if shutdown is not None: hw["shutdown_seq"] = shutdown
            if shutdown_delays is not None: hw["shutdown_delay_ms"] = shutdown_delays
        return self.edit_hw(m)

    def multi(self, mutate, check=None):
        return self.edit_hw(mutate, check=check)

    def fast_cooldown(self):
        def m(hw):
            hw["shutdown_delay_ms"] = [800 if d and d > 800 else d for d in hw.get("shutdown_delay_ms", [])]
        return self.edit_hw(m)

    # ── snapshot / restore ───────────────────────────────────
    def snapshot(self):
        return self.dut.hardware()

    def restore(self, snap):
        code, resp = self.dut._post("/api/hardware", snap)
        if code == 200:
            self._wait_reboot()
        return code == 200, resp
