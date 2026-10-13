"""Assemble the submitted ZIP from committed source, then verify its extracted runtime."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import zipfile

REPO=Path(__file__).resolve().parents[1]
ROOT=REPO.parent
DELIVERY=ROOT/'delivery'
from create_report import TEST_LOGS, test_result

def main():
    if subprocess.check_output(['git','status','--porcelain'],cwd=REPO):
        raise RuntimeError('Commit source changes before bundling so BUILD_INFO identifies the archived source exactly.')
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=REPO,text=True).strip()
    tracked=subprocess.check_output(['git','ls-files','-z'],cwd=REPO).decode('utf-8').split('\0')
    intro='''实验6 Socket通信编程实验提交包

1. 打开 delivery/report 的报告DOCX/PDF。封面姓名、学号及最后的真实成员分工必须填写后再提交。
2. 图形演示：完整保留 delivery/runtime 目录，双击 start_server.cmd，然后启动三个 start_client.cmd，分别填不同昵称。聊天室默认8889。
3. 基础演示：在两个终端分别运行 runtime/tcp_basic_server.exe 8888 和 runtime/tcp_basic_client.exe 127.0.0.1 8888。
4. 详细演示步骤见 code/docs/DEMO.md；构建、测试、抓包与报告生成见 code/README.md。
5. 原始运行截图、测试日志、三份pcapng及tcp_analysis.md位于 delivery/evidence。
6. 已验证同机Windows x64回环通信与去开发环境PATH运行。双机局域网、全新系统和外网NAT未实测。
7. 本包未录制视频；实验要求中视频为可选项。
'''
    (DELIVERY/'开始阅读.txt').write_text(intro,encoding='utf-8')
    results=[test_result(DELIVERY/'evidence'/name) for _,name in TEST_LOGS]
    assert all(r['status']=='通过' for r in results),results
    info={'source_commit':revision,'repository':'https://github.com/SHB2233/--6','date':'2026-10-13','behavior_tests':sum(r['passed'] for r in results),'scope':'Windows x64 same-host loopback; no second-machine test','personal_fields':'姓名、学号和真实队内分工待填写'}
    (DELIVERY/'BUILD_INFO.json').write_text(json.dumps(info,ensure_ascii=False,indent=2),encoding='utf-8')
    archive=DELIVERY/'实验6_Socket通信编程实验_提交包.zip'
    prefix='EX6_Socket/'
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for relative in tracked:
            if relative and (REPO/relative).is_file(): z.write(REPO/relative,prefix+'code/'+relative)
        for folder in ['runtime','evidence','report']:
            for file in (DELIVERY/folder).rglob('*'):
                if file.is_file() and 'archive_verification' not in file.parts and file.name!='vc_redist.x64.exe':
                    z.write(file,prefix+'delivery/'+file.relative_to(DELIVERY).as_posix())
        for file in ['开始阅读.txt','BUILD_INFO.json']:
            z.write(DELIVERY/file,prefix+file)
        z.write(ROOT/'实验报告模板.docx',prefix+'实验报告模板.docx')
    extracted=ROOT/'build/archive-verification'/revision[:12]
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        z.extractall(extracted)
    extraction=extracted/'EX6_Socket'
    evidence=DELIVERY/'evidence/archive_verification'
    subprocess.run([sys.executable,'-X','utf8',str(extraction/'code/scripts/verify_runtime.py'),'--runtime',str(extraction/'delivery/runtime'),'--evidence-dir',str(evidence)],check=True,timeout=100)
    source_binary_match={}
    for role in ['server','client']:
        name=f'tcp_basic_{role}.exe'
        source_binary_match[name]=(extraction/'code'/name).read_bytes()==(extraction/'delivery/runtime'/name).read_bytes()
    assert all(source_binary_match.values())
    receipt={'status':'PASS','source_commit':revision,'archive':archive.name,'zip_crc_check':'PASS','source_runtime_basic_exe_match':source_binary_match,'extracted_runtime_verification':'PASS','scope':'same Windows computer; no clean second computer'}
    result=DELIVERY/'evidence/archive_verification.json'
    result.write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
    with zipfile.ZipFile(archive,'a',compression=zipfile.ZIP_DEFLATED) as z:
        z.write(result,prefix+'delivery/evidence/archive_verification.json')
    with zipfile.ZipFile(archive) as complete: assert complete.testzip() is None
    print(json.dumps({'archive':str(archive),'size_MiB':round(archive.stat().st_size/1024/1024,2),'source_commit':revision,'status':'PASS'},ensure_ascii=False))

if __name__=='__main__': main()
