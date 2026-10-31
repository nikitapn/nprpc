#!/usr/bin/env python3
"""Real QUIC handshakes and Monitor RPCs against isolated certificate fixtures."""
import datetime
import ipaddress
import os
import signal
import socket
import pathlib
import select
import subprocess
import sys
import tempfile
import threading
import time

try:
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
except ImportError:
    print('quic_tls requires Python cryptography')
    sys.exit(77)

fixture = str(pathlib.Path(sys.argv[1]).resolve())
now = datetime.datetime.now(datetime.timezone.utc)

with tempfile.TemporaryDirectory(prefix='nprpc-quic-tls-') as directory:
    root = pathlib.Path(directory)

    def issue(name, issuer=None, ca=False, client=False, expired=False, hostname='localhost', ip=False):
        key = ec.generate_private_key(ec.SECP256R1())
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
        builder = (x509.CertificateBuilder().subject_name(subject)
                   .issuer_name(issuer[0].subject if issuer else subject)
                   .public_key(key.public_key()).serial_number(x509.random_serial_number())
                   .not_valid_before(now - datetime.timedelta(days=2))
                   .not_valid_after(now + datetime.timedelta(days=-1 if expired else 7))
                   .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True))
        if not ca:
            builder = builder.add_extension(x509.ExtendedKeyUsage([
                ExtendedKeyUsageOID.CLIENT_AUTH if client else ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            if not client:
                builder = builder.add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address("127.0.0.1")) if ip else x509.DNSName(hostname)]), critical=False)
        cert = builder.sign(issuer[1] if issuer else key, hashes.SHA256())
        (root / (name + '.pem')).write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        key_path = root / (name + '.key')
        key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        key_path.chmod(0o600)
        return cert, key

    ca = issue('ca', ca=True)
    rogue = issue('rogue', ca=True)
    issue('server', ca)
    issue('ip-server', ca, ip=True)
    issue('named-server', ca, hostname='quic-test.invalid')
    issue('wrong-host', ca, hostname='other.invalid')
    issue('expired-server', ca, expired=True)
    issue('client', ca, client=True)
    issue('rogue-client', rogue, client=True)
    issue('expired-client', ca, client=True, expired=True)
    issue('wrong-purpose', ca)
    issue('self-client', client=True)
    issue('other-self-client', client=True)
    (root / 'mismatch.pem').write_bytes((root / 'client.pem').read_bytes())
    (root / 'mismatch.key').write_bytes((root / 'rogue-client.key').read_bytes())

    def path(name, suffix='.pem'):
        return '-' if name is None else str(root / (name + suffix))

    # A real name has to decide where packets go, which localhost cannot show.
    private_hosts = ['unshare', '-rm', 'sh', '-c', 'mount --bind "$1" /etc/hosts && shift && exec "$@"', 'sh']
    hosts_files = {
        # IPv6 sorts first.  The fixture makes it silent, so only the second
        # address can reach the server.
        'named': '::1 quic-test.invalid\n127.0.0.1 quic-test.invalid\n',
        'named-hole': '127.0.0.2 quic-test.invalid\n',
        # Not the address the kernel would pick to reply from on its own, so
        # these only work if the server replies from where it was reached.
        'secondary-v4': '127.0.0.3 quic-test.invalid\n',
        'secondary-v6': '::1 quic-test.invalid\n',
    }
    for name, content in list(hosts_files.items()):
        hosts_files[name] = root / (name + '.hosts')
        hosts_files[name].write_text(content)
    private_hosts_work = subprocess.run(
        [*private_hosts, str(hosts_files['named']), 'true'], capture_output=True).returncode == 0

    def start_rebinding_relay(rebind_after):
        """Forwards to the server, moving to a new source port as a NAT rebinding would."""
        downstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        downstream.bind(('127.0.0.1', 0))
        stop = threading.Event()

        def run():
            upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            client = None
            rebind_at = time.monotonic() + rebind_after
            while not stop.is_set():
                if rebind_at and time.monotonic() >= rebind_at:
                    upstream.close()
                    upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    rebind_at = None
                for ready in select.select([downstream, upstream], [], [], 0.05)[0]:
                    data, sender = ready.recvfrom(65535)
                    if ready is downstream:
                        client = sender
                        upstream.sendto(data, ('127.0.0.1', 28444))
                    elif client:
                        downstream.sendto(data, client)
            upstream.close()
            downstream.close()

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        return downstream.getsockname()[1], stop

    def group(server_cert, trusted_clients, cases):
        server = subprocess.Popen([fixture, 'server', path(server_cert), path(server_cert, '.key'),
                                   path(trusted_clients)], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        try:
            if not select.select([server.stdout], [], [], 10)[0]:
                raise AssertionError('Server startup timed out')
            reference = server.stdout.readline().strip()
            if not reference.startswith('NPRPC1:'):
                raise AssertionError('Server did not publish a reference: ' + reference)
            for label, trusted_server, client_cert, accepted, live in cases:
                hosts = hosts_files.get(live)
                if hosts and not private_hosts_work:
                    print('SKIP:', label, '(needs unprivileged user namespaces)', flush=True)
                    continue
                prefix = [*private_hosts, str(hosts)] if hosts else []
                relay_port, stop_relay = start_rebinding_relay(1.0) if live == 'rebinding' else (0, None)
                result = subprocess.run([*prefix, fixture, {'alt': 'client-alt', 'ip': 'client-ip', 'cap': 'client-cap', 'stats': 'client-stats', 'abandon': 'client-abandon', 'no-pool': 'client-no-pool', 'on-ioc': 'client-on-ioc', 'black-hole': 'client-black-hole', 'frozen': 'client-frozen-server', 'named': 'client-named', 'named-hole': 'client-named-black-hole', 'unresolvable': 'client-unresolvable', 'secondary-v4': 'client-named-direct', 'secondary-v6': 'client-named-direct', 'rebinding': 'client-rebinding'}.get(live, 'client-live' if live else 'client'), reference,
                                         path(trusted_server), path(client_cert), path(client_cert, '.key')],
                                        capture_output=True, text=True, timeout=10,
                                        env={**os.environ, 'QUIC_TLS_SERVER_PID': str(server.pid),
                                             'QUIC_TLS_RELAY_PORT': str(relay_port)})
                if stop_relay:
                    stop_relay.set()
                if live == 'frozen':
                    os.kill(server.pid, signal.SIGCONT)
                if live == 'alt' and result.returncode == 77:
                    print('SKIP: TCP transport not compiled', flush=True)
                    continue
                if live == 'secondary-v6' and not socket.has_ipv6:
                    continue
                if live == 'named' and result.returncode == 77:
                    print('SKIP:', label, '(no IPv6 loopback)', flush=True)
                    continue
                if result.returncode not in (0, 1) or (result.returncode == 0) != accepted:
                    raise AssertionError(f'{label}: return={result.returncode}\n{result.stdout}\n{result.stderr}')
                if server.poll() is not None:
                    raise AssertionError('Server exited during ' + label)
                print('PASS:', label, flush=True)
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()

    group('server', None, [
        ('system trust rejects private server', None, None, False, False),
        ('explicit server CA succeeds', 'ca', None, True, False),
        ('runtime TLS configuration succeeds', 'ca', None, True, True),
        ('telemetry watches are limited per session', 'ca', None, True, 'cap'),
        ('a call needs no thread pool', 'ca', None, True, 'no-pool'),
        ('a call can block the only io_context thread', 'ca', None, True, 'on-ioc'),
        ('connecting to nothing times out', 'ca', None, True, 'black-hole'),
        ('a frozen server times out', 'ca', None, True, 'frozen'),
        ('a stream survives a NAT rebinding', 'ca', None, True, 'rebinding'),
        ('a hostname decides where packets go', 'ca', None, True, 'named-hole'),
        ('an unresolvable hostname fails', 'ca', None, True, 'unresolvable'),
        ('wrong server CA fails', 'rogue', None, False, False),
        ('DNS certificate rejected for IP endpoint', 'ca', None, False, 'ip'),
        ('missing trust bundle fails', 'missing', None, False, False),
        ('mismatched identity key fails', 'ca', 'mismatch', False, False),
    ])
    group('named-server', None, [
        ('a hostname reaches its server, past a dead address', 'ca', None, True, 'named'),
        ('a server replies from the IPv4 address it was reached on', 'ca', None, True, 'secondary-v4'),
        ('a server replies from the IPv6 address it was reached on', 'ca', None, True, 'secondary-v6'),
    ])
    group('ip-server', None, [('IP SAN succeeds for IP endpoint', 'ca', None, True, 'ip')])
    group('wrong-host', None, [('hostname mismatch fails', 'ca', None, False, False)])
    group('expired-server', None, [('expired server fails', 'ca', None, False, False)])
    group('server', 'ca', [
        ('mTLS trusted client succeeds after reload', 'ca', 'client', True, False),
        ('mTLS live configuration succeeds', 'ca', 'client', True, True),
        ('mTLS missing client fails', 'ca', None, False, False),
        ('telemetry cannot bypass mTLS through TCP', 'ca', None, False, 'alt'),
        ('mTLS untrusted client fails', 'ca', 'rogue-client', False, False),
        ('mTLS expired client fails', 'ca', 'expired-client', False, False),
        ('mTLS wrong certificate purpose fails', 'ca', 'wrong-purpose', False, False),
        ('a Watch left open does not outlive its connection', 'ca', 'client', True, 'abandon'),
        ('telemetry counts refused handshakes and live sessions', 'ca', 'client', True, 'stats'),
    ])
    group('server', 'self-client', [
        ('explicitly trusted self-signed client succeeds', 'ca', 'self-client', True, False),
        ('unlisted self-signed client fails', 'ca', 'other-self-client', False, False),
    ])
