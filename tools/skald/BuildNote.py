"""Extract one short acoustic pluck from the installed Skyrim lute recording.

No pitch change, synthesized instrument, or added accompaniment. research.cjs
extracts the source WAV from the user's Skyrim installation.
"""
from pathlib import Path
import hashlib
import json
import wave
import numpy as np

root = Path(__file__).resolve().parents[2]
source = root / 'build/skald-research/mus_bardlute_03.wav'
with wave.open(str(source)) as reader:
    assert reader.getnchannels() == 1 and reader.getsampwidth() == 2
    rate = reader.getframerate()
    audio = np.frombuffer(reader.readframes(reader.getnframes()), '<i2').astype(float)

# The opening pluck begins around 0.07 s; the following attack is at 0.93 s.
note = audio[round(.04 * rate):round(.84 * rate)].copy()
note *= .75 * 32767 / max(float(np.abs(note).max()), 1)
attack, tail = round(.004 * rate), round(.18 * rate)
note[:attack] *= np.linspace(0, 1, attack)
note[-tail:] *= np.linspace(1, 0, tail) ** 2
destination = root / 'assets/sound/fx/FightWithMe/Skald/SkaldNote.wav'
with wave.open(str(destination), 'wb') as writer:
    writer.setparams((1, 2, rate, len(note), 'NONE', 'not compressed'))
    writer.writeframes(np.rint(note).astype('<i2').tobytes())
(root / 'tools/skald/note-audio.json').write_text(json.dumps({
    'source': source.name, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'start': .04, 'end': .84, 'seconds': len(note) / rate,
    'provenance': 'Bethesda Skyrim lute recording, excerpted for use with Skyrim.',
    'output_sha256': hashlib.sha256(destination.read_bytes()).hexdigest()
}, indent=2) + '\n')
print(f'{destination.name}: {len(note) / rate:.2f}s acoustic lute pluck, {rate}Hz mono PCM16')
