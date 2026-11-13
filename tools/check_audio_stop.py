"""Hardware regression: python3 tools/check_audio_stop.py http://IP_ESP32."""
import json
import sys
import time
from urllib.request import Request, urlopen

base = sys.argv[1].rstrip('/')


def status():
    with urlopen(base + '/api/status', timeout=50) as response:
        return json.load(response)


initial = status()


def post(path, data):
    request = Request(base + '/api/' + path, data=json.dumps(data).encode(),
                      headers={'Content-Type': 'application/json',
                               'X-Carevision-Token': initial['token']})
    with urlopen(request, timeout=10) as response:
        return json.load(response)


post('auto-listen', {'enabled': False})
try:
    for _ in range(2):
        post('test', {'action': 'speaker'})
        deadline = time.monotonic() + 25
        while True:
            time.sleep(.5)
            current = status()
            assert current['token'] == initial['token'], 'Device rebooted during playback'
            if current['speakerStatus'] == 'finished':
                assert current['speakerBytes'] > 0, 'No MP3 bytes consumed'
                break
            assert current['speakerStatus'] not in ('download-failed', 'decoder-failed', 'no-audio'), current['speakerStatus']
            assert time.monotonic() < deadline, 'Playback did not finish'
    for _ in range(2):
        post('test', {'action': 'speaker'})
        time.sleep(.4)
        post('stop', {})
        deadline = time.monotonic() + 8
        while status()['voiceState'] == 'speaking':
            assert time.monotonic() < deadline, 'Speaker failed to stop within 8 seconds'
            time.sleep(.2)
    print('PASS: complete playback twice without reboot; speaker stop twice')
finally:
    post('stop', {})
    post('auto-listen', {'enabled': initial['autoListen']})
