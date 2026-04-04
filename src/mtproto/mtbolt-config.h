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
