#!/usr/bin/env python3
"""Regression test for fake-TLS ClientHello with coalesced tail bytes."""
import hashlib
import hmac as hmac_mod
import os
import socket
import struct
import sys
import time

from test_tls_e2e import build_client_hello, _verify_server_hmac, wait_for_proxy


def _build_obfs2_init(secret_hex, target_dc=2):
    """Build a valid randomized-intermediate obfs2 init header via Telethon."""
    from telethon.network.connection.tcpintermediate import (
        RandomizedIntermediatePacketCodec,
    )
    from telethon.network.connection.tcpmtproxy import MTProxyIO

    header, _enc, _dec = MTProxyIO.init_header(
        bytes.fromhex("dd" + secret_hex), target_dc, RandomizedIntermediatePacketCodec
    )
    return header


def _wrap_tls_appdata(payload):
    return b"\x17\x03\x03" + struct.pack(">H", len(payload)) + payload


def _wrap_tls_ccs():
    return b"\x14\x03\x03\x00\x01\x01"


def _do_handshake_with_tail(
    host,
    port,
    secret_bytes,
    tail,
    domain=None,
    record_version=b"\x03\x01",
    session_id_length=32,
):
    """Send ClientHello plus post-handshake tail in a single TCP write."""
    if domain is None:
        domain = os.environ.get(
            "EE_DOMAIN", os.environ.get("TLS_BACKEND_HOST", "172.30.0.10")
        )

    hello = build_client_hello(
        domain,
        record_version=record_version,
        session_id_length=session_id_length,
    )
    hello_zeroed = bytearray(hello)
    hello_zeroed[11:43] = b"\x00" * 32
    expected = hmac_mod.new(secret_bytes, bytes(hello_zeroed), hashlib.sha256).digest()

    timestamp = int(time.time())
    ts_bytes = struct.pack("<I", timestamp)
    xored_ts = bytes(a ^ b for a, b in zip(ts_bytes, expected[28:32]))
    client_random = expected[:28] + xored_ts
    hello[11:43] = client_random

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(10)
    sock.connect((socket.gethostbyname(host), port))
    sock.sendall(bytes(hello) + tail)

    data = b""
    expected_total = 0
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            chunk = sock.recv(16384)
            if not chunk:
                break
            data += chunk
            if expected_total == 0 and len(data) >= 5:
                server_hello_size = 5 + struct.unpack(">H", data[3:5])[0]
                if len(data) >= server_hello_size + 11:
                    enc_len = struct.unpack(
                        ">H", data[server_hello_size + 9 : server_hello_size + 11]
                    )[0]
                    expected_total = server_hello_size + 11 + enc_len
            if expected_total > 0 and len(data) >= expected_total:
                break
        except socket.timeout:
            break
    return sock, data, bytes(client_random), bytes(hello)


def _assert_socket_not_closed(sock):
    sock.setblocking(False)
    try:
        data = sock.recv(1)
        assert data != b"", "Proxy closed the socket immediately"
    except BlockingIOError:
        pass
    finally:
        sock.close()


def test_tls_handshake_accepts_coalesced_tail():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))
    secret_hex = os.environ.get("TELEPROXY_SECRET", "")
    assert secret_hex, "TELEPROXY_SECRET environment variable not set"

    secret_bytes = bytes.fromhex(secret_hex)
    tail = b"\x14\x03\x03\x00\x01\x01\x17\x03\x03\x00\x40" + (b"\x00" * 64)
    sock, data, client_random, _hello = _do_handshake_with_tail(
        host, port, secret_bytes, tail
    )

    assert len(data) >= 138, f"Response too short after coalesced tail: {len(data)} bytes"
    assert _verify_server_hmac(data, client_random, secret_bytes), (
        "Proxy rejected a valid ClientHello when post-handshake TLS bytes "
        "were coalesced into the same TCP segment"
    )
    time.sleep(0.2)
    _assert_socket_not_closed(sock)
    print("  Coalesced ClientHello tail accepted")


def test_tls_handshake_accepts_coalesced_tail_without_ccs():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))
    secret_hex = os.environ.get("TELEPROXY_SECRET", "")
    assert secret_hex, "TELEPROXY_SECRET environment variable not set"

    secret_bytes = bytes.fromhex(secret_hex)
    # telemt tolerates clients that send ApplicationData immediately after the
    # fake-TLS handshake without a dummy CCS record. mtbolt should do the same.
    tail = b"\x17\x03\x03\x00\x40" + os.urandom(64)
    sock, data, client_random, _hello = _do_handshake_with_tail(
        host, port, secret_bytes, tail
    )

    assert len(data) >= 138, f"Response too short after no-CCS tail: {len(data)} bytes"
    assert _verify_server_hmac(data, client_random, secret_bytes), (
        "Proxy rejected a valid ClientHello when ApplicationData was "
        "coalesced without a dummy CCS record"
    )
    time.sleep(0.2)
    _assert_socket_not_closed(sock)
    print("  Coalesced no-CCS ClientHello tail accepted")


def test_tls_fragmented_mtproto_header_with_interleaved_ccs_is_accepted():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))
    secret_hex = os.environ.get("TELEPROXY_SECRET", "")
    assert secret_hex, "TELEPROXY_SECRET environment variable not set"

    secret_bytes = bytes.fromhex(secret_hex)
    obfs_header = _build_obfs2_init(secret_hex)
    tail = (
        _wrap_tls_appdata(obfs_header[:13])
        + _wrap_tls_ccs()
        + _wrap_tls_appdata(obfs_header[13:37])
        + _wrap_tls_ccs()
        + _wrap_tls_appdata(obfs_header[37:])
    )

    sock, data, client_random, _hello = _do_handshake_with_tail(
        host, port, secret_bytes, tail
    )

    assert len(data) >= 138, (
        f"Response too short after fragmented MTProto header: {len(data)} bytes"
    )
    assert _verify_server_hmac(data, client_random, secret_bytes), (
        "Proxy rejected a valid ClientHello when the first 64-byte MTProto header "
        "was split across multiple TLS ApplicationData records with interleaved CCS"
    )
    time.sleep(0.2)
    _assert_socket_not_closed(sock)
    print("  Fragmented MTProto header with interleaved CCS accepted")


def test_tls_post_handshake_ccs_is_ignored():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))
    secret_hex = os.environ.get("TELEPROXY_SECRET", "")
    assert secret_hex, "TELEPROXY_SECRET environment variable not set"

    secret_bytes = bytes.fromhex(secret_hex)
    obfs_header = _build_obfs2_init(secret_hex)
    sock, data, client_random, _hello = _do_handshake_with_tail(
        host, port, secret_bytes, _wrap_tls_appdata(obfs_header)
    )

    assert len(data) >= 138, f"Response too short after obfs2 init: {len(data)} bytes"
    assert _verify_server_hmac(data, client_random, secret_bytes), (
        "Proxy rejected a valid ClientHello before post-handshake CCS test"
    )

    time.sleep(0.1)
    sock.sendall(_wrap_tls_ccs())
    time.sleep(0.2)
    _assert_socket_not_closed(sock)
    print("  Post-handshake CCS ignored")


def test_tls13_record_header_and_short_session_id_are_accepted():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))
    secret_hex = os.environ.get("TELEPROXY_SECRET", "")
    assert secret_hex, "TELEPROXY_SECRET environment variable not set"

    secret_bytes = bytes.fromhex(secret_hex)
    sock, data, client_random, hello = _do_handshake_with_tail(
        host,
        port,
        secret_bytes,
        b"",
        record_version=b"\x03\x03",
        session_id_length=16,
    )

    assert len(data) >= 5, f"Response too short after TLS 1.3-record ClientHello: {len(data)} bytes"
    assert _verify_server_hmac(data, client_random, secret_bytes), (
        "Proxy rejected a valid ClientHello when the record header used "
        "legacy_version 0x0303 and a short session_id"
    )
    assert data[43] == 16, f"ServerHello echoed wrong session_id length: {data[43]}"
    assert data[44:60] == hello[44:60], "ServerHello did not echo the short session_id"
    time.sleep(0.2)
    _assert_socket_not_closed(sock)
    print("  TLS 1.3-record header with short session_id accepted")


def main():
    host = os.environ.get("TELEPROXY_HOST", "teleproxy")
    port = int(os.environ.get("TELEPROXY_PORT", "8443"))

    print("Starting coalesced ClientHello TLS test...\n", flush=True)
    print(f"Waiting for proxy at {host}:{port}...", flush=True)
    if not wait_for_proxy(host, port, timeout=90):
        print("ERROR: Proxy not ready after 90s")
        sys.exit(1)
    print("Proxy is ready.\n", flush=True)

    try:
        print("[RUN]  test_tls_handshake_accepts_coalesced_tail")
        test_tls_handshake_accepts_coalesced_tail()
        print("[PASS] test_tls_handshake_accepts_coalesced_tail")
        print("[RUN]  test_tls_handshake_accepts_coalesced_tail_without_ccs")
        test_tls_handshake_accepts_coalesced_tail_without_ccs()
        print("[PASS] test_tls_handshake_accepts_coalesced_tail_without_ccs")
        print("[RUN]  test_tls_fragmented_mtproto_header_with_interleaved_ccs_is_accepted")
        test_tls_fragmented_mtproto_header_with_interleaved_ccs_is_accepted()
        print("[PASS] test_tls_fragmented_mtproto_header_with_interleaved_ccs_is_accepted")
        print("[RUN]  test_tls_post_handshake_ccs_is_ignored")
        test_tls_post_handshake_ccs_is_ignored()
        print("[PASS] test_tls_post_handshake_ccs_is_ignored")
        print("[RUN]  test_tls13_record_header_and_short_session_id_are_accepted")
        test_tls13_record_header_and_short_session_id_are_accepted()
        print("[PASS] test_tls13_record_header_and_short_session_id_are_accepted")
        sys.exit(0)
    except Exception as e:
        print(f"[FAIL] test_tls_handshake_accepts_coalesced_tail: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
