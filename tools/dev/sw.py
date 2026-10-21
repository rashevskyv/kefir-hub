"""Drive a Switch through the NX-MCP server (controller + cur_frame) from the shell.

    python tools/dev/sw.py shot out.jpg            # current frame as JPEG
    python tools/dev/sw.py press A DOWN DOWN A     # taps, 0.4 s apart
    python tools/dev/sw.py hold PLUS 2             # hold a button for N seconds
Host: --host 192.168.50.69 (default), port 12345.
"""
import argparse
import base64
import json
import sys
import time
import urllib.request

BUTTONS = {
    'A': 0x1, 'B': 0x2, 'X': 0x4, 'Y': 0x8, 'LSTICK': 0x10, 'RSTICK': 0x20, 'L': 0x40, 'R': 0x80,
    'ZL': 0x100, 'ZR': 0x200, 'PLUS': 0x400, 'MINUS': 0x800, 'LEFT': 0x1000, 'UP': 0x2000,
    'RIGHT': 0x4000, 'DOWN': 0x8000, 'HOME': 0x10000, 'CAPTURE': 0x20000,
}


class Mcp:
    def __init__(self, host):
        self.url = f'http://{host}:12345/mcp'
        self.sid = None
        self.n = 0
        self.call('initialize', {'protocolVersion': '2025-03-26', 'capabilities': {},
                                 'clientInfo': {'name': 'kefir-hub-sw', 'version': '1'}})
        self.post({'jsonrpc': '2.0', 'method': 'notifications/initialized'})

    def post(self, body):
        req = urllib.request.Request(self.url, json.dumps(body).encode(), {
            'Content-Type': 'application/json', 'Accept': 'application/json, text/event-stream',
            **({'Mcp-Session-Id': self.sid} if self.sid else {})})
        with urllib.request.urlopen(req, timeout=30) as r:
            self.sid = self.sid or r.headers.get('Mcp-Session-Id')
            data = r.read().decode('utf-8', 'replace')
        return json.loads(data) if data.strip().startswith('{') else None

    def call(self, method, params):
        self.n += 1
        return self.post({'jsonrpc': '2.0', 'id': self.n, 'method': method, 'params': params})

    def tool(self, name, args):
        res = self.call('tools/call', {'name': name, 'arguments': args})
        if 'error' in res:
            sys.exit(f'{name}: {res["error"]}')
        return res['result']


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--host', default='192.168.50.69')
    p.add_argument('cmd', choices=['shot', 'press', 'hold'])
    p.add_argument('args', nargs='*')
    a = p.parse_args()
    m = Mcp(a.host)
    if a.cmd == 'shot':
        res = m.tool('cur_frame', {})
        for c in res.get('content', []):
            if c.get('type') == 'image':
                open(a.args[0] if a.args else 'shot.jpg', 'wb').write(base64.b64decode(c['data']))
                print('shot:', a.args[0] if a.args else 'shot.jpg')
                return
        print(json.dumps(res)[:500])
    elif a.cmd == 'press':
        for b in a.args:
            m.tool('controller', {'buttons': BUTTONS[b.upper()]})
            time.sleep(0.4)
    else:
        m.tool('controller', {'buttons': BUTTONS[a.args[0].upper()], 'long_press': int(float(a.args[1]) * 1000)})


if __name__ == '__main__':
    main()
