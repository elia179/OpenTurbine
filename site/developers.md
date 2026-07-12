---
layout: document
title: Developers and integrators
lede: Build source, validate changes, package releases, or integrate the OpenTurbine Cluster protocol.
---

The developer path is intentionally separate from normal Windows setup. Use the repository’s [developer documentation](https://github.com/elia179/OpenTurbine/tree/main/docs), [setup-tool packaging guide](https://github.com/elia179/OpenTurbine/blob/main/docs/SETUP_TOOL.md), and [OTC protocol](https://github.com/elia179/OpenTurbine/blob/main/docs/OTC_CLUSTER_PROTOCOL.md).

Run the existing browser audits with `npm ci` then `npm run audit:ui`. Build both supported firmware environments and filesystem images before release. Public setup-tool releases must be signed; current v0.5.23 is explicitly unsigned.
