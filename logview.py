#!/usr/bin/env python3
# Live viewer for hv.log: serves a page at http://localhost:8765/ that streams
# every new line as it is written, over Server-Sent Events. Stdlib only.
import http.server, socketserver, time, os, json, sys
from pathlib import Path

LOG = os.environ.get("HVLOG", str(Path(__file__).with_name("hv.log")))
PORT = int(os.environ.get("HVPORT", "8765"))

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>hv.log live</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; background:#0b0e14; color:#c8d3e0;
         font:13px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace; }
  header { position:sticky; top:0; background:#11161f; border-bottom:1px solid #222b3a;
           padding:8px 14px; display:flex; gap:14px; align-items:center; }
  header b { color:#7fd1ff; }
  #status { padding:2px 8px; border-radius:10px; font-size:11px; background:#243; color:#9f9; }
  #status.off { background:#422; color:#f99; }
  label { font-size:12px; color:#8a97a8; }
  header button { font:inherit; font-size:12px; background:#1b2431; color:#c8d3e0;
                  border:1px solid #2b3a4d; border-radius:6px; padding:3px 10px; cursor:pointer; }
  header button:hover { background:#243247; }
  header button.ok { background:#1e3a1e; border-color:#2f5a2f; color:#9f9; }
  #log { padding:8px 14px; white-space:pre-wrap; word-break:break-all; }
  .l { display:block; }
  .stuck { color:#ffd479; }
  .sample { color:#7fb0ff; }
  .fiq { color:#5a6b82; }
  .psci { color:#c39bd3; }
  .err { color:#ff8b8b; }
  #controls input { vertical-align:middle; }
</style></head><body>
<header>
  <b>hv.log</b>
  <span id="status" class="off">connecting…</span>
  <span id="controls"><label><input type="checkbox" id="follow" checked> follow tail</label></span>
  <label><input type="checkbox" id="wrap" checked> wrap</label>
  <button id="copy" title="Copy every line to the clipboard">Copy all</button>
  <button id="clear" title="Clear the view (does not touch the log file)">Clear</button>
  <span id="count" style="margin-left:auto;color:#8a97a8"></span>
</header>
<div id="log"></div>
<script>
const log = document.getElementById('log');
const statusEl = document.getElementById('status');
const followEl = document.getElementById('follow');
const wrapEl = document.getElementById('wrap');
const countEl = document.getElementById('count');
const copyBtn = document.getElementById('copy');
const clearBtn = document.getElementById('clear');
const lines = [];   // every line received this session, for a complete copy
let n = 0;
wrapEl.onchange = () => log.style.whiteSpace = wrapEl.checked ? 'pre-wrap' : 'pre';
copyBtn.onclick = async () => {
  const text = lines.join('\\n');
  try {
    await navigator.clipboard.writeText(text);
  } catch (e) {
    // Fallback for non-secure contexts: select a hidden textarea and execCommand.
    const ta = document.createElement('textarea');
    ta.value = text; document.body.appendChild(ta); ta.select();
    try { document.execCommand('copy'); } catch (_) {}
    document.body.removeChild(ta);
  }
  const old = copyBtn.textContent;
  copyBtn.textContent = 'Copied ' + lines.length + ' lines';
  copyBtn.classList.add('ok');
  setTimeout(() => { copyBtn.textContent = old; copyBtn.classList.remove('ok'); }, 1500);
};
clearBtn.onclick = () => { log.innerHTML = ''; lines.length = 0; n = 0; countEl.textContent = ''; };
function cls(t){
  if (t.includes('HV STUCK')) return 'stuck';
  if (t.includes('HV SAMPLE')) return 'sample';
  if (t.includes('HV FIQ')) return 'fiq';
  if (t.includes('PSCI')) return 'psci';
  if (/error|Error|Exception|Traceback|Timeout/.test(t)) return 'err';
  return '';
}
function add(t){
  lines.push(t);
  const d = document.createElement('span');
  d.className = 'l ' + cls(t);
  d.textContent = t;
  log.appendChild(d);
  n++;
  if (n % 25 === 0) countEl.textContent = n + ' lines';
  if (followEl.checked) window.scrollTo(0, document.body.scrollHeight);
}
function connect(){
  const es = new EventSource('/stream');
  es.onopen = () => { statusEl.textContent = 'live'; statusEl.className=''; };
  es.onmessage = (e) => { const v = JSON.parse(e.data);
                          if (v === 'reset') { log.innerHTML=''; lines.length=0; n=0; }
                          else add(v); };
  es.onerror = () => { statusEl.textContent = 'reconnecting…'; statusEl.className='off';
                       es.close(); setTimeout(connect, 1000); };
}
connect();
</script></body></html>
"""

class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/':
            body = PAGE.encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path != '/stream':
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'keep-alive')
        self.end_headers()

        def send(obj):
            self.wfile.write(b'data: ' + json.dumps(obj).encode('utf-8') + b'\n\n')
            self.wfile.flush()

        pos = 0
        inode = None
        try:
            # Prime with the tail of the current file, then follow.
            while True:
                try:
                    st = os.stat(LOG)
                except FileNotFoundError:
                    time.sleep(0.4)
                    continue
                # New file or truncation (a fresh boot run recreates hv.log): reset.
                if inode is None:
                    inode = st.st_ino
                    with open(LOG, 'r', errors='replace') as f:
                        f.seek(max(0, st.st_size - 40000))
                        if st.st_size > 40000:
                            f.readline()
                        for line in f:
                            send(line.rstrip('\n'))
                        pos = f.tell()
                    continue
                if st.st_ino != inode or st.st_size < pos:
                    send('reset')
                    inode = st.st_ino
                    pos = 0
                    continue
                with open(LOG, 'r', errors='replace') as f:
                    f.seek(pos)
                    chunk = f.read()
                    pos = f.tell()
                if chunk:
                    for line in chunk.splitlines():
                        send(line)
                else:
                    time.sleep(0.3)
        except (BrokenPipeError, ConnectionResetError, OSError):
            return

    def log_message(self, *a):
        pass

class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

if __name__ == '__main__':
    print(f"hv.log live viewer on http://localhost:{PORT}/  (log: {LOG})", flush=True)
    Server(('127.0.0.1', PORT), H).serve_forever()
