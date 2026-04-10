/*
    This file is part of Mtproto-proxy Library.

    Mtproto-proxy Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    Mtproto-proxy Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with Mtproto-proxy Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2014-2018 Telegram Messenger Inc
              2015-2016 Vitaly Valtman
              2016-2018 Nikolai Durov
    Copyright 2025 Teleproxy contributors
*/

#include "net/net-tls-parse.h"

#include <string.h>
#include <openssl/crypto.h>

#ifndef FUZZ_TARGET
#include "common/kprintf.h"
#else
static inline int kprintf (const char *format, ...) { (void)format; return 0; }
#endif

int tls_read_length (const unsigned char *data, int *pos) {
  *pos += 2;
  return data[*pos - 2] * 256 + data[*pos - 1];
}

int tls_is_client_hello_record_header (const unsigned char *header) {
  if (header == NULL) {
    return 0;
  }
  if (header[0] != 0x16 || header[1] != 0x03) {
    return 0;
  }
  if (header[2] != 0x01 && header[2] != 0x03) {
    return 0;
  }
  int record_length = 256 * header[3] + header[4];
  return record_length >= TLS_CLIENT_HELLO_MIN_LEN &&
         record_length <= TLS_CLIENT_HELLO_MAX_LEN;
}

int tls_parse_client_hello_layout (const unsigned char *client_hello, int len,
                                   struct tls_client_hello_layout *layout) {
#define CHECK_LENGTH(length)  \
  if (pos + (length) > len) { \
    return -1;                \
  }

  if (client_hello == NULL || layout == NULL || len < 5 ||
      !tls_is_client_hello_record_header (client_hello)) {
    return -1;
  }

  memset (layout, 0, sizeof (*layout));

  int record_length = 256 * client_hello[3] + client_hello[4];
  if (len < 5 + record_length) {
    return -1;
  }

  int pos = 5;
  CHECK_LENGTH(4);
  if (client_hello[pos] != 0x01) {
    return -1;
  }

  int handshake_length = (client_hello[pos + 1] << 16) |
                         (client_hello[pos + 2] << 8) |
                         client_hello[pos + 3];
  pos += 4;

  if (handshake_length < 2 + 32 + 1 + 2 + 1 + 2 ||
      handshake_length > record_length - 4) {
    return -1;
  }

  int body_end = 9 + handshake_length;
  if (body_end > 5 + record_length) {
    return -1;
  }

  CHECK_LENGTH(2 + 32);
  layout->record_length = record_length;
  layout->handshake_length = handshake_length;
  pos += 2;
  layout->client_random_offset = pos;
  pos += 32;

  CHECK_LENGTH(1);
  layout->session_id_length = client_hello[pos++];
  if (layout->session_id_length > 32) {
    return -1;
  }
  CHECK_LENGTH(layout->session_id_length);
  layout->session_id_offset = pos;
  pos += layout->session_id_length;

  CHECK_LENGTH(2);
  layout->cipher_suites_length = tls_read_length (client_hello, &pos);
  if ((layout->cipher_suites_length & 1) != 0 || layout->cipher_suites_length <= 0) {
    return -1;
  }
  CHECK_LENGTH(layout->cipher_suites_length);
  layout->cipher_suites_offset = pos;
  pos += layout->cipher_suites_length;

  CHECK_LENGTH(1);
  layout->compression_methods_length = client_hello[pos++];
  if (layout->compression_methods_length <= 0) {
    return -1;
  }
  CHECK_LENGTH(layout->compression_methods_length);
  layout->compression_methods_offset = pos;
  pos += layout->compression_methods_length;

  CHECK_LENGTH(2);
  layout->extensions_length = tls_read_length (client_hello, &pos);
  CHECK_LENGTH(layout->extensions_length);
  layout->extensions_offset = pos;
  pos += layout->extensions_length;

  if (pos != body_end) {
    return -1;
  }

  return 0;

#undef CHECK_LENGTH
}

int tls_check_server_hello (const unsigned char *response, int len,
                            const unsigned char *request_session_id,
                            int *is_reversed_extension_order,
                            int *encrypted_record_sizes,
                            int *encrypted_record_count) {
#define FAIL(error) {                                               \
    kprintf ("Failed to parse upstream TLS response: " error "\n"); \
    return 0;                                                       \
  }
#define CHECK_LENGTH(length)  \
  if (pos + (length) > len) { \
    FAIL("Too short");        \
  }
#define EXPECT_STR(pos, str, error)                          \
  if (memcmp (response + pos, str, sizeof (str) - 1) != 0) { \
    FAIL(error);                                             \
  }

  int pos = 0;
  CHECK_LENGTH(3);
  EXPECT_STR(0, "\x16\x03\x03", "Non-TLS response or TLS <= 1.1");
  pos += 3;
  CHECK_LENGTH(2);
  int server_hello_length = tls_read_length (response, &pos);
  if (server_hello_length <= 39) {
    FAIL("Receive too short ServerHello");
  }
  CHECK_LENGTH(server_hello_length);

  EXPECT_STR(5, "\x02\x00", "Non-TLS response 2");
  EXPECT_STR(9, "\x03\x03", "Non-TLS response 3");

  if (memcmp (response + 11, "\xcf\x21\xad\x74\xe5\x9a\x61\x11\xbe\x1d\x8c\x02\x1e\x65\xb8\x91"
                             "\xc2\xa2\x11\x16\x7a\xbb\x8c\x5e\x07\x9e\x09\xe2\xc8\xa8\x33\x9c", 32) == 0) {
    FAIL("TLS 1.3 servers returning HelloRetryRequest are not supprted");
  }
  if (response[43] == '\x00') {
    FAIL("TLS <= 1.2: empty session_id");
  }
  EXPECT_STR(43, "\x20", "Non-TLS response 4");
  if (server_hello_length <= 75) {
    FAIL("Receive too short server hello 2");
  }
  if (CRYPTO_memcmp (response + 44, request_session_id, 32) != 0) {
    FAIL("TLS <= 1.2: expected mirrored session_id");
  }
  EXPECT_STR(76, "\x13\x01\x00", "TLS <= 1.2: expected x25519 as a chosen cipher");
  pos += 74;
  int extensions_length = tls_read_length (response, &pos);
  if (extensions_length + 76 != server_hello_length) {
    FAIL("Receive wrong extensions length");
  }
  int sum = 0;
  while (pos < 5 + server_hello_length - 4) {
    int extension_id = tls_read_length (response, &pos);
    if (extension_id != 0x33 && extension_id != 0x2b) {
      FAIL("Receive unexpected extension");
    }
    if (pos == 83) {
      *is_reversed_extension_order = (extension_id == 0x2b);
    }
    sum += extension_id;

    int extension_length = tls_read_length (response, &pos);
    if (pos + extension_length > 5 + server_hello_length) {
      FAIL("Receive wrong extension length");
    }
    if (extension_length != (extension_id == 0x33 ? 36 : 2)) {
      FAIL("Unexpected extension length");
    }
    pos += extension_length;
  }
  if (sum != 0x33 + 0x2b) {
    FAIL("Receive duplicate extensions");
  }
  if (pos != 5 + server_hello_length) {
    FAIL("Receive wrong extensions list");
  }

  CHECK_LENGTH(6);
  EXPECT_STR(pos, "\x14\x03\x03\x00\x01\x01", "Expected dummy ChangeCipherSpec");
  pos += 6;

  *encrypted_record_count = 0;
  while (pos + 5 <= len && memcmp (response + pos, "\x17\x03\x03", 3) == 0) {
    pos += 3;
    int rec_len = tls_read_length (response, &pos);
    if (rec_len == 0 || pos + rec_len > len) {
      break;
    }
    if (*encrypted_record_count < MAX_ENCRYPTED_RECORDS) {
      encrypted_record_sizes[*encrypted_record_count] = rec_len;
    }
    (*encrypted_record_count)++;
    pos += rec_len;
  }
  if (*encrypted_record_count == 0) {
    FAIL("No encrypted application data records");
  }

#undef FAIL
#undef CHECK_LENGTH
#undef EXPECT_STR

  return 1;
}

int tls_parse_sni (const unsigned char *client_hello, int len,
                   char *out_domain, int max_domain_len) {
#define CHECK_LENGTH(length)  \
  if (pos + (length) > len) { \
    return -1;                \
  }

  struct tls_client_hello_layout layout;
  if (tls_parse_client_hello_layout (client_hello, len, &layout) < 0) {
    return -1;
  }

  int pos = layout.extensions_offset;
  int ext_end = pos + layout.extensions_length;
  int saw_sni_extension = 0;

  while (pos < ext_end) {
    CHECK_LENGTH(4);
    int extension_id = tls_read_length (client_hello, &pos);
    int extension_length = tls_read_length (client_hello, &pos);
    if (pos + extension_length > ext_end) {
      return -1;
    }

    if (extension_id == 0) {
      if (saw_sni_extension) {
        return -1;
      }
      saw_sni_extension = 1;
      /* found SNI */
      CHECK_LENGTH(5);
      int inner_length = tls_read_length (client_hello, &pos);
      if (inner_length != extension_length - 2 || pos + inner_length > ext_end) {
        return -1;
      }
      if (client_hello[pos++] != 0) {
        return -1;
      }
      int domain_length = tls_read_length (client_hello, &pos);
      if (domain_length != extension_length - 5) {
        return -1;
      }
      int i;
      for (i = 0; i < domain_length; i++) {
        if (client_hello[pos + i] == 0) {
          return -1;
        }
      }
      if (domain_length >= max_domain_len || domain_length <= 0) {
        return -1;
      }
      memcpy (out_domain, client_hello + pos, domain_length);
      out_domain[domain_length] = '\0';
      return domain_length;
    }

    pos += extension_length;
  }

#undef CHECK_LENGTH
}

int tls_parse_client_hello_ciphers (const unsigned char *client_hello, int len,
                                    unsigned char *cipher_suite_id) {
  struct tls_client_hello_layout layout;
  if (tls_parse_client_hello_layout (client_hello, len, &layout) < 0) {
    return -1;
  }

  int pos = layout.cipher_suites_offset;
  int end = pos + layout.cipher_suites_length;
  while (pos + 1 < end) {
    if ((client_hello[pos] & 0x0F) == 0x0A &&
        (client_hello[pos + 1] & 0x0F) == 0x0A) {
      pos += 2;
      continue;
    }
    if (client_hello[pos] == 0x13 &&
        client_hello[pos + 1] >= 0x01 &&
        client_hello[pos + 1] <= 0x03) {
      *cipher_suite_id = client_hello[pos + 1];
      return 0;
    }
    pos += 2;
  }
  return -1;
}
