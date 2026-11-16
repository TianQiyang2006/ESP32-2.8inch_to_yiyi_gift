"""One-shot QR login; credentials are written privately, never printed."""
import asyncio
import json
import os
import sys

from qqmusic_api import Client
from qqmusic_api.models.login import QRLoginType
from qqmusic_api.modules.login_utils import QRCodeLoginSession

from music_library import ROOT


def state(value):
    (ROOT / 'data/qq-login-status.json').write_text(json.dumps(value), encoding='utf-8')


async def main():
    login_type = QRLoginType[sys.argv[1] if len(sys.argv) > 1 else 'MOBILE']
    state({'state': 'requesting', 'login_type': login_type.name})
    try:
        async with Client() as client:
            session = QRCodeLoginSession(client.login, login_type, interval=2, timeout_seconds=180)
            qr = await asyncio.wait_for(session.get_qrcode(), 25)
            (ROOT / 'data/qq-login.png').write_bytes(qr.data)
            state({'state': 'waiting_for_scan', 'login_type': login_type.name})
            credential = await session.wait_qrcode_login()
            path = ROOT / 'secrets/qq-credential.json'
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as stream:
                stream.write(credential.model_dump_json(by_alias=True))
            state({'state': 'authenticated', 'login_type': login_type.name})
            print('QQ music login completed', flush=True)
    except Exception as exc:
        state({'state': 'failed', 'error_type': type(exc).__name__, 'login_type': login_type.name})
        print('QQ music login failed: ' + type(exc).__name__, flush=True)
        raise SystemExit(1)


if __name__ == '__main__':
    asyncio.run(main())
