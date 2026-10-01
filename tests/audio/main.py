# audio module check: run with tools/run-ppsspp.sh --files tests/audio <EBOOT>.
# Writes its own WAV files, plays them and tone.mp3 (a 1 s, 440 Hz tone made
# for this test), and checks what the mixer reports and how long things take
# (headless PPSSPP keeps emulated time, so durations hold). Each check prints
# "audiotest: <name>: ok" or "... FAIL <detail>", then a summary line that CI
# greps for.
import math
import struct
import time

import audio

_failures = 0


def _result(name, ok, detail=""):
    global _failures
    if ok:
        print("audiotest: {}: ok".format(name))
    else:
        _failures += 1
        print("audiotest: {}: FAIL {}".format(name, detail))


def _check(name, fn):
    try:
        ok, detail = fn()
    except Exception as e:
        ok, detail = False, "{}: {}".format(type(e).__name__, e)
    _result(name, ok, detail)


def _raises(exc, fn):
    try:
        fn()
    except exc as e:
        return True, str(e)
    return False, "no {}".format(exc.__name__)


def write_wav(name, seconds, rate=22050, channels=1, bits=16, freq=440):
    frames = int(rate * seconds)
    data = bytearray(frames * channels * bits // 8)
    for i in range(frames):
        v = math.sin(2 * math.pi * freq * i / rate) * 0.5
        for c in range(channels):
            at = (i * channels + c) * bits // 8
            if bits == 16:
                struct.pack_into("<h", data, at, int(v * 32767))
            else:
                data[at] = int(128 + v * 127)
    block = channels * bits // 8
    with open(name, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * block, block, bits))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def wait(ms):
    time.sleep_ms(ms)


write_wav("half.wav", 0.5)
write_wav("long.wav", 2.0)
write_wav("stereo8.wav", 0.3, rate=11025, channels=2, bits=8)
with open("bad.wav", "wb") as f:
    f.write(b"RIFF\0\0\0\0WAVEjunk")

_check("voices", lambda: (audio.VOICES == 8, audio.VOICES))


def plays_then_ends():
    s = audio.play("half.wav")
    a = s.playing
    wait(300)
    b = s.playing
    wait(500)
    return (a, b, s.playing) == (True, True, False), repr((a, b, s.playing))
_check("wav plays for its length", plays_then_ends)


def loops():
    s = audio.play("half.wav", loop=True)
    wait(1200)
    a = s.playing
    s.stop()
    return (a, s.playing) == (True, False), repr((a, s.playing))
_check("loop and stop", loops)


def pause_resume():
    s = audio.play("long.wav")
    wait(200)
    s.pause()
    a = s.playing
    wait(2500)   # longer than the sound: paused, it mustn't finish
    s.resume()
    b = s.playing
    s.stop()
    return (a, b) == (False, True), repr((a, b))
_check("pause and resume", pause_resume)


def volume():
    s = audio.play("long.wav", volume=0.5)
    a = s.volume
    s.volume = 0.25
    b = s.volume
    s.stop()
    return abs(a - 0.5) < 0.01 and abs(b - 0.25) < 0.01, repr((a, b))
_check("sound volume", volume)
_check("stereo 8-bit wav", lambda: (audio.play("stereo8.wav").playing, ""))


def oldest_stops():
    audio.stop()
    sounds = [audio.play("half.wav", loop=True) for _ in range(9)]
    states = [s.playing for s in sounds]
    audio.stop()
    after = [s.playing for s in sounds]
    return states == [False] + [True] * 8 and not any(after), repr((states, after))
_check("9th sound stops the oldest", oldest_stops)


def stream_paces():
    out = audio.Stream(rate=22050, channels=1, bits=16)
    second = bytes(22050 * 2)
    t = time.ticks_ms()
    out.write(second)         # first 0.5 s fills the buffer at once
    out.write(second)         # then it waits for the mixer
    ms = time.ticks_diff(time.ticks_ms(), t)
    space = out.space()
    out.close()
    # 2 s written into a 0.5 s buffer: about 1.5 s of waiting.
    return 1200 <= ms <= 1800 and space >= 0, "{} ms, space {}".format(ms, space)
_check("stream paces writes", stream_paces)


def mp3_plays():
    s = audio.play("tone.mp3")
    a = s.playing
    wait(1600)
    return (a, s.playing) == (True, False), repr((a, s.playing))
_check("mp3 plays for its length", mp3_plays)


def mp3_loops():
    s = audio.play("tone.mp3", loop=True)
    wait(2500)
    a = s.playing
    s.stop()
    return a, ""
_check("mp3 loops", mp3_loops)


def mp3_slots():
    a = audio.play("tone.mp3", loop=True)
    b = audio.play("tone.mp3", loop=True)
    c = audio.play("tone.mp3", loop=True)   # only 2 decoders: a stops
    states = (a.playing, b.playing, c.playing)
    audio.stop()
    return states == (False, True, True), repr(states)
_check("3rd mp3 stops the oldest mp3", mp3_slots)

_check("master volume", lambda: (abs(audio.volume(0.5) - 0.5) < 0.01 and abs(audio.volume() - 0.5) < 0.01, ""))
audio.volume(1.0)
_check("rejects other formats", lambda: _raises(ValueError, lambda: audio.play("song.ogg")))
_check("missing file", lambda: _raises(OSError, lambda: audio.play("missing.wav")))
_check("bad wav", lambda: _raises(ValueError, lambda: audio.play("bad.wav")))
_check("volume range", lambda: _raises(ValueError, lambda: audio.play("half.wav", volume=2)))
_check("stream args", lambda: _raises(ValueError, lambda: audio.Stream(rate=22050, channels=3)))

for name in ("half.wav", "long.wav", "stereo8.wav", "bad.wav"):
    import os
    os.remove(name)
print("audiotest: {}".format("PASS" if _failures == 0 else "FAIL ({} failed)".format(_failures)))
