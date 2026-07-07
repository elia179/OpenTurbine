"""The bench test suite.

Each test drives stimulus on one side and asserts the other side agrees:
  - input paths:  tester drives a pin  -> assert DUT telemetry
  - output paths: DUT drives a pin      -> assert tester measurement

Tests that need a feature not fitted/enabled on the DUT raise SkipTest with a
hint rather than failing.
"""

import time

from .runner import SkipTest


# ── shared helpers ───────────────────────────────────────────
def _to_standby(ctx):
    ctx.tester.reset()
    ok, d = ctx.dut.ensure_mode_standby()
    return ok, d


def _tool_output(ctx, c, cmd, sig_name, telem_key, pwm=False, window=1.8):
    """Fire a STANDBY actuator self-test command and confirm both the DUT
    telemetry and the tester's pin measurement see it drive."""
    ctx.dut.ensure_mode_standby()
    # Actuator self-tests are mutually exclusive; a previous one may still be
    # expiring. Retry briefly while the DUT reports another output active.
    deadline = time.time() + 14
    code, resp = ctx.dut.command(cmd)
    while code == 409 and "already active" in str(resp.get("error", "")) and time.time() < deadline:
        time.sleep(0.5)
        code, resp = ctx.dut.command(cmd)
    if code != 200:
        raise SkipTest("%s rejected (HTTP %s): %s — actuator likely not enabled on DUT"
                       % (cmd, code, resp.get("error")))
    active_pin = False
    active_tel = False
    deadline = time.time() + window
    while time.time() < deadline:
        if pwm:
            _us, _hz, duty, level = ctx.tester.get_pwm(sig_name)
            if duty > 0.02 or level == 1:
                active_pin = True
        else:
            if ctx.tester.get_level(sig_name) == 1:
                active_pin = True
        if ctx.dut.data().get(telem_key):
            active_tel = True
        if active_pin and active_tel:
            break
    c.expect(active_tel, "%s -> DUT telemetry '%s' active" % (cmd, telem_key))
    c.expect(active_pin, "%s -> tester measures %s driven" % (cmd, sig_name))
    # Let this tool expire so the next test starts from a clean idle DUT.
    ctx.dut.poll_until(lambda x: not x.get(telem_key), timeout=12)


# ── connectivity ─────────────────────────────────────────────
def t_handshake(ctx, c):
    line = ctx.tester.ping()
    c.expect(line.startswith("OK OTBench"), "tester PING -> %r" % line)
    ok, s = ctx.dut.ping()
    c.expect(ok, "DUT /api/status reachable" if ok else "DUT unreachable: %s" % s)
    if ok:
        d = ctx.dut.data()
        c.info("DUT fw=%s mode=%s profile_match=%s"
               % (d.get("fw_version"), d.get("mode"), d.get("profile_match")))


def t_safe_state(ctx, c):
    ok, d = _to_standby(ctx)
    c.expect(ok, "DUT settled to STANDBY (mode=%s)" % d.get("mode"))


# ── input paths (tester -> DUT) ──────────────────────────────
def t_stop_switch(ctx, c):
    ctx.tester.set("STOP", 0)
    time.sleep(0.2)
    ctx.tester.set("STOP", 1)
    ok, d = ctx.dut.poll_until(lambda x: x.get("stop_switch_active") is True, timeout=2)
    c.expect(ok, "STOP pressed -> stop_switch_active True (got %s)" % d.get("stop_switch_active"))
    ctx.tester.set("STOP", 0)
    ok2, d2 = ctx.dut.poll_until(lambda x: x.get("stop_switch_active") is False, timeout=2)
    c.expect(ok2, "STOP released -> stop_switch_active False (got %s)" % d2.get("stop_switch_active"))


def t_n1_rpm(ctx, c):
    pm = ctx.pinmap
    samples = []
    for rpm in (2000, 5000, 9000):
        hz = pm.rpm_to_hz("N1", rpm)
        ctx.tester.set("N1", round(hz, 2))
        time.sleep(0.6)  # PCNT integrates over ~100 ms; allow health/average to settle
        got = ctx.dut.data().get("n1", 0)
        samples.append((rpm, hz, got))
        c.info("N1 %d rpm (%.1f Hz) -> telemetry n1=%s" % (rpm, hz, got))
    ctx.tester.set("N1", 0)
    if all(g == 0 for _, _, g in samples):
        raise SkipTest("n1 stayed 0 — enable OT_HAS_N1_RPM on DUT GPIO %d and check ppr"
                       % pm.sig("N1")["dut_gpio"])
    c.expect(samples[0][2] < samples[-1][2], "n1 telemetry rises with stimulus")
    # The S3 PCNT integrates over ~100 ms, so at ppr=1 one pulse is ~600 rpm of
    # quantization — widen the tolerance accordingly at low rpm.
    near = all(abs(got - rpm) <= max(650, 0.10 * rpm) for rpm, _, got in samples)
    c.expect(near, "n1 within tolerance of commanded rpm (allowing PCNT quantization)")


def t_throttle_input(ctx, c):
    d0 = ctx.dut.data()
    t = d0.get("throttle_input_type")
    if t == "none":
        raise SkipTest("throttle input not enabled on DUT")
    if t == "servo":
        raise SkipTest("throttle input is RC-PWM on DUT; this analog test does not apply")
    pm = ctx.pinmap
    seq = []
    for volts in (0.0, 1.65, 3.3):
        ctx.tester.set("THROTTLE_IN", volts)
        time.sleep(0.4)
        raw = ctx.dut.data().get("throttle_input_raw", 0)
        seq.append((volts, raw))
        c.info("THROTTLE_IN %.2f V -> throttle_input_raw=%s (expect ~%d)"
               % (volts, raw, pm.volts_to_counts(volts)))
    ctx.tester.set("THROTTLE_IN", 0.0)
    c.expect(seq[0][1] < seq[-1][1], "throttle_input_raw rises 0 V -> 3.3 V")
    mid_exp = pm.volts_to_counts(1.65)
    c.expect(abs(seq[1][1] - mid_exp) <= 500, "mid-scale within +/-500 counts of %d" % mid_exp)


def t_oil_pressure_input(ctx, c):
    pm = ctx.pinmap
    ctx.tester.set("OILP", 0.3)      # true DAC — clean, no RC settling needed
    time.sleep(0.4)
    lo = ctx.dut.data().get("oil_raw", 0)
    ctx.tester.set("OILP", 2.5)
    time.sleep(0.4)
    hi = ctx.dut.data().get("oil_raw", 0)
    ctx.tester.set("OILP", 0.0)
    c.info("oil_raw: 0.3 V -> %s, 2.5 V -> %s (DAC on tester GPIO %d)"
           % (lo, hi, pm.sig("OILP")["tester_gpio"]))
    if lo == 0 and hi == 0:
        raise SkipTest("oil_raw stayed 0 — enable OT_HAS_OIL_PRESS on DUT GPIO %d"
                       % pm.sig("OILP")["dut_gpio"])
    c.expect(hi > lo, "oil_raw rises with driven DAC voltage")


def t_flame_input(ctx, c):
    ctx.tester.set("FLAME", 1)      # digital HIGH -> above threshold
    time.sleep(0.5)
    on = ctx.dut.data().get("flame")
    ctx.tester.set("FLAME", 0)      # digital LOW -> below threshold
    time.sleep(0.5)
    off = ctx.dut.data().get("flame")
    c.info("flame: driven HIGH -> %s, driven LOW -> %s" % (on, off))
    if on is None:
        raise SkipTest("no 'flame' telemetry — flame sensor not enabled on DUT")
    c.expect(on is True and off is False, "flame detect follows threshold crossing")


# ── output paths (DUT -> tester) ─────────────────────────────
def t_igniter_output(ctx, c):
    _tool_output(ctx, c, "IGN_TEST", "IGNITER", "igniter_on")


def t_oilpump_output(ctx, c):
    _tool_output(ctx, c, "OIL_PRIME", "OILPUMP_OUT", "oil_pct", pwm=True)


def t_fuelsol_output(ctx, c):
    _tool_output(ctx, c, "FUEL_SOL_TEST", "FUEL_SOL", "fuel_sol_open")


def t_starter_en_output(ctx, c):
    _tool_output(ctx, c, "STARTER_EN_TEST", "STARTER_EN", "starter_enabled")


# ── advanced (may move the engine state machine) ─────────────
def t_start_switch(ctx, c):
    ctx.dut.ensure_mode_standby()
    ctx.tester.set("START", 1)
    ok, d = ctx.dut.poll_until(lambda x: x.get("start_switch_active") is True, timeout=1.5)
    ctx.tester.set("START", 0)
    ctx.dut.stop()               # abort any start the edge may have triggered
    ctx.dut.ensure_mode_standby()
    c.expect(ok, "START pressed -> start_switch_active True (got %s)" % d.get("start_switch_active"))


def t_sequence_bench(ctx, c):
    """Run a bench-mode timed startup and confirm the oil pump and igniter
    actually fire on their sequence pins."""
    ctx.dut.ensure_mode_standby()
    dev = ctx.dut.ensure_dev_mode(True)
    bench = ctx.dut.ensure_bench_mode(True)
    c.info("dev_mode=%s bench_mode=%s" % (dev, bench))
    ctx.tester.reset()
    code, resp = ctx.dut.start()
    if code != 200:
        ctx.dut.ensure_bench_mode(False)
        ctx.dut.ensure_dev_mode(False)
        raise SkipTest("start rejected (HTTP %s): %s" % (code, resp.get("error")))
    saw_oil = False
    saw_ign = False
    blocks = []
    try:
        # Sample the tester pins fast (serial only, ~40 ms) so short action-block
        # windows aren't aliased past — the IgniterOn..IgniterOff window is only a
        # few hundred ms. Only reach for the slower DUT HTTP poll occasionally, to
        # track the block and notice when the sequence has settled (STARTUP -> a
        # steady mode). current_block reports the timed blocks; the 0 ms action
        # blocks (OilPumpOn/IgniterOn/...) execute instantly and aren't observable.
        deadline = time.time() + ctx.opts.get("seq_secs", 45)
        next_http = 0.0
        steady_since = None
        while time.time() < deadline:
            st = ctx.tester.state()
            if st.get("OILPUMP_OUT_duty", 0) > 0.02 or st.get("OILPUMP_OUT_level") == 1:
                saw_oil = True
            if st.get("IGNITER", 0) == 1:
                saw_ign = True
            now = time.time()
            if now >= next_http:
                next_http = now + 0.3
                d = ctx.dut.data()
                blk = d.get("current_block")
                if blk and (not blocks or blocks[-1] != blk):
                    blocks.append(blk)
                mode = d.get("mode")
                # Startup finished when we leave STARTUP for a steady RUNNING (bench
                # mode never returns to STANDBY on its own) or back to STANDBY.
                if blocks and mode in ("RUNNING", "STANDBY"):
                    if steady_since is None:
                        steady_since = now
                    elif saw_ign and now - steady_since > 1.0:
                        break
                else:
                    steady_since = None
            time.sleep(0.04)
    finally:
        ctx.dut.stop()
        ctx.dut.ensure_mode_standby()
        ctx.dut.ensure_bench_mode(False)
        ctx.dut.ensure_dev_mode(False)
    c.info("blocks: %s" % (" -> ".join(blocks) if blocks else "(none seen)"))
    c.expect(saw_oil, "oil pump driven during startup sequence")
    c.expect(saw_ign, "igniter fired during startup sequence")


BASIC = [
    t_handshake,
    t_safe_state,
    t_stop_switch,
    t_n1_rpm,
    t_throttle_input,
    t_oil_pressure_input,
    t_flame_input,
    t_igniter_output,
    t_oilpump_output,
    t_fuelsol_output,
    t_starter_en_output,
]

ADVANCED = [
    t_start_switch,
    t_sequence_bench,
]


def get_tests(advanced=False):
    return BASIC + (ADVANCED if advanced else [])
