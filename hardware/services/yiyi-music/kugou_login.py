#!/usr/bin/env python3
"""One-shot scan login. Writes no tokens to logs or command arguments."""
import base64
import json
import os
import time

from kugou_client import api, new_cookie, KugouError
from music_library import ROOT


def main():
    cookie = new_cookie()
    data = api('login/qr/key', cookie=cookie)
    key = data.get('data', {}).get('qrcode')
    if not key:
        print('QR_KEY_FAILED', flush=True)
        return
    qr = api('login/qr/create', {'key': key, 'qrimg': True}, cookie)
    image = qr['data']['base64'].split(',', 1)[1]
    (ROOT / 'data/kugou-login.png').write_bytes(base64.b64decode(image))
    print('QR_READY', flush=True)
    deadline = time.monotonic() + 180
    previous = None
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            response = api('login/qr/check', {'key': key}, cookie)
        except KugouError:
            continue
        data = response.get('data', {})
        state = int(data.get('status', -1))
        if state != previous:
            print('QR_STATE', state, flush=True)
            previous = state
        if state == 4 and data.get('token') and data.get('userid'):
            cookie.update(token=data['token'], userid=str(data['userid']))
            target = ROOT / 'secrets/kugou-credential.json'
            fd = os.open(str(target), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, 'w', encoding='utf-8') as f:
                json.dump({'cookie': cookie, 'saved_at': int(time.time())}, f)
            print('LOGIN_SAVED', flush=True)
            return
        if state == 0:
            print('QR_EXPIRED', flush=True)
            return
    print('QR_TIMEOUT', flush=True)


if __name__ == '__main__':
    main()
