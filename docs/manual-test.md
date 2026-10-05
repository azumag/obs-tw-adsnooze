# Manual Test Guide

This guide separates safe local validation from tests that can mutate a real Twitch ad schedule.

## 1. Core tests

```bash
cmake -S . -B build -DBUILD_OBS_PLUGIN=OFF -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Expected result: `adsnooze-core-tests` passes.

The tests cover:

- RMS conversion to dBFS;
- audio attack, release hold, hysteresis, and mute behavior;
- chat rolling window, unique chatters, duplicate IDs, and safe zero-threshold behavior;
- snooze lead time, reserve count, and duplicate handling;
- RFC3339 UTC, offsets, fractional seconds, and invalid dates.

## 2. Plugin build check

Build against the OBS development SDK and libcurl:

```bash
cmake -S . -B build-plugin -DBUILD_OBS_PLUGIN=ON -DBUILD_TESTING=ON
cmake --build build-plugin --parallel
```

Confirm that the module links and that `data/locale` is installed beside the plugin data.

## 3. OBS audio filter check without Twitch mutation

1. Use a disposable OBS profile and scene collection.
2. Install the development module.
3. Start OBS and verify the log contains `Plugin loaded`.
4. Confirm the default `config.json` was created with `enabled: false` and `dry_run: true`.
5. Add **Twitch Ad Snooze: Audio Activity Monitor** to a microphone or media source.
6. Speak or play audio above the default threshold.
7. Mute the source and confirm it becomes inactive immediately in a debugger or temporary diagnostic build.
8. Stop a media source while it is active and confirm stale-source protection clears it after the configured hold plus guard time.
9. Change thresholds while audio is running and confirm there is no crash, deadlock, or audio dropout.

The current prototype does not expose live state in the UI, so steps 6–9 require logs, a debugger, or temporary diagnostic instrumentation. The status dock is a Phase 1 item.

## 4. Twitch read-only dry run

Use a development Twitch application and a test broadcaster account where possible.

1. Obtain a broadcaster user access token containing `channel:read:ads` and `channel:manage:ads`.
2. Populate `client_id`, `access_token`, and numeric `broadcaster_id`.
3. Keep `dry_run: true`.
4. Set `enabled: true` and restart OBS.
5. Verify the log reports a successful token validation.
6. Verify schedule polling succeeds when the channel is live and Ads Manager has an upcoming automatic ad.
7. Make a monitored source active inside the configured lead window.
8. Confirm the log says `Dry run: would snooze upcoming ad` exactly once for that scheduled timestamp.
9. Confirm no mutation was made in Twitch Ads Manager.

## 5. Real snooze test

This step mutates the broadcaster's real ad schedule. Perform it only on an account and stream where that is acceptable.

1. Complete the dry-run checklist first.
2. Ensure at least one snooze is available and note the scheduled ad time.
3. Set `dry_run: false` and restart OBS.
4. Activate a monitored source inside the lead window.
5. Confirm Twitch moves the upcoming ad five minutes later and decrements the available snooze count.
6. Confirm the OBS log records one successful snooze with the trigger reason.
7. Keep the source active and confirm the same old timestamp is not submitted twice.
8. Simulate an invalid token and verify a 401 forces revalidation and no further snooze attempt.

## 6. Tests intentionally deferred

The following cannot pass until their roadmap phases are implemented:

- Twitch login and refresh through the OBS UI;
- chat EventSub connection and message-rate decisions;
- secure credential storage;
- signed installer and upgrade testing;
- accessible settings and localization QA.
