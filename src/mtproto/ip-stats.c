/*
 *  ip-stats.c — Per-IP connection tracking and GeoIP country metrics
 *
 *  Shared-memory hash table for multi-worker mode.
 *  Uses mmap(MAP_SHARED) + atomic CAS/add for lock-free operation.
 *  GeoIP: optional libmaxminddb for country/city/coordinates.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <sys/mman.h>

#include "ip-stats.h"
#include "kprintf.h"

#ifdef HAVE_MAXMINDDB
#include <maxminddb.h>
#endif

/* ---- Shared memory hash table ---- */

/* Fixed size — mmap pages are lazy, only touched pages consume RAM.
   4M slots × 104 bytes = 416MB virtual, but real RSS only for used slots. */
#define IP_HT_SIZE       (1 << 22)   /* 4M slots */
#define IP_HT_MASK       (IP_HT_SIZE - 1)

struct ip_ht_slot {
  uint32_t ip;        /* 0 = empty, set via CAS */
  int active_conns;   /* modified via atomic add */
  long long total_conns;
  /* cached geo — populated once on first connect */
  char cc[4];
  char region[8];     /* subdivision ISO code, e.g. "MOW", "SPE" */
  char city[64];
  double latitude;
  double longitude;
};

/* Shared state — lives in mmap(MAP_SHARED) */
struct ip_stats_shared {
  int used;  /* approximate, not exact under concurrency */
  struct ip_ht_slot ht[IP_HT_SIZE];
};

struct country_stats_shared {
  int count;
  struct country_stats_entry entries[256];
};

struct region_stats_entry {
  char code[8];        /* e.g. "MOW", "SPE" */
  int unique_ips;
  long long total_ips;
};

#define MAX_REGIONS 128

struct region_stats_shared {
  int count;
  struct region_stats_entry entries[MAX_REGIONS];
};

static struct ip_stats_shared *shared;
static struct country_stats_shared *countries_shared;
static struct region_stats_shared *regions_shared;

static inline int ip_ht_hash (uint32_t ip) {
  uint32_t h = ip;
  h ^= h >> 16;
  h *= 0x45d9f3b;
  h ^= h >> 16;
  return h & IP_HT_MASK;
}

static struct ip_ht_slot *ip_ht_find (uint32_t ip) {
  int idx = ip_ht_hash (ip);
  for (;;) {
    struct ip_ht_slot *s = &shared->ht[idx];
    uint32_t cur = s->ip;
    if (cur == ip) return s;
    if (cur == 0) return s;
    idx = (idx + 1) & IP_HT_MASK;
  }
}

/* ---- Country tracking ---- */

static struct country_stats_entry *country_find_or_create (const char *code) {
  struct country_stats_shared *cs = countries_shared;
  for (int i = 0; i < cs->count; i++) {
    if (cs->entries[i].code[0] == code[0] && cs->entries[i].code[1] == code[1]) {
      return &cs->entries[i];
    }
  }
  if (cs->count >= 256) {
    return NULL;
  }
  /* Atomically claim next slot */
  int idx = __sync_fetch_and_add (&cs->count, 1);
  if (idx >= 256) {
    __sync_fetch_and_add (&cs->count, -1);
    return NULL;
  }
  struct country_stats_entry *e = &cs->entries[idx];
  e->code[0] = code[0];
  e->code[1] = code[1];
  e->code[2] = 0;
  return e;
}

static struct region_stats_entry *region_find_or_create (const char *code) {
  if (!code || !code[0] || !regions_shared) return NULL;
  struct region_stats_shared *rs = regions_shared;
  for (int i = 0; i < rs->count; i++) {
    if (strcmp (rs->entries[i].code, code) == 0) {
      return &rs->entries[i];
    }
  }
  if (rs->count >= MAX_REGIONS) return NULL;
  int idx = __sync_fetch_and_add (&rs->count, 1);
  if (idx >= MAX_REGIONS) {
    __sync_fetch_and_add (&rs->count, -1);
    return NULL;
  }
  struct region_stats_entry *e = &rs->entries[idx];
  strncpy (e->code, code, 7);
  e->code[7] = 0;
  return e;
}

/* ---- GeoIP ---- */

#ifdef HAVE_MAXMINDDB
static MMDB_s mmdb;
static int geoip_loaded;
#endif

struct geoip_result {
  char cc[4];
  char region[8];
  char city[64];
  double latitude;
  double longitude;
};

static struct geoip_result geoip_lookup (uint32_t ip) {
  struct geoip_result r = { .cc = "??", .region = "", .city = "", .latitude = 0, .longitude = 0 };
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

  status = MMDB_get_value (&result.entry, &data, "location", "latitude", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
    r.latitude = data.double_value;
  }
  status = MMDB_get_value (&result.entry, &data, "location", "longitude", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
    r.longitude = data.double_value;
  }

  status = MMDB_get_value (&result.entry, &data, "city", "names", "en", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING && data.data_size > 0) {
    int len = data.data_size < 63 ? data.data_size : 63;
    memcpy (r.city, data.utf8_string, len);
    r.city[len] = 0;
  }
  /* Subdivision/region code (e.g. MOW, SPE for Russia) */
  status = MMDB_get_value (&result.entry, &data, "subdivisions", "0", "iso_code", NULL);
  if (status == MMDB_SUCCESS && data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING && data.data_size > 0) {
    int len = data.data_size < 7 ? data.data_size : 7;
    memcpy (r.region, data.utf8_string, len);
    r.region[len] = 0;
  }
#else
  (void) ip;
#endif
  return r;
}

/* ---- Public API ---- */

void ip_stats_init (void) {
  /* mmap shared anonymous — survives fork, all workers see same data */
  shared = mmap (NULL, sizeof (struct ip_stats_shared),
                 PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shared == MAP_FAILED) {
    kprintf ("ip_stats: mmap failed for hash table\n");
    exit (1);
  }

  countries_shared = mmap (NULL, sizeof (struct country_stats_shared),
                           PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (countries_shared == MAP_FAILED) {
    kprintf ("ip_stats: mmap failed for countries\n");
    exit (1);
  }

  regions_shared = mmap (NULL, sizeof (struct region_stats_shared),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (regions_shared == MAP_FAILED) {
    kprintf ("ip_stats: mmap failed for regions\n");
    exit (1);
  }

  kprintf ("ip_stats: shared hash table %d slots (%ld MB virtual)\n",
           IP_HT_SIZE, (long) sizeof (struct ip_stats_shared) / (1024 * 1024));
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
  if (!ip || !shared) return;

  struct ip_ht_slot *s = ip_ht_find (ip);
  int is_new_ip = 0;

  if (s->ip == 0) {
    /* Try to claim this slot atomically */
    uint32_t old = __sync_val_compare_and_swap (&s->ip, 0, ip);
    if (old == 0) {
      /* We claimed it — populate geo cache */
      is_new_ip = 1;
      __sync_fetch_and_add (&shared->used, 1);
      struct geoip_result geo = geoip_lookup (ip);
      memcpy (s->cc, geo.cc, 4);
      memcpy (s->region, geo.region, 8);
      memcpy (s->city, geo.city, 64);
      s->latitude = geo.latitude;
      s->longitude = geo.longitude;
    } else if (old != ip) {
      /* Someone else claimed it with different IP — re-find */
      s = ip_ht_find (ip);
      if (s->ip == 0) {
        old = __sync_val_compare_and_swap (&s->ip, 0, ip);
        if (old == 0) {
          is_new_ip = 1;
          __sync_fetch_and_add (&shared->used, 1);
          struct geoip_result geo = geoip_lookup (ip);
          memcpy (s->cc, geo.cc, 4);
          memcpy (s->city, geo.city, 64);
          s->latitude = geo.latitude;
          s->longitude = geo.longitude;
        }
      }
    }
  }

  int was_inactive = (__sync_fetch_and_add (&s->active_conns, 1) == 0);
  __sync_fetch_and_add (&s->total_conns, 1);

  /* Country tracking */
  struct country_stats_entry *ce = country_find_or_create (s->cc);
  if (ce) {
    if (was_inactive) {
      __sync_fetch_and_add (&ce->unique_ips, 1);
    }
    if (is_new_ip) {
      __sync_fetch_and_add (&ce->total_ips, 1);
    }
    if (ce->latitude == 0 && ce->longitude == 0 && (s->latitude != 0 || s->longitude != 0)) {
      ce->latitude = s->latitude;
      ce->longitude = s->longitude;
    }
  }

  /* RU region tracking */
  if (s->cc[0] == 'R' && s->cc[1] == 'U' && s->region[0]) {
    struct region_stats_entry *re = region_find_or_create (s->region);
    if (re) {
      if (was_inactive) __sync_fetch_and_add (&re->unique_ips, 1);
      if (is_new_ip) __sync_fetch_and_add (&re->total_ips, 1);
    }
  }
}

void ip_stats_disconnect (uint32_t ip) {
  if (!ip || !shared) return;

  struct ip_ht_slot *s = ip_ht_find (ip);
  if (s->ip == 0) return;

  int old_active = __sync_fetch_and_add (&s->active_conns, -1);
  if (old_active == 1) {
    /* Was 1, now 0 — IP no longer active */
    struct country_stats_entry *ce = country_find_or_create (s->cc);
    if (ce && ce->unique_ips > 0) {
      __sync_fetch_and_add (&ce->unique_ips, -1);
    }
    if (s->cc[0] == 'R' && s->cc[1] == 'U' && s->region[0]) {
      struct region_stats_entry *re = region_find_or_create (s->region);
      if (re && re->unique_ips > 0) {
        __sync_fetch_and_add (&re->unique_ips, -1);
      }
    }
  }
  if (old_active <= 0) {
    /* Underflow guard */
    __sync_val_compare_and_swap (&s->active_conns, old_active - 1, 0);
  }
}

int ip_stats_count (void) {
  return shared ? shared->used : 0;
}

/* ---- Prometheus output ---- */

void ip_stats_prometheus (stats_buffer_t *sb, int top_n) {
  if (!shared || !countries_shared) return;

  /* Country metrics */
  struct country_stats_shared *cs = countries_shared;
  if (cs->count > 0) {
    sb_printf (sb,
      "# HELP teleproxy_country_unique_ips Currently active unique IPs by country.\n"
      "# TYPE teleproxy_country_unique_ips gauge\n");
    for (int i = 0; i < cs->count && i < 256; i++) {
      if (cs->entries[i].unique_ips > 0) {
        sb_printf (sb, "teleproxy_country_unique_ips{country=\"%s\",latitude=\"%.4f\",longitude=\"%.4f\"} %d\n",
          cs->entries[i].code, cs->entries[i].latitude, cs->entries[i].longitude, cs->entries[i].unique_ips);
      }
    }
    sb_printf (sb,
      "# HELP teleproxy_country_total_ips All-time unique IPs seen by country.\n"
      "# TYPE teleproxy_country_total_ips counter\n");
    for (int i = 0; i < cs->count && i < 256; i++) {
      if (cs->entries[i].total_ips > 0) {
        sb_printf (sb, "teleproxy_country_total_ips{country=\"%s\"} %lld\n",
          cs->entries[i].code, cs->entries[i].total_ips);
      }
    }
  }

  /* IP stats summary */
  sb_printf (sb,
    "# HELP teleproxy_unique_ips Total unique IPs tracked.\n"
    "# TYPE teleproxy_unique_ips gauge\n"
    "teleproxy_unique_ips %d\n", shared->used);

  /* Count currently active unique IPs (with active_conns > 0) */
  int online_ips = 0;
  for (int i = 0; i < 256 && i < cs->count; i++) {
    online_ips += cs->entries[i].unique_ips;
  }
  sb_printf (sb,
    "# HELP teleproxy_online_ips Currently online unique IPs.\n"
    "# TYPE teleproxy_online_ips gauge\n"
    "teleproxy_online_ips %d\n", online_ips);

  /* RU region metrics */
  if (regions_shared && regions_shared->count > 0) {
    struct region_stats_shared *rs = regions_shared;
    sb_printf (sb,
      "# HELP teleproxy_ru_region_unique_ips Active unique IPs by Russian region.\n"
      "# TYPE teleproxy_ru_region_unique_ips gauge\n");
    for (int i = 0; i < rs->count && i < MAX_REGIONS; i++) {
      if (rs->entries[i].unique_ips > 0) {
        sb_printf (sb, "teleproxy_ru_region_unique_ips{region=\"%s\"} %d\n",
          rs->entries[i].code, rs->entries[i].unique_ips);
      }
    }
    sb_printf (sb,
      "# HELP teleproxy_ru_region_total_ips All-time unique IPs by Russian region.\n"
      "# TYPE teleproxy_ru_region_total_ips counter\n");
    for (int i = 0; i < rs->count && i < MAX_REGIONS; i++) {
      if (rs->entries[i].total_ips > 0) {
        sb_printf (sb, "teleproxy_ru_region_total_ips{region=\"%s\"} %lld\n",
          rs->entries[i].code, rs->entries[i].total_ips);
      }
    }
  }

  (void) top_n;
}
