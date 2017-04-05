# Chromium for Wayland

The goal of this project is to enable
[Chromium browser](https://www.chromium.org/) to run on
[Wayland](https://wayland.freedesktop.org/). Note that contrary to
[01.org/ozone-wayland](https://github.com/01org/ozone-wayland), the idea is
to keep very close to upstream developments as well as aligned on Google's own
plans. In particular, this fork is rebased against
[Chromium ToT](https://chromium.googlesource.com/chromium/src.git) each week
and patches are upstreamed as soon as possible. The implementation also relies
on actively developed Chromium technologies:

* [Aura](https://www.chromium.org/developers/design-documents/aura/aura-overview) and [MUS](https://www.chromium.org/developers/mus-ash) for the user interface.
* [Ozone](https://chromium.googlesource.com/chromium/src/+/master/docs/ozone_overview.md) as a platform abstraction layer together with the [upstream Wayland backend](https://chromium.googlesource.com/chromium/src.git/+/master/ui/ozone/platform/wayland/).
* [Mojo](https://chromium.googlesource.com/chromium/src/+/master/mojo) to perform IPC communication.

Notice that the effort done here is also useful to run Chromium via MUS on
Linux Desktop for X11.

# Building Chromium

General information is provided by the upstream documentation for
[Chromium on Linux](https://chromium.googlesource.com/chromium/src/+/master/docs/linux_build_instructions.md)
and
[Ozone](https://chromium.googlesource.com/chromium/src/+/master/docs/ozone_overview.md).
Here is the summary of commands to build and run Chrome for Wayland:

```
gn args out/Ozone --args="use_ozone=true enable_package_mash_services=true"
ninja -C out/Ozone chrome
./out/Ozone/chrome --mus --ozone-platform=wayland
```

By default, the `headless`, `x11` and `wayland` Ozone backends are
compiled and X11 is selected when `--ozone-platform` is not specified.
Please refer to the
[GN Configuration notes](https://chromium.googlesource.com/chromium/src/+/master/docs/ozone_overview.md#GN-Configuration-notes) for details on how to change
that behavior.

# Running Tests

Most of the Chromium tests should pass although they are not regularly tested
for now. One can run the MUS Demo as follows:

```
ninja -C out/Ozone mus_demo mash:all
./out/Ozone/mash --service=mus_demo --external-window-count=2
```

One can also run automated unit tests. For example to check the MUS demo and
window server:

```
ninja -C out/Ozone mus_ws_unittests mus_demo_unittests
./out/Ozone/mus_demo_unittests
./out/Ozone/mus_ws_unittests
```

Note that `--ozone-platform` can be passed to all the programs above to select
a specific Ozone backend.
