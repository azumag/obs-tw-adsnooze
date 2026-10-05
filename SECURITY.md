# Security Policy

## Prototype warning

The current developer prototype accepts a Twitch access token in an OBS module JSON configuration file. That is not an acceptable credential-storage design for a public release.

Until operating-system credential storage and an in-plugin OAuth flow are implemented:

- use only a development Twitch application and a test account where possible;
- do not commit a populated configuration file;
- do not paste tokens into issues, logs, screenshots, or pull requests;
- revoke the token after testing;
- keep `dry_run` enabled until the decision behavior is verified.

The plugin never needs a Twitch client secret and a public build must not contain one.

## Data minimization

The intended chat implementation stores only bounded in-memory message IDs, chatter IDs, and timestamps needed for activity metrics and deduplication. It does not need message text. Audio is analyzed locally as levels and is not recorded or uploaded.

## Reporting a vulnerability

Please use GitHub's private security advisory feature for this repository rather than opening a public issue. Include affected versions, reproduction steps, and impact. Do not include working Twitch credentials or private stream data.
