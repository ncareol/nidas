# Changelog

This file summarizes notable changes to NIDAS source.  The format is based on
[Keep a Changelog], the versions should follow [semantic versioning].

## [buster] - Branch

This is the development and production branch for the DSM3, a DSM based on
Raspberry Pi 3 and a custom serial interface card with software control for
sensor power and serial transceiver modes.  Following are the main changes on
the buster branch.

### Hardware Configuration

DSM3 hardware support is built into NIDAS, especially autoconfiguration of the
serial transceiver mode according to the sensor type: RS232, RS485 full
duplex, or RS485 half-duplex.  This also includes the sensor power relays, so
`dsm` can turn on power to a port when a sensor is opened.

### Sensor Configuration

Select ISFS sensors can be queried and configured automatically, including the
NCAR TRH, Gill 2D sonic anemometers, PTB210, PTB220, and Wisard Motes.  Sensor
parameters can be specified in the XML, then those parameter settings can be
applied to the sensor.  The sensor classes can also query for the current
sensor settings and related metadata like serial number, so those metadata can
be logged.  Eventually the metadata will be captured and preserved at regular
intervals.

### Sensor Search and Detection

Sensors can now specify a set of standard serial port configurations,
including hardware transceiver modes on DSM3 ports, and NIDAS will search
those port configurations to find the one in use by the attached sensor.  Once
the sensor serial comms are detected, some sensors can then be switched
automatically to use the serial communication settings set in the XML
configuration.

This does not yet include identifying the kind of sensor attached.  NIDAS only
looks for a response according to the single sensor specified for the port.

## [1.4.4] - Pending

Serial sensors with a `timeout` setting will now be powered off when they time
out, and powered on again in the first reopen attempt.

## [1.4.3] - 2026-08-12

A bug was fixed in port configuration settings.  Sensors with `autoconfig`
enabled in the XML would start checking port configs looking for a response
from the sensor, but the port configs applied to the serial device were
missing the raw mode setting.  This meant serial ports had unexpected settings
like `ICRNL` and `ICANON`, so TRH sensor messages would have two newlines
`\n\n` at the end of a message instead of `\r\n`. All `PortConfig` instances
now default to raw mode.

## [1.4.2] - 2026-02-28

The latest `data_stats` has been merged from the main development branch into
the DSM3 release branch.  This includes the JSON file schemes required by the
latest DSM Dashboard and the fix which advances the stats period when the
system clock is corrected forward to GPS.

## [1.4.1] - 2025-07-07

Source now builds on Debian Bookworm aarch64.

The `button_action` utility has been added to trigger actions on the DSM
when the push buttons on the serial interface card are pressed, such as
blocking or unblocking the wifi radio.

Skip DNS lookups while parsing XML configuration files.  The lookups were only
to warn of unknown host names, but they severely slowed down parsing when the
XML was parsed on a host where those host names were not expected to resolve
anyway.

Fixes to support non-char sample files like from the hotfilm program. NIDAS
tools like `data_dump` and `data_stats` no longer assume data files with
non-char samples are corrupt, and they have a `--raw` command-line flag to
accept those samples as if they were processed.

Fixes for the NIDAS install tree to allow the hotfilm program to build against
NIDAS and without the source tree.

The `pio` utility can write hardware status in JSON format and update
status to an output path at regular intervals:

```sh
pio --json --update 5 --output ports.json
```

## [1.4]

This is the first of a series of beta versions to test ongoing refactors of
the buster branch before merging it with master.

The hardware API was refactored to simplify it, consolidate it into one
header, hide the implementation so it can be swapped or compiled out entirely,
and fix a memory leak.

The `pio` utility has been simplified and has subsumed the `dsm_port_config`
utility.  It now reports hardware status for all available FTDI itnerfaces:
outputs, inputs, and serial modes.  Any of the settings can be changed with
simple command-line arguments.  See `pio -h` for usage.

## [1.3.253]

One DSM during SOS, `cb`, had this version installed.  This version included
enhancements to better handle the prompting for the snow pillow instruments.
Released as version string `v1.3-253`.

## [1.3.235]

This is the version deployed for the SOS field project, released with version
string `v1.3-235`.

## [1.3] - 2019-06-03

This is a version tag on the auto-config branch that was not meant for
release.  All versions relative to v1.3 (`v1.3-316-gfaba0065d` as of
2023-06-01) are on the auto-config branch, and those versions are only built
and installed on the DSM3.  The DSM3 runs NIDAS on a Raspberry Pi3 running the
Buster release of Raspbian OS, so that branch is now known as [buster].  See
the changes on that branch in the section above.

## [1.2] - 2015-10-18

This version tag is on the main development branch.  All NIDAS package
versions since then have been generated relative to that tag, except for the
DSM3 packages which use `v1.3`.  As of 2023-06-21 the generated version is up
to `v1.2-1721-g1ebc8bcd4`.

<!-- Links -->
[keep a changelog]: https://keepachangelog.com/en/1.0.0/
[semantic versioning]: https://semver.org/spec/v2.0.0.html

<!-- Versions -->
[buster]: https://github.com/ncareol/nidas/tree/buster
[1.4.4]: https://github.com/ncareol/nidas/compare/v1.4.3...buster
[1.4.3]: https://github.com/ncareol/nidas/compare/v1.4.2...v1.4.3
[1.4.2]: https://github.com/ncareol/nidas/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/ncareol/nidas/compare/v1.4...v1.4.1
[1.4]: https://github.com/ncareol/nidas/compare/v1.3.253...v1.4
[1.3.253]: https://github.com/ncareol/nidas/compare/v1.3.235...v1.3.253
[1.3.235]: https://github.com/ncareol/nidas/compare/v1.3...v1.3.235
[1.3]: https://github.com/ncareol/nidas/compare/master...v1.3
[1.2]: https://github.com/ncareol/nidas/releases/tag/v1.2
