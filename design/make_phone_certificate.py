"""Generate a private LAN CA and HTTPS certificate; never installs trust settings."""
from pathlib import Path
from datetime import datetime, timedelta, timezone
from ipaddress import ip_address
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from phone_server import lan_ip

def main():
    folder = Path(__file__).resolve().parent / '.local'; folder.mkdir(exist_ok=True)
    now = datetime.now(timezone.utc)
    current_ip = lan_ip()
    if (folder / 'phone-server.pem').exists() and (folder / 'phone-server-key.pem').exists():
        try:
            current = x509.load_pem_x509_certificate((folder / 'phone-server.pem').read_bytes())
            current.extensions.get_extension_for_class(x509.AuthorityKeyIdentifier)
            current.extensions.get_extension_for_class(x509.SubjectKeyIdentifier)
            current.extensions.get_extension_for_class(x509.KeyUsage)
            addresses = current.extensions.get_extension_for_class(x509.SubjectAlternativeName).value.get_values_for_type(x509.IPAddress)
            if ip_address(current_ip) in addresses and current.not_valid_after_utc > now+timedelta(days=7):
                print('Existing local HTTPS certificate retained.'); return
        except (ValueError, x509.ExtensionNotFound):
            pass
    if (folder / 'phone-root-key.pem').exists() and (folder / 'phone-root.cer').exists():
        root_key = serialization.load_pem_private_key((folder / 'phone-root-key.pem').read_bytes(), password=None)
        root = x509.load_der_x509_certificate((folder / 'phone-root.cer').read_bytes())
    else:
        root_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Yiyi local phone settings')])
        root = x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(root_key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=3650)).add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True).add_extension(x509.SubjectKeyIdentifier.from_public_key(root_key.public_key()), critical=False).add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True).sign(root_key, hashes.SHA256())
        (folder / 'phone-root-key.pem').write_bytes(root_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        print('New local root certificate generated; install the current trust.cer on the phone.')
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    leaf = x509.CertificateBuilder().subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, current_ip)])).issuer_name(root.subject).public_key(key.public_key()).serial_number(x509.random_serial_number()).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=365)).add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True).add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False).add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(root_key.public_key()), critical=False).add_extension(x509.SubjectAlternativeName([x509.IPAddress(ip_address(current_ip)), x509.IPAddress(ip_address('127.0.0.1')), x509.DNSName('localhost')]), critical=False).add_extension(x509.KeyUsage(True, False, True, False, False, False, False, False, False), critical=True).add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False).sign(root_key, hashes.SHA256())
    (folder / 'phone-root.cer').write_bytes(root.public_bytes(serialization.Encoding.DER))
    (folder / 'phone-server.pem').write_bytes(leaf.public_bytes(serialization.Encoding.PEM)+root.public_bytes(serialization.Encoding.PEM))
    (folder / 'phone-server-key.pem').write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
    print('Local HTTPS files generated; phone trust is configured on the phone.')

if __name__ == '__main__': main()
