# MTBolt Release Design Spec

## Overview

Transform the `teleproxy-orig` fork into a publishable GitHub project **dpibreaker/mtbolt** with runtime configuration, comprehensive documentation, and proper GitHub presence.

## Scope

4 tasks, all within `teleproxy-orig/`:

1. **Runtime configuration** — move hardcoded constants to TOML + CLI
2. **docs/SETTINGS.md** — full parameter reference (EN + RU)
3. **README.md** — bilingual project README (EN + RU)
4. **GitHub publish** — rename to mtbolt, push to `dpibreaker/mtbolt`

---

## Task 1: Runtime Configuration (TOML + CLI)

### Config file: `mtbolt.toml`

Default search path: next to binary, overridable with `--config <path>`.

Priority: **CLI > TOML > compiled defaults**

### Structure

```toml
[network]
max_connections = 10485760      # connection table size
max_targets = 10485760          # target table size
backlog = 131072                # listen backlog (SO_BACKLOG)
tcp_defer_accept = 3            # seconds, 0 to disable
window_clamp = 131072           # TCP window clamp

[buffers]
max_allocated_bytes = 17179869184  # 16GB, max msg buffer pool
std_buffer = 2048
small_buffer = 512
tiny_buffer = 48
chunk_size = 2097088               # 2MB - 64 bytes
max_connection_buffer = 33554432   # 32MB per connection

[timeouts]
handshake = 5.0                 # incomplete MTProto handshake
rpc = 5.0                       # RPC timeout
ping_interval = 5.0
connect = 3.0                   # upstream DC connect timeout
http_max_wait = 960.0           # HTTP long-poll max wait

[geoip]
enabled = true
database = "/etc/geoip/GeoLite2-City.mmdb"
ht_size = 4194304               # hash table slots for IP tracking
max_regions = 128

[workers]
count = 6                       # -M flag equivalent

[stats]
http_enabled = true
buffer_size = 1048576           # 1MB stats response buffer
```

### Implementation

- New files: `src/mtproto/mtbolt-config.c`, `src/mtproto/mtbolt-config.h`
- Uses existing `src/common/toml-parser.c` for TOML parsing
- Config struct `mtbolt_config_t` with all fields, initialized to compiled defaults
- Load order: defaults → TOML file → CLI overrides
- Validation: bounds checking (e.g., max_connections >= 1, handshake > 0)
- Each constant currently hardcoded in source files reads from the global config struct instead

### CLI backward compatibility

Existing flags preserved:
- `-M N` → `workers.count`
- `-c N` / `-C N` → `network.max_connections` / `network.max_targets`
- `-b N` → `network.backlog`
- `--geoip-db PATH` → `geoip.database`
- `--http-stats` → `stats.http_enabled`

New long-form flags added for all TOML keys:
- `--network.max-connections`, `--buffers.max-allocated-bytes`, etc.
- `--config PATH` — path to TOML config file

### Files to modify

| File | Constant | Config key |
|------|----------|------------|
| `net-connections.h` | MAX_CONNECTIONS, MAX_TARGETS | network.max_connections, network.max_targets |
| `net-msg-buffers.h` | MSG_DEFAULT_MAX_ALLOCATED_BYTES, MSG_STD_BUFFER, MSG_SMALL_BUFFER, MSG_TINY_BUFFER, MSG_BUFFERS_CHUNK_SIZE | buffers.* |
| `server-functions.h` | DEFAULT_BACKLOG | network.backlog |
| `mtproto-proxy.c` | EXT_CONN_TABLE_SIZE, RPC_TIMEOUT_INTERVAL, PING_INTERVAL, CONNECT_TIMEOUT, HTTP_MAX_WAIT_TIMEOUT, DEFAULT_WINDOW_CLAMP, MAX_CONNECTION_BUFFER_SPACE, STATS_BUFF_SIZE | various |
| `net-events.c` | TCP_DEFER_ACCEPT value | network.tcp_defer_accept |
| `net-tcp-rpc-ext-server.c` | handshake timeout (5s) | timeouts.handshake |
| `ip-stats.c` | IP_HT_SIZE, MAX_REGIONS | geoip.ht_size, geoip.max_regions |

### Constraints

- Constants used at compile-time for array sizes (`IP_HT_SIZE` for static arrays) need to become dynamically allocated
- `MSG_BUFFERS_CHUNK_SIZE` used in alignment math — keep as power-of-2 validated
- Thread safety: config is read-only after initialization (load before fork/workers)

---

## Task 2: docs/SETTINGS.md (EN + RU)

### Files
- `docs/SETTINGS.md` — English
- `docs/SETTINGS.ru.md` — Russian

Both have language selector at top: `🇬🇧 English | 🇷🇺 Русский`

### Content

1. **Overview** — config file location, priority (CLI > TOML > defaults)
2. **Parameter reference** — table per section:

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `network.max_connections` | `-c`, `--network.max-connections` | int | 10485760 | Max simultaneous connections |

3. **Example configs**:
   - Minimal (just workers + geoip path)
   - Production (current NL server setup)
   - Low-resource VPS (2GB RAM, 2 cores)
4. **Validation rules** — what values are rejected and why

---

## Task 3: README.md (EN + RU)

### Files
- `README.md` — English (GitHub default)
- `README.ru.md` — Russian

Language selector at top: `🇬🇧 English | 🇷🇺 Русский`

### Structure

1. **Header**: MTBolt name, one-line description, badges (GPLv2, build)
2. **Features**: bullet list — TCP_DEFER_ACCEPT, jemalloc, GeoIP/Prometheus metrics, dynamic connection table, multi-worker, build hardening
3. **Quick Start**: build + run in 3 commands
4. **Configuration**: link to SETTINGS.md, minimal TOML example
5. **Build**: dependencies (OpenSSL, optional jemalloc/libmaxminddb), `make`, flags
6. **Deployment**: systemd unit example, sysctl recommendations, link to server-configs/
7. **Metrics**: Prometheus endpoint, metric names, Grafana setup
8. **Security**: compile-time hardening, geoblock, handshake timeout
9. **License**: GPLv2
10. **Credits**: upstream teleproxy acknowledgment

---

## Task 4: GitHub Publish

### Repository: `dpibreaker/mtbolt`

- Create repo via `gh repo create dpibreaker/mtbolt --public`
- Source: contents of `teleproxy-orig/` (not the parent mtproto/ wrapper)
- Binary rename: `teleproxy` → `mtbolt` in Makefile output path and references

### Files to add/update
- `LICENSE` — GPLv2 full text
- `.gitignore` — compiled objects, editor files, .DS_Store
- `CHANGELOG.md` — initial release notes (v4.7.0 base + our additions)
- Rename binary output in Makefile: `objs/bin/teleproxy` → `objs/bin/mtbolt`
- Update references to "teleproxy" in code comments/strings where it's user-facing

### What NOT to rename
- Internal C identifiers, struct names, function prefixes — too invasive, not worth breaking git blame
- systemd user `teleproxy` — documented in SETTINGS.md as configurable

---

## Non-goals

- No CI/CD pipeline (can add later)
- No Docker support (can add later)
- No web UI for configuration
- No refactoring of existing C code beyond what's needed for config loading
