"""Record the actual basic client/server conversation and its TCP packets."""
from pathlib import Path
import argparse
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'delivery/evidence'

def main(wireshark_dir):
    capture=subprocess.Popen([str(wireshark_dir/'dumpcap.exe'),'-i',r'\Device\NPF_Loopback','-f','tcp port 18888','-a','duration:5','-w',str(EVIDENCE/'basic_session.pcapng')],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    server=None
    try:
        ready=capture.stderr.readline()
        assert b'Capturing on' in ready,ready
        server=subprocess.Popen([str(ROOT/'build/basic/tcp_basic_server.exe'),'18888'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
        server.stdin.write('你好客户端，第一条消息已收到。\n第二条消息收到，TCP双向通信正常。\n'); server.stdin.flush()
        time.sleep(.15)
        client=subprocess.run([str(ROOT/'build/basic/tcp_basic_client.exe'),'127.0.0.1','18888'],input='你好服务器，这是第一条中文消息。\n第二条消息：Socket实验6。\n/quit\n',text=True,encoding='utf-8',capture_output=True,timeout=8)
        out,err=server.communicate(timeout=8)
        assert client.returncode==0 and server.returncode==0,(client.stderr,err)
        assert '第一条消息已收到' in client.stdout and 'Socket实验6' in out
        record='实际执行：tcp_basic_server.exe 18888 / tcp_basic_client.exe 127.0.0.1 18888\n\n[客户端标准输出]\n'+client.stdout+'\n[服务器标准输出]\n'+out+'\n[标准错误]\n'+client.stderr+err
        (EVIDENCE/'basic_conversation.txt').write_text(record,encoding='utf-8')
        tail=capture.communicate(timeout=10)[1]
        assert capture.returncode==0,tail
        (EVIDENCE/'basic_capture.log').write_bytes(ready+tail)
        print(record)
    finally:
        if server and server.poll() is None: server.kill(); server.wait()
        if capture.poll() is None: capture.terminate(); capture.wait()

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--wireshark-dir',type=Path,default=Path(r'F:\Wireshark'))
    main(parser.parse_args().wireshark_dir)
