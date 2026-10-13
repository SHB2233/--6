"""Analyze only the experiment captures using TShark's actual TCP reassembly."""
from pathlib import Path
import argparse
import json
import re
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[2]
E=ROOT/'delivery/evidence'
TSHARK=r'F:\Wireshark\tshark.exe'

def tshark(*args):
    return subprocess.run([TSHARK,*map(str,args)],capture_output=True,check=True).stdout.decode('utf-8',errors='replace')

def analyze(file):
    fields=['frame.number','tcp.stream','tcp.srcport','tcp.dstport','tcp.flags','tcp.len']
    args=['-r',E/file,'-T','fields']
    for field in fields: args+=['-e',field]
    table=tshark(*args)
    (E/(Path(file).stem+'_packets.tsv')).write_text('\t'.join(fields)+'\n'+table,encoding='utf-8')
    packets=[]
    for line in table.splitlines():
        n,stream,src,dst,flags,length=line.split('\t')
        packets.append(dict(frame=int(n),stream=int(stream),src=int(src),dst=int(dst),flags=int(flags,16),length=int(length)))
    streams=[]
    for index in sorted({p['stream'] for p in packets}):
        rows=[p for p in packets if p['stream']==index]
        raw=tshark('-r',E/file,'-q','-z',f'follow,tcp,raw,{index}')
        (E/(Path(file).stem+f'_stream{index}.txt')).write_text(raw,encoding='utf-8')
        buffers=[bytearray(),bytearray()]
        for line in raw.splitlines():
            if re.fullmatch(r'\t?[0-9a-fA-F]+',line): buffers[int(line.startswith('\t'))].extend(bytes.fromhex(line.strip()))
        messages=[]
        for direction,buffer in enumerate(buffers):
            while buffer:
                assert len(buffer)>=4,(file,index,'incomplete prefix')
                size=struct.unpack('!I',buffer[:4])[0]
                assert 0<size<=10*1024*1024 and len(buffer)>=4+size,(file,index,'incomplete body')
                body=bytes(buffer[4:4+size]).decode('utf-8'); del buffer[:4+size]
                try: body=json.loads(body)
                except json.JSONDecodeError: pass
                messages.append({'direction':direction,'body':body})
        node0=re.search(r'^Node 0: (.+)$',raw,re.M).group(1).strip()
        node1=re.search(r'^Node 1: (.+)$',raw,re.M).group(1).strip()
        syn=[p['frame'] for p in rows if p['flags']&2 and not p['flags']&16]
        synack=[p['frame'] for p in rows if p['flags']&2 and p['flags']&16]
        ack=next((p['frame'] for p in rows if synack and p['frame']>synack[0] and p['flags']&16 and not p['flags']&2),None)
        streams.append(dict(stream=index,node0=node0,node1=node1,syn_frames=syn,syn_ack_frames=synack,first_ack_frame=ack,
            fin_frames=[p['frame'] for p in rows if p['flags']&1],rst_frames=[p['frame'] for p in rows if p['flags']&4],
            data_frames=[p['frame'] for p in rows if p['length']],payload_types=sorted({m['body'].get('type','') for m in messages if isinstance(m['body'],dict)}),messages=messages))
    return dict(file=file,packet_count=len(packets),streams=streams)

def main():
    captures=[analyze(n) for n in ['basic_session.pcapng','chat_session.pcapng','direct_call.pcapng']]
    streams=[s for c in captures for s in c['streams']]
    direct=captures[2]['streams'][0]
    server_types=set(t for s in captures[1]['streams'] for t in s['payload_types'])
    assert {'call_hello','call_ready','call_chat','call_end'} <= set(direct['payload_types'])
    assert 'call_chat' not in server_types
    accepted=[m['body'] for s in captures[1]['streams'] for m in s['messages'] if isinstance(m['body'],dict) and m['body'].get('type')=='call_accepted']
    assert accepted and int(direct['node1'].rsplit(':',1)[1])==accepted[0]['port']
    result={'status':'PASS','capture_scope':'Only the experiment TCP ports on Npcap Loopback; same Windows host. Separate files have independent tcp.stream numbering.',
        'summary':{'tcp_streams':len(streams),'handshakes':sum(bool(s['syn_frames'] and s['syn_ack_frames'] and s['first_ack_frame']) for s in streams),
            'fin_closed_streams':sum(bool(s['fin_frames']) for s in streams),'direct_call_port':accepted[0]['port'],
            'direct_call_payload_types':direct['payload_types'],'server_payload_has_call_chat':False},'captures':captures}
    (E/'capture_analysis.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    lines=['# 实验6 TCP抓包分析','', '数据来自实际实验程序；仅抓取基础端口18888、聊天端口18889和本次被叫端动态端口。三份文件的tcp.stream编号各自独立。','']
    for capture in captures:
        lines += [f'## {capture["file"]}',f'共 {capture["packet_count"]} 个TCP包。','', '| TCP流 | 端点 | SYN / SYN-ACK / ACK帧 | 有数据的帧 | FIN帧 | RST帧 |','|---|---|---|---|---|---|']
        for s in capture['streams']:
            lines.append(f'| {s["stream"]} | {s["node0"]} ↔ {s["node1"]} | {s["syn_frames"]} / {s["syn_ack_frames"]} / {s["first_ack_frame"]} | {s["data_frames"]} | {s["fin_frames"]} | {s["rst_frames"]} |')
        lines.append('')
    lines += ['## 应用层与TCP字节流','', '基础程序使用4字节大端长度和UTF-8文字。抓包中长度字段与正文多次分别进入不同TCP段，程序仍按长度正确还原，说明一次recv或一个TCP包不等于一条消息。',
        'Qt程序使用相同长度前缀，正文为JSON。Follow TCP Stream重组后可读出join、群组、private和file消息；空文件的data为空字符串，接收后文件大小为0字节。','',
        '## 真正的客户端直连','',f'服务器call_accepted消息返回被叫地址127.0.0.1、端口{accepted[0]["port"]}。独立捕获的端点为{direct["node0"]} ↔ {direct["node1"]}，没有使用聊天室监听端口18889。',
        f'该流握手帧为{direct["syn_frames"]} / {direct["syn_ack_frames"]} / {direct["first_ack_frame"]}；重组正文依次包含call_hello、call_ready、双向call_chat和call_end。聊天服务器的全部流不含call_chat。这共同支持通话文字通过独立TCP连接直接传输。',
        f'直连关闭出现FIN帧{direct["fin_frames"]}，RST帧{direct["rst_frames"]}。ACK可与其他标志合并，不能强行把每次关闭都解释为恰好四个包。','',
        '## 复核方法','', '在Wireshark打开pcapng，使用tcp.stream eq N筛选，并选择“追踪TCP流”。逐包查看Flags、端口和Seq/Ack；原始字段导出位于*_packets.tsv，TShark重组结果位于*_streamN.txt。',
        '例如：F:\\Wireshark\\tshark.exe -r direct_call.pcapng -q -z follow,tcp,raw,0','',
        '## 验证范围','', '此次为同一台Windows电脑上的回环测试。它验证程序通信、消息分帧和连接拓扑；没有据此声称跨电脑局域网或外网NAT环境已通过。']
    (E/'tcp_analysis.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps(result['summary'],ensure_ascii=False))

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--wireshark-dir',type=Path,default=Path(r'F:\Wireshark'))
    TSHARK=str(parser.parse_args().wireshark_dir/'tshark.exe'); main()
