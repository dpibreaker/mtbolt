# MTBolt

High-performance MTProto proxy built for scale. Handles 100k+ concurrent clients on a single 16GB box.

Fork of [teleproxy](https://github.com/teleproxy/teleproxy) with security hardening, memory optimizations, and native GeoIP metrics.

## What's different

**Performance**
- `TCP_DEFER_ACCEPT` — kernel drops idle connections before they reach userspace. Cuts socket count by ~75%
- Dynamic connection table sized from `RLIMIT_NOFILE`, not a hardcoded array
- Socket buffer cap at 256KB — the original inflated every socket to 8MB via `maximize_buffers`
- 5-second handshake timeout — closes connections that never complete MTProto negotiation
- jemalloc support — better memory return to OS under high churn
- Buffer limit raised to 6GB (was 256MB)

**Security** (ported from [kavore/teleproxy](https://github.com/kavore/teleproxy))
- Atomic refcount operations — fixes double-free race conditions
- `OPENSSL_cleanse` for key material, `RAND_bytes` instead of `lrand48`
- `CRYPTO_memcmp` for TLS session ID (timing-safe)
- `snprintf` everywhere, integer overflow checks, fd leak fixes
- Build hardening: `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`, PIE, full RELRO

**Native GeoIP metrics**
- Per-IP tracking with city and coordinates from MaxMind GeoLite2-City
- `teleproxy_client{ip, country, city, latitude, longitude}` — every active client on a map
- `teleproxy_country_unique_ips{country, latitude, longitude}` — unique IPs per country
- Replaces external `ss`-parsing exporters with zero-overhead in-process stats

## Build

```bash
# Dependencies
apt install libssl-dev zlib1g-dev libjemalloc-dev libmaxminddb-dev

# Build
make -j$(nproc)

# Binary at objs/bin/teleproxy
```

jemalloc and libmaxminddb are auto-detected via pkg-config. Both optional — works without them.

## Run

```bash
SECRET=$(head -c 16 /dev/urandom | xxd -ps)

./teleproxy \
  -S "$SECRET" \
  -H 443 \
  -p 8888 \
  --http-stats \
  --geoip-db /path/to/GeoLite2-City.mmdb \
  --aes-pwd proxy-secret \
  proxy-multi.conf
```

### Systemd

```ini
[Service]
Type=simple
User=nobody
ExecStart=/opt/teleproxy/teleproxy -S <secret> -H 443 -p 9090 --http-stats --geoip-db /etc/geoip/GeoLite2-City.mmdb --aes-pwd proxy-secret proxy-multi.conf
Restart=on-failure
LimitNOFILE=1048576
AmbientCapabilities=CAP_NET_BIND_SERVICE
```

### Recommended sysctl

```bash
# Fast orphan cleanup
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_orphan_retries = 1

# Socket buffer cap (prevent maximize_buffers bloat)
net.core.rmem_max = 262144
net.core.wmem_max = 262144
net.ipv4.tcp_rmem = 4096 32768 262144
net.ipv4.tcp_wmem = 4096 32768 262144
```

## Metrics

Prometheus endpoint at `http://localhost:<stats_port>/metrics`.

All original teleproxy metrics plus:

| Metric | Type | Description |
|--------|------|-------------|
| `teleproxy_client` | gauge | Active clients with `ip`, `country`, `city`, `latitude`, `longitude` labels |
| `teleproxy_country_unique_ips` | gauge | Unique online IPs per country with coordinates |
| `teleproxy_country_total_ips` | counter | All-time unique IPs per country |
| `teleproxy_unique_ips` | gauge | Total unique IPs in tracking table |

## Based on

- [teleproxy/teleproxy](https://github.com/teleproxy/teleproxy) — original fork with TOML config, DPI resistance, E2E tests
- [kavore/teleproxy](https://github.com/kavore/teleproxy) — security hardening patches
- [TelegramMessenger/MTProxy](https://github.com/TelegramMessenger/MTProxy) — original Telegram MTProto proxy

## License

GPLv2 — see [LICENSE](LICENSE).
