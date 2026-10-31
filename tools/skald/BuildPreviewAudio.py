"""Preview the identical retimed acoustic lute WAVs shipped with the mod."""
from pathlib import Path
import hashlib
import json
import shutil
import wave
ROOT=Path(__file__).resolve().parents[2]
for i,track in enumerate(json.loads((ROOT/'tools/skald/beat-map.json').read_text())):
    source=ROOT/'assets/sound/fx/FightWithMe/Skald'/track['file']
    assert hashlib.sha256(source.read_bytes()).hexdigest()==track['sha256']
    with wave.open(str(source)) as reader:
        assert reader.getnchannels()==1 and reader.getsampwidth()==2 and reader.getframerate()==44100
        assert abs(reader.getnframes()/reader.getframerate()-track['seconds']-.25)<.001
    shutil.copyfile(source,ROOT/f'tools/wolfhead/preview/out/skald-{i}.wav')
    print(track['label'])
