#pragma once
#include <Arduino.h>

extern const char INDEX_HTML[] PROGMEM;

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<title>ESP32S3 文件上传 (LittleFS + RAM)</title>
<style>
  body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Helvetica,Arial;max-width:760px;margin:24px auto;padding:0 12px;}
  .card{border:1px solid #ddd;border-radius:12px;padding:16px;margin:12px 0;box-shadow:0 1px 2px rgba(0,0,0,.04);}
  button{cursor:pointer;border:1px solid #ccc;border-radius:10px;padding:8px 14px;background:#f7f7f7}
  button:hover{background:#efefef}
  button[disabled]{opacity:.6;cursor:not-allowed}
  table{border-collapse:collapse;width:100%}
  td,th{border-bottom:1px solid #eee;padding:8px;text-align:left}
  /* 操作列右对齐 */
  td:last-child,th:last-child{text-align:right}
  .muted{color:#666;font-size:12px}
  #log{white-space:pre-wrap;background:#111;color:#eee;border-radius:10px;padding:10px;min-height:60px}
  .pill{display:inline-block;background:#eef;border:1px solid #dde;border-radius:999px;padding:2px 8px;font-size:12px;margin-left:8px}
  .progress{height:14px;border-radius:10px;overflow:hidden;background:#eee;margin-top:6px;}
  .bar{height:100%;background:#3b82f6;width:0%;}
  .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}
  /* 按钮靠右的容器 */
  .actions{margin-left:auto;display:flex;gap:10px;align-items:center}
  .drop{border:2px dashed #bbb;border-radius:12px;padding:10px;text-align:center;margin-top:8px;color:#555}
  .drop.drag{background:#f0f7ff;border-color:#3b82f6;color:#1e40af}
  .tag{display:inline-block;background:#eef;border-radius:8px;padding:2px 6px;margin:2px;font-size:12px}

  /* 预览弹窗 */
  .viewer{position:fixed;inset:0;background:rgba(0,0,0,.75);display:none;align-items:center;justify-content:center;z-index:9999;padding:24px}
  .viewer.open{display:flex}
  .viewer .box{background:#000;color:#fff;max-width:92vw;max-height:92vh;border-radius:12px;padding:10px;box-shadow:0 10px 30px rgba(0,0,0,.4)}
  .viewer header{display:flex;align-items:center;justify-content:space-between;gap:10px;margin-bottom:8px}
  .viewer .content{text-align:center}
  .viewer img,.viewer video,.viewer audio{max-width:90vw;max-height:80vh}
  .viewer .close{border:1px solid #444;background:#111;color:#fff;border-radius:8px;padding:6px 10px;cursor:pointer}
  .viewer .close:hover{background:#222}
</style>
</head>
<body>
  <h2>ESP32S3 本地文件管理 <span class="pill" id="ip"></span></h2>

  <div class="card">
    <h3>存储空间（LittleFS）</h3>
    <div id="fsinfo">
      <span id="usedText">已用 0 KB</span> /
      <span id="totalText">总 0 KB</span>
      <div class="progress"><div class="bar" id="bar"></div></div>
    </div>
  </div>

  <!-- 独立：上传到 LittleFS -->
  <div class="card">
    <h3>上传到 LittleFS（掉电不丢失）</h3>
    <form id="uploadFormFlash">
      <div class="row">
        <input type="file" id="fileFlash" multiple />
        <div class="actions">
          <button id="btnUploadFlash" type="submit">上传到 LittleFS</button>
        </div>
      </div>
      <div class="drop" id="dropFlash">拖拽文件到此，或在此区域内粘贴（Ctrl/⌘+V）</div>
      <div class="muted" id="selFlash"></div>
      <div class="progress"><div class="bar" id="upbarFlash"></div></div>
      <div class="muted" id="uptextFlash"></div>
    </form>
    <p class="muted">前端使用 <code>/info</code> 做容量预检查，上传到 <code>/upload</code>。 仅允许一个上传在进行；并发会被拒绝（423）</p>
  </div>

  <!-- 独立：上传到内存（PSRAM） -->
  <div class="card">
    <h3>上传到内存（PSRAM，断电丢失，更快）</h3>
    <form id="uploadFormRAM">
      <div class="row">
        <input type="file" id="fileRAM" multiple />
        <div class="actions">
          <button id="btnUploadRAM" type="submit">上传到内存</button>
          <button id="btnClearRAMTop" type="button">清空内存区</button>
        </div>
      </div>
      <div class="drop" id="dropRAM">拖拽文件到此，或在此区域内粘贴（Ctrl/⌘+V）</div>
      <div class="muted" id="selRAM"></div>
      <div class="progress"><div class="bar" id="upbarRAM"></div></div>
      <div class="muted" id="uptextRAM"></div>
    </form>
    <p class="muted">前端使用 <code>/ram/info</code> 做容量预检查，上传到 <code>/upload-ram</code>。 仅允许一个上传在进行；并发会被拒绝（423）</p>
    <p class="muted">内存区仅允许存放一个文件，存放下一个文件时需要清空内存区后再上传</p>
  </div>

  <div class="card">
    <h3>文件列表（LittleFS）</h3>
    <table id="tbl">
      <thead><tr><th>名称</th><th>大小</th><th>操作</th></tr></thead>
      <tbody></tbody>
    </table>
    <div class="muted" id="empty">暂无文件</div>
  </div>

  <div class="card">
    <h3>内存区（PSRAM）</h3>
    <div id="ramInfoLine" class="muted">PSRAM 信息加载中...</div>
    <table id="tblRam" style="margin-top:8px">
      <thead><tr><th>名称</th><th>大小</th><th>操作</th></tr></thead>
      <tbody></tbody>
    </table>
    <div class="muted" id="emptyRam">内存区为空</div>
  </div>

  <div class="card">
    <h3>日志</h3>
    <div id="log"></div>
  </div>

  <!-- 预览弹窗 -->
  <div class="viewer" id="viewer" onclick="closeViewer()">
    <div class="box" onclick="event.stopPropagation()">
      <header>
        <div id="viewerTitle" style="font-weight:600"></div>
        <button class="close" onclick="closeViewer()">关闭</button>
      </header>
      <div class="content" id="viewerInner"></div>
    </div>
  </div>

<script>
const $ = (id) => document.getElementById(id);

// 日志：正确换行
const log = (t = '') => {
  const el = $('log');
  const msg = String(t).replace(/\r\n?/g, '\n');
  if (el.textContent.length) el.textContent += '\n';
  el.textContent += msg;
  el.scrollTop = el.scrollHeight;
};

// === 工具 ===
function fmtKB(b){ return (b/1024).toFixed(1) + " KB"; }
function fmtMB(b){ return (b/1024/1024).toFixed(2) + " MB"; }
function setSelectedList(id, files){
  const box = $(id);
  if(!files || !files.length){ box.textContent = ''; return; }
  box.innerHTML = Array.from(files).map(f=>`<span class="tag">${f.name} (${fmtKB(f.size)})</span>`).join('');
}
// 规范化：仅取文件名，去掉前导斜杠与路径
function baseName(p){
  const s = String(p || '');
  const noLead = s.replace(/^[\\/]+/, '');
  const parts = noLead.split(/[\\/]/);
  return parts[parts.length - 1] || noLead;
}

// 根据后缀判断媒体类型
function mediaKind(name){
  const n = String(name).toLowerCase();
  if (/(\.png|jpe?g|gif|webp|bmp|svg)$/.test(n)) return 'image';
  if (/(\.mp4|webm|ogv)$/.test(n)) return 'video';
  if (/(\.mp3|m4a|wav|ogg)$/.test(n)) return 'audio';
  if (/\.pdf$/.test(n)) return 'pdf';
  return null;
}

// 打开/关闭预览
function openViewer(kind, url, title){
  if (kind === 'pdf') { window.open(url, '_blank'); return; }
  const v = $('viewer'), c = $('viewerInner'), t = $('viewerTitle');
  c.innerHTML = ''; t.textContent = title || '';
  if (kind === 'image') {
    const el = document.createElement('img'); el.src = url; el.alt = title || '';
    c.appendChild(el);
  } else if (kind === 'video') {
    const el = document.createElement('video'); el.src = url; el.controls = true; el.autoplay = false;
    c.appendChild(el);
  } else if (kind === 'audio') {
    const el = document.createElement('audio'); el.src = url; el.controls = true;
    c.appendChild(el);
  } else {
    window.open(url, '_blank'); return;
  }
  v.classList.add('open');
}
function closeViewer(){ $('viewer').classList.remove('open'); $('viewerInner').innerHTML = ''; }

document.addEventListener('keydown', (e)=>{ if(e.key==='Escape') closeViewer(); });

// === LittleFS 容量信息 ===
async function updateFSInfo(){
  try {
    const res = await fetch('/info?t=' + Date.now(), {cache:'no-store'});
    const data = await res.json();
    const total = data.total || 0;
    const used  = data.used  || 0;
    const percent = total ? Math.min(100, (used / total * 100)) : 0;
    $('usedText').textContent  = "已用 " + fmtKB(used);
    $('totalText').textContent = "总 "   + fmtKB(total);
    $('bar').style.width = percent.toFixed(1) + "%";
    $('bar').style.background = percent > 90 ? "#f87171" : percent > 70 ? "#facc15" : "#3b82f6";
  } catch(e) { console.error(e); }
  return true;
}

// === RAM 信息 + 区域渲染 ===
async function updateRAMInfo(){
  try {
    const res = await fetch('/ram/info?t=' + Date.now(), {cache:'no-store'});
    const d = await res.json();
    const _btnClearTop = $('btnClearRAMTop'); if(_btnClearTop){ _btnClearTop.disabled = !(d && d.has); }
    $('ramInfoLine').textContent =
      (d.psram ? "PSRAM 已检测到" : "PSRAM 不可用") +
      `；总 ${fmtMB(d.total||0)}，空闲 ${fmtMB(d.free||0)}，上限 ${fmtMB(d.maxUpload||0)}（断电丢失）`;

    const tb = document.querySelector('#tblRam tbody');
    tb.innerHTML = '';
    $('emptyRam').style.display = d.has ? 'none' : 'block';
    if (d.has) {
      const rawName = d.name || 'ram.bin';
      const name = baseName(rawName);
      const kind = mediaKind(name);

      const tr = document.createElement('tr');
      const tdName = document.createElement('td');
      tdName.textContent = name;

      const tdSize = document.createElement('td');
      tdSize.textContent = fmtKB(d.used || 0);

      const tdAct = document.createElement('td');

      if (kind) {
        const btnPrev = document.createElement('button');
        btnPrev.textContent = '预览';
        btnPrev.onclick = () => {
          const url = '/ram/view?name=' + encodeURIComponent(name);
          openViewer(kind, url, name);
        };
        tdAct.appendChild(btnPrev);
      }

      const btnDown = document.createElement('button');
      btnDown.style.marginLeft = '8px';
      btnDown.textContent = '下载';
      btnDown.onclick = () => {
        window.location = '/ram/download?name=' + encodeURIComponent(name);
      };

      tdAct.appendChild(btnDown);
      tr.appendChild(tdName);
      tr.appendChild(tdSize);
      tr.appendChild(tdAct);
      tb.appendChild(tr);
    }

  } catch(e) { console.error(e); }
}

// === LittleFS 文件列表 ===
async function refresh(){
  const res = await fetch('/list?t=' + Date.now(), {cache:'no-store'});
  const data = await res.json();
  const tb = document.querySelector('#tbl tbody');
  tb.innerHTML = '';
  $('empty').style.display = data.length ? 'none' : 'block';

  data.forEach(it => {
    const tr = document.createElement('tr');
    tr.innerHTML = `<td>${it.name}</td><td>${fmtKB(it.size)}</td>`;
    const tdAct = document.createElement('td');

    const kind = mediaKind(it.name);
    if (kind) {
      const btnPrev = document.createElement('button');
      btnPrev.textContent = '预览';
      btnPrev.onclick = () => {
        const url = '/view?name=' + encodeURIComponent(it.name);
        openViewer(kind, url, it.name);
      };
      tdAct.appendChild(btnPrev);
    }

    const btnDown = document.createElement('button');
    btnDown.style.marginLeft = '8px';
    btnDown.textContent = '下载';
    btnDown.onclick = () => window.location = '/download?name=' + encodeURIComponent(it.name);

    const btnDel = document.createElement('button');
    btnDel.style.marginLeft = '8px';
    btnDel.textContent = '删除';
    btnDel.onclick = async () => {
      if(!confirm('确认删除 ' + it.name + ' ?')) return;
      const fd = new FormData();
      fd.append('name', it.name);
      const r = await fetch('/delete', {method:'POST', body: fd});
      log((await r.text()).trim());
      await refresh();
      await updateFSInfo();
    };

    tdAct.appendChild(btnDown);
    tdAct.appendChild(btnDel);
    tr.appendChild(tdAct);
    tb.appendChild(tr);
  });
}

// === 拖拽与粘贴（双区域） ===
let activeTarget = 'flash'; // 用于粘贴定位
function initDnD(dropId, fileInputId, selId, target){
  const drop = $(dropId);
  ['dragenter','dragover'].forEach(ev => drop.addEventListener(ev, e => {
    e.preventDefault(); e.stopPropagation(); drop.classList.add('drag');
  }));
  ['dragleave','drop'].forEach(ev => drop.addEventListener(ev, e => {
    e.preventDefault(); e.stopPropagation(); drop.classList.remove('drag');
  }));
  drop.addEventListener('drop', e => {
    const files = e.dataTransfer && e.dataTransfer.files;
    if(files && files.length){
      const dt = new DataTransfer();
      Array.from(files).forEach(f => dt.items.add(f));
      $(fileInputId).files = dt.files;
      setSelectedList(selId, dt.files);
    }
  });
  drop.addEventListener('mouseenter', ()=>{ activeTarget = target; });
  $(fileInputId).addEventListener('change', e => setSelectedList(selId, e.target.files));
}

document.addEventListener('paste', e => {
  const files = e.clipboardData && e.clipboardData.files;
  if(files && files.length){
    const dt = new DataTransfer();
    Array.from(files).forEach(f => dt.items.add(f));
    if(activeTarget === 'ram'){
      $('fileRAM').files = dt.files; setSelectedList('selRAM', dt.files);
      log(`向 RAM 区添加 ${files.length} 个文件（粘贴）`);
    } else {
      $('fileFlash').files = dt.files; setSelectedList('selFlash', dt.files);
      log(`向 LittleFS 区添加 ${files.length} 个文件（粘贴）`);
    }
  }
});

initDnD('dropFlash','fileFlash','selFlash','flash');
initDnD('dropRAM','fileRAM','selRAM','ram');

// === 上传（分别处理） ===
function attachUploadHandler({formId, fileInputId, btnId, upbarId, uptextId, target}){
  const form = $(formId), fileInput = $(fileInputId), btn = $(btnId);
  const upbar = $(upbarId), uptext = $(uptextId);
  form.addEventListener('submit', async (e) => {
    e.preventDefault();
    const files = fileInput.files;
    if(!files.length){ alert('先选择文件'); return; }

    const infoUrl = target === 'ram' ? '/ram/info' : '/info';
    const uploadUrl = target === 'ram' ? '/upload-ram' : '/upload';

    btn.disabled = true; fileInput.disabled = true; upbar.style.width = '0%'; uptext.textContent = '';

    for(const f of files){
      try {
        const info = await (await fetch(infoUrl + '?t=' + Date.now(), {cache:'no-store'})).json();
        if (f.size > (info.maxUpload || 0)) {
          const cap = info.maxUpload ? (fmtKB(info.maxUpload)) : '0 KB';
          alert(`${f.name} 超过单文件上限：${cap}；已跳过`);
          log(`跳过 ${f.name}: 超过单文件上限 ${cap}`);
          continue;
        }
        const freeAfter = (info.free || 0) - (info.safetyMargin || 0);
        if (target === 'flash' && f.size > freeAfter) {
          alert(`${f.name} 可用空间不足（需 ${fmtKB(f.size)}，可用 ${fmtKB(Math.max(0, freeAfter))}）；已跳过`);
          log(`跳过 ${f.name}: LittleFS 空间不足`);
          continue;
        }
      } catch (e) {
        console.warn('预检查失败，继续尝试上传', e);
      }

      upbar.style.width = '0%'; uptext.textContent = '';

      await new Promise((resolve) => {
        const fd = new FormData();
        fd.append('file', f, f.name);
        const xhr = new XMLHttpRequest();
        xhr.open('POST', uploadUrl, true);
        xhr.upload.onprogress = (evt) => {
          if (evt.lengthComputable) {
            const p = Math.min(100, (evt.loaded / evt.total * 100));
            upbar.style.width = p.toFixed(1) + '%';
            uptext.textContent = `${f.name}  ${p.toFixed(1)}%  (${fmtKB(evt.loaded)} / ${fmtKB(evt.total)})`;
          }
        };
        xhr.onload = () => {
          const code = xhr.status;
          if (code === 200) {
            log(`上传 ${f.name} → ${target.toUpperCase()}: OK`);
          } else if (code === 413) {
            log(`上传 ${f.name}: 413 超限或空间不足`);
            alert(`${f.name} 上传失败：超限或空间不足（413）`);
          } else if (code === 423) {
            log(`上传 ${f.name}: 423 已有其他上传在进行`);
            alert(`已有一个上传在进行，请稍后重试（423）`);
          } else {
            log(`上传 ${f.name}: ${code} ${xhr.responseText || ''}`);
            alert(`${f.name} 上传失败（${code}）`);
          }
          resolve();
        };
        xhr.onerror = () => { log(`上传 ${f.name}: 网络错误`); resolve(); };
        xhr.send(fd);
      });

      if (target === 'flash') { await refresh(); await updateFSInfo(); }
      if (target === 'ram')   { await updateRAMInfo(); }

      upbar.style.width = '0%'; uptext.textContent = '';
    }

    btn.disabled = false; fileInput.disabled = false; fileInput.value = ''; setSelectedList(target==='ram'?'selRAM':'selFlash', null);
  });
}

attachUploadHandler({formId:'uploadFormFlash', fileInputId:'fileFlash', btnId:'btnUploadFlash', upbarId:'upbarFlash', uptextId:'uptextFlash', target:'flash'});
attachUploadHandler({formId:'uploadFormRAM',   fileInputId:'fileRAM',   btnId:'btnUploadRAM',   upbarId:'upbarRAM',   uptextId:'uptextRAM',   target:'ram'});

// 顶部清空按钮（位于“上传到内存”卡片）
(function(){
  const btnClearTop = $('btnClearRAMTop');
  if(btnClearTop){
    btnClearTop.onclick = async () => {
      await fetch('/ram/clear', {method:'POST'});
      log('已清空内存区');
      updateRAMInfo();
    };
  }
})();

$('ip').textContent = location.host ? ('http://' + location.host) : '';
refresh();
updateFSInfo();
updateRAMInfo();
</script>
</body>
</html>
)HTML";
