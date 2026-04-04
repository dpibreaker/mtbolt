/* src/mtproto/mtbolt-config.c — MTBolt runtime configuration */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mtbolt-config.h"
#include "common/toml/tomlc17.h"
#include "kprintf.h"

struct mtbolt_config mtbolt_cfg;

void mtbolt_config_defaults (struct mtbolt_config *cfg) {
  memset (cfg, 0, sizeof (*cfg));

  cfg->max_connections     = 10485760;
  cfg->max_targets         = 10485760;
  cfg->backlog             = 131072;
  cfg->tcp_defer_accept    = 3;
  cfg->window_clamp        = 131072;

  cfg->max_allocated_bytes = 16LL * 1024 * 1024 * 1024;
  cfg->std_buffer          = 2048;
  cfg->small_buffer        = 512;
  cfg->tiny_buffer         = 48;
  cfg->chunk_size          = (1L << 21) - 64;
  cfg->max_connection_buffer = 1 << 25;

  cfg->handshake       = 5.0;
  cfg->rpc             = 5.0;
  cfg->ping_interval   = 5.0;
  cfg->connect         = 3.0;
  cfg->http_max_wait   = 960.0;

  cfg->geoip_enabled     = 1;
  cfg->geoip_database[0] = '\0';
  cfg->geoip_ht_size     = 1 << 22;
  cfg->geoip_max_regions = 128;

  cfg->workers = 0;

  cfg->stats_http_enabled = 0;
  cfg->stats_buffer_size  = 1 << 20;
}

/* Helpers: read typed values from a toml_datum_t table if key exists */

static void cfg_read_int (toml_datum_t tab, const char *key, int *dst) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_INT64) {
    *dst = (int) v.u.int64;
  }
}

static void cfg_read_llong (toml_datum_t tab, const char *key, long long *dst) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_INT64) {
    *dst = (long long) v.u.int64;
  }
}

static void cfg_read_long (toml_datum_t tab, const char *key, long *dst) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_INT64) {
    *dst = (long) v.u.int64;
  }
}

static void cfg_read_double (toml_datum_t tab, const char *key, double *dst) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_FP64) {
    *dst = v.u.fp64;
  } else if (v.type == TOML_INT64) {
    *dst = (double) v.u.int64;
  }
}

static void cfg_read_bool (toml_datum_t tab, const char *key, int *dst) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_BOOLEAN) {
    *dst = v.u.boolean ? 1 : 0;
  }
}

static void cfg_read_string (toml_datum_t tab, const char *key, char *dst, int maxlen) {
  toml_datum_t v = toml_get (tab, key);
  if (v.type == TOML_STRING) {
    snprintf (dst, maxlen, "%s", v.u.s);
  }
}

int mtbolt_config_load_toml (struct mtbolt_config *cfg, const char *path,
                             char *errbuf, int errlen) {
  FILE *fp = fopen (path, "r");
  if (!fp) {
    snprintf (errbuf, errlen, "cannot open config file '%s': %s", path, strerror (errno));
    return -1;
  }

  toml_result_t res = toml_parse_file (fp);
  fclose (fp);

  if (!res.ok) {
    snprintf (errbuf, errlen, "TOML parse error: %s", res.errmsg);
    return -1;
  }

  toml_datum_t sec;

  /* [network] */
  sec = toml_get (res.toptab, "network");
  if (sec.type == TOML_TABLE) {
    cfg_read_int (sec, "max_connections", &cfg->max_connections);
    cfg_read_int (sec, "max_targets",     &cfg->max_targets);
    cfg_read_int (sec, "backlog",         &cfg->backlog);
    cfg_read_int (sec, "tcp_defer_accept", &cfg->tcp_defer_accept);
    cfg_read_int (sec, "window_clamp",    &cfg->window_clamp);
  }

  /* [buffers] */
  sec = toml_get (res.toptab, "buffers");
  if (sec.type == TOML_TABLE) {
    cfg_read_llong (sec, "max_allocated_bytes", &cfg->max_allocated_bytes);
    cfg_read_int   (sec, "std_buffer",          &cfg->std_buffer);
    cfg_read_int   (sec, "small_buffer",        &cfg->small_buffer);
    cfg_read_int   (sec, "tiny_buffer",         &cfg->tiny_buffer);
    cfg_read_long  (sec, "chunk_size",          &cfg->chunk_size);
    cfg_read_int   (sec, "max_connection_buffer", &cfg->max_connection_buffer);
  }

  /* [timeouts] */
  sec = toml_get (res.toptab, "timeouts");
  if (sec.type == TOML_TABLE) {
    cfg_read_double (sec, "handshake",     &cfg->handshake);
    cfg_read_double (sec, "rpc",           &cfg->rpc);
    cfg_read_double (sec, "ping_interval", &cfg->ping_interval);
    cfg_read_double (sec, "connect",       &cfg->connect);
    cfg_read_double (sec, "http_max_wait", &cfg->http_max_wait);
  }

  /* [geoip] */
  sec = toml_get (res.toptab, "geoip");
  if (sec.type == TOML_TABLE) {
    cfg_read_bool   (sec, "enabled",     &cfg->geoip_enabled);
    cfg_read_string (sec, "database",    cfg->geoip_database, sizeof (cfg->geoip_database));
    cfg_read_int    (sec, "ht_size",     &cfg->geoip_ht_size);
    cfg_read_int    (sec, "max_regions", &cfg->geoip_max_regions);
  }

  /* [workers] */
  sec = toml_get (res.toptab, "workers");
  if (sec.type == TOML_TABLE) {
    cfg_read_int (sec, "count", &cfg->workers);
  }

  /* [stats] */
  sec = toml_get (res.toptab, "stats");
  if (sec.type == TOML_TABLE) {
    cfg_read_bool (sec, "http_enabled", &cfg->stats_http_enabled);
    cfg_read_int  (sec, "buffer_size",  &cfg->stats_buffer_size);
  }

  toml_free (res);
  return 0;
}

int mtbolt_config_validate (struct mtbolt_config *cfg, char *errbuf, int errlen) {
#define FAIL(...) do { snprintf (errbuf, errlen, __VA_ARGS__); return -1; } while (0)

  if (cfg->max_connections < 1)
    FAIL ("max_connections must be >= 1 (got %d)", cfg->max_connections);
  if (cfg->max_targets < 1)
    FAIL ("max_targets must be >= 1 (got %d)", cfg->max_targets);
  if (cfg->backlog < 1)
    FAIL ("backlog must be >= 1 (got %d)", cfg->backlog);
  if (cfg->tcp_defer_accept < 0)
    FAIL ("tcp_defer_accept must be >= 0 (got %d)", cfg->tcp_defer_accept);
  if (cfg->max_allocated_bytes < (1LL << 20))
    FAIL ("max_allocated_bytes must be >= 1MB (got %lld)", cfg->max_allocated_bytes);
  if (cfg->handshake <= 0)
    FAIL ("handshake timeout must be > 0 (got %.3f)", cfg->handshake);
  if (cfg->rpc <= 0)
    FAIL ("rpc timeout must be > 0 (got %.3f)", cfg->rpc);
  if (cfg->ping_interval <= 0)
    FAIL ("ping_interval must be > 0 (got %.3f)", cfg->ping_interval);
  if (cfg->connect <= 0)
    FAIL ("connect timeout must be > 0 (got %.3f)", cfg->connect);
  if (cfg->geoip_ht_size < 1024)
    FAIL ("geoip_ht_size must be >= 1024 (got %d)", cfg->geoip_ht_size);
  if ((cfg->geoip_ht_size & (cfg->geoip_ht_size - 1)) != 0)
    FAIL ("geoip_ht_size must be a power of 2 (got %d)", cfg->geoip_ht_size);
  if (cfg->geoip_max_regions < 1 || cfg->geoip_max_regions > 1024)
    FAIL ("geoip_max_regions must be 1..1024 (got %d)", cfg->geoip_max_regions);
  if (cfg->stats_buffer_size < 4096)
    FAIL ("stats_buffer_size must be >= 4096 (got %d)", cfg->stats_buffer_size);

#undef FAIL
  return 0;
}
