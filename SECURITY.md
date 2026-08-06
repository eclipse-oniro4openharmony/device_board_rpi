# Security Policy

This project follows the Eclipse Foundation Vulnerability Reporting Policy:

* https://www.eclipse.org/security/

## Reporting a Vulnerability

Please report vulnerabilities that are not yet publicly disclosed by email to
the Eclipse Foundation Security Team at security@eclipse-foundation.org. Do
NOT open a public GitHub issue for an undisclosed vulnerability.

## Scope

This repository contains the Raspberry Pi board support package: board-level configuration, kernel configuration and patches, and boot firmware for the Raspberry Pi 3 and 4 targets. Please report here only vulnerabilities in content authored by this
project:

* Vulnerabilities in upstream OpenHarmony components (the
  `eclipse-oniro-mirrors` repositories) should be reported to the upstream
  OpenAtom OpenHarmony project.
* Vulnerabilities in the Linux kernel should be reported upstream
  (see https://docs.kernel.org/process/security-bugs.html).
* Vulnerabilities in third-party vendor binaries fetched at build time (e.g.
  Halium / vendor blobs) should be reported to their respective vendors.
* Vulnerabilities in the Raspberry Pi / Broadcom boot firmware shipped under
  `rpi3/rpiboot/` and `rpi4/rpiboot/` should be reported to the Raspberry Pi
  Foundation at https://github.com/raspberrypi/firmware.

## Supported Versions

Only the most recent release branch (`OpenHarmony-6.1-LTS`) is supported with security
updates. Older release branches are not maintained.
