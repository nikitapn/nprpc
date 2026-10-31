#!/usr/bin/env python3
"""Real QUIC handshakes and Monitor RPCs against isolated certificate fixtures."""
import datetime
import ipaddress
import os
import signal
import pathlib
import select
import subprocess
import sys
import tempfile

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
                result = subprocess.run([fixture, {'alt': 'client-alt', 'ip': 'client-ip', 'cap': 'client-cap', 'stats': 'client-stats', 'abandon': 'client-abandon', 'no-pool': 'client-no-pool', 'on-ioc': 'client-on-ioc', 'black-hole': 'client-black-hole', 'frozen': 'client-frozen-server'}.get(live, 'client-live' if live else 'client'), reference,
                                         path(trusted_server), path(client_cert), path(client_cert, '.key')],
                                        capture_output=True, text=True, timeout=10,
                                        env={**os.environ, 'QUIC_TLS_SERVER_PID': str(server.pid)})
                if live == 'frozen':
                    os.kill(server.pid, signal.SIGCONT)
                if live == 'alt' and result.returncode == 77:
                    print('SKIP: TCP transport not compiled', flush=True)
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
        ('wrong server CA fails', 'rogue', None, False, False),
        ('DNS certificate rejected for IP endpoint', 'ca', None, False, 'ip'),
        ('missing trust bundle fails', 'missing', None, False, False),
        ('mismatched identity key fails', 'ca', 'mismatch', False, False),
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
