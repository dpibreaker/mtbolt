#pragma once

#include <stdint.h>
#include "common/common-stats.h"

struct ip_stats_entry {
  uint32_t ip;
  int active_conns;
  long long total_conns;
};

struct country_stats_entry {
  char code[4];          // "IR", "RU", etc.
  int unique_ips;        // IPs with active_conns > 0
  long long total_ips;   // all-time unique IPs seen
  double latitude;
  double longitude;
};

enum ip_stats_transport_kind {
  IP_STATS_TRANSPORT_UNKNOWN = 0,
  IP_STATS_TRANSPORT_EE = 1,
  IP_STATS_TRANSPORT_DD = 2,
};

/* Initialize ip stats hash table. Call once at startup. */
void ip_stats_init (void);

/* Load GeoIP database. path may be NULL to disable GeoIP. */
int ip_stats_geoip_load (const char *mmdb_path);

/* Called when a client connection is accepted. */
void ip_stats_connect (uint32_t ip);

/* Mark a transport kind as seen for this IP (all-time unique only). */
void ip_stats_transport_seen (uint32_t ip, int transport);

/* Called when a client connection is closed. */
void ip_stats_disconnect (uint32_t ip);

/* Write Prometheus-formatted ip/country metrics into sb. */
void ip_stats_prometheus (stats_buffer_t *sb, int top_n);

/* Return total number of tracked IPs. */
int ip_stats_count (void);
