# macOS development build

This repository can produce an OBS-loadable universal `.plugin` bundle by
building through the official OBS plugin template toolchain. The source tree
keeps its dependency-free core-test path; the macOS entry point creates an
isolated template workspace, overlays this project, and copies only the release
output back.

## Supported development target

- macOS 12 or later
- Apple Silicon and Intel (`arm64` + `x86_64` universal binary)
- Xcode 16.1
- OBS plugin template commit `3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc`
- OBS SDK/dependency set pinned by `buildspec.json` to OBS Studio 31.1.1

The older SDK baseline is intentional for the first prototype so that the build
does not accidentally require a newer OBS ABI. Runtime compatibility still has
to be verified against every OBS version offered to testers.

## Prerequisites

Install Xcode 16.1 and accept its license, then install the command-line tools:

```bash
brew install cmake jq ccache xcbeautify
```

The script also uses the macOS-provided `git`, `rsync`, `zsh`, `lipo`, `otool`,
`codesign`, and `ditto` commands.

## Build

```bash
scripts/build-macos-dev
```

The first build downloads the pinned OBS source and prebuilt dependencies. They
are cached in `.cache/obs-template-deps`; later builds reuse that directory.
The verified bundle is written to:

```text
release/RelWithDebInfo/obs-tw-adsnooze.plugin
```

The script fails unless the bundle is ad-hoc signed and its executable contains
both `arm64` and `x86_64` slices. It also prints linked libraries with `otool`.

## Build and install for the current user

Quit OBS Studio, then run:

```bash
scripts/build-macos-dev --install
```

This replaces the development copy at:

```text
~/Library/Application Support/obs-studio/plugins/obs-tw-adsnooze.plugin
```

Restart OBS and open **Help > Log Files > View Current Log**. A successful load
contains a line similar to:

```text
[obs-tw-adsnooze] Plugin loaded (version 0.1.0)
```

Then add **Twitch Ad Snooze: Audio Activity Monitor** to a disposable audio
source and confirm that activity transitions appear in the OBS log before
configuring any Twitch credential.

## CI artifact

The `OBS Plugin Build` workflow performs the same universal build and publishes
`obs-tw-adsnooze-0.1.0-macos-universal.zip` as a direct GitHub Actions artifact.
The ZIP is created with `ditto --keepParent`, so extracting it produces the
`obs-tw-adsnooze.plugin` bundle rather than a loose `Contents` directory.

This is a development artifact: it is ad-hoc signed, not notarized, and must not
be presented as a public macOS release.

## Troubleshooting

- If Xcode 16.1 is not selected, run `sudo xcode-select --switch` with the exact
  installed Xcode application path.
- If dependency verification fails, remove `.cache/obs-template-deps` and build
  again; hashes are enforced by the official template CMake modules.
- If OBS refuses to load the bundle, inspect the current OBS log and run
  `codesign --verify --deep --strict` and `otool -L` on the plugin binary.
- Use `--keep-workdir` to preserve the isolated template workspace for CMake or
  Xcode inspection.
