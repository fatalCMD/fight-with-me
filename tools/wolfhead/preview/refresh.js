// One step after any change to the wolf head, its bite, the healer's circle or Elemental Burst's blasts: rebuilds the
// meshes, the samplers (the plugin's own src/WolfBite.cpp, HealCircle.cpp and Blast.cpp), and the preview's data. Then
// reload the page.
//   node tools\wolfhead\preview\refresh.js
const path = require('path');
const { execFileSync } = require('child_process');

const repo = path.resolve(__dirname, '..', '..', '..');
const cmake = 'X:/VisualStudio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe';
const run = (file, args, options = {}) => execFileSync(file, args, { stdio: 'inherit', cwd: repo, ...options });

if (process.argv.includes('--tank-only')) {
  run(process.execPath, [path.join(repo, 'tools/weakwill/BuildWeakWill.js')]);
  run(process.execPath, [path.join(repo, 'tools/bastion/BuildBastion.js')]);
  run(cmake, ['--build', path.join(repo, 'build/skyrim'), '--config', 'Release', '--target', 'FightWithMeWeakWillLook', 'FightWithMeBastionLook'], {env:{...process.env,VCPKG_ROOT:'X:/vcpkg'}});
  run(process.execPath, [path.join(__dirname, 'build.js'), '--tank-only']);
  console.log('Tank previews refreshed.'); process.exit(0);
}
if (process.argv.includes('--chaos-only')) {
  run(process.execPath, [path.join(repo, 'tools', 'chaosdown', 'BuildChaosBinding.js')]);
  run(cmake, ['--build', path.join(repo, 'build', 'skyrim'), '--config', 'Release', '--target', 'FightWithMeChaosLook'],
    { env: { ...process.env, VCPKG_ROOT: 'X:\\vcpkg' } });
  run(process.execPath, [path.join(__dirname, 'build.js'), '--chaos-only']);
  console.log('Chaos Down preview refreshed: reload http://127.0.0.1:8471/?scene=chaos');
  process.exit(0);
}
run(process.execPath, [path.join(repo, 'tools', 'chaosdown', 'BuildChaosBinding.js')]);
run(process.execPath, [path.join(repo, 'tools', 'wolfhead', 'BuildWolfHead.js')]);
run(process.execPath, [path.join(repo, 'tools', 'healcircle', 'BuildHealCircle.js')]);
run(process.execPath, [path.join(repo, 'tools', 'rejuvenation', 'BuildRejuvenation.js')]);
run(process.execPath, [path.join(repo, 'tools', 'blast', 'BuildBlast.js')]);
run(process.execPath, [path.join(repo, 'tools', 'magnus', 'BuildMagnus.js')]);
run(process.execPath, [path.join(repo, 'tools', 'quake', 'BuildQuake.js')]);
run(process.execPath, [path.join(repo, 'tools', 'sunder', 'BuildSunder.js')]);
run(cmake, ['--build', path.join(repo, 'build', 'skyrim'), '--config', 'Release', '--target', 'FightWithMeWolfPose', 'FightWithMeCircleLook', 'FightWithMeBlastLook',
  'FightWithMeMagnusLook', 'FightWithMeQuakeLook', 'FightWithMeRejuvenationLook', 'FightWithMeSunderLook', 'FightWithMeChaosLook'],
  { env: { ...process.env, VCPKG_ROOT: 'X:\\vcpkg' } });
run(process.execPath, [path.join(repo, 'tools/weakwill/BuildWeakWill.js')]);
run(process.execPath, [path.join(repo, 'tools/bastion/BuildBastion.js')]);
run(cmake, ['--build', path.join(repo, 'build/skyrim'), '--config', 'Release', '--target', 'FightWithMeWeakWillLook', 'FightWithMeBastionLook'], {env:{...process.env,VCPKG_ROOT:'X:/vcpkg'}});
run(process.execPath, [path.join(__dirname, 'build.js')]);
console.log('Preview data refreshed: reload http://127.0.0.1:8471');
