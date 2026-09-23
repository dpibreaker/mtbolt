#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net/net-tls-parse.h"

static unsigned char *read_fixture (const char *path, int *len) {
  FILE *f = fopen (path, "rb");
  if (!f) {
    return NULL;
  }
  fseek (f, 0, SEEK_END);
  long size = ftell (f);
  rewind (f);
  unsigned char *buf = malloc ((size_t)size);
  if (!buf || fread (buf, 1, (size_t)size, f) != (size_t)size) {
    free (buf);
    fclose (f);
    return NULL;
  }
  fclose (f);
  *len = (int)size;
  return buf;
}

static int parse_response (const unsigned char *response, int len) {
  int is_reversed = -1;
  int record_sizes[MAX_ENCRYPTED_RECORDS] = {};
  int record_count = 0;
  return tls_check_server_hello (response, len, response + 44,
                                 &is_reversed, record_sizes, &record_count);
}

static int test_mlkem_client_hello (void) {
  unsigned char hello[63] = {0};
  hello[44] = 0; hello[45] = 2; /* cipher suites */
  hello[46] = 0x13; hello[47] = 1;
  hello[48] = 1;               /* compression methods */
  hello[50] = 0; hello[51] = 11; /* extensions */
  hello[52] = 0; hello[53] = 0x33;
  hello[54] = 0; hello[55] = 7;
  hello[56] = 0; hello[57] = 5; /* key_share list */
  hello[58] = 0x11; hello[59] = 0xec;
  hello[60] = 0; hello[61] = 1;
  if (!tls_client_hello_offers_mlkem (hello, sizeof (hello))) return 0;
  hello[59] = 0x1d;
  if (tls_client_hello_offers_mlkem (hello, sizeof (hello))) return 0;
  hello[59] = 0xec;
  hello[61] = 2; /* truncated share must not be accepted */
  if (tls_client_hello_offers_mlkem (hello, sizeof (hello))) return 0;
  if (tls_client_hello_offers_mlkem (hello, sizeof (hello) - 1)) return 0;
  return 1;
}

static int test_mlkem_server_hello (const unsigned char *fixture, int len) {
  const int extra = 1088;
  unsigned char *response = malloc ((size_t)len + extra);
  if (!response) return 0;
  /* Replace the fixture's 32-byte X25519 share with a 1120-byte hybrid share. */
  memcpy (response, fixture, 89);
  memset (response + 89, 0xa5, extra);
  memcpy (response + 89 + extra, fixture + 89, (size_t)len - 89);
  response[3] = 0x04; response[4] = 0xba; /* record: 1210 bytes */
  response[7] = 0x04; response[8] = 0xb6; /* handshake: 1206 bytes */
  response[79] = 0x04; response[80] = 0x6e; /* extensions: 1134 bytes */
  response[83] = 0x04; response[84] = 0x64; /* key_share extension: 1124 bytes */
  response[85] = 0x11; response[86] = 0xec;
  response[87] = 0x04; response[88] = 0x60; /* share: 1120 bytes */
  int ok = parse_response (response, len + extra);
  response[83] = 0xff; /* malformed extension length */
  ok = ok && !parse_response (response, len + extra);
  free (response);
  return ok;
}

int main (void) {
  int len = 0;
  unsigned char *response =
    read_fixture ("fuzz/corpus/tls_server_hello/valid_tls13.bin", &len);
  if (!response) {
    fprintf (stderr, "failed to read TLS fixture\n");
    return 1;
  }

  if (!parse_response (response, len)) {
    fprintf (stderr, "valid TLS 1.3 fixture was rejected\n");
    free (response);
    return 1;
  }

  if (!test_mlkem_client_hello () || !test_mlkem_server_hello (response, len)) {
    fprintf (stderr, "ML-KEM ClientHello or ServerHello parsing failed\n");
    free (response);
    return 1;
  }

  int server_hello_end = 5 + response[3] * 256 + response[4];
  if (server_hello_end + 6 >= len ||
      memcmp (response + server_hello_end, "\x14\x03\x03\x00\x01\x01", 6)) {
    fprintf (stderr, "fixture has no dummy ChangeCipherSpec\n");
    free (response);
    return 1;
  }
  memmove (response + server_hello_end, response + server_hello_end + 6,
           (size_t)(len - server_hello_end - 6));
  len -= 6;

  if (!parse_response (response, len)) {
    fprintf (stderr, "TLS 1.3 response without dummy ChangeCipherSpec was rejected\n");
    free (response);
    return 1;
  }

  free (response);
  puts ("TLS ServerHello parser tests passed");
  return 0;
}
