"""Verify packaged binaries with no Qt/Conda development paths or environment."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import json
import os
import socket
import struct
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
RUNTIME=ROOT/'delivery/runtime'
EVIDENCE=ROOT/'delivery/evidence'
sys.path.insert(0,str(ROOT/'code/tests'))
from test_chat import Peer

def loaded_modules(pid):
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    ps=ctypes.WinDLL('psapi',use_last_error=True)
    k.OpenProcess.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.DWORD]; k.OpenProcess.restype=wintypes.HANDLE
    k.CloseHandle.argtypes=[wintypes.HANDLE]
    ps.EnumProcessModules.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.HMODULE),wintypes.DWORD,ctypes.POINTER(wintypes.DWORD)]
    ps.GetModuleFileNameExW.argtypes=[wintypes.HANDLE,wintypes.HMODULE,wintypes.LPWSTR,wintypes.DWORD]
    h=k.OpenProcess(0x410,False,pid)
    if not h: raise ctypes.WinError(ctypes.get_last_error())
    try:
        mods=(wintypes.HMODULE*2048)(); needed=wintypes.DWORD()
        if not ps.EnumProcessModules(h,mods,ctypes.sizeof(mods),ctypes.byref(needed)): raise ctypes.WinError(ctypes.get_last_error())
        paths=[]
        for module in mods[:needed.value//ctypes.sizeof(wintypes.HMODULE)]:
            b=ctypes.create_unicode_buffer(32768)
            if ps.GetModuleFileNameExW(h,module,b,len(b)): paths.append(b.value)
        return paths
    finally: k.CloseHandle(h)

def main():
    EVIDENCE.mkdir(parents=True,exist_ok=True)
    env={k:v for k,v in os.environ.items() if not k.upper().startswith(('QT','QML','CONDA','PYTHON'))}
    env['PATH']=str(RUNTIME)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32')+os.pathsep+os.environ['SystemRoot']
    env['PYTHONUTF8']='1'
    records={'environment_path':env['PATH'],'platform':'Windows x64, local machine; not a second clean computer','tests':[]}
    for name,args in [('basic',['--bin-dir',str(RUNTIME)]),('chat',['--exe',str(RUNTIME/'QtTcpChat.exe')])]:
        result=subprocess.run([sys.executable,'-X','utf8',str(ROOT/f'code/tests/test_{name}.py'),*args,'-v'],env=env,capture_output=True,timeout=90)
        (EVIDENCE/f'packaged_{name}.txt').write_bytes(result.stdout+result.stderr)
        assert result.returncode==0,(name,result.stderr.decode(errors='replace'))
        records['tests'].append(name)
    with socket.socket() as probe: probe.bind(('127.0.0.1',0)); port=probe.getsockname()[1]
    processes=[]; monitor=None
    try:
        processes.append(subprocess.Popen([str(RUNTIME/'QtTcpChat.exe'),'--server','--port',str(port)],env=env))
        deadline=time.monotonic()+10
        while True:
            try: monitor=Peer(socket.create_connection(('127.0.0.1',port),timeout=2)); break
            except OSError:
                if time.monotonic()>deadline: raise
                time.sleep(.05)
        monitor.send({'type':'join','nickname':'PackageCheck'}); monitor.wait('join_ok')
        for name in ['Alice','Bob','Charlie']:
            processes.append(subprocess.Popen([str(RUNTIME/'QtTcpChat.exe'),'--connect','--port',str(port),'--nickname',name],env=env))
        monitor.wait('users',lambda m:set(m['users'])=={'PackageCheck','Alice','Bob','Charlie'},timeout=10)
        records['gui_processes']=[]
        for p in processes:
            assert p.poll() is None
            modules=loaded_modules(p.pid)
            qt=[m for m in modules if Path(m).name.lower().startswith('qt5')]
            assert any('Qt5Widgets' in m for m in qt),qt
            assert all(Path(m).parent.resolve()==RUNTIME.resolve() for m in qt),qt
            assert not any('anaconda' in m.lower() for m in modules),modules
            records['gui_processes'].append({'pid':p.pid,'qt_modules':qt})
        records['status']='PASS'
        (EVIDENCE/'runtime_verification.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
        print('PASS: packaged basic/chat tests; 4 graphical EXE processes; Qt DLLs loaded from runtime; no Anaconda modules')
    finally:
        if monitor: monitor.close()
        for p in processes:
            if p.poll() is None: p.terminate()
            p.wait(timeout=5)

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--runtime',type=Path,default=RUNTIME); parser.add_argument('--evidence-dir',type=Path,default=EVIDENCE)
    args=parser.parse_args(); RUNTIME=args.runtime.resolve(); EVIDENCE=args.evidence_dir.resolve(); main()
