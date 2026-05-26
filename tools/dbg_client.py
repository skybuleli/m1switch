#!/usr/bin/env python3
"""
M1Switch Debug Client — 通过 Unix socket 连接 DebugServer 交互调试

用法:
  python3 tools/dbg_client.py                          # 交互模式
  python3 tools/dbg_client.py regs                     # 单条命令
  python3 tools/dbg_client.py "read 0x340653000 64"    # 读内存
  python3 tools/dbg_client.py trace svc on             # 开启 SVC trace

命令列表:
  regs                     — 读全部 CPU 寄存器
  read <addr> <len>        — 读 guest 内存
  write <addr> <hexbytes>  — 写 guest 内存
  break <addr>             — 设断点
  continue                 — 继续执行
  step                     — 单步
  pause                    — 暂停
  svc_trace [N]            — 最近 N 条 SVC (默认 20)
  ipc_trace [N]            — 最近 N 条 IPC
  gpu_trace [N]            — 最近 N 条 GPU
  threads                  — 列出线程
  trace <ch> on/off        — 开关 trace 通道 (svc/ipc/gpu/thread/mem)
  snap                     — 取快照
  stats                    — trace 统计
  help                     — 帮助
  quit                     — 退出
"""

import json, socket, sys, os

SOCKET_PATH = "/tmp/m1switch_debug.sock"

def send_cmd(cmd_dict):
    """发送 JSON 命令到 DebugServer，返回响应"""
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(5)
        s.connect(SOCKET_PATH)
        s.sendall((json.dumps(cmd_dict) + "\n").encode())
        resp_data = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            resp_data += chunk
            if b"\n" in resp_data:
                break
        s.close()
        return json.loads(resp_data.decode())
    except ConnectionRefusedError:
        return {"success": False, "error": "连接被拒绝 — DebugServer 是否在运行？"}
    except FileNotFoundError:
        return {"success": False, "error": f"找不到 socket {SOCKET_PATH} — 先启动 headless runner"}
    except Exception as e:
        return {"success": False, "error": str(e)}

def format_regs(data):
    """格式化寄存器输出"""
    if isinstance(data, str):
        data = json.loads(data)
    lines = []
    if "x0" in data:
        for i in range(0, 30, 4):
            row = []
            for j in range(4):
                if i + j <= 29:
                    val = data.get(f"x{i+j}", 0)
                    row.append(f"x{i+j:02d}=0x{val:016x}")
            lines.append("  ".join(row))
        lines.append(f"sp=0x{data.get('sp',0):016x}  pc=0x{data.get('pc',0):016x}")
        if "pstate" in data:
            lines.append(f"pstate=0x{data['pstate']:08x}")
    return "\n".join(lines)

def format_trace(events):
    """格式化 trace 事件输出"""
    lines = []
    for ev in events:
        ts = ev.get("timestamp", 0)
        ch = ev.get("channel", "?")
        data = ev.get("data", {})
        if ch == "SVC":
            lines.append(f"[{ts:09d}] SVC #{data.get('num',0):02x}  "
                         f"x0=0x{data.get('x0',0):x}  x1=0x{data.get('x1',0):x}")
        elif ch == "IPC":
            lines.append(f"[{ts:09d}] IPC  cmd={data.get('cmd_id',0)}  "
                         f"session=0x{data.get('session',0):x}  "
                         f"result={data.get('result',0)}")
        else:
            lines.append(f"[{ts:09d}] {ch}: {data}")
    return "\n".join(lines)

def interactive():
    import readline  # 提供行编辑和历史
    history_file = os.path.expanduser("~/.m1switch_dbg_history")
    try:
        readline.read_history_file(history_file)
    except:
        pass

    print(f"M1Switch Debug Client — 连接 {SOCKET_PATH}")
    print("输入 help 查看命令, quit 退出")
    print()

    while True:
        try:
            line = input("dbg> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        if line == "quit":
            break

        try:
            readline.write_history_file(history_file)
        except:
            pass

        result = run_line(line)
        print(result)

def run_line(line):
    parts = line.split()
    if not parts:
        return ""
    cmd = parts[0]
    args = parts[1:]

    cmd_map = {
        "regs":     {"cmd": "regs"},
        "pause":    {"cmd": "pause"},
        "continue": {"cmd": "continue"},
        "step":     {"cmd": "step"},
        "threads":  {"cmd": "threads"},
        "snap":     {"cmd": "snap"},
        "stats":    {"cmd": "stats"},
        "breakpoints": {"cmd": "breakpoints"},
        "help":     {"cmd": "help"},
    }

    if cmd in cmd_map:
        resp = send_cmd(cmd_map[cmd])
    elif cmd == "read" and len(args) >= 2:
        resp = send_cmd({"cmd": "read", "args": [args[0], args[1]]})
    elif cmd == "write" and len(args) >= 2:
        resp = send_cmd({"cmd": "write", "args": [args[0], args[1]]})
    elif cmd == "break" and len(args) >= 1:
        resp = send_cmd({"cmd": "break", "args": [args[0]]})
    elif cmd == "svc_trace":
        n = args[0] if args else "20"
        resp = send_cmd({"cmd": "svc_trace", "args": [n]})
    elif cmd == "ipc_trace":
        n = args[0] if args else "20"
        resp = send_cmd({"cmd": "ipc_trace", "args": [n]})
    elif cmd == "gpu_trace":
        n = args[0] if args else "20"
        resp = send_cmd({"cmd": "gpu_trace", "args": [n]})
    elif cmd == "trace" and len(args) >= 2:
        resp = send_cmd({"cmd": "trace", "args": [args[0], args[1]]})
    else:
        return f"未知命令: {line}  (输入 help)"

    if not resp.get("success"):
        return f"❌ {resp.get('error', '未知错误')}"

    data = resp.get("data", "")
    if cmd == "regs":
        return format_regs(data)
    elif cmd in ("svc_trace", "ipc_trace", "gpu_trace"):
        try:
            events = json.loads(data) if isinstance(data, str) else data
            return format_trace(events)
        except:
            return str(data)
    elif cmd == "help":
        return __doc__
    else:
        # 尝试格式化 JSON
        if isinstance(data, str):
            try:
                parsed = json.loads(data)
                return json.dumps(parsed, indent=2, ensure_ascii=False)
            except:
                pass
        return str(data)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        result = run_line(" ".join(sys.argv[1:]))
        print(result)
    else:
        interactive()
