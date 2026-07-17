# IRCaBot Reborn (v3)

An undemanding IRC chat logger with a JS-free web interface.
Complete rewrite of IRCaBot on Qt6: only the logger and the web UI, nothing else.

## Key points

- **Storage format is fully compatible with v1/v2**: plain text files
  `data/<server>/<channel>/yyyy/MM/dd.txt` with `[nick] message` lines.
  An existing production data folder is picked up as is, no conversion needed.
- **Configuration is plain JSON** (`./ircabot --example config.json` writes
  a documented template). Top-level keys: `data_path`, `web{}` (address, port,
  service name/emoji, `realtime_disabled`), `defaults{}` (nick/user/real_name/
  password for all servers), `triggers{}` (request -> answer) and `servers[]`
  (name, address, port, optional `ssl`, channels, per-server overrides).
  Keys starting with `_` are ignored and can be used as comments.
- **URL scheme is compatible with v1/v2**: old links to
  `/<server>/<channel>/yyyy/MM/dd` (and `.txt`), `/~realtime/...`, `/~images/...`
  keep working.
- **No JavaScript** anywhere in the interface. The only exception is the
  real time reading page (`/~realtime/<server>/<channel>`), which uses a small
  polling script. It can be turned off entirely with `realtime_disabled = true`.
- **Multithreaded asynchronous web server**: QHttpServer, heavy handlers run on
  the global thread pool (`QFuture<QHttpServerResponse>`), the IRC event loop is
  never blocked. Search is bounded by a time budget and a hit limit, so a greedy
  regexp cannot hang the service (the main v1 disease).
- **No worker threads for IRC**, no blocking `waitFor*()` calls: every connection
  is event-driven, reconnects are timer-based.
- No X server or offscreen platform hacks: pure QCoreApplication console binary.

## Features

- Dark and light themes: the default follows the browser
  (`prefers-color-scheme`), the manual choice (sidebar switcher) is stored
  in a persistent cookie server-side - still no JavaScript;
- Mobile friendly: collapsible menu and adaptive log layout, pure CSS;
- Unlimited servers and channels, current online and topic per channel;
- Connection status in real time (green/red dot);
- Whole-history search, plain substring or regular expression;
- Plain text day log: append `.txt` to the day URL;
- Messages starting with a dot are stored as "Blinded message";
- CTCP ACTION (`/me`) is stored as `*** text ***`;
- Customizable triggers: the bot answers when addressed
  (`botnick, webui`), `%CHANNEL_FOR_URL%` is substituted automatically;
- NickServ authorization, busy nickname fallback and automatic recovery;
- Customizable pages: `main_page.txt` (with `%LOCAL_TIME%` and
  `%DAILY_REQUESTS%` placeholders) and a per-server page
  `data/<server>/about_server.txt` - plain HTML, edited on disk, no rebuild
  or restart needed. Custom images go to `data/custom_images/` and are served
  via `/~images/<name>`.

## Build

```bash
sudo apt install qt6-base-dev qt6-httpserver-dev cmake g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The version reported by `--version`, the startup banner and the web footer is
taken from the nearest `v*` git tag (`git describe --tags`), falling back to the
CMake project version when built outside a tagged checkout. Override explicitly
with `-DIRCABOT_VERSION=x.y.z`.

## Run

```bash
# Create a configuration file template:
./build/ircabot --example ./config.json

# Edit config.json, then:
./build/ircabot --config ./config.json
```

## Install

### From a .deb (Debian 13)

Download the package from the [Releases](https://github.com/freeacetone/ircabot/releases)
page (or build it locally with `cd build && cpack -G DEB`) and install:

```bash
sudo apt install ./ircabot_<version>_amd64.deb
```

The package:

- installs the binary to `/usr/bin/ircabot` and a systemd unit to
  `/usr/lib/systemd/system/ircabot.service`;
- creates the `ircabot` system user and the data directory `/srv/ircabot/data`;
- writes a starter config to `/etc/ircabot/config.json` **only if it does not
  already exist** - your configuration is never overwritten on upgrades;
- starts the service on first install and restarts it on upgrade.

Edit `/etc/ircabot/config.json`, then `sudo systemctl restart ircabot`.

### Manual (from source)

```bash
sudo cmake --install build --prefix /usr
ircabot --example /etc/ircabot/config.json   # then edit it
sudo systemctl daemon-reload
sudo systemctl enable --now ircabot.service
```

## Releases

Pushing a `v*` tag runs a GitHub Action that builds the `.deb` under Debian 13
and attaches it to the matching GitHub Release.

## License

GPLv3 (c) acetone, 2021-2026.
Source: https://github.com/freeacetone/ircabot
