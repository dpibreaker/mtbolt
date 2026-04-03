# Kavore Security/Perf Merge + MAX_CONNECTIONS Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge security hardening and performance fixes from kavore/teleproxy fork into teleproxy-orig, raise MAX_CONNECTIONS from 65536 to 524288 to eliminate crash at high connection counts.

**Architecture:** Apply kavore's security patches (crypto zeroization, atomic refcounts, timing-safe compares, snprintf, overflow checks, fd leak fixes) to teleproxy-orig while preserving orig's TOML config, macOS build support, and `set_maxconn()` API. Increase `MAX_CONNECTIONS` compile-time constant to 524288.

**Tech Stack:** C, OpenSSL, Linux (primary), GCC/Clang

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/net/net-connections.h:38` | Modify | Raise MAX_CONNECTIONS to 524288 |
| `src/net/net-msg-buffers.h:103-108` | Modify | Fix double-decrement race in msg_buffer_decref |
| `src/net/net-msg-buffers.c:84,87` | Modify | Remove ChunkSave TLS cache, fix sizeof divisor |
| `src/net/net-msg.c:100-106` | Modify | Fix double-decrement race in msg_part_decref |
| `src/crypto/aesni256.c:28-41` | Modify | Add proper error handling for EVP calls |
| `src/net/net-crypto-aes.c` | Modify | Zeroize key material, use RAND_bytes |
| `src/net/net-crypto-dh.c` | Modify | Add OPENSSL_cleanse, BN_CTX NULL checks |
| `src/net/net-tls-parse.c` | Modify | CRYPTO_memcmp for timing-safe session_id check |
| `src/common/md5.c` | Modify | OPENSSL_cleanse for md5_context cleanup |
| `src/common/kprintf.c:85-94` | Modify | sprintf → snprintf with bounds checking |
| `src/common/parse-config.c` | Modify | Integer overflow checks in cfg_parse_long_long |
| `src/common/resolver.c` | Modify | Fix fd leaks, remove static stat struct |
| `src/common/tl-parse.c` | Modify | NULL check in tl_query_header_clone |
| `src/engine/engine-signals.c` | Modify | Atomic signal_check_pending_and_clear |
| `src/mtproto/mtproto-proxy.c` | Modify | Thread-local HTTP vars, RAND_bytes for crypto |

---

### Task 1: Raise MAX_CONNECTIONS (the crash fix)

**Files:**
- Modify: `src/net/net-connections.h:38`

This is the root cause of the production crash: `Assertion '(unsigned) CONN_INFO(C)->fd < MAX_CONNECTIONS' failed` when fd reaches 65536.

- [ ] **Step 1: Change MAX_CONNECTIONS**

In `src/net/net-connections.h`, line 38:

```c
// OLD:
#define MAX_CONNECTIONS	65536

// NEW:
#define MAX_CONNECTIONS	524288
```

Note: This increases the static `ExtConnectionHead[]` array from ~65K entries to ~524K entries. At roughly 16-32 bytes per entry, this adds ~8-16 MB of BSS. Acceptable for a proxy server with 16GB RAM.

- [ ] **Step 2: Build and verify**

```bash
cd teleproxy-orig && make clean && make -j$(nproc)
```

Expected: Clean build, no warnings.

- [ ] **Step 3: Commit**

```bash
git add src/net/net-connections.h
git commit -m "fix: raise MAX_CONNECTIONS to 524288 to prevent crash at >65k fds"
```

---

### Task 2: Fix double-decrement race conditions (crash/corruption fix)

**Files:**
- Modify: `src/net/net-msg-buffers.h:103-108`
- Modify: `src/net/net-msg.c:100-106`

The non-atomic fast-path `if (refcnt == 1)` creates a TOCTOU race: two threads can both see refcnt==1, both set it to 0, and both free — double-free.

- [ ] **Step 1: Fix msg_buffer_decref in net-msg-buffers.h**

In `src/net/net-msg-buffers.h`, replace lines 103-108:

```c
// OLD:
static inline void msg_buffer_decref (struct msg_buffer *buffer) {
  if (buffer->refcnt == 1 || __sync_fetch_and_add (&buffer->refcnt, -1) == 1) {
    buffer->refcnt = 0;
    free_msg_buffer (buffer);
  }
}

// NEW:
static inline void msg_buffer_decref (struct msg_buffer *buffer) {
  if (__sync_fetch_and_add (&buffer->refcnt, -1) == 1) {
    free_msg_buffer (buffer);
  }
}
```

- [ ] **Step 2: Fix msg_part_decref in net-msg.c**

In `src/net/net-msg.c`, replace lines 99-106:

```c
// OLD:
    if (mp->refcnt == 1) {
      mp->refcnt = 0;
    } else {
      if (__sync_fetch_and_add (&mp->refcnt, -1) > 1) {
        break;
      }
    }

// NEW:
    if (__sync_fetch_and_add (&mp->refcnt, -1) > 1) {
      break;
    }
```

- [ ] **Step 3: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add src/net/net-msg-buffers.h src/net/net-msg.c
git commit -m "fix: eliminate TOCTOU double-free race in refcount decrement"
```

---

### Task 3: Remove ChunkSave thread-local cache and fix sizeof divisor

**Files:**
- Modify: `src/net/net-msg-buffers.c:84,87`

ChunkSave prevents buffer chunk reclamation in long-running processes (memory leak). The `/ 4` assumes sizeof(int)==4.

- [ ] **Step 1: Remove ChunkSave and fix divisor**

In `src/net/net-msg-buffers.c`:

Line 84 — delete the line:
```c
// DELETE:
__thread struct msg_buffers_chunk *ChunkSave[MAX_BUFFER_SIZE_VALUES];
```

Line 87 — fix sizeof divisor:
```c
// OLD:
int default_buffer_sizes_cnt = sizeof (default_buffer_sizes) / 4;

// NEW:
int default_buffer_sizes_cnt = sizeof (default_buffer_sizes) / sizeof (default_buffer_sizes[0]);
```

- [ ] **Step 2: Remove all references to ChunkSave in net-msg-buffers.c**

Search for `ChunkSave` in the file and remove/replace all usages. The allocation path that checks `ChunkSave[i]` before allocating a new chunk should be removed — just always go through the normal allocation path.

- [ ] **Step 3: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add src/net/net-msg-buffers.c
git commit -m "fix: remove ChunkSave TLS cache to allow buffer reclamation, fix sizeof"
```

---

### Task 4: Crypto hardening — EVP error handling and key zeroization

**Files:**
- Modify: `src/crypto/aesni256.c`
- Modify: `src/net/net-crypto-aes.c`
- Modify: `src/net/net-crypto-dh.c`

- [ ] **Step 1: Fix aesni256.c error handling**

Replace the assert-based error handling with explicit checks:

```c
// In evp_cipher_ctx_init():
// OLD:
  assert(EVP_CipherInit(evp_ctx, cipher, key, iv, is_encrypt) == 1);
  assert(EVP_CIPHER_CTX_set_padding(evp_ctx, 0) == 1);

// NEW:
  if (EVP_CipherInit(evp_ctx, cipher, key, iv, is_encrypt) != 1) {
    fprintf(stderr, "FATAL: EVP_CipherInit failed\n");
    abort();
  }
  if (EVP_CIPHER_CTX_set_padding(evp_ctx, 0) != 1) {
    fprintf(stderr, "FATAL: EVP_CIPHER_CTX_set_padding failed\n");
    abort();
  }
```

```c
// In evp_crypt():
// OLD:
  assert (EVP_CipherUpdate(evp_ctx, out, &len, in, size) == 1);
  assert (len == size);

// NEW:
  assert (size >= 0);
  if (EVP_CipherUpdate(evp_ctx, out, &len, in, size) != 1) {
    fprintf(stderr, "FATAL: EVP_CipherUpdate failed\n");
    abort();
  }
  if (len != size) {
    fprintf(stderr, "FATAL: EVP_CipherUpdate output length mismatch (%d != %d)\n", len, size);
    abort();
  }
```

- [ ] **Step 2: Add key zeroization in net-crypto-aes.c**

Add `#include <openssl/crypto.h>` and `#include <openssl/rand.h>` at the top.

In `aes_load_pwd_file()` and any function that handles key material, add `OPENSSL_cleanse()` after the key data is consumed:

```c
// After key data is loaded and used:
OPENSSL_cleanse(rand_buf, sizeof(rand_buf));
```

Replace any `lrand48()` calls used for crypto with `RAND_bytes()`:

```c
// OLD:
unsigned char rand_buf[16];
// ... filled with lrand48() ...

// NEW:
unsigned char rand_buf[16];
RAND_bytes(rand_buf, sizeof(rand_buf));
```

- [ ] **Step 3: Fix net-crypto-dh.c**

Add `#include <openssl/crypto.h>`.

Add NULL check for `BN_CTX_new()`:

```c
BN_CTX *ctx = BN_CTX_new();
if (!ctx) {
  return -1;
}
```

Zeroize shared secret bytes after use with `OPENSSL_cleanse()`.

- [ ] **Step 4: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 5: Commit**

```bash
git add src/crypto/aesni256.c src/net/net-crypto-aes.c src/net/net-crypto-dh.c
git commit -m "security: harden crypto — zeroize keys, proper error handling, RAND_bytes"
```

---

### Task 5: Timing-safe TLS session_id comparison

**Files:**
- Modify: `src/net/net-tls-parse.c`

- [ ] **Step 1: Add include and replace memcmp**

Add `#include <openssl/crypto.h>` at the top.

Find the `memcmp` comparing session_id:
```c
// OLD:
if (memcmp (response + 44, request_session_id, 32) != 0)

// NEW:
if (CRYPTO_memcmp (response + 44, request_session_id, 32) != 0)
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/net/net-tls-parse.c
git commit -m "security: use constant-time comparison for TLS session_id"
```

---

### Task 6: MD5 context zeroization

**Files:**
- Modify: `src/common/md5.c`

- [ ] **Step 1: Add include and zeroize**

Add `#include <openssl/crypto.h>` at the top.

In `md5_finish()`, after the hash is computed and before returning, zeroize the context:

```c
// At end of md5_finish(), add:
OPENSSL_cleanse(ctx, sizeof(md5_context));
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/common/md5.c
git commit -m "security: zeroize md5_context after use"
```

---

### Task 7: sprintf → snprintf in hexdump (buffer overflow fix)

**Files:**
- Modify: `src/common/kprintf.c:85-94`

- [ ] **Step 1: Replace sprintf with snprintf**

```c
// OLD (line 85):
p += sprintf (s + p, "%08x", (int) (ptr - (char *) start));

// NEW:
p += snprintf (s + p, sizeof(s) - p, "%08x", (int) (ptr - (char *) start));
```

```c
// OLD (line 87):
s[p ++] = ' ';

// NEW:
if (p < (int)sizeof(s) - 1) { s[p ++] = ' '; }
```

```c
// OLD (line 89):
s[p ++] = ' ';

// NEW:
if (p < (int)sizeof(s) - 1) { s[p ++] = ' '; }
```

```c
// OLD (line 92):
p += sprintf (s + p, "%02x", (unsigned char) ptr[i]);

// NEW:
p += snprintf (s + p, sizeof(s) - p, "%02x", (unsigned char) ptr[i]);
```

```c
// OLD (line 94):
p += sprintf (s + p, "  ");

// NEW:
p += snprintf (s + p, sizeof(s) - p, "  ");
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/common/kprintf.c
git commit -m "security: prevent buffer overflow in hexdump with snprintf"
```

---

### Task 8: Integer overflow checks in config parsing

**Files:**
- Modify: `src/common/parse-config.c`

- [ ] **Step 1: Add limits.h and overflow guards**

Add `#include <limits.h>` at the top.

In `cfg_parse_long_long()`, before the multiply-accumulate loop, add:

```c
if (x > LLONG_MAX / 10) { return LLONG_MAX; }
```

In `cfg_parse_long_long_signed()`, add:

```c
if (x > LLONG_MAX / 10 || x < LLONG_MIN / 10) { return (sgn > 0) ? LLONG_MAX : LLONG_MIN; }
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/common/parse-config.c
git commit -m "security: add integer overflow checks in config number parsing"
```

---

### Task 9: Fix fd leaks in resolver.c

**Files:**
- Modify: `src/common/resolver.c`

- [ ] **Step 1: Remove static from stat struct and fix fd leaks**

Change `static struct stat s;` to `struct stat s;`.

Move `open()` call BEFORE `stat()` — use `fstat()` on the opened fd instead.

Add `close(fd)` in ALL error return paths after the fd is opened:
- After `fstat()` fails
- After `!S_ISREG()` check
- After cache hit early return
- After size bounds check

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/common/resolver.c
git commit -m "fix: close fd in all error paths in resolver, remove static stat"
```

---

### Task 10: NULL check in tl-parse.c

**Files:**
- Modify: `src/common/tl-parse.c`

- [ ] **Step 1: Add NULL check**

In `tl_query_header_clone()`:

```c
// Add at function start:
if (!h) { return NULL; }
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/common/tl-parse.c
git commit -m "fix: NULL check in tl_query_header_clone to prevent segfault"
```

---

### Task 11: Atomic signal handling

**Files:**
- Modify: `src/engine/engine-signals.c`

- [ ] **Step 1: Make signal_check_pending_and_clear atomic**

Find `signal_check_pending_and_clear()` and replace with atomic version:

```c
// OLD (two-step, racy):
int signal_check_pending_and_clear (int sig) {
  if (pending_signals & SIG2INT(sig)) {
    pending_signals &= ~SIG2INT(sig);
    return 1;
  }
  return 0;
}

// NEW (atomic):
int signal_check_pending_and_clear (int sig) {
  unsigned long long old = __sync_fetch_and_and (&pending_signals, ~SIG2INT(sig));
  return (old & SIG2INT(sig)) != 0;
}
```

- [ ] **Step 2: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 3: Commit**

```bash
git add src/engine/engine-signals.c
git commit -m "fix: make signal check-and-clear atomic to prevent race condition"
```

---

### Task 12: Thread-local HTTP header variables and RAND_bytes in mtproto-proxy.c

**Files:**
- Modify: `src/mtproto/mtproto-proxy.c`

- [ ] **Step 1: Make HTTP header buffers thread-local**

Find static HTTP header variables and make them thread-local:

```c
// OLD:
static char cur_http_origin[1024];
// etc.

// NEW:
static __thread char cur_http_origin[1024];
```

- [ ] **Step 2: Replace lrand48() with RAND_bytes() in crypto paths**

Search for `lrand48()` in the file. Replace any usage in security-sensitive contexts:

```c
// OLD:
*(int *)(result + i) = lrand48();

// NEW:
RAND_bytes((unsigned char *)(result + i), 4);
```

Or for bulk fills:
```c
RAND_bytes(result, len);
```

- [ ] **Step 3: Build and verify**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add src/mtproto/mtproto-proxy.c
git commit -m "security: thread-local HTTP vars, replace lrand48 with RAND_bytes"
```

---

### Task 13: Final build, deploy to server

- [ ] **Step 1: Full clean build**

```bash
make clean && make -j$(nproc)
```

- [ ] **Step 2: Verify binary**

```bash
./teleproxy --help 2>&1 | head -5
file teleproxy
```

- [ ] **Step 3: Upload to server**

```bash
scp -P 2222 teleproxy root@5.83.134.98:/opt/teleproxy/teleproxy.new
ssh -p 2222 root@5.83.134.98 "cd /opt/teleproxy && cp teleproxy teleproxy.old2 && mv teleproxy.new teleproxy && chmod +x teleproxy && systemctl restart teleproxy && sleep 2 && systemctl status teleproxy --no-pager"
```

- [ ] **Step 4: Verify on server**

```bash
ssh -p 2222 root@5.83.134.98 "journalctl -u teleproxy --no-pager -n 10"
```

Confirm: no crashes, connections accepted, fd numbers growing past 65536 without assertion failures.

- [ ] **Step 5: Commit all remaining changes**

```bash
git add -A
git commit -m "chore: all kavore security/perf patches merged into teleproxy-orig"
```
