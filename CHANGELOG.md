# Changelog

This file summarizes notable changes to NIDAS source.  The format is based on
[Keep a Changelog], the versions should follow [semantic versioning].

## [2.0] - Unreleased

The 2.0 version of NIDAS will be a merge of the [master] and [buster]
branches.  The [refactor-builds] branch has already been merged into [master].
Following are the significant changes on each branch.  See the Changelog on
the [buster] branch for the changes on that branch.

## [master] - Unreleased on master branch

### Changes related to M2HATS

- NIDAS XML configurations can now associate a different site with a sensor
  than the DSM which acquires that sensor.  If the site has been defined, the
  name of that site can be added as an attribute to the sensor:

  ```xml
  <site name="t6" class="isff.GroundStation" suffix=".${SITE}"/>
  <site name="t5" class="isff.GroundStation" suffix=".${SITE}">
    <dsm IDREF="COREV" name="${SITE}" id="5">
      ...
      <serialSensor IDREF="CSAT3B" devicename="/dev/ttyDSM5" height="4m" id="60" site="t6"/>
    </dsm>
  </site>
  ```

  The sensor's variable names then contain the right site suffix as well as
  important metadata like `height`.

- `svnStatus()` has been removed.  It was only used by the NetCDF RPC outputs,
  and it has been a while since the project configurations were under
  subversion control.  See notes in `nc-server` about using the
  `ISFS_CONFIG_VERSION` environment variable instead.

### data_stats and related improvements

- `data_stats` JSON output includes problems detected in the statistics, so
  far sample rate mismatches, missing values, and no samples.  The schema is
  still likely to change.
- The `-i` argument to programs like `data_dump` and `data_stats` now accepts
  '.' to select the DSM ID in the first sample in the data, and '/' to accept
  all IDs.  So this works to report on all of the samples from a single DSM:
  `data_stats -i . -a sock:localhost`.
- `data_stats` can generate periodic reports on archive data using the `-P
  period` and `-n` arguments, and multiple json reports can be written by
  adding time specifiers to the `--json` file path.

## [1.2.3] - 2024-03-02

- Removed TWODS detection.
- Statsproc now supports --clip argument to clip netcdf output like prep.
- UTime API improvements for unset times.

## [1.2.2] - 2023-12-13

Variable substitution is now done on sensor element attributes height, depth,
and suffix when they are applied to a sensor object.  This accommodates the
SOS config which uses environment  variables to specify changes in sonic
heights during the project.

`NDEBUG` is never defined by default for any target, so any `assert()` calls
in the code will now be compiled everywhere, where before they were being
omitted everywhere.

The SCons variant builds are now done with `duplicate` disabled.  Kernel
modules are built in the variant directories by using explicit symbolic links
to the module source files.

Fixed `fork()` and `exec()` problems in the _UDPArinc_ and _UDPiPM_ sensors.

Cleaned up compiler warnings.

## [1.2.1] - 2023-10-01

### bug fixes for min/max and variance statistics

The min/max statistics could be reported as +- 1e37 even when there were no
values during a time period.  Similarly, the mean in the variance statistics
could be 0 even when there were no values during the statistics period.

### udev rules restored, nidas-daq package removed

The `nidas-daq` package is no longer built.  Instead, the `dsm.init` script is
now installed into the NIDAS bin directory with other scripts, where it can be
copied or linked on systems which still use it.  The udev rules file no longer
changes permissions on all serial devices.  It only sets permissions on
devices associated with NIDAS Linux modules, so the rules file is only
installed as part of a nidas modules package, such as `nidas-modules-vortex`.
RPM and Debian packages are more consistent in when and how the rules file and
the `dsm.init` script are packaged.

### Libraries install into lib

The nidas install layout now uses just `lib` for the library directory name,
rather than an arch-specific path like `lib64` or `lib/i386-linux-gnu`.  The
libraries in `lib` have the same architecture as the executables in `bin`.
There are no NIDAS targets which require multiple architectures installed
under the same `/opt/nidas` install path.

For software that builds against nidas using the `nidas.py` tool from
[eol_scons](https://github.com/NCAR/eol_scons), that tool already checks for a
plain `lib` subdirectory if the arch-specific path is not found.  Likewise,
`pkg-config` will return the new library path for linking to NIDAS libraries.
However, if `PKG_CONFIG_PATH` is being used to select the `nidas.pc` settings
for a specific NIDAS installation, then it probably needs to be adjusted.  For
example, it must be set like below to use the `pkg-config` settings for a NIDAS
install in `/opt/local/nidas`:

```plain
export PKG_CONFIG_PATH=/opt/local/nidas/lib/pkgconfig
```

## [refactor-builds] - Branch

This branch includes all the changes done on the branches
`scons-build-single-target`, `container-builds`, `simplify-nidas-rpms`, and
`move-netcdf-to-nc_server`.

- The [SCons](https://www.scons.org/) setup now builds a single target OS and
  architecture, specified with the BUILD variable, or else the native host by
  default.  The variant build directory names include an OS name as well as
  architecture so different OS builds can share the same source tree,
  especially useful when testing builds with containers.  There are a few key
  changes related to this to prevent the scons cache and auto-configuration
  from conflicting between variants.  Likewise, `Revision.h` is generated in
  the source and not in a variant dir, since it does not depend on the target
  and needs to be part of source archives. Lots of similar scons code has been
  consolidated into tool files in the `nidas/src/tools` subdirectory.  Since
  there is no longer any confusion caused by sharing environments across
  multiple build targets, tools can be used safely and easily to share
  environment setups with the rest of the source tree.  There is now just a
  single `PREFIX` setting, `ARCHPREFIX` has been removed.

- SCons help is more brief by default by leaving out lots of less used
  variables.  Run `scons -h -Q` to see the short help, `scons --help-all` to see
  all of it.

- The optional `nc_server` dependency has been removed.  The two classes which
  needed it, `NetcdfRPCChannel` and `NetcdfRPCOutput` in the
  `nidas::dynld::isff` namespace, have moved to the
  [nc-server](https://github.com/ncareol/nc-server) source and are now built
  and installed from there.  NIDAS loads them from the `nc_server` lib
  directory (usually under `/opt/nc_server`) as long as that directory is on
  the `LD_LIBRARY_PATH`.

- The `pkg_files` directory in the source tree is now deprecated.  Anything
  that was installed as part of a package script should now be generated and
  installed by scons instead.  Files are installed from the relevant location
  in the source tree.  For example, module conf files are now in the module
  source directory.

- To allow scons to install everything needed by system packages, there is a
  second install target, `install.root`, for installing files into root
  locations.  The plain install target works as usual, installing everything
  under `PREFIX`, typically `/opt/nidas`.  The packaging scripts use the
  `install.root` target and set a new scons variable, `INSTALL_ROOT`, which is
  prepended to every install path.  This allows scons to generate files with
  the correct `PREFIX` path, even when the files will be installed into a
  temporary location for packaging.  Packaging scripts no longer need to
  regenerate those files with the correct install path.

- There is a script `setup_nidas.sh` which gets installed into the NIDAS bin
  directory.  It can be used with bash shells to setup `PATH` and
  `LD_LIBRARY_PATH` to run nidas from different install directories.  This
  makes it convenient to install NIDAS builds into alternate locations, then
  select which NIDAS install to use in the shell.  Thus `nidas-buildeol` is
  not needed anymore.  The `nidas.csh` and `nidas.sh` files in `profile.d`
  have been removed, as they likely were never used, and `setup_nidas.sh` can
  be used instead, at least for bash.

- The layout of the variant build directories has changed to match the install
  layout.  Programs are built into `variant_dir/bin` and libraries into
  `variant_dir/lib`.  Since `setup_nidas.sh` is installed there also, it can
  be used to setup a shell to use the NIDAS binaries in that build tree,
  without installing them first.  For example:

  ```plain
  source build_x86_64_fedora37/bin/setup_nidas.sh
  which dsm
  ~/code/nidas/src/build_x86_64_fedora37/bin/dsm
  ```

- NIDAS RPM packages have been simplified.  The `nidas-build`, `nidas-min`,
  and `nidas-buildeol` packages have been removed.

- There is a new script, `cnidas.sh`, which consolidates scripting for
  container builds.  It can build containers for different OS release and
  architecture targets from the available Dockerfiles, but it also automates
  mounting the source tree, install paths, and package output directory into
  the container, so all the container outputs will be available on the host.
  The `dsm_rsync.sh` script can be used to copy a NIDAS install,
  cross-compiled on the local host with a container, onto a DSM under an
  alternate prefix.  That alternate install can be selected and tested using
  `setup_nidas.sh`.  See
  [Develop_Pi.md](https://github.com/ncareol/nidas/blob/buster/Develop_Pi.md)
  for an example of using it for Pi3 development.

- Lots of obsolete code, executables, and scripts have been removed.

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
[master]: https://github.com/ncareol/nidas
[buster]: https://github.com/ncareol/nidas/tree/buster
[refactor-builds]: https://github.com/ncareol/nidas/tree/refactor-builds
[2.0]: https://github.com/ncareol/nidas/compare/master
[1.3]: https://github.com/ncareol/nidas/compare/master...v1.3
[1.2.3]: https://github.com/ncareol/nidas/releases/tag/v1.2.3
[1.2.2]: https://github.com/ncareol/nidas/releases/tag/v1.2.2
[1.2.1]: https://github.com/ncareol/nidas/releases/tag/v1.2.1
[1.2]: https://github.com/ncareol/nidas/releases/tag/v1.2
