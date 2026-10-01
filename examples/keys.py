# Keys: the buttons play notes, for as long as you hold them, using the
# audio module. Hold L for an octave lower, R for higher. START finishes.
import math

import audio
import psp

RATE = 22050
NOTES = {  # button: (name, frequency in Hz)
    psp.LEFT: ("C", 261.63), psp.UP: ("D", 293.66), psp.RIGHT: ("E", 329.63),
    psp.DOWN: ("F", 349.23), psp.SQUARE: ("G", 392.00), psp.TRIANGLE: ("A", 440.00),
    psp.CIRCLE: ("B", 493.88), psp.CROSS: ("C", 523.25),
}


def tone(freq):
    # A few whole cycles of a softened square wave, as 16-bit samples, which
    # repeat without a click.
    cycles = max(1, round(freq * 0.02))
    frames = round(RATE * cycles / freq)
    out = bytearray(frames * 2)
    for i in range(frames):
        x = math.sin(2 * math.pi * cycles * i / frames)
        v = int(9000 * (x + x * x * x / 3))
        out[i * 2] = v & 255
        out[i * 2 + 1] = (v >> 8) & 255
    return bytes(out)


print("Keys: d-pad and buttons play notes, L/R change octave, START finishes.")
print("Making the notes...")
tones = {}
for button, (name, freq) in NOTES.items():
    for octave in (0.5, 1, 2):
        tones[button, octave] = tone(freq * octave)
print("Ready.")

# A short buffer, so notes start and stop promptly.
out = audio.Stream(rate=RATE, channels=1, bits=16, buffer_ms=60)
playing = None
while psp.START not in psp.pressed():
    held = psp.held()
    octave = 0.5 if psp.L in held else 2 if psp.R in held else 1
    note = next((b for b in NOTES if b in held), None)
    if note != playing:
        if note is not None:
            print(NOTES[note][0], {0.5: "(low)", 1: "", 2: "(high)"}[octave])
        playing = note
    if note is not None:
        wave = tones[note, octave]
        while out.space() >= len(wave):
            out.write(wave)
    psp.vsync()
out.close()
