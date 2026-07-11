# Developer and beta documentation

The repository root [`README.md`](../README.md) is the authoritative user installation and operating guide. User-facing setup instructions should be corrected there first instead of creating another competing guide.

This directory contains material for people developing, validating, packaging, or integrating OpenTurbine.

## Product principles

- Support hobby turbine installations broadly: turbojets, APUs, generators, turboshafts, turboprops, test rigs, and unusual custom systems.
- Keep the ordinary path understandable, but preserve advanced configuration rather than deciding what a user is allowed to build.
- Derive visibility and suggestions from fitted hardware. Do not silently impose aircraft assumptions or one preferred turbine architecture.
- Treat examples and suggested values as unverified starting points, never as authoritative limits.
- Require user action only for immediate safety, destructive operations, or configuration integrity. Prefer warnings and clear consequences when informed choice is possible.
- Keep the classic ESP32 target viable; its application partition has little remaining headroom, so firmware-size impact is a release concern.

## Integrations

- [`OTC_CLUSTER_PROTOCOL.md`](OTC_CLUSTER_PROTOCOL.md) — complete OpenTurbine Cluster wire protocol
- [`../examples/cluster/README.md`](../examples/cluster/README.md) — practical cluster/companion-device implementation guide
- [`../examples/OTCClusterClient.h`](../examples/OTCClusterClient.h) — reusable Arduino/ESP32 OTC client

## Release engineering

- [`SETUP_TOOL.md`](SETUP_TOOL.md) — building and packaging the Windows setup tool and release assets
- [`WINDOWS_FLASHER_INSTALL.md`](WINDOWS_FLASHER_INSTALL.md) — legacy focused SmartScreen/download troubleshooting; normal installation now lives in the root README
- [`../CHANGELOG.md`](../CHANGELOG.md) — version history

## Beta and internal work

- [`BETA_USER_GUIDE.md`](BETA_USER_GUIDE.md) — beta reporting and extended historical notes; the root README is authoritative for current setup
- [`internal/BETA_READINESS_PLAN.md`](internal/BETA_READINESS_PLAN.md) — internal validation plan
- [`../dev/`](../dev/) — design specification, code map, bench harness, campaigns, and validation records
- [`../tools/`](../tools/) — UI audits, asset generation, setup packaging, and build helpers

## Repository layout

| Path | Purpose |
|---|---|
| `src/` | ECU firmware implementation |
| `data_src/` | editable web interface sources |
| `data/` | generated gzip web assets stored in LittleFS |
| `examples/` | cluster and integration examples |
| `tools/` | release and validation tooling |
| `dev/` | developer design and bench-test material |
| `docs/internal/` | internal beta/release planning |
| `dist/` | locally generated packages and executables |

## Required release checks

Before publishing a release:

1. Confirm the **Release checks** GitHub Actions workflow passes. It runs every browser audit, both firmware/filesystem builds, and the Windows installer tests/build on clean hosted machines.
2. Locally, `npm ci` followed by `npm run audit:ui` runs every `tools/ui_*.cjs` audit.
3. Build firmware and LittleFS for `esp32dev` and `esp32s3dev`.
4. Run `go test ./...` and rebuild the setup tool from `tools/setup_tool/`.
5. Build the recommended ZIP with complete CP210x and CH340 driver packages.
6. Test USB installation on a blank board and Wi-Fi update on an installed ECU.
7. Perform the physical ECU bench campaign required by the changed control paths.
8. Publish `OpenTurbineSetupTool.exe`, `OpenTurbine_Recommended.zip`, and their SHA-256 files under the exact stable asset names used by the root README.
