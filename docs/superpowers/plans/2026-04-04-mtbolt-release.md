# MTBolt Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform teleproxy-orig into a publishable GitHub project `dpibreaker/mtbolt` with runtime-configurable constants, bilingual docs, and proper GitHub presence.

**Architecture:** Extend the existing `toml_config` struct with new fields for all hardcoded constants. Add a config layer (`mtbolt-config.c`) that loads defaults → TOML → CLI overrides into a global struct. Replace `#define` constants with reads from this struct. Rename binary output to `mtbolt`.

**Tech Stack:** C11, existing tomlc17 TOML parser, Make, gh CLI

**Spec:** `docs/superpowers/specs/2026-04-04-mtbolt-release-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `src/mtproto/mtbolt-config.h` | Config struct definition, defaults, accessor macros |
| Create | `src/mtproto/mtbolt-config.c` | TOML loading, CLI override application, validation |
| Modify | `src/mtproto/mtproto-proxy.c` | Use config struct instead of `#define` constants, add new CLI flags |
| Modify | `src/net/net-connections.h` | Replace `MAX_CONNECTIONS`/`MAX_TARGETS` with extern vars |
| Modify | `src/net/net-msg-buffers.h` | Replace `MSG_DEFAULT_MAX_ALLOCATED_BYTES` with extern var |
| Modify | `src/common/server-functions.h` | Replace `DEFAULT_BACKLOG` with extern var |
| Modify | `src/net/net-events.c` | Read `tcp_defer_accept` from config |
| Modify | `src/net/net-tcp-rpc-ext-server.c` | Read handshake timeout from config |
| Modify | `src/mtproto/ip-stats.c` | Dynamic allocation using config `ht_size`/`max_regions` |
| Modify | `Makefile` | Rename binary to `mtbolt`, add `mtbolt-config.o` |
| Create | `README.md` | English project README (replaces existing) |
| Create | `README.ru.md` | Russian project README |
| Create | `docs/SETTINGS.md` | English parameter reference |
| Create | `docs/SETTINGS.ru.md` | Russian parameter reference |
| Create | `LICENSE` | GPLv2 full text |
| Modify | `.gitignore` | Add editor files, .DS_Store, objs/ |
| Create | `mtbolt.toml.example` | Example config with all defaults documented |

---

### Task 1: Create mtbolt-config.h — config struct and defaults

**Files:**
- Create: `src/mtproto/mtbolt-config.h`

- [ ] **Step 1: Create the config header with struct and defaults**

```c
/* src/mtproto/mtbolt-config.h — MTBolt runtime configuration */
#ifndef MTBOLT_CONFIG_H
#define MTBOLT_CONFIG_H

#include <stdint.h>

struct mtbolt_config {
  /* [network] */
  int max_connections;          /* default: 10485760 */
  int max_targets;              /* default: 10485760 */
  int backlog;                  /* default: 131072 */
  int tcp_defer_accept;         /* default: 3 (seconds, 0=disable) */
  int window_clamp;             /* default: 131072 */

  /* [buffers] */
  long long max_allocated_bytes; /* default: 16GB */
  int std_buffer;               /* default: 2048 */
  int small_buffer;             /* default: 512 */
  int tiny_buffer;              /* default: 48 */
  long chunk_size;              /* default: (1L<<21)-64 */
  int max_connection_buffer;    /* default: 1<<25 (32MB) */

  /* [timeouts] */
  double handshake;             /* default: 5.0 */
  double rpc;                   /* default: 5.0 */
  double ping_interval;         /* default: 5.0 */
  double connect;               /* default: 3.0 */
  double http_max_wait;         /* default: 960.0 */

  /* [geoip] */
  int geoip_enabled;            /* default: 1 */
  char geoip_database[512];     /* default: "" */
  int geoip_ht_size;            /* default: 1<<22 (4M) */
  int geoip_max_regions;        /* default: 128 */

  /* [workers] */
  int workers;                  /* default: 0 (single process) */

  /* [stats] */
  int stats_http_enabled;       /* default: 0 */
  int stats_buffer_size;        /* default: 1<<20 (1MB) */
};

/* Global config instance — read-only after init */
extern struct mtbolt_config mtbolt_cfg;

/* Initialize config with compiled defaults */
void mtbolt_config_defaults (struct mtbolt_config *cfg);

/* Load TOML overrides. Returns 0 on success, -1 on error (msg in errbuf) */
int mtbolt_config_load_toml (struct mtbolt_config *cfg, const char *path,
                             char *errbuf, int errlen);

/* Validate config values. Returns 0 on success, -1 on error (msg in errbuf) */
int mtbolt_config_validate (struct mtbolt_config *cfg, char *errbuf, int errlen);

#endif /* MTBOLT_CONFIG_H */
```

- [ ] **Step 2: Commit**

```bash
cd /Users/Fatyzzz/PycharmProjects/mtproto/teleproxy-orig
git add src/mtproto/mtbolt-config.h
git commit -m "feat: add mtbolt-config.h with runtime config struct and defaults"
```

---

### Task 2: Create mtbolt-config.c — defaults, TOML loading, validation

**Files:**
- Create: `src/mtproto/mtbolt-config.c`

- [ ] **Step 1: Create the config implementation**

```c
/* src/mtproto/mtbolt-config.c — MTBolt runtime configuration */
#include "mtbolt-config.h"
#include "common/toml/tomlc17.h"
#include "common/kprintf.h"

#include <string.h>
#include <stdio.h>
#include <errno.h>

struct mtbolt_config mtbolt_cfg;

void mtbolt_config_defaults (struct mtbolt_config *cfg) {
  memset (cfg, 0, sizeof (*cfg));

  /* [network] */
  cfg->max_connections = 10485760;
  cfg->max_targets = 10485760;
  cfg->backlog = 131072;
  cfg->tcp_defer_accept = 3;
  cfg->window_clamp = 131072;

  /* [buffers] */
  cfg->max_allocated_bytes = 16LL * 1024 * 1024 * 1024;
  cfg->std_buffer = 2048;
  cfg->small_buffer = 512;
  cfg->tiny_buffer = 48;
  cfg->chunk_size = (1L << 21) - 64;
  cfg->max_connection_buffer = 1 << 25;

  /* [timeouts] */
  cfg->handshake = 5.0;
  cfg->rpc = 5.0;
  cfg->ping_interval = 5.0;
  cfg->connect = 3.0;
  cfg->http_max_wait = 960.0;

  /* [geoip] */
  cfg->geoip_enabled = 1;
  cfg->geoip_database[0] = '\0';
  cfg->geoip_ht_size = 1 << 22;
  cfg->geoip_max_regions = 128;

  /* [workers] */
  cfg->workers = 0;

  /* [stats] */
  cfg->stats_http_enabled = 0;
  cfg->stats_buffer_size = 1 << 20;
}

/* Helper: read int from TOML table, set if present */
static int toml_read_int (toml_table_t *tbl, const char *key, int *out) {
  toml_value_t v = toml_table_int (tbl, key);
  if (v.ok) { *out = (int)v.u.i; return 1; }
  return 0;
}

static int toml_read_int64 (toml_table_t *tbl, const char *key, long long *out) {
  toml_value_t v = toml_table_int (tbl, key);
  if (v.ok) { *out = v.u.i; return 1; }
  return 0;
}

static int toml_read_double (toml_table_t *tbl, const char *key, double *out) {
  toml_value_t v = toml_table_double (tbl, key);
  if (v.ok) { *out = v.u.d; return 1; }
  return 0;
}

static int toml_read_bool (toml_table_t *tbl, const char *key, int *out) {
  toml_value_t v = toml_table_bool (tbl, key);
  if (v.ok) { *out = v.u.b ? 1 : 0; return 1; }
  return 0;
}

static int toml_read_string (toml_table_t *tbl, const char *key, char *out, int maxlen) {
  toml_value_t v = toml_table_string (tbl, key);
  if (v.ok) {
    snprintf (out, maxlen, "%s", v.u.s);
    free (v.u.s);
    return 1;
  }
  return 0;
}

int mtbolt_config_load_toml (struct mtbolt_config *cfg, const char *path,
                             char *errbuf, int errlen) {
  FILE *fp = fopen (path, "r");
  if (!fp) {
    snprintf (errbuf, errlen, "cannot open %s: %s", path, strerror (errno));
    return -1;
  }

  toml_table_t *root = toml_parse_file (fp, errbuf, errlen);
  fclose (fp);
  if (!root) {
    return -1;
  }

  toml_table_t *t;

  /* [network] */
  if ((t = toml_table_table (root, "network"))) {
    toml_read_int (t, "max_connections", &cfg->max_connections);
    toml_read_int (t, "max_targets", &cfg->max_targets);
    toml_read_int (t, "backlog", &cfg->backlog);
    toml_read_int (t, "tcp_defer_accept", &cfg->tcp_defer_accept);
    toml_read_int (t, "window_clamp", &cfg->window_clamp);
  }

  /* [buffers] */
  if ((t = toml_table_table (root, "buffers"))) {
    toml_read_int64 (t, "max_allocated_bytes", &cfg->max_allocated_bytes);
    toml_read_int (t, "std_buffer", &cfg->std_buffer);
    toml_read_int (t, "small_buffer", &cfg->small_buffer);
    toml_read_int (t, "tiny_buffer", &cfg->tiny_buffer);
    long long cs;
    if (toml_read_int64 (t, "chunk_size", &cs)) {
      cfg->chunk_size = (long)cs;
    }
    toml_read_int (t, "max_connection_buffer", &cfg->max_connection_buffer);
  }

  /* [timeouts] */
  if ((t = toml_table_table (root, "timeouts"))) {
    toml_read_double (t, "handshake", &cfg->handshake);
    toml_read_double (t, "rpc", &cfg->rpc);
    toml_read_double (t, "ping_interval", &cfg->ping_interval);
    toml_read_double (t, "connect", &cfg->connect);
    toml_read_double (t, "http_max_wait", &cfg->http_max_wait);
  }

  /* [geoip] */
  if ((t = toml_table_table (root, "geoip"))) {
    toml_read_bool (t, "enabled", &cfg->geoip_enabled);
    toml_read_string (t, "database", cfg->geoip_database, sizeof (cfg->geoip_database));
    toml_read_int (t, "ht_size", &cfg->geoip_ht_size);
    toml_read_int (t, "max_regions", &cfg->geoip_max_regions);
  }

  /* [workers] */
  if ((t = toml_table_table (root, "workers"))) {
    toml_read_int (t, "count", &cfg->workers);
  }

  /* [stats] */
  if ((t = toml_table_table (root, "stats"))) {
    toml_read_bool (t, "http_enabled", &cfg->stats_http_enabled);
    toml_read_int (t, "buffer_size", &cfg->stats_buffer_size);
  }

  toml_free (root);
  return 0;
}

int mtbolt_config_validate (struct mtbolt_config *cfg, char *errbuf, int errlen) {
  if (cfg->max_connections < 1) {
    snprintf (errbuf, errlen, "network.max_connections must be >= 1");
    return -1;
  }
  if (cfg->max_targets < 1) {
    snprintf (errbuf, errlen, "network.max_targets must be >= 1");
    return -1;
  }
  if (cfg->backlog < 1) {
    snprintf (errbuf, errlen, "network.backlog must be >= 1");
    return -1;
  }
  if (cfg->tcp_defer_accept < 0) {
    snprintf (errbuf, errlen, "network.tcp_defer_accept must be >= 0");
    return -1;
  }
  if (cfg->max_allocated_bytes < 1024 * 1024) {
    snprintf (errbuf, errlen, "buffers.max_allocated_bytes must be >= 1MB");
    return -1;
  }
  if (cfg->handshake <= 0) {
    snprintf (errbuf, errlen, "timeouts.handshake must be > 0");
    return -1;
  }
  if (cfg->rpc <= 0) {
    snprintf (errbuf, errlen, "timeouts.rpc must be > 0");
    return -1;
  }
  if (cfg->ping_interval <= 0) {
    snprintf (errbuf, errlen, "timeouts.ping_interval must be > 0");
    return -1;
  }
  if (cfg->connect <= 0) {
    snprintf (errbuf, errlen, "timeouts.connect must be > 0");
    return -1;
  }
  if (cfg->geoip_ht_size < 1024) {
    snprintf (errbuf, errlen, "geoip.ht_size must be >= 1024");
    return -1;
  }
  /* ht_size must be power of 2 */
  if (cfg->geoip_ht_size & (cfg->geoip_ht_size - 1)) {
    snprintf (errbuf, errlen, "geoip.ht_size must be a power of 2");
    return -1;
  }
  if (cfg->geoip_max_regions < 1 || cfg->geoip_max_regions > 1024) {
    snprintf (errbuf, errlen, "geoip.max_regions must be 1..1024");
    return -1;
  }
  if (cfg->stats_buffer_size < 4096) {
    snprintf (errbuf, errlen, "stats.buffer_size must be >= 4096");
    return -1;
  }
  return 0;
}
```

- [ ] **Step 2: Commit**

```bash
git add src/mtproto/mtbolt-config.c
git commit -m "feat: add mtbolt-config.c with TOML loading, defaults, validation"
```

---

### Task 3: Wire config into mtproto-proxy.c — init, CLI overrides, replace constants

**Files:**
- Modify: `src/mtproto/mtproto-proxy.c`

- [ ] **Step 1: Add include and init config in mtfront_pre_init**

At the top of `mtproto-proxy.c`, add include:
```c
#include "mtbolt-config.h"
```

In `mtfront_pre_init()` (line ~3168), add config initialization **before** the existing TOML load:
```c
void mtfront_pre_init (void) {
  /* Initialize mtbolt config with defaults */
  mtbolt_config_defaults (&mtbolt_cfg);

  /* Load mtbolt runtime config from TOML if --config was given */
  if (toml_config_path) {
    char errbuf[512];
    if (mtbolt_config_load_toml (&mtbolt_cfg, toml_config_path, errbuf, sizeof (errbuf)) < 0) {
      kprintf ("mtbolt config error: %s\n", errbuf);
      exit (1);
    }
    if (mtbolt_config_validate (&mtbolt_cfg, errbuf, sizeof (errbuf)) < 0) {
      kprintf ("mtbolt config validation error: %s\n", errbuf);
      exit (1);
    }
  }
  // ... existing code continues
```

- [ ] **Step 2: Replace hardcoded constants with config reads**

Replace `#define` usages throughout `mtproto-proxy.c`:

```c
/* Replace these #define constants at the top: */

/* OLD: #define RPC_TIMEOUT_INTERVAL 5.0 */
/* NEW: use mtbolt_cfg.rpc */

/* OLD: #define HTTP_MAX_WAIT_TIMEOUT 960.0 */
/* NEW: use mtbolt_cfg.http_max_wait */

/* OLD: #define PING_INTERVAL 5.0 */
/* NEW: use mtbolt_cfg.ping_interval — also remove static double ping_interval */

/* OLD: #define CONNECT_TIMEOUT 3 */
/* NEW: use (int)mtbolt_cfg.connect */

/* OLD: #define RESPONSE_FAIL_TIMEOUT 5 */
/* NEW: use (int)mtbolt_cfg.rpc */

/* OLD: #define DEFAULT_WINDOW_CLAMP 131072 */
/* NEW: use mtbolt_cfg.window_clamp */

/* OLD: #define MAX_CONNECTION_BUFFER_SPACE (1 << 25) */
/* NEW: use mtbolt_cfg.max_connection_buffer */

/* OLD: #define STATS_BUFF_SIZE (1 << 20) */
/* NEW: dynamically allocate stats_buff using mtbolt_cfg.stats_buffer_size */

/* OLD: static double ping_interval = PING_INTERVAL; */
/* REMOVE: use mtbolt_cfg.ping_interval directly */

/* STOP_INTERVAL and FAIL_INTERVAL are derived: */
#define STOP_INTERVAL (2 * mtbolt_cfg.ping_interval)
#define FAIL_INTERVAL (20 * mtbolt_cfg.ping_interval)
```

For `stats_buff`, change from static array to dynamic:
```c
/* OLD: */
char stats_buff[STATS_BUFF_SIZE];

/* NEW: */
char *stats_buff;
int stats_buff_size;

/* In mtfront_pre_init, after config load: */
stats_buff_size = mtbolt_cfg.stats_buffer_size;
stats_buff = malloc (stats_buff_size);
assert (stats_buff);
```

- [ ] **Step 3: Add new CLI flags for mtbolt config overrides**

In `mtfront_prepare_parse_options()`, add new options (use codes 2100+):
```c
  /* mtbolt runtime config overrides */
  parse_option ("max-connections", required_argument, 0, 2100, "max simultaneous connections (default 10485760)");
  parse_option ("max-targets", required_argument, 0, 2101, "max target connections (default 10485760)");
  parse_option ("backlog", required_argument, 0, 2102, "listen backlog (default 131072)");
  parse_option ("tcp-defer-accept", required_argument, 0, 2103, "TCP_DEFER_ACCEPT seconds (default 3, 0=disable)");
  parse_option ("max-allocated-bytes", required_argument, 0, 2104, "max buffer memory in bytes (default 16GB)");
  parse_option ("handshake-timeout", required_argument, 0, 2105, "MTProto handshake timeout seconds (default 5.0)");
  parse_option ("rpc-timeout", required_argument, 0, 2106, "RPC timeout seconds (default 5.0)");
  parse_option ("connect-timeout", required_argument, 0, 2107, "upstream connect timeout seconds (default 3.0)");
  parse_option ("http-max-wait", required_argument, 0, 2108, "HTTP long-poll max wait seconds (default 960.0)");
  parse_option ("geoip-ht-size", required_argument, 0, 2109, "GeoIP hash table slots, power of 2 (default 4194304)");
  parse_option ("geoip-max-regions", required_argument, 0, 2110, "max tracked regions (default 128)");
  parse_option ("stats-buffer-size", required_argument, 0, 2111, "stats response buffer bytes (default 1048576)");
  parse_option ("max-connection-buffer", required_argument, 0, 2112, "per-connection buffer limit bytes (default 33554432)");
```

In `f_parse_option()`, add cases:
```c
  case 2100:
    mtbolt_cfg.max_connections = atoi (optarg);
    break;
  case 2101:
    mtbolt_cfg.max_targets = atoi (optarg);
    break;
  case 2102:
    mtbolt_cfg.backlog = atoi (optarg);
    break;
  case 2103:
    mtbolt_cfg.tcp_defer_accept = atoi (optarg);
    break;
  case 2104:
    mtbolt_cfg.max_allocated_bytes = atoll (optarg);
    break;
  case 2105:
    mtbolt_cfg.handshake = atof (optarg);
    break;
  case 2106:
    mtbolt_cfg.rpc = atof (optarg);
    break;
  case 2107:
    mtbolt_cfg.connect = atof (optarg);
    break;
  case 2108:
    mtbolt_cfg.http_max_wait = atof (optarg);
    break;
  case 2109:
    mtbolt_cfg.geoip_ht_size = atoi (optarg);
    break;
  case 2110:
    mtbolt_cfg.geoip_max_regions = atoi (optarg);
    break;
  case 2111:
    mtbolt_cfg.stats_buffer_size = atoi (optarg);
    break;
  case 2112:
    mtbolt_cfg.max_connection_buffer = atoi (optarg);
    break;
```

Also wire existing flags to config struct:
```c
  /* In case 'M': */
  case 'M':
    workers = atoi (optarg);
    mtbolt_cfg.workers = workers;  /* sync */
    break;

  /* In case 'W': */
  case 'W':
    window_clamp = atoi (optarg);
    mtbolt_cfg.window_clamp = window_clamp;  /* sync */
    break;

  /* In case 'T': */
  case 'T':
    mtbolt_cfg.ping_interval = atof (optarg);
    break;

  /* In case 2000 (http-stats): */
  case 2000:
    mtbolt_cfg.stats_http_enabled = 1;
    http_stats = 1;  /* keep existing var in sync */
    break;

  /* In case 2007 (geoip-db): */
  case 2007:
    geoip_db_path = strdup (optarg);
    snprintf (mtbolt_cfg.geoip_database, sizeof (mtbolt_cfg.geoip_database), "%s", optarg);
    break;
```

- [ ] **Step 4: Commit**

```bash
git add src/mtproto/mtproto-proxy.c
git commit -m "feat: wire mtbolt config into mtproto-proxy.c, add CLI overrides"
```

---

### Task 4: Replace hardcoded constants in header files

**Files:**
- Modify: `src/net/net-connections.h`
- Modify: `src/net/net-msg-buffers.h`
- Modify: `src/common/server-functions.h`

- [ ] **Step 1: net-connections.h — make MAX_CONNECTIONS/MAX_TARGETS extern**

```c
/* OLD: */
#define MAX_CONNECTIONS	10485760
#define MAX_TARGETS	10485760

/* NEW: */
#include "mtproto/mtbolt-config.h"
#define MAX_CONNECTIONS	(mtbolt_cfg.max_connections)
#define MAX_TARGETS	(mtbolt_cfg.max_targets)
```

- [ ] **Step 2: net-msg-buffers.h — make buffer constants configurable**

```c
/* OLD: */
#define MSG_DEFAULT_MAX_ALLOCATED_BYTES	(16L * 1024 * 1024 * 1024)

/* NEW: */
#include "mtproto/mtbolt-config.h"
#define MSG_DEFAULT_MAX_ALLOCATED_BYTES	(mtbolt_cfg.max_allocated_bytes)
```

Note: `MSG_STD_BUFFER`, `MSG_SMALL_BUFFER`, `MSG_TINY_BUFFER`, `MSG_BUFFERS_CHUNK_SIZE` are used for struct sizing and alignment. Keep them as compile-time constants — only `MSG_DEFAULT_MAX_ALLOCATED_BYTES` needs to be runtime-configurable (it's a soft limit checked at allocation time, not used for array dimensions).

- [ ] **Step 3: server-functions.h — make DEFAULT_BACKLOG configurable**

```c
/* OLD: */
#define DEFAULT_BACKLOG 131072

/* NEW: */
#include "mtproto/mtbolt-config.h"
#define DEFAULT_BACKLOG (mtbolt_cfg.backlog)
```

- [ ] **Step 4: Verify compilation**

```bash
cd /Users/Fatyzzz/PycharmProjects/mtproto/teleproxy-orig
make clean && make 2>&1 | tail -20
```
Expected: clean compile with no errors.

- [ ] **Step 5: Commit**

```bash
git add src/net/net-connections.h src/net/net-msg-buffers.h src/common/server-functions.h
git commit -m "feat: replace hardcoded limits in headers with mtbolt config reads"
```

---

### Task 5: Replace hardcoded constants in .c files

**Files:**
- Modify: `src/net/net-events.c`
- Modify: `src/net/net-tcp-rpc-ext-server.c`
- Modify: `src/mtproto/ip-stats.c`

- [ ] **Step 1: net-events.c — read tcp_defer_accept from config**

Find the TCP_DEFER_ACCEPT setsockopt call (~line 635):
```c
/* OLD: */
int defer = 3;
setsockopt (socket_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer, sizeof (defer));

/* NEW: */
#include "mtproto/mtbolt-config.h"
if (mtbolt_cfg.tcp_defer_accept > 0) {
  int defer = mtbolt_cfg.tcp_defer_accept;
  setsockopt (socket_fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &defer, sizeof (defer));
}
```

- [ ] **Step 2: net-tcp-rpc-ext-server.c — read handshake timeout from config**

Find the `job_timer_insert` call (~line 2045):
```c
/* OLD: */
job_timer_insert (C, precise_now + 5);

/* NEW: */
#include "mtproto/mtbolt-config.h"
job_timer_insert (C, precise_now + mtbolt_cfg.handshake);
```

- [ ] **Step 3: ip-stats.c — use config for ht_size and max_regions**

Replace the `#define` constants and make the hash table dynamically allocated:

```c
/* OLD: */
#define IP_HT_SIZE       (1 << 22)
#define IP_HT_MASK       (IP_HT_SIZE - 1)
#define MAX_REGIONS 128

struct ip_stats_shared {
  int used;
  struct ip_ht_slot ht[IP_HT_SIZE];
};

struct region_stats_shared {
  int count;
  struct region_stats_entry entries[MAX_REGIONS];
};

/* NEW: */
#include "mtbolt-config.h"

#define IP_HT_SIZE       (mtbolt_cfg.geoip_ht_size)
#define IP_HT_MASK       (IP_HT_SIZE - 1)
#define MAX_REGIONS      (mtbolt_cfg.geoip_max_regions)

/* These structs use flexible array members now */
struct ip_stats_shared {
  int used;
  int ht_size;
  struct ip_ht_slot ht[];
};

struct region_stats_shared {
  int count;
  int max_regions;
  struct region_stats_entry entries[];
};
```

Update `ip_stats_init()` to use `mmap` with dynamic sizes:
```c
void ip_stats_init (void) {
  int ht_size = mtbolt_cfg.geoip_ht_size;
  size_t shm_size = sizeof (struct ip_stats_shared) + ht_size * sizeof (struct ip_ht_slot);
  /* existing mmap code, but with shm_size instead of sizeof(struct ip_stats_shared) */
  ip_stats_shm = mmap (NULL, shm_size, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  assert (ip_stats_shm != MAP_FAILED);
  ip_stats_shm->ht_size = ht_size;

  int max_regions = mtbolt_cfg.geoip_max_regions;
  size_t region_size = sizeof (struct region_stats_shared) + max_regions * sizeof (struct region_stats_entry);
  region_stats_shm = mmap (NULL, region_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  assert (region_stats_shm != MAP_FAILED);
  region_stats_shm->max_regions = max_regions;
}
```

Update all `IP_HT_SIZE` and `MAX_REGIONS` references in the file to use `ip_stats_shm->ht_size` and `region_stats_shm->max_regions` respectively (for accesses after init). The macros serve as pre-init defaults via the config.

- [ ] **Step 4: Verify compilation**

```bash
make clean && make 2>&1 | tail -20
```
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/net/net-events.c src/net/net-tcp-rpc-ext-server.c src/mtproto/ip-stats.c
git commit -m "feat: replace hardcoded constants in net-events, tcp-rpc, ip-stats with config"
```

---

### Task 6: Update Makefile — rename binary, add mtbolt-config.o

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Add mtbolt-config.o to build and rename binary**

```makefile
# Change binary name:
# OLD:
EXELIST := ${EXE}/teleproxy

# NEW:
EXELIST := ${EXE}/mtbolt

# Change build target:
# OLD:
${EXE}/teleproxy: ${OBJ}/src/mtproto/mtproto-proxy.o ...

# NEW:
${EXE}/mtbolt: ${OBJ}/src/mtproto/mtproto-proxy.o ${OBJ}/src/mtproto/mtbolt-config.o ...
```

Add `${OBJ}/src/mtproto/mtbolt-config.o` to the object list for the mtbolt target (alongside the existing mtproto-proxy.o).

- [ ] **Step 2: Verify build**

```bash
make clean && make 2>&1 | tail -5
ls -la objs/bin/mtbolt
```
Expected: binary at `objs/bin/mtbolt`.

- [ ] **Step 3: Commit**

```bash
git add Makefile
git commit -m "build: rename binary to mtbolt, add mtbolt-config.o"
```

---

### Task 7: Create mtbolt.toml.example

**Files:**
- Create: `mtbolt.toml.example`

- [ ] **Step 1: Write example config with all parameters documented**

```toml
# MTBolt Configuration
# All values shown are defaults. Uncomment and modify as needed.
# CLI flags override TOML values. TOML overrides compiled defaults.

[network]
# max_connections = 10485760     # Max simultaneous client connections
# max_targets = 10485760         # Max upstream target connections
# backlog = 131072               # listen() backlog queue size
# tcp_defer_accept = 3           # Seconds to wait for data after TCP handshake (0 = disable)
# window_clamp = 131072          # TCP window clamp for client connections

[buffers]
# max_allocated_bytes = 17179869184  # 16GB — max memory for message buffer pool
# std_buffer = 2048                  # Standard buffer size (compile-time, informational)
# small_buffer = 512                 # Small buffer size (compile-time, informational)
# tiny_buffer = 48                   # Tiny buffer size (compile-time, informational)
# chunk_size = 2097088               # Buffer chunk allocation size (2MB - 64)
# max_connection_buffer = 33554432   # 32MB — per-connection buffer limit

[timeouts]
# handshake = 5.0        # MTProto handshake timeout (seconds)
# rpc = 5.0              # RPC response timeout (seconds)
# ping_interval = 5.0    # Keepalive ping interval (seconds)
# connect = 3.0          # Upstream DC connect timeout (seconds)
# http_max_wait = 960.0  # HTTP long-poll max wait (seconds)

[geoip]
# enabled = true
# database = "/etc/geoip/GeoLite2-City.mmdb"
# ht_size = 4194304      # Hash table slots for IP tracking (must be power of 2)
# max_regions = 128      # Max tracked sub-country regions

[workers]
# count = 0              # Worker processes (0 = single process, -M flag equivalent)

[stats]
# http_enabled = false   # Enable HTTP stats endpoint (--http-stats equivalent)
# buffer_size = 1048576  # 1MB — stats response buffer size
```

- [ ] **Step 2: Commit**

```bash
git add mtbolt.toml.example
git commit -m "docs: add mtbolt.toml.example with all configurable parameters"
```

---

### Task 8: Create docs/SETTINGS.md (English)

**Files:**
- Create: `docs/SETTINGS.md`

- [ ] **Step 1: Write the English settings reference**

```markdown
[🇬🇧 English](SETTINGS.md) | [🇷🇺 Русский](SETTINGS.ru.md)

# MTBolt Configuration Reference

## Overview

MTBolt loads configuration in this priority order:

1. **CLI flags** (highest priority)
2. **TOML config file** (`--config path/to/mtbolt.toml`)
3. **Compiled defaults** (lowest priority)

## Configuration File

Default: `mtbolt.toml` in the working directory, or specify with `--config`.

See [`mtbolt.toml.example`](../mtbolt.toml.example) for a complete template.

---

## Parameter Reference

### Network

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `network.max_connections` | `--max-connections`, `-c` | int | 10485760 | Maximum simultaneous client connections |
| `network.max_targets` | `--max-targets`, `-C` | int | 10485760 | Maximum upstream target connections |
| `network.backlog` | `--backlog` | int | 131072 | `listen()` backlog queue size |
| `network.tcp_defer_accept` | `--tcp-defer-accept` | int | 3 | Seconds to wait for data after TCP handshake. Drops zero-byte connections at kernel level. Set to 0 to disable |
| `network.window_clamp` | `--window-clamp`, `-W` | int | 131072 | TCP window clamp for client connections |

### Buffers

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `buffers.max_allocated_bytes` | `--max-allocated-bytes` | int64 | 17179869184 (16GB) | Maximum memory for message buffer pool |
| `buffers.max_connection_buffer` | `--max-connection-buffer` | int | 33554432 (32MB) | Per-connection buffer limit |

> **Note:** `std_buffer`, `small_buffer`, `tiny_buffer`, and `chunk_size` are compile-time constants shown in the example config for reference. They cannot be changed at runtime.

### Timeouts

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `timeouts.handshake` | `--handshake-timeout` | float | 5.0 | Seconds to complete MTProto handshake before disconnect |
| `timeouts.rpc` | `--rpc-timeout` | float | 5.0 | RPC response timeout in seconds |
| `timeouts.ping_interval` | `--ping-interval`, `-T` | float | 5.0 | Keepalive ping interval in seconds |
| `timeouts.connect` | `--connect-timeout` | float | 3.0 | Upstream DC connect timeout in seconds |
| `timeouts.http_max_wait` | `--http-max-wait` | float | 960.0 | HTTP long-poll maximum wait in seconds |

### GeoIP

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `geoip.enabled` | — | bool | true | Enable GeoIP tracking |
| `geoip.database` | `--geoip-db` | string | "" | Path to MaxMind GeoLite2-City.mmdb |
| `geoip.ht_size` | `--geoip-ht-size` | int | 4194304 | Hash table slots for IP tracking. Must be a power of 2 |
| `geoip.max_regions` | `--geoip-max-regions` | int | 128 | Maximum tracked sub-country regions |

### Workers

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `workers.count` | `-M`, `--slaves` | int | 0 | Worker processes. 0 = single process mode |

### Stats

| TOML Key | CLI Flag | Type | Default | Description |
|----------|----------|------|---------|-------------|
| `stats.http_enabled` | `--http-stats` | bool | false | Enable HTTP stats/metrics endpoint |
| `stats.buffer_size` | `--stats-buffer-size` | int | 1048576 (1MB) | Stats response buffer size |

---

## Example Configurations

### Minimal

```toml
[workers]
count = 2

[geoip]
database = "/etc/geoip/GeoLite2-City.mmdb"
```

### Production (high-traffic server)

```toml
[network]
max_connections = 10485760
max_targets = 10485760
backlog = 131072
tcp_defer_accept = 3

[buffers]
max_allocated_bytes = 17179869184

[timeouts]
handshake = 5.0
rpc = 5.0
ping_interval = 5.0
connect = 3.0

[geoip]
database = "/etc/geoip/GeoLite2-City.mmdb"
ht_size = 4194304
max_regions = 128

[workers]
count = 6

[stats]
http_enabled = true
```

### Low-resource VPS (2GB RAM, 2 cores)

```toml
[network]
max_connections = 65536
max_targets = 65536
backlog = 4096

[buffers]
max_allocated_bytes = 1073741824  # 1GB

[geoip]
ht_size = 65536

[workers]
count = 2

[stats]
http_enabled = true
```

---

## Validation Rules

| Parameter | Constraint |
|-----------|-----------|
| `network.max_connections` | >= 1 |
| `network.max_targets` | >= 1 |
| `network.backlog` | >= 1 |
| `network.tcp_defer_accept` | >= 0 |
| `buffers.max_allocated_bytes` | >= 1MB |
| `timeouts.handshake` | > 0 |
| `timeouts.rpc` | > 0 |
| `timeouts.ping_interval` | > 0 |
| `timeouts.connect` | > 0 |
| `geoip.ht_size` | >= 1024, must be power of 2 |
| `geoip.max_regions` | 1–1024 |
| `stats.buffer_size` | >= 4096 |
```

- [ ] **Step 2: Commit**

```bash
git add docs/SETTINGS.md
git commit -m "docs: add SETTINGS.md — English configuration reference"
```

---

### Task 9: Create docs/SETTINGS.ru.md (Russian)

**Files:**
- Create: `docs/SETTINGS.ru.md`

- [ ] **Step 1: Write the Russian settings reference**

Same structure as `docs/SETTINGS.md` but fully translated to Russian. Language selector at top:

```markdown
[🇬🇧 English](SETTINGS.md) | [🇷🇺 Русский](SETTINGS.ru.md)

# MTBolt — Справочник по конфигурации

## Обзор

MTBolt загружает конфигурацию в следующем порядке приоритета:

1. **Флаги CLI** (высший приоритет)
2. **TOML-файл конфигурации** (`--config path/to/mtbolt.toml`)
3. **Значения по умолчанию** (низший приоритет)
```

Tables, examples, and validation rules — same data, Russian descriptions. All TOML keys and CLI flags stay in English (they are code identifiers).

- [ ] **Step 2: Commit**

```bash
git add docs/SETTINGS.ru.md
git commit -m "docs: add SETTINGS.ru.md — Russian configuration reference"
```

---

### Task 10: Create README.md (English)

**Files:**
- Create: `README.md` (replace existing)

- [ ] **Step 1: Write the English README**

```markdown
[🇬🇧 English](README.md) | [🇷🇺 Русский](README.ru.md)

# MTBolt

High-performance MTProto proxy for Telegram with native GeoIP metrics, runtime configuration, and security hardening.

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

---

## Features

- **TCP_DEFER_ACCEPT** — drops zero-byte connections at kernel level, zero overhead
- **Dynamic connection table** — scales to 10M+ concurrent connections
- **jemalloc integration** — tuned memory allocation with background thread GC
- **Native Prometheus metrics** — per-country unique IPs, Russian region breakdown, online/total counters
- **Multi-worker mode** — shared-memory IP stats across worker processes
- **Runtime configuration** — TOML config file + CLI overrides, no recompilation needed
- **Build hardening** — stack protector, FORTIFY_SOURCE, PIE, full RELRO

## Quick Start

### Build

```bash
# Dependencies: OpenSSL (required), jemalloc + libmaxminddb (optional, recommended)
# Debian/Ubuntu:
apt install libssl-dev libjemalloc-dev libmaxminddb-dev

make
```

### Run

```bash
# Direct mode with secret:
./objs/bin/mtbolt --direct -S <your-hex-secret> --http-stats --geoip-db /path/to/GeoLite2-City.mmdb -M 4 -p 443

# With TOML config:
./objs/bin/mtbolt --config mtbolt.toml --direct
```

## Configuration

All tunable parameters can be set via TOML config file and/or CLI flags.
See [Configuration Reference](docs/SETTINGS.md) for the complete parameter list.

Quick example (`mtbolt.toml`):
```toml
[workers]
count = 4

[geoip]
database = "/etc/geoip/GeoLite2-City.mmdb"

[stats]
http_enabled = true
```

## Build

### Dependencies

| Library | Required | Purpose |
|---------|----------|---------|
| OpenSSL | Yes | TLS, AES crypto |
| jemalloc | No | Memory allocator (recommended for production) |
| libmaxminddb | No | GeoIP country/region metrics |

### Compile

```bash
make              # auto-detects optional libraries
make clean        # clean build artifacts
make lint         # run cppcheck static analysis
```

Build flags are auto-detected per architecture (x86_64, aarch64) and platform (Linux, macOS).

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

When `--http-stats` is enabled, Prometheus metrics are served on the stats port:

| Metric | Description |
|--------|-------------|
| `teleproxy_country_unique_ips` | Unique IPs per country (with lat/lon labels) |
| `teleproxy_ru_region_unique_ips` | Unique IPs per Russian region (ISO 3166-2:RU) |
| `teleproxy_online_ips` | Currently connected unique IPs |
| `teleproxy_unique_ips` | Total unique IPs seen |

## Security

- Compile-time hardening: `-fstack-protector-strong -D_FORTIFY_SOURCE=2 -fPIE -Wl,-z,relro,-z,now`
- Atomic refcounts to prevent connection race conditions
- OpenSSL key cleansing (`OPENSSL_cleanse`)
- Constant-time secret comparison (`CRYPTO_memcmp`)
- 5-second handshake timeout for incomplete MTProto sessions
- `TCP_DEFER_ACCEPT` to drop idle TCP connections at kernel level

## License

This project is licensed under the [GNU General Public License v2.0](LICENSE).

## Credits

Fork of [vproxy/teleproxy](https://github.com/ArsenyMalkov/teleproxy) with performance, security, and observability enhancements.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: add English README for MTBolt"
```

---

### Task 11: Create README.ru.md (Russian)

**Files:**
- Create: `README.ru.md`

- [ ] **Step 1: Write the Russian README**

Full translation of README.md into Russian. Same structure, same badges, language selector at top. All code blocks, CLI flags, and metric names stay in English. Section headers and descriptions in Russian.

- [ ] **Step 2: Commit**

```bash
git add README.ru.md
git commit -m "docs: add Russian README (README.ru.md)"
```

---

### Task 12: GitHub setup — LICENSE, .gitignore, publish

**Files:**
- Create: `LICENSE`
- Modify: `.gitignore`

- [ ] **Step 1: Create GPLv2 LICENSE file**

Use the full GPLv2 text from https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt — copy the standard text verbatim.

- [ ] **Step 2: Update .gitignore**

```gitignore
# Build artifacts
objs/
dep/

# Editor files
*.swp
*.swo
*~
.idea/
.vscode/
*.sublime-*

# OS files
.DS_Store
Thumbs.db

# Debug
core
core.*
*.dSYM/
```

- [ ] **Step 3: Commit all remaining files**

```bash
git add LICENSE .gitignore
git commit -m "chore: add GPLv2 LICENSE and .gitignore"
```

- [ ] **Step 4: Create GitHub repo and push**

```bash
gh repo create dpibreaker/mtbolt --public --description "High-performance MTProto proxy for Telegram" --license gpl-2.0
cd /Users/Fatyzzz/PycharmProjects/mtproto/teleproxy-orig
git remote add origin https://github.com/dpibreaker/mtbolt.git
git branch -M main
git push -u origin main
```

- [ ] **Step 5: Verify on GitHub**

```bash
gh repo view dpibreaker/mtbolt --web
```

---

## Task Dependency Graph

```
Task 1 (config.h) ──┐
                     ├── Task 3 (wire into proxy.c) ──┐
Task 2 (config.c) ──┘                                 │
                                                       ├── Task 6 (Makefile) ── Task 12 (GitHub)
Task 4 (headers) ─────────────────────────────────────┤
                                                       │
Task 5 (.c files) ────────────────────────────────────┘

Task 7 (toml.example) ──── independent
Task 8 (SETTINGS.md) ────── independent
Task 9 (SETTINGS.ru.md) ── independent
Task 10 (README.md) ─────── independent
Task 11 (README.ru.md) ──── independent
Task 12 (GitHub) ─────────── depends on all above
```

Tasks 1-2 first, then 3-5 (depend on 1-2), then 6 (depends on 3-5). Tasks 7-11 are independent and can run in parallel. Task 12 is last.
