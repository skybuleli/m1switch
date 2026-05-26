#!/usr/bin/env python3
"""读取 .nro.trace 二进制文件并显示最后 N 条 IPC/SVC 事件"""
import struct, sys

def read_trace(path, n_last=30):
    with open(path, 'rb') as f:
        data = f.read()
    
    events = []
    pos = 0
    while pos + 32 <= len(data):
        hdr = struct.unpack_from('<IIQQ', data, pos)
        magic, size, tid, ts = hdr
        if magic != 0xE1E2E3E4:
            pos += 1
            continue
        # channel = (magic & 0xFF) or from another field
        if pos + size > len(data):
            break
        ev_data = data[pos+24:pos+size]
        events.append({'ts': ts, 'size': size, 'data': ev_data})
        pos += size
    
    # Filter and show last N
    ipc_events = []
    svc_events = []
    for e in events:
        d = e['data']
        if len(d) >= 8:
            # SVC events start with specific markers
            if d[0] == 0x29:  # SVC #0x29 = GetInfo
                svc_events.append(f"[{e['ts']}] SVC #0x29 GetInfo")
            elif d[0] == 0x21 and len(d) >= 12:  # SVC #0x21 = SendSyncRequest
                session = struct.unpack_from('<I', d, 4)[0]
                cmd = struct.unpack_from('<I', d, 8)[0]
                svc_events.append(f"[{e['ts']}] SVC #0x21 session=0x{session:x} cmd={cmd}")
            elif d[0] == 0x01:  # SetMemoryPermission
                svc_events.append(f"[{e['ts']}] SVC #0x01 SetMemPerm")
    
    print(f"Total events: {len(events)}")
    print(f"SVC events: {len(svc_events)}")
    print(f"\nLast {min(n_last, len(svc_events))} SVC calls:")
    for ev in svc_events[-n_last:]:
        print(f"  {ev}")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "/Users/liliang/m1switch/hello_colours.nro.trace"
    read_trace(path, int(sys.argv[2]) if len(sys.argv) > 2 else 30)
