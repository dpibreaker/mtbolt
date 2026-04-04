/* src/mtproto/mtbolt-config.c — MTBolt runtime configuration implementation */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mtbolt-config.h"
#include "common/toml/tomlc17.h"
#include "kprintf.h"

/* Global config instance */
struct mtbolt_config mtbolt_cfg;

void mtbolt_config_defaults (struct mtbolt_config *cfg) {
  memset (cfg, 0, sizeof (*cfg));

  /* [network] */
  cfg->max_connections     = 10485760;
  cfg->max_targets         = 10485760;
  cfg->backlog             = 131072;
  cfg->tcp_defer_accept    = 3;
  cfg->window_clamp        = 131072;

  /* [buffers] */
  cfg->max_allocated_bytes = 16LL * 1024 * 1024 * 1024;
  cfg->std_buffer          = 2048;
  cfg->small_buffer        = 512;
  cfg->tiny_buffer         = 48;
  cfg->chunk_size          = (1L << 21) - 64;
  cfg->max_connection_buffer = 1 << 25;

  /* [timeouts] */
  cfg->handshake       = 5.0;
  cfg->rpc             = 5.0;
  cfg->ping_interval   = 5.0;
  cfg->connect         = 3.0;
  cfg->http_max_wait   = 960.0;

  /* [geoip] */
  cfg->geoip_enabled     = 1;
  cfg->geoip_database[0] = '\0';
  cfg->geoip_ht_size     = 1 << 22;
  cfg->geoip_max_regions = 128;

  /* [workers] */
  cfg->workers = 0;

  /* [stats] */
  cfg->stats_http_enabled = 0;
  cfg->stats_buffer_size  = 1 << 20;
}

/* Helper: read an int from a TOML table if present */
static void read_int (toml_table_t *t, const char *key, int *dst) {
  toml_value_t v = toml_table_int (t, key);
  if (v.ok) {
    *dst = (int) v.u.i;
  }
}

/* Helper: read a long long from a TOML table if present */
static void read_llong (toml_table_t *t, const char *key, long long *dst) {
  toml_value_t v = toml_table_int (t, key);
  if (v.ok) {
    *dst = (long long) v.u.i;
  }
}

/* Helper: read a long from a TOML table if present */
static void read_long (toml_table_t *t, const char *key, long *dst) {
  toml_value_t v = toml_table_int (t, key);
  if (v.ok) {
    *dst = (long) v.u.i;
  }
}

/* Helper: read a double from a TOML table if present */
static void read_double (toml_table_t *t, const char *key, double *dst) {
  toml_value_t v = toml_table_double (t, key);
  if (v.ok) {
    *dst = v.u.d;
  }
}

/* Helper: read a bool as int from a TOML table if present */
static void read_bool (toml_table_t *t, const char *key, int *dst) {
  toml_value_t v = toml_table_bool (t, key);
  if (v.ok) {
    *dst = v.u.b ? 1 : 0;
  }
}

/* Helper: read a string into a fixed buffer */
static void read_string (toml_table_t *t, const char *key, char *dst, int maxlen) {
  toml_value_t v = toml_table_string (t, key);
  if (v.ok) {
    snprintf (dst, maxlen, "%s", v.u.s);
    free (v.u.s);
  }
}

int mtbolt_config_load_toml (struct mtbolt_config *cfg, const char *path,
                             char *errbuf, int errlen) {
  FILE *fp = fopen (path, "r");
  if (!fp) {
    snprintf (errbuf, errlen, "cannot open config file '%s': %s", path, strerror (errno));
    return -1;
  }

  toml_table_t *root = toml_parse_file (fp, errbuf, errlen);
  fclose (fp);

  if (!root) {
    return -1;
  }

  toml_table_t *sec;

  /* [network] */
  sec = toml_table_table (root, "network");
  if (sec) {
    read_int (sec, "max_connections", &cfg->max_connections);
    read_int (sec, "max_targets",     &cfg->max_targets);
    read_int (sec, "backlog",         &cfg->backlog);
    read_int (sec, "tcp_defer_accept", &cfg->tcp_defer_accept);
    read_int (sec, "window_clamp",    &cfg->window_clamp);
  }

  /* [buffers] */
  sec = toml_table_table (root, "buffers");
  if (sec) {
    read_llong (sec, "max_allocated_bytes", &cfg->max_allocated_bytes);
    read_int   (sec, "std_buffer",          &cfg->std_buffer);
    read_int   (sec, "small_buffer",        &cfg->small_buffer);
    read_int   (sec, "tiny_buffer",         &cfg->tiny_buffer);
    read_long  (sec, "chunk_size",          &cfg->chunk_size);
    read_int   (sec, "max_connection_buffer", &cfg->max_connection_buffer);
  }

  /* [timeouts] */
  sec = toml_table_table (root, "timeouts");
  if (sec) {
    read_double (sec, "handshake",     &cfg->handshake);
    read_double (sec, "rpc",           &cfg->rpc);
    read_double (sec, "ping_interval", &cfg->ping_interval);
    read_double (sec, "connect",       &cfg->connect);
    read_double (sec, "http_max_wait", &cfg->http_max_wait);
  }

  /* [geoip] */
  sec = toml_table_table (root, "geoip");
  if (sec) {
    read_bool   (sec, "enabled",     &cfg->geoip_enabled);
    read_string (sec, "database",    cfg->geoip_database, sizeof (cfg->geoip_database));
    read_int    (sec, "ht_size",     &cfg->geoip_ht_size);
    read_int    (sec, "max_regions", &cfg->geoip_max_regions);
  }

  /* [workers] */
  sec = toml_table_table (root, "workers");
  if (sec) {
    read_int (sec, "count", &cfg->workers);
  }

  /* [stats] */
  sec = toml_table_table (root, "stats");
  if (sec) {
    read_bool (sec, "http_enabled", &cfg->stats_http_enabled);
    read_int  (sec, "buffer_size",  &cfg->stats_buffer_size);
  }

  toml_free (root);
  return 0;
}

int mtbolt_config_validate (struct mtbolt_config *cfg, char *errbuf, int errlen) {
#define FAIL(...) do { snprintf (errbuf, errlen, __VA_ARGS__); return -1; } while (0)

  if (cfg->max_connections < 1) {
    FAIL ("max_connections must be >= 1 (got %d)", cfg->max_connections);
  }
  if (cfg->max_targets < 1) {
    FAIL ("max_targets must be >= 1 (got %d)", cfg->max_targets);
  }
  if (cfg->backlog < 1) {
    FAIL ("backlog must be >= 1 (got %d)", cfg->backlog);
  }
  if (cfg->tcp_defer_accept < 0) {
    FAIL ("tcp_defer_accept must be >= 0 (got %d)", cfg->tcp_defer_accept);
  }
  if (cfg->max_allocated_bytes < (1LL << 20)) {
    FAIL ("max_allocated_bytes must be >= 1MB (got %lld)", cfg->max_allocated_bytes);
  }
  if (cfg->handshake <= 0) {
    FAIL ("handshake timeout must be > 0 (got %.3f)", cfg->handshake);
  }
  if (cfg->rpc <= 0) {
    FAIL ("rpc timeout must be > 0 (got %.3f)", cfg->rpc);
  }
  if (cfg->ping_interval <= 0) {
    FAIL ("ping_interval must be > 0 (got %.3f)", cfg->ping_interval);
  }
  if (cfg->connect <= 0) {
    FAIL ("connect timeout must be > 0 (got %.3f)", cfg->connect);
  }
  if (cfg->geoip_ht_size < 1024) {
    FAIL ("geoip_ht_size must be >= 1024 (got %d)", cfg->geoip_ht_size);
  }
  if ((cfg->geoip_ht_size & (cfg->geoip_ht_size - 1)) != 0) {
    FAIL ("geoip_ht_size must be a power of 2 (got %d)", cfg->geoip_ht_size);
  }
  if (cfg->geoip_max_regions < 1 || cfg->geoip_max_regions > 1024) {
    FAIL ("geoip_max_regions must be 1..1024 (got %d)", cfg->geoip_max_regions);
  }
  if (cfg->stats_buffer_size < 4096) {
    FAIL ("stats_buffer_size must be >= 4096 (got %d)", cfg->stats_buffer_size);
  }

#undef FAIL
  return 0;
}
