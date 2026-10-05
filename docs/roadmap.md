# Roadmap

## Phase 0 — Foundation prototype (current)

- [x] Platform-independent audio activity detector.
- [x] Rolling chat-activity algorithm.
- [x] Snooze policy with lead time, reserve, cooldown, stale-schedule handling, and duplicate prevention.
- [x] OBS audio monitor filter and thread-safe state registry.
- [x] Twitch token validation, ad schedule polling, and snooze call.
- [x] Disabled-by-default and dry-run-by-default configuration.
- [x] Core CI on Linux, macOS, and Windows.
- [x] Architecture and manual validation documentation.

Exit condition: the core builds and tests without OBS, and the plugin source has a coherent end-to-end audio-to-decision path.

## Phase 1 — Usable developer alpha

- [ ] Adopt the current official OBS plugin template and release workflow.
- [ ] Build the module against supported OBS releases on Windows, macOS, and Linux.
- [ ] Add an OBS Tools dialog or dock for status and settings.
- [ ] Show monitored source activity, next ad time, snooze count, and last decision.
- [ ] Support configuration reload without restarting OBS.
- [ ] Add a manual “test decision” mode that never calls the mutation endpoint.
- [ ] Add structured rate-limit and transient-error backoff.

Exit condition: a developer can install a package, select sources, and verify dry-run decisions without editing JSON.

## Phase 2 — Production Twitch authorization and chat

- [ ] Implement Twitch Device Code Grant without embedding a client secret.
- [ ] Store refresh credentials in Keychain, Windows Credential Manager, or Secret Service.
- [ ] Refresh tokens and disconnect cleanly on revocation.
- [ ] Implement EventSub WebSocket lifecycle and `channel.chat.message` subscription.
- [ ] Deduplicate EventSub messages and handle keepalive, reconnect, revocation, and resubscription.
- [ ] Expose message-rate, unique-chatter, and post-burst hold settings.
- [ ] Add tests for EventSub parsing and reconnect state transitions.

Exit condition: a broadcaster connects Twitch from OBS and can use both audio and chat rules without handling tokens manually.

## Phase 3 — Rule builder and richer busy signals

- [ ] `ANY`, `ALL`, and grouped rules.
- [ ] Per-source rules rather than only aggregate audio activity.
- [ ] Raid, Hype Train, subscriptions, cheers, and channel-point windows.
- [ ] Scene state and manual hotkey conditions.
- [ ] Optional local speech VAD with no audio upload.
- [ ] Maximum consecutive snoozes and per-condition priorities.
- [ ] Import/export of rule presets without credentials.

Exit condition: nontechnical broadcasters can express common “do not interrupt this moment” policies from the UI.

## Phase 4 — Public distribution

- [ ] Signed/notarized Windows and macOS packages and Linux packages.
- [ ] Supported OBS-version compatibility matrix.
- [ ] Upgrade/migration tests and uninstall documentation.
- [ ] Accessibility and localization review.
- [ ] Privacy notice and security review.
- [ ] Beta telemetry only if explicitly opt-in and content-free.
- [ ] OBS Project resource submission and release notes.

Exit condition: repeatable, signed releases with a documented support and rollback path.
