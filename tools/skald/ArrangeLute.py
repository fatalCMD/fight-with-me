"""Make faster, pitch-preserved phrases from the installed game's real lute takes.

Requires NumPy and imageio-ffmpeg (PyPI). research.cjs extracts the four stock WAVs.
These are Skyrim-derived recordings for use with Skyrim, not original compositions.
"""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import wave
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'build/skald-research'
sys.path.insert(0,str(WORK/'audio-tools'))
import imageio_ffmpeg

# Quarter-note grids fitted to spectral attack energy in the opening 14 seconds.
# Keep the original performance's pitch, dynamics, string scrape and body resonance.
SOURCES=[('Fervor',101.,.16,138.),('Echoes',87.4,.60,126.),
         ('Hollow',89.6,.03,126.),('Glory',100.2,.59,132.)]

def main():
    out=ROOT/'assets/sound/fx/FightWithMe/Skald';out.mkdir(parents=True,exist_ok=True)
    tracks=[];provenance=[]
    for index,(song,source_bpm,source_phase,bpm) in enumerate(SOURCES):
        source=WORK/f'mus_bardlute_{index+1:02d}.wav'
        factor=bpm/source_bpm;phase=round(.08+source_phase/factor,3)
        seconds=round(phase+20*60/bpm,3)
        raw=WORK/f'retimed-{song}.wav'
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-hide_banner','-loglevel','error','-y',
                        '-i',str(source),'-af',f'atempo={factor:.9f}',
                        '-t',str(seconds+.25),'-ar','44100','-ac','1','-c:a','pcm_s16le',str(raw)],check=True)
        with wave.open(str(raw)) as reader:
            audio=np.frombuffer(reader.readframes(reader.getnframes()),dtype='<i2').astype(float)/32768
        signal=np.zeros(round((seconds+.25)*44100));lead=round(.08*44100)
        available=min(len(audio),len(signal)-lead);signal[lead:lead+available]=audio[:available]
        signal*=.88/max(np.abs(signal).max(),1e-9)
        # Preserve the recorded plucks; only soften the excerpt boundaries.
        attack=round(.008*44100);signal[lead:lead+attack]*=np.linspace(0,1,attack)
        tail=round(.30*44100);signal[-tail:]*=np.linspace(1,0,tail)**2
        file='Skald'+song+'.wav'
        with wave.open(str(out/file),'wb') as writer:
            writer.setparams((1,2,44100,len(signal),'NONE','not compressed'))
            writer.writeframes(np.rint(signal*32767).astype('<i2').tobytes())
        tracks.append(dict(song=song,file=file,label=f'{song} · acoustic lute',bpm=bpm,phase=phase))
        provenance.append(dict(song=song,source=source.name,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                               source_bpm=source_bpm,source_phase=source_phase,tempo_factor=factor,
                               output=file,sha256=hashlib.sha256((out/file).read_bytes()).hexdigest(),
                               seconds=seconds,peak=float(np.abs(signal).max()),rms=float(np.sqrt(np.mean(signal**2)))))
        print(f'{song}: real lute, pitch unchanged, {factor:.2f}x tempo, {bpm:g} BPM, {seconds}s')
    (ROOT/'tools/skald/music-selection.json').write_text(json.dumps(dict(method='Pitch-preserved faster phrases from installed Skyrim lute recordings; two quarter-note beats per wave.',tracks=tracks),indent=2)+'\n')
    (ROOT/'tools/skald/arrangements.json').write_text(json.dumps(dict(provenance='Derived from Bethesda Skyrim lute recordings, for use with Skyrim. Tempo changed with FFmpeg atempo; no oscillators or added instruments.',tracks=provenance),indent=2)+'\n')

if __name__=='__main__':main()
