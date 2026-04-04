[🇬🇧 English](README.md) | [🇷🇺 Русский](README.ru.md)

# MTBolt

High-performance MTProto proxy for Telegram with native GeoIP metrics, runtime configuration, and security hardening.

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

## Features

- **TCP_DEFER_ACCEPT** — drops zero-byte connections at kernel level, zero overhead
- **Dynamic connection table** — scales to 10M+ concurrent connections
- **jemalloc integration** — tuned memory allocation with background thread GC
- **Native Prometheus metrics** — per-country unique IPs, Russian region breakdown, online/total counters
- **Multi-worker mode** — shared-memory IP stats across worker processes
- **Runtime configuration** — TOML config file + CLI overrides, no recompilation needed
- **Build hardening** — stack protector, FORTIFY_SOURCE, PIE, full RELRO

## Quick Start

```bash
# Debian/Ubuntu dependencies
apt install libssl-dev libjemalloc-dev libmaxminddb-dev

# Build
make

# Run (direct mode)
./objs/bin/mtbolt --direct -S <hex-secret> --http-stats --geoip-db /path/to/GeoLite2-City.mmdb -M 4 -p 443

# Run with TOML config
./objs/bin/mtbolt --config mtbolt.toml --direct
```

## Configuration

All tunable parameters can be set via TOML config file and/or CLI flags.
See [Configuration Reference](docs/SETTINGS.md) for the complete parameter list.

```toml
[workers]
count = 4

[geoip]
database = "/etc/geoip/GeoLite2-City.mmdb"

[stats]
http_enabled = true
```

## Build

| Library | Required | Purpose |
|---------|----------|---------|
| OpenSSL | Yes | TLS, AES crypto |
| jemalloc | No | Memory allocator (recommended) |
| libmaxminddb | No | GeoIP country/region metrics |

```bash
make              # auto-detects optional libraries
make clean        # clean build artifacts
make lint         # run cppcheck static analysis
```

## Deployment

### systemd

```ini
[Unit]
Description=MTBolt MTProto Proxy
After=network.target

[Service]
ExecStart=/opt/mtbolt/mtbolt --config /etc/mtbolt/mtbolt.toml --direct
User=teleproxy
LimitNOFILE=10485760
TasksMax=infinity
Restart=always
RestartSec=5
Environment=MALLOC_CONF=background_thread:true,dirty_decay_ms:5000,muzzy_decay_ms:30000

[Install]
WantedBy=multi-user.target
```

### Recommended sysctl

```bash
net.core.somaxconn = 524288
net.ipv4.tcp_congestion_control = bbr
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_fastopen = 3
fs.file-max = 10485760
fs.nr_open = 10485760
```

## Metrics

Prometheus metrics on the stats port when `--http-stats` is enabled:

| Metric | Description |
|--------|-------------|
| `teleproxy_country_unique_ips` | Unique IPs per country (with lat/lon) |
| `teleproxy_ru_region_unique_ips` | Unique IPs per Russian region (ISO 3166-2:RU) |
| `teleproxy_online_ips` | Currently connected unique IPs |
| `teleproxy_unique_ips` | Total unique IPs seen |

## Security

- Compile-time: `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -Wl,-z,relro,-z,now`
- Atomic refcounts preventing connection race conditions
- OpenSSL key cleansing (`OPENSSL_cleanse`)
- Constant-time secret comparison (`CRYPTO_memcmp`)
- Configurable handshake timeout (default 5s)
- `TCP_DEFER_ACCEPT` drops idle TCP connections at kernel level

## License

[GNU General Public License v2.0](LICENSE)

## Credits

Fork of [teleproxy](https://github.com/teleproxy/teleproxy) with performance, security, and observability enhancements.
