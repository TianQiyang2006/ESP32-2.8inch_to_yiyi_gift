"""Embed the public CA roots used by voice, reading and weather services."""
from pathlib import Path
import re
from cryptography import x509
from cryptography.x509.oid import NameOID
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'tools/downloads/roots.pem').read_text()
certs=re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----',source,re.S)
names=['ISRG Root','DigiCert Global Root','GTS Root','GlobalSign Root','Amazon Root','USERTrust','Sectigo Public']
selected=[]
for pem in certs:
    cert=x509.load_pem_x509_certificate(pem.encode())
    common=cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
    if common and any(name in common[0].value for name in names):selected.append(pem)
(ROOT/'hardware/arduino/yiyi_desk/src/gift_ca.h').write_text('#pragma once\nstatic const char GiftRootCa[] = R"pem('+ '\n'.join(selected)+'\n)pem";\n',encoding='utf-8')
print('Embedded',len(selected),'public roots,',len(''.join(selected)),'bytes.')
