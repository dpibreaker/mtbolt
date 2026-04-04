[🇬🇧 English](SETTINGS.md) | [🇷🇺 Русский](SETTINGS.ru.md)

# MTBolt Configuration Reference

## Overview

MTBolt supports three layers of configuration with the following priority order (highest to lowest):

1. **CLI flags** — override everything
2. **TOML config file** — set with `--config <path>`
3. **Compiled defaults** — applied at startup if no other value is provided

The TOML config file is loaded once at startup and reloaded on `SIGHUP` (secrets and ACLs sections only). Pass the config path with `--config /path/to/mtbolt.toml`. An annotated example with all parameters and their defaults is provided in `mtbolt.toml.example` at the project root.

---

## Parameter Reference

### Network

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `network.max_connections` | `--max-connections` | int | `10485760` | Maximum simultaneous client connections |
| `network.max_targets` | `--max-targets` | int | `10485760` | Maximum upstream target connections |
| `network.backlog` | `--backlog` | int | `131072` | `listen()` backlog queue size |
| `network.tcp_defer_accept` | `--tcp-defer-accept` | int | `3` | Seconds to wait for first data after TCP handshake; `0` disables `TCP_DEFER_ACCEPT` |
| `network.window_clamp` | `-W` / `--window-clamp` | int | `131072` | TCP receive window clamp for client connections (bytes) |

> Note: `--max-special-connections` (`-C`) is a legacy alias that maps to the same `max_connections` field.

### Buffers

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `buffers.max_allocated_bytes` | `--max-allocated-bytes` | int | `17179869184` (16 GB) | Maximum total memory for the message buffer pool |
| `buffers.max_connection_buffer` | `--max-connection-buffer` | int | `33554432` (32 MB) | Per-connection buffer limit |
| `buffers.std_buffer` | *(compile-time only)* | int | `2048` | Standard buffer size — informational, cannot be changed at runtime |
| `buffers.small_buffer` | *(compile-time only)* | int | `512` | Small buffer size — informational, cannot be changed at runtime |
| `buffers.tiny_buffer` | *(compile-time only)* | int | `48` | Tiny buffer size — informational, cannot be changed at runtime |
| `buffers.chunk_size` | *(compile-time only)* | int | `2097088` (2 MB − 64) | Buffer chunk allocation size — informational, cannot be changed at runtime |

> `std_buffer`, `small_buffer`, `tiny_buffer`, and `chunk_size` are baked in at compile time. The TOML keys are accepted by the parser and stored but do not affect runtime behavior. They are exposed here for reference only.

### Timeouts

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `timeouts.handshake` | `--handshake-timeout` | float | `5.0` | MTProto handshake timeout (seconds) |
| `timeouts.rpc` | `--rpc-timeout` | float | `5.0` | RPC response timeout (seconds) |
| `timeouts.ping_interval` | `-T` / `--ping-interval` | float | `5.0` | Keepalive ping interval for local TCP connections (seconds) |
| `timeouts.connect` | `--connect-timeout` | float | `3.0` | Upstream DC connect timeout (seconds) |
| `timeouts.http_max_wait` | `--http-max-wait` | float | `960.0` | Maximum wait time for HTTP long-poll requests (seconds) |

### GeoIP

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `geoip.enabled` | *(TOML only)* | bool | `true` | Enable GeoIP country/region tracking |
| `geoip.database` | `--geoip-db` | string | `""` | Path to MaxMind `GeoLite2-City.mmdb` or `GeoLite2-Country.mmdb` |
| `geoip.ht_size` | `--geoip-ht-size` | int | `4194304` | Hash table slot count for IP tracking; must be a power of 2 and ≥ 1024 |
| `geoip.max_regions` | `--geoip-max-regions` | int | `128` | Maximum number of tracked sub-country regions (1–1024) |

### Workers

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `workers.count` | `-M` / `--slaves` | int | `0` | Number of slave worker processes to spawn; `0` runs in single-process mode |

> TLS-transport mode (`--domain`) is not recommended with multiple workers because replay protection is weakened when state is not shared between processes.

### Stats

| TOML Key | CLI Flag | Type | Default | Description |
|---|---|---|---|---|
| `stats.http_enabled` | `--http-stats` | bool | `false` | Enable the HTTP stats endpoint |
| `stats.buffer_size` | `--stats-buffer-size` | int | `1048576` (1 MB) | Response buffer size for stats queries (bytes) |

---

## Example Configurations

### Minimal

```toml
# Minimal config — only secrets are required for relay mode.
# All MTBolt parameters use their defaults.
```

### Production (high-traffic)

```toml
[network]
max_connections = 10485760
max_targets = 10485760
backlog = 131072
tcp_defer_accept = 3
window_clamp = 131072

[buffers]
max_allocated_bytes = 17179869184  # 16 GB
max_connection_buffer = 33554432   # 32 MB

[timeouts]
handshake = 5.0
rpc = 5.0
ping_interval = 5.0
connect = 3.0
http_max_wait = 960.0

[geoip]
enabled = true
database = "/etc/geoip/GeoLite2-City.mmdb"
ht_size = 4194304
max_regions = 128

[workers]
count = 4

[stats]
http_enabled = true
buffer_size = 4194304  # 4 MB for large installs
```

### Low-resource VPS (2 GB RAM)

```toml
[network]
max_connections = 65536
max_targets = 65536
backlog = 4096
tcp_defer_accept = 3

[buffers]
max_allocated_bytes = 1073741824   # 1 GB
max_connection_buffer = 4194304    # 4 MB

[geoip]
enabled = false

[workers]
count = 0

[stats]
http_enabled = false
```

---

## Validation Rules

| Parameter | Rule |
|---|---|
| `max_connections` | Must be ≥ 1 |
| `max_targets` | Must be ≥ 1 |
| `backlog` | Must be ≥ 1 |
| `tcp_defer_accept` | Must be ≥ 0 |
| `max_allocated_bytes` | Must be ≥ 1 MB (1048576) |
| `handshake` | Must be > 0 |
| `rpc` | Must be > 0 |
| `ping_interval` | Must be > 0 |
| `connect` | Must be > 0 |
| `geoip.ht_size` | Must be ≥ 1024 and a power of 2 |
| `geoip.max_regions` | Must be in range 1–1024 |
| `stats.buffer_size` | Must be ≥ 4096 |
