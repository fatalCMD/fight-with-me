"""Sample stock HKX with an installed PyNifly reader (build dependency only)."""
import argparse
import json
import sys
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--pynifly', required=True, type=Path)
parser.add_argument('--input', required=True, type=Path)
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
sys.path.insert(0, str(args.pynifly / 'hkx'))
from anim_skyrim import load_skyrim_animation, load_skyrim_skeleton

result = {'source': 'Skyrim - Animations.bsa', 'animation': 'bow_idledrawn.hkx', 'frame': 0}
for label, skeleton, animation in [
    ('male', 'skeleton.hkx', 'bow_idledrawn.hkx'),
    ('female', 'skeleton_female.hkx', 'bow_idledrawn.hkx'),
    ('bow', 'bow-skeleton.hkx', 'bow-weapon-idledrawn.hkx'),
]:
    rig = load_skyrim_skeleton(str(args.input / skeleton))
    clip = load_skyrim_animation(str(args.input / animation))
    assert rig and clip.blend_hint == 0, 'Requires an absolute stock pose'
    mapping = clip.track_to_bone_indices or list(range(clip.num_tracks))
    assert len(mapping) == clip.num_tracks
    tracks = dict(zip(mapping, clip.tracks))
    result[label] = []
    for i, name in enumerate(rig.bones):
        ref, track = rig.reference_pose[i], tracks.get(i)
        result[label].append({'name': name, 'parent': rig.parents[i],
            't': track.translations[0] if track and track.translations else ref.translation,
            'q': track.rotations[0] if track and track.rotations else ref.rotation,
            's': track.scales[0] if track and track.scales else ref.scale})
    print(label, len(rig.bones), 'bones;', clip.num_frames, 'frames, sampled fully drawn pose')
args.output.write_text(json.dumps(result, indent=2))
