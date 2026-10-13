"""Create an app-local Windows runtime and resolve Conda Qt's DLL dependencies."""
import argparse
import json
from pathlib import Path
import os
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]

def package(qt_bin, dumpbin, redist):
    runtime = ROOT / 'delivery' / 'runtime'
    runtime.mkdir(parents=True, exist_ok=True)
    for origin in [ROOT/'build/qt/release/QtTcpChat.exe', *[(ROOT/'build/basic')/f'tcp_basic_{role}.exe' for role in ('server','client')]]:
        shutil.copy2(origin, runtime/origin.name)
    # Qt deployment locates platform, image-format and style plugins, plus ICU/OpenGL.
    subprocess.run([str(qt_bin/'windeployqt.exe'), '--release', '--no-translations', '--no-system-d3d-compiler', '--dir', str(runtime), str(runtime/'QtTcpChat.exe')], check=True)
    redist_dirs = list(redist.glob('**/x64/Microsoft.VC*.CRT'))
    search = [*redist_dirs, qt_bin, qt_bin.parent.parent, qt_bin.parent.parent/'DLLs']
    system = Path(os.environ['SystemRoot'])/'System32'
    checked = set()
    while True:
        candidates = [p for p in runtime.rglob('*') if p.suffix.lower() in ('.exe','.dll') and p.name.lower() not in checked]
        if not candidates: break
        for binary in candidates:
            checked.add(binary.name.lower())
            result = subprocess.run([str(dumpbin), '/DEPENDENTS', str(binary)], capture_output=True, check=True)
            names = re.findall(r'^\s+([A-Za-z0-9_.-]+\.dll)\s*$', result.stdout.decode('utf-8', errors='replace'), re.M | re.I)
            for name in names:
                if (runtime/name).exists(): continue
                if name.lower().startswith(('api-ms-', 'ext-ms-')): continue
                origin = next((p/name for p in search if (p/name).is_file()), None)
                if origin: shutil.copy2(origin, runtime/name)
                elif not (system/name).is_file(): raise RuntimeError(f'Unresolved dependency: {binary.name} -> {name}')
    (runtime/'qt.conf').write_text('[Paths]\nPlugins=.\n', encoding='utf-8')
    (runtime/'start_server.cmd').write_text('@echo off\ncd /d "%~dp0"\nstart "TCP Server" QtTcpChat.exe --server\n', encoding='ascii')
    (runtime/'start_client.cmd').write_text('@echo off\ncd /d "%~dp0"\nstart "TCP Client" QtTcpChat.exe\n', encoding='ascii')
    # Keep the original notices supplied with the locally installed DLL packages.
    prefix = qt_bin.parent.parent
    names = {p.name.lower() for p in runtime.rglob('*.dll')}
    packages = []
    for metadata in (prefix/'conda-meta').glob('*.json'):
        record = json.loads(metadata.read_text(encoding='utf-8'))
        owned = sorted({Path(p).name for p in record.get('files', []) if Path(p).name.lower() in names})
        if not owned: continue
        package = f"{record['name']}-{record['version']}-{record['build']}"
        original = prefix/'pkgs'/package/'info/licenses'
        for license_file in original.rglob('*'):
            if license_file.is_file():
                target = runtime/'licenses'/record['name']/license_file.relative_to(original)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(license_file,target)
        packages.append({'package':package,'license':record.get('license','See package license'),'dlls':owned})
    (runtime/'third_party_packages.json').write_text(json.dumps(packages,ensure_ascii=False,indent=2),encoding='utf-8')
    (runtime/'THIRD_PARTY_NOTICES.txt').write_text('Qt and its dependencies are distributed as replaceable shared DLLs. Original available package license texts are in licenses/. Package versions and DLL ownership are in third_party_packages.json. Qt source: https://download.qt.io/archive/qt/5.15/5.15.2/single/ ; Conda Qt recipe: https://github.com/AnacondaRecipes/qt-main-feedstock . Microsoft Visual C++ runtime DLLs originate from the installed Visual Studio redistributable.\n',encoding='utf-8')
    print(f'Runtime: {runtime}; {len(list(runtime.rglob("*.dll")))} DLLs')

if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--qt-bin', type=Path, required=True)
    p.add_argument('--dumpbin', type=Path, required=True)
    p.add_argument('--redist', type=Path, required=True)
    a=p.parse_args(); package(a.qt_bin,a.dumpbin,a.redist)
