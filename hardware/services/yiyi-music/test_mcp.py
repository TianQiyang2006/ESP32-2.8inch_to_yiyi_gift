"""Exercise real MCP stdio discovery, results and persistence in a temporary root."""
import asyncio
import json
import os
import sys
import tempfile
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run():
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for name in ('data', 'secrets', 'library/media'):
            (root / name).mkdir(parents=True)
        (root / 'library/media/test.mp3').write_bytes(b'fixture')
        (root / 'library/catalog.json').write_text(json.dumps([
            {'id': 'test', 'title': '测试曲', 'artist': '测试歌手', 'media': 'test.mp3'}]), encoding='utf-8')
        (root / 'secrets/config.json').write_text(json.dumps({
            'public_base': 'http://example.test/private', 'path_key': 'private'}))
        env = dict(os.environ, YIYI_MUSIC_ROOT=work, PYTHONDONTWRITEBYTECODE='1')
        params = StdioServerParameters(command=sys.executable,
                                       args=[str(Path(__file__).with_name('mcp_service.py'))], env=env)
        async def call(session, name, args):
            result = await session.call_tool(name, args)
            assert not result.isError, name
            return json.loads(result.content[0].text)
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                listing = await session.list_tools()
                assert len(listing.tools) == 22
                assert (await call(session, 'yiyi_weather', {}))['needs_city']
                assert (await call(session, 'yiyi_playlist_manage', {'action': 'save', 'name': '测试歌单', 'songs': [{'id': 'test'}]}))['ok']
                assert (await call(session, 'yiyi_player_control', {'action': 'status'}))['online'] is False
                song = await call(session, 'yiyi_music_resolve', {'song_name': 'test'})
                assert song['found'] and not song['device_playback_started']
                assert song['device_call']['tool'] == 'self.music.play_song'
                await call(session, 'yiyi_favorite_set', {'song_name': 'test'})
                await call(session, 'yiyi_memory_save', {'key': '偏好', 'value': '安静的纯音乐'})
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                assert (await call(session, 'yiyi_favorites_list', {}))['count'] == 1
                memories = await call(session, 'yiyi_memory_recall', {'query': '偏好'})
                assert memories['memories'][0]['value'] == '安静的纯音乐'
                lists = await call(session, 'yiyi_playlist_manage', {'name': '测试歌单'})
                assert lists['playlists'][0]['songs'][0]['id'] == 'test'
                invalid = await call(session, 'yiyi_reminder_create', {'text': 'invalid', 'when': 'yesterday'})
                assert invalid['ok'] is False
        print('PASS: 22 MCP tools, calls, city requirement, playlist/favorite/memory persistence across restart')


if __name__ == '__main__':
    asyncio.run(run())
