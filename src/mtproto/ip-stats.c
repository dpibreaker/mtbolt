/*
 *  ip-stats.c — Per-IP connection tracking and GeoIP country metrics
 *
 *  Hash table: open addressing, power-of-2 size, linear probing.
 *  GeoIP: optional libmaxminddb integration for country-level counters.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "ip-stats.h"
#include "kprintf.h"

#ifdef HAVE_MAXMINDDB
#include <maxminddb.h>
#endif

/* ---- IP hash table ---- */

#define IP_HT_INITIAL_SIZE  (1 << 18)   /* 256K slots, ~3MB */
#define IP_HT_MAX_SIZE      (1 << 24)   /* 16M slots */
#define IP_HT_LOAD_FACTOR   70          /* resize at 70% */

struct ip_ht_slot {
  uint32_t ip;        /* 0 = empty */
  int active_conns;
  long long total_conns;
};

static struct ip_ht_slot *ip_ht;
static int ip_ht_size;
static int ip_ht_mask;
static int ip_ht_used;       /* slots with ip != 0 */

static inline int ip_ht_hash (uint32_t ip) {
  /* fast mix */
  uint32_t h = ip;
  h ^= h >> 16;
  h *= 0x45d9f3b;
  h ^= h >> 16;
  return h & ip_ht_mask;
}

static struct ip_ht_slot *ip_ht_find (uint32_t ip) {
  int idx = ip_ht_hash (ip);
  for (;;) {
    struct ip_ht_slot *s = &ip_ht[idx];
    if (s->ip == ip) return s;
    if (s->ip == 0) return s;
    idx = (idx + 1) & ip_ht_mask;
  }
}

static void ip_ht_resize (int new_size) {
  struct ip_ht_slot *old = ip_ht;
  int old_size = ip_ht_size;

  ip_ht = calloc (new_size, sizeof (struct ip_ht_slot));
  assert (ip_ht);
  ip_ht_size = new_size;
  ip_ht_mask = new_size - 1;
  ip_ht_used = 0;

  if (old) {
    for (int i = 0; i < old_size; i++) {
      if (old[i].ip != 0) {
        struct ip_ht_slot *s = ip_ht_find (old[i].ip);
        *s = old[i];
        ip_ht_used++;
      }
    }
    free (old);
  }
}

/* ---- Country tracking ---- */

#define MAX_COUNTRIES 256

static struct country_stats_entry countries[MAX_COUNTRIES];
static int country_count;

static struct country_stats_entry *country_find_or_create (const char *code) {
  for (int i = 0; i < country_count; i++) {
    if (countries[i].code[0] == code[0] && countries[i].code[1] == code[1]) {
      return &countries[i];
    }
  }
  if (country_count >= MAX_COUNTRIES) {
    return NULL;
  }
  struct country_stats_entry *e = &countries[country_count++];
  e->code[0] = code[0];
  e->code[1] = code[1];
  e->code[2] = 0;
  e->unique_ips = 0;
  e->total_ips = 0;
  return e;
}

/* ---- GeoIP ---- */

#ifdef HAVE_MAXMINDDB
static MMDB_s mmdb;
static int geoip_loaded;
#endif

struct geoip_result {
  char cc[4];
  char city[64];
  double latitude;
  double longitude;
};

static struct geoip_result geoip_lookup (uint32_t ip) {
  struct geoip_result r = { .cc = "??", .city = "", .latitude = 0, .longitude = 0 };
#ifdef HAVE_MAXMINDDB
  if (!geoip_loaded) return r;

  struct sockaddr_in sa;
  memset (&sa, 0, sizeof (sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl (ip);

  int mmdb_error;
  MMDB_lookup_result_s result = MMDB_lookup_sockaddr (&mmdb, (struct sockaddr *) &sa, &mmdb_error);

  if (mmdb_error != MMDB_SUCCESS || !result.found_entry) {
    return r;
  }

  MMDB_entry_data_s data;
  int status = MMDB_get_value (&result.entry, &data, "country", "iso_code", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING && data.data_size >= 2) {
    r.cc[0] = data.utf8_string[0];
    r.cc[1] = data.utf8_string[1];
    r.cc[2] = 0;
  }

  /* Get coordinates from location */
  status = MMDB_get_value (&result.entry, &data, "location", "latitude", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
    r.latitude = data.double_value;
  }
  status = MMDB_get_value (&result.entry, &data, "location", "longitude", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
    r.longitude = data.double_value;
  }

  /* City name (English) */
  status = MMDB_get_value (&result.entry, &data, "city", "names", "en", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING && data.data_size > 0) {
    int len = data.data_size < 63 ? data.data_size : 63;
    memcpy (r.city, data.utf8_string, len);
    r.city[len] = 0;
  }
#else
  (void) ip;
#endif
  return r;
}

/* ---- Public API ---- */

void ip_stats_init (void) {
  ip_ht_resize (IP_HT_INITIAL_SIZE);
  country_count = 0;
  memset (countries, 0, sizeof (countries));
  vkprintf (1, "ip_stats: initialized with %d slots\n", ip_ht_size);
}

int ip_stats_geoip_load (const char *mmdb_path) {
#ifdef HAVE_MAXMINDDB
  if (!mmdb_path) return 0;
  int status = MMDB_open (mmdb_path, MMDB_MODE_MMAP, &mmdb);
  if (status != MMDB_SUCCESS) {
    kprintf ("ip_stats: failed to open GeoIP database %s: %s\n", mmdb_path, MMDB_strerror (status));
    return -1;
  }
  geoip_loaded = 1;
  kprintf ("ip_stats: loaded GeoIP database %s\n", mmdb_path);
  return 0;
#else
  if (mmdb_path) {
    kprintf ("ip_stats: GeoIP support not compiled in (need -DHAVE_MAXMINDDB and -lmaxminddb)\n");
  }
  return -1;
#endif
}

void ip_stats_connect (uint32_t ip) {
  if (!ip) return;

  /* Resize if needed */
  if (ip_ht_used * 100 / ip_ht_size >= IP_HT_LOAD_FACTOR && ip_ht_size < IP_HT_MAX_SIZE) {
    ip_ht_resize (ip_ht_size * 2);
  }

  struct ip_ht_slot *s = ip_ht_find (ip);
  int is_new_ip = (s->ip == 0);
  int was_inactive = (s->active_conns == 0);
  if (is_new_ip) {
    s->ip = ip;
    ip_ht_used++;
  }
  s->active_conns++;
  s->total_conns++;

  /* Country tracking — count unique IPs, not connections */
  struct geoip_result geo = geoip_lookup (ip);
  struct country_stats_entry *ce = country_find_or_create (geo.cc);
  if (ce) {
    if (was_inactive) {
      ce->unique_ips++;  /* this IP just became active */
    }
    if (is_new_ip) {
      ce->total_ips++;   /* never seen this IP before */
    }
    if (ce->latitude == 0 && ce->longitude == 0 && (geo.latitude != 0 || geo.longitude != 0)) {
      ce->latitude = geo.latitude;
      ce->longitude = geo.longitude;
    }
  }
}

void ip_stats_disconnect (uint32_t ip) {
  if (!ip) return;

  struct ip_ht_slot *s = ip_ht_find (ip);
  if (s->ip == 0) return;

  if (s->active_conns > 0) {
    s->active_conns--;
    if (s->active_conns == 0) {
      /* IP no longer active — decrement country unique_ips */
      struct geoip_result geo = geoip_lookup (ip);
      struct country_stats_entry *ce = country_find_or_create (geo.cc);
      if (ce && ce->unique_ips > 0) {
        ce->unique_ips--;
      }
    }
  }
}

int ip_stats_count (void) {
  return ip_ht_used;
}

/* ---- Prometheus output ---- */

/* Helper for sorting top-N by active_conns (descending) */
static int cmp_active_desc (const void *a, const void *b) {
  const struct ip_ht_slot *sa = a, *sb = b;
  if (sa->active_conns != sb->active_conns) {
    return (sb->active_conns > sa->active_conns) ? 1 : -1;
  }
  return 0;
}

void ip_stats_prometheus (stats_buffer_t *sb, int top_n) {
  /* Country metrics — unique IPs with lat/lon for geomap */
  if (country_count > 0) {
    sb_printf (sb,
      "# HELP teleproxy_country_unique_ips Currently active unique IPs by country.\n"
      "# TYPE teleproxy_country_unique_ips gauge\n");
    for (int i = 0; i < country_count; i++) {
      if (countries[i].unique_ips > 0) {
        sb_printf (sb, "teleproxy_country_unique_ips{country=\"%s\",latitude=\"%.4f\",longitude=\"%.4f\"} %d\n",
          countries[i].code, countries[i].latitude, countries[i].longitude, countries[i].unique_ips);
      }
    }
    sb_printf (sb,
      "# HELP teleproxy_country_total_ips All-time unique IPs seen by country.\n"
      "# TYPE teleproxy_country_total_ips counter\n");
    for (int i = 0; i < country_count; i++) {
      if (countries[i].total_ips > 0) {
        sb_printf (sb, "teleproxy_country_total_ips{country=\"%s\"} %lld\n",
          countries[i].code, countries[i].total_ips);
      }
    }
  }

  /* IP stats summary */
  sb_printf (sb,
    "# HELP teleproxy_unique_ips Total unique IPs tracked.\n"
    "# TYPE teleproxy_unique_ips gauge\n"
    "teleproxy_unique_ips %d\n", ip_ht_used);

  /* All active IPs with geo data — for geomap */
  if (ip_ht_used == 0) return;

  (void) top_n; /* emit all active IPs */

  sb_printf (sb,
    "# HELP teleproxy_client Active client IPs with geo coordinates.\n"
    "# TYPE teleproxy_client gauge\n");

  for (int i = 0; i < ip_ht_size; i++) {
    if (ip_ht[i].ip == 0 || ip_ht[i].active_conns <= 0) continue;
    struct in_addr addr;
    addr.s_addr = htonl (ip_ht[i].ip);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop (AF_INET, &addr, ip_str, sizeof (ip_str));
    struct geoip_result geo = geoip_lookup (ip_ht[i].ip);
    sb_printf (sb, "teleproxy_client{ip=\"%s\",country=\"%s\",city=\"%s\",latitude=\"%.4f\",longitude=\"%.4f\"} %d\n",
      ip_str, geo.cc, geo.city, geo.latitude, geo.longitude, ip_ht[i].active_conns);
  }
}
