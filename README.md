# OBS Twitch Ad Snooze

A native OBS Studio plugin prototype that postpones an upcoming Twitch automatic mid-roll ad when the stream is in a moment that should not be interrupted.

The first implemented condition is activity on explicitly selected OBS audio sources. The product design also includes Twitch chat activity, raids, hype events, and configurable rule combinations.

> [!IMPORTANT]
> This repository is an **early developer prototype**, not an end-user release. Audio monitoring, ad-schedule polling, dry-run decisions, the Twitch snooze API call, and a macOS development bundle pipeline are implemented. Twitch login UI, secure token storage, EventSub chat transport, settings UI, notarization, and supported installers are still on the roadmap.

This project is not affiliated with or endorsed by Twitch or OBS Project.

## Why this exists

Twitch can automatically snooze ads during some high-engagement moments, but a broadcaster may also want rules based on the actual production state, for example:

- a microphone, TTS source, Discord call, or media source is currently audible;
- chat has exceeded a configurable message and unique-chatter rate;
- a raid, Hype Train, or important interaction has just started;
- one or more of those conditions are active shortly before an automatic ad.

The goal is to make those rules approachable in OBS without requiring Streamer.bot scripts or custom API code.

## Current prototype

Implemented now:

- Native OBS audio filter that can be attached only to the sources the broadcaster wants to protect.
- RMS level detection in dBFS with separate activation/deactivation thresholds, attack time, release hold, mute handling, and stale-source protection.
- A thread-safe registry that keeps network and policy work out of the real-time OBS audio callback.
- Twitch token validation at startup and at least hourly.
- Polling of the Twitch ad schedule and a call to the official **Snooze Next Ad** endpoint.
- Safety controls: disabled-by-default configuration, dry-run by default, lead window, retry cooldown, reserved snooze count, and duplicate-attempt prevention.
- Rolling chat-activity and unique-chatter calculation as a transport-independent core component.
- Cross-platform core tests on Linux, macOS, and Windows through GitHub Actions.
- Official-template-compatible build metadata and a verified universal macOS development bundle workflow.

Not implemented yet:

- Twitch EventSub WebSocket connection and `channel.chat.message` subscription.
- In-OBS Twitch login, refresh-token handling, or operating-system credential storage.
- A user-facing settings dock and rule builder.
- Notarized release packaging and supported automatic installation.
- Speech-only VAD. The current filter detects audible energy, not semantic speech.

See [Architecture](docs/architecture.md), [Roadmap](docs/roadmap.md), [Manual test guide](docs/manual-test.md), and [macOS development build](docs/macos-dev-build.md).

## Decision model

The prototype snoozes only when all of the following are true:

1. The integration is enabled.
2. Twitch reports an upcoming automatic ad within the configured lead window.
3. More snoozes remain than the configured reserve.
4. At least one enabled activity condition is active.
5. The same scheduled timestamp has not already been handled and is not in retry cooldown.

Audio and chat are currently modeled as an `ANY` rule. A general `ANY` / `ALL` / grouped rule editor is planned.

## Repository layout

```text
src/core/       Platform-independent activity and snooze policy
src/obs/        OBS audio filter, state registry, config, and worker runtime
src/twitch/     Twitch Helix API client
tests/          Dependency-free core unit tests
data/locale/    OBS localization strings
docs/           Architecture, roadmap, build, and validation notes
scripts/        Reproducible development build entry points
```

## Build and test the core

The policy and detector core has no OBS or Twitch dependency.

```bash
cmake -S . -B build -DBUILD_OBS_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Build an OBS-loadable macOS development bundle

The macOS build overlays this project onto a pinned revision of the official OBS plugin template, uses the dependency hashes in `buildspec.json`, and verifies that the resulting bundle is ad-hoc signed and universal.

```bash
brew install cmake jq ccache xcbeautify
scripts/build-macos-dev
```

Output:

```text
release/RelWithDebInfo/obs-tw-adsnooze.plugin
```

To replace the current user's development copy after quitting OBS:

```bash
scripts/build-macos-dev --install
```

See [`docs/macos-dev-build.md`](docs/macos-dev-build.md) for prerequisites, verification, CI artifacts, and troubleshooting. The generated artifact is for development testing only; it is not notarized or supported as a public release.

## Build against an existing OBS development SDK

A conventional CMake path remains available for developers that already expose the `OBS::libobs` target and libcurl to CMake:

```bash
cmake -S . -B build-plugin -DBUILD_OBS_PLUGIN=ON -DBUILD_TESTING=ON
cmake --build build-plugin --parallel
```

When the official template support files are present, `CMakePresets.json` and `buildspec.json` drive the bundle layout. Without them, the same CMake file retains the lightweight core-test and external-SDK paths.

## Developer-only configuration

On first load the plugin creates `config.json` in the module configuration directory resolved by OBS. It remains disabled and in dry-run mode. A documented example is available at [`config.example.json`](config.example.json).

For development testing only:

1. Build and install the module into a development OBS profile.
2. Start OBS once so the default configuration is created.
3. Add **Twitch Ad Snooze: Audio Activity Monitor** as a filter to each source that should block an ad while audible.
4. Register a Twitch application and obtain a broadcaster user access token with `channel:read:ads` and `channel:manage:ads`.
5. Fill in `client_id`, `access_token`, and the numeric `broadcaster_id`.
6. Keep `dry_run` set to `true` until the OBS log shows the expected decisions.
7. Set `enabled` to `true` and restart OBS after configuration changes.

The manual token is currently stored as plaintext in the OBS configuration directory. Do not use the prototype with a valuable production credential. Secure OAuth and credential storage are release blockers.

## 日本語概要

OBS上で選択した音声ソースが鳴っているとき、または将来的にチャットが盛り上がっているときに、直前のTwitch自動広告を5分延期するためのプラグインです。

現段階は開発者向け初期実装です。音声判定、広告予定取得、判定ロジック、dry-run、広告スヌーズAPI呼び出しに加え、公式OBSプラグインテンプレートを利用したmacOS Universal開発ビルドまで用意しています。一般配布に必要なTwitchログイン画面、チャットEventSub接続、設定画面、署名・公証済みインストーラーは未実装です。

## License

GPL-2.0-or-later. See [`LICENSE`](LICENSE).
