# Security Policy

## Supported versions

| Version | Supported |
|---------|-----------|
| 1.0.0   | Yes       |
| < 1.0.0 | No        |

## Reporting a vulnerability

Please **do not** open a public issue for security problems.

Use GitHub's private vulnerability reporting: go to the repo's
**Security → Advisories → Report a vulnerability** and describe what you
found. Reports typically get a first response within a few days.

Firmware-relevant areas we care most about:

- NMEA/UART input handling from untrusted GPS sources
- SSID and BLE name strings from untrusted broadcast frames
- SD-card file parsing (session CSV read-back, OTA images)
- NVS-stored configuration integrity

## Scope notes

SiteSurvey Pro is a passive survey tool: it receives and records broadcast
radio traffic but does not deauthenticate, inject, or actively attack
networks. Vulnerabilities in third-party components (ESP-IDF, LVGL, NimBLE)
should be reported upstream, but tell us too so we can bump the dependency.
