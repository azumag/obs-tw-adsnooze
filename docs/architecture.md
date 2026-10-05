# Architecture

## Product objective

OBS Twitch Ad Snooze decides whether the next Twitch automatic mid-roll ad should be delayed because the live production is currently busy. It is intended for general broadcasters, not for one specific bot or channel.

The end-user product should support rules such as:

```text
upcoming automatic ad is within 90 seconds
AND
(
  microphone is active
  OR TTS is active
  OR Discord capture is active
  OR chat has at least 15 messages from 5 users in 30 seconds
  OR a raid happened within 3 minutes
)
AND
at least 1 snooze remains after this action
```

The initial implementation deliberately separates the real-time signal collection from Twitch networking and policy evaluation.

## Components

### 1. Audio activity detector (`src/core/audio-activity-detector.*`)

The detector receives planar floating-point PCM frames from an OBS audio filter and calculates RMS dBFS across available channels.

Its state machine provides:

- activation threshold;
- lower deactivation threshold for hysteresis;
- attack time to reject very short spikes;
- hold time to bridge short gaps between words or clips;
- immediate inactivity when the OBS source is muted.

This is an **audibility detector**, not speech recognition. A future optional VAD can classify speech while retaining the same activity interface.

### 2. OBS audio monitor filter (`src/obs/audio-monitor-filter.*`)

A broadcaster adds the filter only to sources whose active output should protect the current moment. Examples include microphone, browser TTS, application audio capture, Discord, or a media source.

The audio callback performs only bounded CPU work and atomic state publication. It does not:

- call Twitch;
- allocate network objects;
- write files;
- acquire the registry or label mutex;
- wait on another thread.

Filter property updates are published with atomics and applied by the audio thread on the next packet, avoiding concurrent mutation of the detector. A stale timeout prevents a source that stops producing audio packets from remaining active forever.

### 3. Audio state registry (`src/obs/audio-monitor-registry.*`)

Each filter owns a shared state object. The worker obtains snapshots from a process-wide registry outside the audio callback. Expired filter instances are represented by weak pointers and removed safely.

The first policy treats multiple monitored sources as an OR condition: any active source makes `audio_active` true. Future rules can target labels or source UUIDs individually.

### 4. Chat activity tracker (`src/core/chat-activity-tracker.*`)

The core accepts only:

- EventSub message ID;
- chatter user ID;
- monotonic receive time.

It calculates messages and unique chatters in a rolling time window and holds the active state briefly after a burst. It does not need or retain message text.

The transport is intentionally outside this component. This makes the algorithm deterministic and testable, and allows EventSub, an external relay, or another supported chat provider to feed the same interface later.

### 5. Snooze decision engine (`src/core/snooze-policy.*`)

The decision engine is pure policy. Inputs are current wall time, Twitch ad schedule, combined activity, and configuration.

It rejects a snooze when:

- the integration is disabled;
- no condition is enabled;
- Twitch has no upcoming ad;
- the returned schedule is stale or outside the lead window;
- only the reserved snoozes remain;
- no enabled activity condition is active;
- the same scheduled timestamp was already handled;
- a failed attempt is still in retry cooldown.

Successful dry-run decisions are recorded as handled so they do not flood the log every polling interval.

### 6. Twitch API client (`src/twitch/twitch-ad-client.*`)

The first client supports:

- `GET https://id.twitch.tv/oauth2/validate`;
- `GET https://api.twitch.tv/helix/channels/ads`;
- `POST https://api.twitch.tv/helix/channels/ads/schedule/snooze`.

Requests use HTTPS verification provided by libcurl, bounded response sizes, connection and request timeouts, no redirect following, and no token logging.

The runtime verifies that the validated token belongs to both the configured client and broadcaster and contains `channel:read:ads` and `channel:manage:ads`.

### 7. Worker runtime (`src/obs/plugin-runtime.*`)

A background worker validates the token, polls the schedule, snapshots activity, evaluates policy, and performs the optional snooze. OBS audio processing never waits for this worker.

The worker is stopped and joined before module unload. HTTP 401 responses force an immediate revalidation rather than leaving a previously valid token trusted until the next hourly check.

## Data flow

```text
OBS source audio packet
        |
        v
Audio Monitor Filter -- RMS/hysteresis --> atomic AudioMonitorState
                                                |
Twitch EventSub chat (planned) --> Chat tracker  |
                    |                           |
                    +--------------+------------+
                                   v
                           CombinedActivitySnapshot
                                   |
Twitch Get Ad Schedule ------------+--> SnoozeDecisionEngine
                                           |
                              dry-run log or Snooze Next Ad
```

## Threading and real-time safety

| Context | Responsibilities | Prohibited work |
| --- | --- | --- |
| OBS audio thread | RMS calculation, state machine, atomic publication | network, disk, blocking locks, UI |
| OBS/UI thread | filter settings and labels, module lifecycle | long network calls |
| Plugin worker | token validation, schedule polling, policy, snooze | direct mutation of OBS audio data |
| EventSub worker (planned) | WebSocket lifecycle, deduplication, chat events | OBS audio work |

The policy uses `steady_clock` for activity windows and cooldown-like local durations, and `system_clock` only when comparing Twitch's RFC3339 ad timestamp.

## Planned Twitch authentication

The public plugin must not ship a client secret. The intended release design is:

1. Twitch Device Code Grant for a public desktop client.
2. Minimum scopes:
   - `channel:read:ads`
   - `channel:manage:ads`
   - `user:read:chat` only when chat rules are enabled
3. Access and refresh token storage through the operating-system credential store.
4. Token validation at startup and at least hourly.
5. Refresh on expiry or 401; disconnect and clear the session when authorization is revoked.

The plaintext manual token in the current JSON configuration is only a development bridge.

## Planned chat transport

Chat will use Twitch EventSub over WebSocket:

1. Connect to Twitch EventSub WebSocket.
2. Receive `session_welcome` and its session ID.
3. Create a `channel.chat.message` version 1 subscription through Helix within the welcome deadline.
4. Feed `message_id` and `chatter_user_id` into `ChatActivityTracker`.
5. Track EventSub metadata message IDs to ignore at-least-once duplicates.
6. Monitor keepalives, follow Twitch reconnect URLs without modifying them, and resubscribe after an ungraceful disconnect.
7. Do not persist chat text. The default metric requires only IDs and timestamps held in memory.

A bounded deduplication cache and a ten-minute timestamp acceptance window will be used before enabling production chat activity.

## Configuration and future UI

The current prototype reads one `config.json` at OBS startup. The target user experience is an OBS dock or Tools dialog with:

- Twitch connection status and Disconnect button;
- global enable and dry-run switches;
- next ad time and remaining snooze count;
- list of monitored sources and live activity indicators;
- audio threshold presets with an advanced mode;
- chat window, message count, and unique chatter thresholds;
- `ANY`, `ALL`, and grouped rule combinations;
- action history explaining why a snooze did or did not occur.

Configuration should be versioned and migrated forward. Secrets must remain outside the ordinary JSON document.

## Failure and safety behavior

The default behavior is fail-closed: a missing schedule, invalid token, malformed response, no available snooze, or uncertain state produces no action.

Additional safety constraints for the public alpha:

- start disabled and in dry-run;
- never disable TLS certificate validation;
- rate-limit and exponential-backoff handling;
- a configurable reserve and maximum consecutive snoozes;
- an emergency global disable control;
- no automatic commercial start endpoint;
- no collection of chat text or audio samples;
- logs must never include access or refresh tokens.

## Extensibility

Activity providers will converge on timestamped snapshots, allowing future providers without changing Twitch API code:

- optional speech VAD;
- raid and Hype Train EventSub state;
- channel point redemption activity;
- scene or source state;
- external HTTP/local WebSocket busy signals;
- manual hotkey hold.

Actions can also be abstracted later, but the initial product intentionally exposes only Twitch's snooze action and never starts an ad itself.
