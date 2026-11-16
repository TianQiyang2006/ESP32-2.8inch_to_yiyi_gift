#!/usr/bin/env python3
"""stdio MCP to XiaoZhi WebSocket bridge, following 78/mcp-calculator."""
import asyncio
import json
import logging
import os
import sys
import time

import websockets

from music_library import ROOT

LOG = logging.getLogger('yiyi-mcp-bridge')
STATUS = ROOT / 'data/mcp-status.json'


def status(**kwargs):
    content = {'updated_at': int(time.time()), **kwargs}
    tmp = STATUS.with_suffix('.tmp')
    tmp.write_text(json.dumps(content), encoding='utf-8')
    tmp.replace(STATUS)


async def connection(endpoint):
    process, tasks = None, []
    state = {'connected': False, 'initialized': False, 'tools_count': 0}
    requests = {}
    try:
        async with websockets.connect(endpoint, open_timeout=20, ping_interval=20,
                                      ping_timeout=20, max_size=2 * 1024 * 1024) as ws:
            state['connected'] = True
            status(**state)
            LOG.info('Connected to XiaoZhi MCP endpoint')
            process = await asyncio.create_subprocess_exec(
                sys.executable, '-u', str(ROOT / 'mcp_service.py'),
                stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
                stderr=None, limit=2 * 1024 * 1024)

            async def incoming():
                async for message in ws:
                    if isinstance(message, bytes):
                        message = message.decode('utf-8')
                    data = json.loads(message)
                    if 'id' in data and 'method' in data:
                        requests[data['id']] = data['method']
                    process.stdin.write(message.encode('utf-8') + b'\n')
                    await process.stdin.drain()

            async def outgoing():
                while line := await process.stdout.readline():
                    data = json.loads(line)
                    method = requests.pop(data.get('id'), None)
                    if method == 'initialize' and 'result' in data:
                        state['initialized'] = True
                        LOG.info('MCP initialize completed')
                    if method == 'tools/list' and 'result' in data:
                        state['tools_count'] = len(data['result'].get('tools', []))
                        LOG.info('MCP registered tools=%s', state['tools_count'])
                    status(**state)
                    await ws.send(line.decode('utf-8'))

            tasks = [asyncio.create_task(incoming()), asyncio.create_task(outgoing())]
            done, _ = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
            for task in done:
                task.result()
    finally:
        status(connected=False, initialized=False, tools_count=0)
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        if process and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), 5)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()


async def main():
    endpoint = (ROOT / 'secrets/mcp-endpoint').read_text().strip()
    delay = 2
    while True:
        try:
            await connection(endpoint)
        except Exception as exc:
            # Exception strings can contain the endpoint token. Log type only.
            LOG.warning('MCP connection retry (%s)', type(exc).__name__)
        await asyncio.sleep(delay)
        delay = min(delay * 2, 60)


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    asyncio.run(main())
