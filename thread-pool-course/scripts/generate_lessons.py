"""Generate two offline, self-contained HTML lessons from the real project sources.
Run from any directory: python thread-pool-course/scripts/generate_lessons.py
No third-party modules; all outputs stay within this new course directory.
"""
from pathlib import Path
import html
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]


def esc(value):
    return html.escape(str(value), quote=True)


def code(text, label="示例"):
    return f'<div class="code"><div class="code-title">{esc(label)}<button class="copy" type="button">复制</button></div><pre><code>{esc(text.strip())}</code></pre></div>'


def table(headers, rows):
    return '<div class="table-wrap"><table><thead><tr>' + ''.join(f'<th scope="col">{h}</th>' for h in headers) + '</tr></thead><tbody>' + ''.join('<tr>' + ''.join(f'<td>{c}</td>' for c in row) + '</tr>' for row in rows) + '</tbody></table></div>'


def section(id_, number, title, body):
    return f'<section id="{id_}" class="chapter"><div class="chapter-heading"><span>{number}</span><h2>{title}</h2></div>{body}</section>'


CSS = r"""
:root{--bg:#f4f6fa;--paper:#fff;--ink:#172336;--muted:#55647a;--line:#dce3ec;--accent:#2261b6;--wash:#eef5ff;--nav:#132239;--warm:#fff4de;--code:#142237}
*{box-sizing:border-box}html{scroll-behavior:smooth;scroll-padding-top:28px}body{margin:0;font:16px/1.85 "Segoe UI","Microsoft YaHei",sans-serif;background:var(--bg);color:var(--ink)}body.csharp{--accent:#6650a6;--wash:#f4f0ff;--nav:#251e3c}a{color:var(--accent);text-underline-offset:4px}button,input,select{font:inherit}button{cursor:pointer;border:1px solid var(--line);background:#fff;color:var(--ink);padding:7px 14px;border-radius:7px}button:hover{border-color:var(--accent);background:var(--wash)}button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible,summary:focus-visible{outline:3px solid #e6a534;outline-offset:3px}button:disabled{opacity:.45;cursor:default}h1,h2,h3{line-height:1.35}h1{font-size:clamp(36px,4.5vw,62px);letter-spacing:-.04em;margin:14px 0 22px}h2{font-size:28px;margin:0}h3{font-size:20px;margin:30px 0 12px}p{margin:12px 0}code,pre,.mono{font-family:Consolas,"Cascadia Code",monospace}code{font-size:.9em}p code,td code,li code{background:var(--wash);padding:2px 5px;border-radius:4px;overflow-wrap:anywhere}aside{position:fixed;inset:0 auto 0 0;width:238px;background:var(--nav);color:#e2e9f5;padding:32px 24px;overflow-y:auto}aside .brand{font-size:21px;color:#fff;text-decoration:none;font-weight:700}aside .caption{font-size:11px;letter-spacing:.15em;color:#9eafc6;margin:10px 0 24px}nav{display:grid;gap:6px}nav a{font-size:14px;color:#d1dbeb;text-decoration:none;padding:7px 10px;border-radius:6px}nav a:hover,nav a.active{background:#ffffff18;color:#fff}.side-footer{border-top:1px solid #ffffff26;margin-top:30px;padding-top:20px;font-size:13px;color:#b9c5d7}.side-footer a{color:#e0e8f7}main{margin-left:238px;max-width:1500px;padding:36px clamp(24px,5vw,76px) 80px}.toolbar{display:flex;align-items:center;gap:10px;margin-bottom:36px}.toolbar input{flex:1;min-width:0;border:1px solid var(--line);border-radius:8px;padding:10px 14px;background:#fff}.search-box{position:relative;flex:1;display:flex}.search-results{position:absolute;top:100%;left:0;right:0;max-height:350px;overflow:auto;background:#fff;border:1px solid var(--line);padding:10px;z-index:5;box-shadow:0 8px 25px #17233620}.search-results a{display:block;padding:8px;text-decoration:none}.eyebrow{font-size:12px;font-family:Consolas,monospace;letter-spacing:.16em;color:var(--accent)}.lead{font-size:18px;color:var(--muted);max-width:820px}.tags{display:flex;flex-wrap:wrap;gap:8px;margin:24px 0}.tag{font-size:12px;border:1px solid var(--line);border-radius:25px;padding:4px 12px;background:var(--paper)}.overview{display:grid;grid-template-columns:repeat(3,1fr);border:1px solid var(--line);border-radius:12px;background:#fff;overflow:hidden;margin:32px 0}.overview div{padding:20px;border-right:1px solid var(--line)}.overview div:last-child{border:0}.overview b{display:block;font-size:26px;color:var(--accent)}.overview small{color:var(--muted)}.chapter{background:var(--paper);border:1px solid var(--line);border-radius:12px;margin:26px 0;padding:clamp(22px,3vw,40px);box-shadow:0 5px 20px #17233604;min-width:0}.chapter-heading{display:flex;align-items:center;gap:17px;margin-bottom:24px}.chapter-heading>span{color:var(--accent);background:var(--wash);padding:12px;border-radius:9px;font:24px/1 Consolas,monospace}.note{border-left:4px solid var(--accent);background:var(--wash);padding:14px 18px;border-radius:0 7px 7px 0;margin:22px 0}.warning{border-left-color:#bf8b31;background:var(--warm)}.table-wrap{overflow:auto;border:1px solid var(--line);border-radius:8px;margin:18px 0}table{border-collapse:collapse;width:100%;font-size:14px}th{background:var(--wash);text-align:left;font-weight:650}th,td{padding:12px 15px;border-bottom:1px solid var(--line);vertical-align:top}tr:last-child td{border-bottom:0}td:first-child{min-width:135px}td{overflow-wrap:anywhere}.code{background:var(--code);color:#e0e9f6;border-radius:9px;overflow:hidden;margin:17px 0}.code-title{font:12px/1.5 Consolas,sans-serif;padding:11px 15px;border-bottom:1px solid #ffffff25;color:#a7bddc;display:flex;justify-content:space-between;align-items:center;gap:12px}.copy{font-size:12px;background:#253c5b;color:#e8f1ff;border-color:#476082;padding:3px 9px}pre{overflow:auto;margin:0;padding:18px;font-size:14px;line-height:1.8;tab-size:4}pre code{font-size:inherit}.steps{padding-left:24px}.steps li{margin:14px 0}.flow{display:flex;align-items:center;gap:9px;flex-wrap:wrap;margin:22px 0}.flow span{padding:10px 14px;background:var(--wash);border:1px solid var(--line);border-radius:8px}.flow i{font-style:normal;color:var(--muted)}.grid{display:grid;grid-template-columns:1fr 1fr;gap:16px}.card{border:1px solid var(--line);border-radius:8px;padding:20px;min-width:0}.card h3{margin:0 0 12px}.small{font-size:13px;color:var(--muted)}.actions{display:flex;flex-wrap:wrap;gap:8px;margin:14px 0}.sim{border:1px solid var(--line);border-radius:10px;padding:22px;margin:22px 0;background:#fbfcff}.sim h3{margin-top:0}.sim-status{color:var(--accent);font-weight:650;min-height:30px}.lane{display:flex;align-items:center;flex-wrap:wrap;gap:8px;padding:14px 0;border-top:1px solid var(--line);min-height:60px}.lane>strong{font-size:13px;width:85px}.chip{display:inline-block;background:#e8effb;color:#244879;padding:4px 11px;border-radius:6px;font:13px/1.7 Consolas,monospace}.chip.running{background:#fff0d4;color:#765013}.chip.done{background:#e3f3eb;color:#245d42}.counter-value{font:40px/1.4 Consolas,monospace;color:var(--accent)}.output{white-space:pre-wrap;min-height:60px;background:var(--wash);padding:15px;border-radius:8px;font-size:14px}.sim label{display:inline-block;margin-right:14px}.sim input{width:85px;padding:5px;border:1px solid var(--line);border-radius:5px}.sim select{padding:5px;max-width:100%}details{margin:16px 0;border:1px solid var(--line);border-radius:8px;padding:15px 18px}summary{cursor:pointer;font-weight:650}details p{color:var(--muted)}.source{padding:0;overflow:hidden}.source summary{padding:16px;font-family:Consolas,monospace;font-size:14px;overflow-wrap:anywhere}.source .code{margin:0;border-radius:0}.line{display:block;min-height:1.8em;scroll-margin-top:30px}.line:target{background:#5c4e2c}.line-number{display:inline-block;width:4em;color:#a0b4ce;text-decoration:none;user-select:none;padding-right:1em;text-align:right}.source pre{padding:15px 8px}.signature{font-family:Consolas,monospace;font-size:11px;color:var(--muted);padding:8px 16px;overflow-wrap:anywhere}.source-jump{font-size:13px}.skip{position:absolute;top:-100px;left:250px;background:#fff;z-index:10;padding:10px}.skip:focus{top:0}footer{color:var(--muted);font-size:13px}[hidden]{display:none!important}
@media(max-width:1050px){aside{width:205px;padding:26px 17px}main{margin-left:205px;padding:24px}.grid{grid-template-columns:1fr}}
@media(max-width:760px){aside{position:static;width:auto;padding:20px}aside .caption,.side-footer{display:none}nav{display:flex;overflow-x:auto;margin-top:12px;gap:6px}nav a{white-space:nowrap;font-size:12px}.brand{display:block}main{margin-left:0;padding:20px 14px}.chapter{padding:22px 17px}h1{font-size:39px}h2{font-size:24px}.toolbar{margin-bottom:24px}.overview div{padding:14px 10px}.overview b{font-size:22px}.overview small{font-size:11px}.sim{padding:15px}.skip{left:10px}}
@media print{aside,.toolbar,.actions,.copy,.search-results,.skip{display:none!important}main{margin:0;padding:0;max-width:none}body{background:white;font-size:11pt}.chapter{box-shadow:none;break-inside:auto;border-color:#aaa;margin:15px 0;padding:20px}h1{font-size:30pt}h2,h3{break-after:avoid}.code{background:#f5f5f5;color:#111;border:1px solid #ccc}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:8pt}.code-title{color:#333;border-color:#aaa}.source:not([open]){display:none}.source-jump{display:none}.note,.sim,.overview{break-inside:avoid}.line-number{color:#555}a{color:#222}html{scroll-behavior:auto}}
@media(prefers-reduced-motion:reduce){html{scroll-behavior:auto}}
"""

JS = r"""
'use strict';
const $ = id => document.getElementById(id);
document.querySelectorAll('.copy').forEach(button => button.addEventListener('click', async () => {
  const text = button.closest('.code').querySelector('pre code').textContent;
  try { await navigator.clipboard.writeText(text); button.textContent = '已复制'; }
  catch { const area=document.createElement('textarea');area.value=text;document.body.append(area);area.select();
    const copied=document.execCommand('copy');area.remove();button.textContent=copied?'已复制':'请手动选择复制'; }
  setTimeout(()=>button.textContent='复制',1800);
}));
$('print').addEventListener('click',()=>window.print());
function revealHash(){const target=document.getElementById(decodeURIComponent(location.hash.slice(1)));if(!target)return;
  const details=target.closest('details');if(details)details.open=true;target.scrollIntoView();}
window.addEventListener('hashchange',revealHash);if(location.hash)revealHash();
document.querySelectorAll('.source-jump').forEach(a=>a.addEventListener('click',()=>{const node=document.querySelector(a.getAttribute('href'));if(node?.closest('details'))node.closest('details').open=true;}));
const chapters=[...document.querySelectorAll('section.chapter')];
$('search').addEventListener('input',()=>{const query=$('search').value.trim().toLowerCase();const box=$('search-results');box.replaceChildren();box.hidden=!query;if(!query)return;
  const found=chapters.filter(s=>s.textContent.toLowerCase().includes(query));
  found.forEach(s=>{const a=document.createElement('a');a.href='#'+s.id;a.textContent=s.querySelector('h2').textContent;a.addEventListener('click',()=>box.hidden=true);box.append(a);});
  if(!found.length)box.textContent='没有找到相关章节，试试 mutex、原子、关闭或 Submit。';});
$('search').addEventListener('keydown',e=>{if(e.key==='Escape')$('search-results').hidden=true;});
document.addEventListener('click',e=>{if(!e.target.closest('.search-box'))$('search-results').hidden=true;});
if('IntersectionObserver' in window){const observer=new IntersectionObserver(entries=>{for(const e of entries)if(e.isIntersecting){document.querySelectorAll('nav a').forEach(a=>a.classList.toggle('active',a.hash==='#'+e.target.id));}},{rootMargin:'-10% 0px -65% 0px'});chapters.forEach(s=>observer.observe(s));}
// 有限状态教学模型：每一步是人为安排的调度点，不创建浏览器线程。
let queue=[], workers=[null,null], done=[], nextId=1, closed=false, tick=0;
function drawPool(message=''){
  $('pool-status').textContent=closed?(queue.length||workers.some(Boolean)?'Draining · 拒绝新任务，继续排空':'Stopped · 任务已完成，工作线程退出'):'Running · 接收新任务';
  $('pool-queue').replaceChildren();queue.forEach(j=>{let n=document.createElement('span');n.className='chip';n.textContent='J'+j.id;$('pool-queue').append(n);});
  if(!queue.length)$('pool-queue').textContent='空';
  $('pool-workers').replaceChildren();workers.forEach((j,i)=>{let n=document.createElement('span');n.className='chip'+(j?' running':'');n.textContent='W'+(i+1)+': '+(j?'J'+j.id+' / 剩余 '+j.left+' 步':queue.length?'待取任务':closed?'退出':'等待');$('pool-workers').append(n);});
  $('pool-done').textContent=done.length?done.map(j=>'J'+j.id).join(' · '):'暂无';
  $('pool-log').textContent='调度步 '+tick+' | queued='+queue.length+' active='+workers.filter(Boolean).length+' completed='+done.length+'\n'+message;
  $('submit-one').disabled=closed;$('submit-four').disabled=closed;$('close-pool').disabled=closed;
}
function submitJobs(n){for(let i=0;i<n;i++)queue.push({id:nextId++,left:2});drawPool('提交者：持锁检查状态 → 入队 → 通知等待者。');}
$('submit-one').addEventListener('click',()=>submitJobs(1));$('submit-four').addEventListener('click',()=>submitJobs(4));
$('pool-step').addEventListener('click',()=>{tick++;for(let i=0;i<workers.length;i++){const j=workers[i];if(j&&--j.left===0){done.push(j);workers[i]=null;}if(!workers[i]&&queue.length)workers[i]=queue.shift();}
  drawPool('工作线程：持锁取任务 → 锁外执行 → 完成后更新计数。队列空但 active > 0 时仍不能算空闲。');});
$('close-pool').addEventListener('click',()=>{closed=true;drawPool('关闭调用开始；继续按“推进一步”排空任务。真实 Shutdown 会等待工作线程退出才返回。');});
$('reset-pool').addEventListener('click',()=>{queue=[];workers=[null,null];done=[];nextId=1;closed=false;tick=0;drawPool('已重置。先提交 4 个任务，再推进调度。');});drawPool('先提交 4 个任务，再逐步观察队列与工作线程。');
let counterStep=0;
const traces={split:[['A 读取 count=0，暂存到 A.local',0],['B 读取 count=0，暂存到 B.local',0],['A 写回 A.local+1=1',1],['B 写回 B.local+1=1，覆盖 A 的更新',1]],atomic:[['A 原子读改写：0 → 1',1],['B 原子读改写：1 → 2',2]],mutex:[['A 获得 mutex，读 0；B 尚不能进入',0],['A 写回 1 并释放 mutex',1],['B 获得 mutex，读 1',1],['B 写回 2 并释放 mutex',2]]};
function resetCounter(){counterStep=0;$('counter-value').textContent='0';$('counter-log').textContent='两个执行者各加 1，最终应为 2。';$('counter-next').disabled=false;}
$('counter-mode').addEventListener('change',resetCounter);$('counter-reset').addEventListener('click',resetCounter);
$('counter-next').addEventListener('click',()=>{const trace=traces[$('counter-mode').value];const step=trace[counterStep++];$('counter-value').textContent=step[1];$('counter-log').textContent=counterStep+'. '+step[0]+(counterStep===trace.length?'\n结束：'+(step[1]===2?'结果正确。':'一次更新丢失。'): '');$('counter-next').disabled=counterStep===trace.length;});
function calc(){const n=Number($('calc-tasks').value),p=Number($('calc-workers').value),c=Number($('calc-cost').value),h=Number($('calc-overhead').value);
  if(!Number.isInteger(n)||!Number.isInteger(p)||n<1||n>10000||p<1||p>64||!Number.isFinite(c)||!Number.isFinite(h)||c<=0||c>10000||h<0||h>10000){$('calc-result').textContent='请输入有效范围：任务 1..10000，线程 1..64，计算耗时 (0,10000]，开销 [0,10000]。';return;}
  const serial=n*c,parallel=Math.ceil(n/p)*c+n*h;$('calc-result').textContent='串行模型：'+serial.toFixed(2)+' ms\n线程池模型：'+parallel.toFixed(2)+' ms\n模型加速比：'+(serial/parallel).toFixed(2)+'×';}
['calc-tasks','calc-workers','calc-cost','calc-overhead'].forEach(id=>$(id).addEventListener('input',calc));calc();
"""


def make_lesson(lang):
    cpp = lang == 'cpp'
    title = 'C++17' if cpp else 'C# / .NET 10'
    short = 'C++' if cpp else 'C#'
    other = '../csharp/lesson.html' if cpp else '../cpp/lesson.html'
    pool_path = 'cpp/include/thread_pool.hpp' if cpp else 'csharp/FixedThreadPool.cs'
    source_paths = [pool_path, 'cpp/src/main.cpp', 'cpp/tests/thread_pool_tests.cpp', 'cpp/CMakeLists.txt'] if cpp else [pool_path, 'csharp/Program.cs', 'csharp/SelfTests.cs', 'csharp/ThreadPoolCourse.csproj']

    def jump(needle, label=None):
        lines = (ROOT / pool_path).read_text(encoding='utf-8').splitlines()
        line = next(i for i, text in enumerate(lines, 1) if needle in text)
        return f'<a class="source-jump" href="#source-0-L{line}">{esc(label or needle)} · 源码 L{line} ↗</a>'

    command = '.\\cpp\\build\\Release\\thread_pool_demo.exe' if cpp else 'dotnet run --project .\\csharp\\ThreadPoolCourse.csproj -c Release --'
    body = section('start', '00', '先运行，再拆开线程池', f'''
    <p>这节课从零实现一个<strong>固定大小、共用 FIFO 任务队列的 CPU 线程池</strong>。你会亲手提交任务、取回结果、观察异常、等待空闲并安全关闭。程序使用控制台输出；学习页面可以双击离线打开。</p>
    <div class="grid"><div class="card"><h3>你需要会什么</h3><p>{'函数、类、lambda、引用与对象生命周期；了解 vector 和智能指针。' if cpp else '类、委托、lambda、泛型与 using；对 Task 的结果概念有基本认识。'} 并发知识从本页开始补。</p></div><div class="card"><h3>完成后能回答什么</h3><p>队列该由谁保护？原子操作解决了哪一件事？任务异常去了哪里？队列空为什么还不能退出？</p></div></div>
    <p>建议用 2–3 小时完成：运行示例 → 观察交错 → 理解锁和等待 → 跟读实现 → 修改参数 → 完成练习。配套的另一份教材采用相同契约，方便逐项对照。</p>
    <h3>启动方式</h3><p>Windows 双击新目录内的 <code>Start-Learning.cmd</code>，按 {'1' if cpp else '2'} 打开本教材，按 {'3' if cpp else '4'} 运行示例。也可以在 <code>thread-pool-course</code> 目录打开 PowerShell：</p>
    {code('powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\build.ps1\n' + command + ' --demo all --workers 4 --tasks 17 --iterations 10000', '首次构建与运行 · 工作目录 thread-pool-course')}
    <p>{'依赖：CMake 3.24+ 和 C++17 编译器；本机 Windows 使用 Visual Studio 的 MSVC。独立构建可执行 cmake -S cpp -B cpp/build，然后 cmake --build cpp/build --config Release。Linux/macOS 使用 -DCMAKE_BUILD_TYPE=Release，运行 cpp/build/thread_pool_demo；这些平台未在本机验证。' if cpp else '依赖：.NET 10 SDK。工程无第三方 NuGet 包，可直接打开 csharp/ThreadPoolCourse.sln，或使用 dotnet run。仅构建 C# 时执行 dotnet build csharp/ThreadPoolCourse.csproj -c Release。'}</p>
    <div class="note">这一目录是独立课程。它有自己的构建输出和入口；理解线程池无需运行 Direct3D 或原有 8 章课程。</div>''')

    body += section('model', '01', '线程复用：把任务交给长期工作的线程', '''
    <p><strong>线程是执行者，任务是待执行的一段工作。</strong> 每个任务新建一个线程，会重复支付创建、栈空间和调度成本；线程池预先创建少量线程，任务到来时放入队列，由空闲线程反复取走执行。</p>
    <div class="flow" aria-label="线程池数据流"><span>主线程 / 生产者</span><i>→ 提交</i><span>受锁保护的任务队列</span><i>→ 取出</i><span>W1 / W2 / … 工作线程</span><i>→ 写回</i><span>结果或异常</span></div>
    <p><strong>并发</strong>是多个任务都在推进；<strong>并行</strong>是同一时刻在不同 CPU 执行资源上运行。工作线程数不是 CPU 核心数，开更多线程不保证更快。FIFO 只约束队列取出顺序，不保证开始执行或完成顺序，也不保证调度公平性。</p>
    <div class="sim" aria-label="线程池状态模拟器"><h3>交互实验 · 2 个工作线程</h3><p class="small">这是可单步观察的逻辑模型，每个任务耗时设为 2 步；不会执行本机 C++/C#，不是性能测量。</p>
    <div id="pool-status" class="sim-status" aria-live="polite"></div><div class="actions"><button id="submit-one">提交 1 个</button><button id="submit-four">提交 4 个</button><button id="pool-step">推进一步</button><button id="close-pool">开始关闭</button><button id="reset-pool">重置</button></div>
    <div class="lane"><strong>等待队列</strong><div id="pool-queue"></div></div><div class="lane"><strong>工作线程</strong><div id="pool-workers"></div></div><div class="lane"><strong>已完成</strong><div id="pool-done"></div></div><div id="pool-log" class="output" aria-live="polite"></div></div>
    <p>操作顺序：提交 4 个 → 推进 1 步 → 开始关闭 → 继续推进。你会看到关闭后不再接收任务，但已经入队的任务继续执行。只有队列空且执行中的任务归零，关闭才可能结束。</p>''')

    body += section('atomic', '02', '原子操作：为什么 count++ 会丢更新', f'''
    <p>逻辑上的 <code>count++</code> 可拆成“读取旧值 → 加 1 → 写回”。两个线程同时读到 0，再各自写入 1，最终只有 1。原子读改写把这组更新作为不可分割的操作，避免两个更新互相覆盖。</p>
    <div class="sim"><h3>交互实验 · 让两个执行者各加一次</h3><label for="counter-mode">操作方式</label><select id="counter-mode"><option value="split">拆分读写（错误交错模型）</option><option value="atomic">原子读改写</option><option value="mutex">mutex 保护整个更新</option></select><div class="counter-value" id="counter-value">0</div><div class="actions"><button id="counter-next">下一步</button><button id="counter-reset">重新开始</button></div><div class="output" id="counter-log" aria-live="polite">两个执行者各加 1，最终应为 2。</div></div>
    <div class="note warning">{'C++ 对同一个普通变量进行无同步的冲突访问会构成数据竞争，属于未定义行为；结果不只是“可能少加几次”。因此工程不运行故意产生数据竞争的 C++ 代码。上方仅展示逻辑交错。' if cpp else 'C# 的普通自增不是原子操作，volatile 也不会把 ++ 变成原子读改写。演示使用安全的顺序步骤重放错误交错；工程中的真实多线程计数只采用正确同步。'}</div>
    {code('std::atomic<std::uint64_t> count{0};\ncount.fetch_add(1, std::memory_order_relaxed);\nauto progress = count.load(std::memory_order_relaxed);' if cpp else 'long count = 0;\nInterlocked.Increment(ref count);\nlong progress = Interlocked.Read(ref count);', '一个计数器的原子更新')}
    <p>{'fetch_add 返回更新前的旧值；默认原子操作使用 seq_cst，这里明确使用 relaxed，因为计数只用来显示进度，不承担其他数据的发布。relaxed 保证本原子变量的操作不被撕裂，却不把旁边的普通数组写入自动发布给观察者。读取结果用 future.get() 或相应同步。' if cpp else 'Interlocked.Increment 返回更新后的新值；Interlocked.Read 用于安全读取 long。Interlocked.CompareExchange 可在当前值等于预期值时写入新值，并返回旧值，适合实现条件更新。它们处理单个存储位置，不能自动把几个字段组成一个事务。'}</p>
    <h3>原子性、可见性、顺序性分别问什么</h3>
    {table(['概念','要回答的问题','本工程中的做法'],[
      ('原子性','另一个执行者能否插入一次读改写的中间？','计数器使用原子增量；队列操作使用锁。'),
      ('可见性','一个线程写好的结果，另一个线程何时可以读取？','通过结果句柄等待完成，或使用同一把锁建立同步。'),
      ('顺序性','普通数据写入与“数据准备好了”的标记，是否有正确先后关系？','任务交接由队列锁完成；不要用进度计数替代结果协议。')])}
    <p>{'扩展：release 写入和读取到相应值的 acquire 读取可以建立发布关系；seq_cst 还提供更强的全局顺序要求。这些都不会自动保护复杂容器。atomic 也不保证任何类型、任何平台都无锁，可查询 is_lock_free()。C++ volatile 不是线程同步工具。' if cpp else '扩展：Volatile.Read/Write 约束特定读取与写入的可见性和重排序，但 volatile 字段的 ++ 仍不具备原子性。C# 不直接提供 C++ 那样的 memory_order 参数；不要机械翻译 relaxed。'} </p>
    <p>源码中的原子计数：{jump('completed() const' if cpp else 'public long Completed', '读取完成数量')}。这里 completed 包含正常结束与抛出异常的任务；它表示工作包装器结束，不代表业务成功。</p>''')

    lock_snippet = '''std::mutex gate;
{
    std::lock_guard<std::mutex> lock(gate);
    // 在同一个临界区里完成检查和修改。
} // 正常离开或抛异常都会释放锁。
// condition_variable.wait 需要可暂时释放的 unique_lock。''' if cpp else '''private readonly object gate = new();
lock (gate)
{
    // 在同一个临界区里完成检查和修改。
} // 正常离开或抛异常都会释放 Monitor 锁。
// 本课程有 Monitor.Wait/PulseAll，必须使用同一个 object gate。'''
    body += section('mutex', '03', 'mutex：保护一组必须一起成立的事实', f'''
    <p>队列中的 <code>empty → front → pop</code> 必须整体互斥。如果只把长度改成原子变量，两个消费者仍可能取走同一元素。<strong>mutex 保护的是访问协议</strong>：所有接触这些共享状态的路径都要遵守同一把锁。</p>
    {code(lock_snippet, '用作用域管理锁的生命周期')}
    {table(['工具','适合保护什么','本课程的选择'],[
      ('std::mutex / C# lock(object)','进程内多个字段构成的不变量','保护队列、关闭标志和 active。'),
      ('std::atomic / Interlocked','一个计数或一个条件更新','对外提供无需取得队列锁的进度读取。'),
      ('条件变量 / Monitor.Wait','条件尚未满足时释放锁并阻塞等待','等待可取任务，或等待全部空闲。'),
      ('C# System.Threading.Mutex','需要操作系统互斥量，例如命名的跨进程互斥','本课程队列在进程内，选择 lock/Monitor；Mutex 用 WaitOne/ReleaseMutex，不能直接替换本实现的 Monitor 协议。'),
      ('Semaphore / SemaphoreSlim','限制可同时进入的数量，或表示可消费的许可','可用于有界队列；单凭许可不能代替队列状态的互斥保护。')])}
    <p>{'std::mutex 不可由同一个线程递归加锁；不要在持有它时再次调用会取得同一把锁的函数。lock_guard 负责固定作用域，unique_lock 允许等待期间临时解锁。两者都采用 RAII，避免手动 unlock 遗漏。' if cpp else '对 object 使用 lock 时采用 Monitor，允许同一线程重入。现代 C# 还支持 System.Threading.Lock 专用类型，但它与 Monitor 的等待协议不能混用；本例明确使用私有 object。不要锁 this、字符串或公开对象，否则外部代码可能参与同一把锁。'}</p>
    <div class="note"><strong>临界区要短：</strong>只在锁内操作任务队列与状态；执行用户函数、等待结果、Join 线程都在队列锁外进行。否则会把线程池串行化，甚至造成死锁。</div>''')

    wait_code = '''std::unique_lock<std::mutex> lock(mutex_);
ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
if (stopping_ && tasks_.empty()) break;
job = std::move(tasks_.front());
tasks_.pop();
++active_;''' if cpp else '''lock (_gate)
{
    while (_queue.Count == 0 && !_stopping)
        Monitor.Wait(_gate);
    if (_stopping && _queue.Count == 0) return;
    job = _queue.Dequeue();
    ++_active;
}'''
    body += section('wait', '04', '条件等待：检查条件，而不是记住通知', f'''
    <p>队列没有任务时，worker 不应该不断循环抢锁、检查、sleep。它先持锁查看条件，条件不满足就进入等待：<strong>释放锁并阻塞，醒来后重新取得锁，再检查条件</strong>。</p>
    {code(wait_code, '实际 WorkerLoop 的取任务片段')}
    <ol class="steps"><li><strong>先取得队列锁。</strong>保证看到的队列与关闭状态互相一致。</li><li><strong>条件不满足就等待。</strong>等待过程中不持有队列锁，生产者才能入队；等待协议连接“释放锁”和“开始等待”，避免自己制造检查与睡眠之间的空隙。</li><li><strong>醒来后重新判断。</strong>{'C++ 条件变量允许虚假唤醒；其他 worker 也可能先取走任务。带谓词的 wait 会反复检查。' if cpp else '收到通知仅表示可以重新竞争锁。另一个 worker 可能先取走任务；因此用 while，不能只用 if。'}</li><li><strong>只有关闭且队列为空时退出。</strong>关闭但队列不空时，继续消费已接收任务。</li></ol>
    <p>通知不储存“任务完成凭证”。若通知发生时没有等待者，下一次等待也不会自动消费旧通知。真实依据一直是锁保护下的条件，生产者必须在同一同步协议中修改条件。</p>
    <div class="note">{'C++ 使用 ready_ 等待可执行任务，用 idle_ 等待空闲，两个等待集合分开。提交时只需 ready_.notify_one()；关闭时 notify_all()；变空闲时 idle_.notify_all()。提交通知放在释放锁后，等待者醒来仍要重新取得锁。' if cpp else '本实现让 worker 和 WaitIdle 调用者共用 _gate 的 Monitor 等待集合。提交时使用 PulseAll，避免只唤醒了等待“空闲”的调用者而没有唤醒 worker。PulseAll 必须在持有该 Monitor 锁时调用；被通知者仍需等当前持锁者释放锁。'}</div>
    <p>完整等待循环：{jump('void worker_loop()' if cpp else 'private void WorkerLoop()', '进入 worker 循环')}。</p>''')

    fields = [
        ('tasks_ / _queue','待执行任务队列','队列锁；FIFO 取出，任务体在锁外执行。'),
        ('active_ / _active','已取出但尚未完成记账的任务数','队列锁；取出时 +1，完成时 −1。'),
        ('submitted_ / _submitted','成功入队的总数','队列锁；关闭后的拒绝不增加它。'),
        ('completed_ / _completed','包装器执行结束的总数','原子读改写；在更新 active 的同一临界区增加。'),
        ('stopping_ / _stopping','是否停止接收任务','队列锁；false → true，不再恢复。'),
        ('workers_ / _workers','构造时创建的固定线程集合','生命周期由所有者管理；关闭时逐个 join。'),
        ('shutdown_mutex_ / _shutdownGate','串行化多个外部关闭调用','独立关闭锁；join 时不持有队列锁。'),
        ('current_pool_ / _currentPool','当前线程正在为哪个池服务','线程局部变量；识别工作线程等待自身的误用。')]
    body += section('implementation', '05', '实现带读：沿着一个任务走完整个生命周期', f'''
    <p>先打开 <code>{esc(pool_path)}</code>。这一文件是线程池实现，示例程序只调用它；下面的源码链接会展开本页内嵌的真实源码快照，无需服务器。</p>
    {table(['字段（C++ / C#）','含义','谁负责同步'], fields)}
    <h3>01 · 构造：一次创建 N 个工作线程</h3><p>参数限定为 1..64。线程启动后立即进入等待循环；构造中途创建线程失败时，先设置关闭状态，再通知并 Join 已创建的线程，避免留下活着的半成品。{jump('explicit ThreadPool' if cpp else 'public FixedThreadPool(', '构造函数')}</p>
    <h3>02 · 提交：先包装结果，再在锁内接收</h3><p>{'submit 推导 callable 返回类型 R，用 packaged_task&lt;R()&gt; 保存调用和共享结果。packaged_task 只能移动；队列里的 std::function 需要可复制目标，因此用 shared_ptr 包住它。用户可以提交返回 void 的任务，也可以提交捕获 unique_ptr 的可移动 lambda。' if cpp else 'Submit&lt;T&gt; 使用 TaskCompletionSource&lt;T&gt; 创建结果句柄，把 Func&lt;T&gt; 包成 Action 入队。Action 调用成功就 SetResult，抛异常就 SetException。RunContinuationsAsynchronously 避免在设置结果时直接内联运行消费者的 continuation；continuation 的调度不属于这个自定义线程池的固定线程数。'}</p><p>取得队列锁后，检查关闭标志，入队成功，再增加 submitted。<strong>检查关闭与入队必须在同一临界区</strong>，否则关闭与提交竞态可能留下无人执行的任务。{jump('auto submit(' if cpp else 'public Task<T> Submit<T>', '提交任务')}</p>
    <h3>03 · 取出：让一个 worker 独占这份工作</h3><p>worker 在锁内弹出任务并增加 active，之后释放锁。其他 worker 此时可以继续取走其他任务，所以多个任务能够并行执行。{jump('void worker_loop()' if cpp else 'private void WorkerLoop()', '取出与执行')}</p>
    <h3>04 · 执行：结果与异常都返回给调用者</h3><p>{'packaged_task 把用户异常存入共享状态；future.get() 会在调用者线程重新抛出。future 只允许 get 一次，不需要结果时也可 wait。丢弃本实现 submit 返回的 future 不取消任务。' if cpp else 'TaskCompletionSource 把业务异常放入 Task，GetAwaiter().GetResult() 或 await 会观察到它。本页控制台示例使用同步等待方便跟读；UI 或异步调用链中宜 await，避免阻塞界面。Task 代表操作结果，不意味着每个 Task 都拥有一条线程。'}</p><p>{'future 就绪' if cpp else 'Task 完成'}与线程池完成记账之间存在一个很短的窗口：结果已经可读，但 active 还未减为 0。需要一致统计时，先调用 {'wait_idle()' if cpp else 'WaitIdle()'}。</p>
    <h3>05 · 完成记账：维护不变量</h3><p>在队列锁内增加 completed，减少 active；当队列空且 active==0，通知空闲等待者。持队列锁取得的快照满足：</p>
    <div class="note mono">submitted = queued + active + completed</div>
    <p>completed 的更新仍放在队列锁内，是为了让统计快照一致；它使用原子类型，是为了让独立的进度 getter 可以不取队列锁。这不是无锁队列。{jump('Stats stats()' if cpp else 'public Snapshot Stats', '统计快照')}</p>
    <h3>06 · 等待与关闭：调用者选择所需的保证</h3>
    {table(['接口','返回时的保证','仍需注意'],[
      ('future.get() / await Task','这一个任务的结果或异常已可观察','不保证其他任务完成；不保证池的记账已完成。'),
      ('wait_idle() / WaitIdle()','在锁内观察到队列为空且 active==0','只是一次空闲观察；并发生产者之后还可以提交。'),
      ('shutdown() / Shutdown()','停止接收，排空已接收任务，所有工作线程已退出','阻塞调用；任务不返回，它也不能返回。'),
      ('析构 / Dispose()','在有效的外部所有者生命周期内执行关闭','所有者要保证对象销毁时没有其他调用者再访问它。')])}
    <p>{jump('void wait_idle()' if cpp else 'public void WaitIdle()', '等待空闲')} · {jump('void shutdown()' if cpp else 'public void Shutdown()', '关闭流程')}。</p>''')

    body += section('lifetime', '06', '生命周期、异常与死锁边界', f'''
    <div class="flow"><span>Running<br>接收与执行</span><i>→ 关闭线性化点</i><span>Draining<br>拒绝新任务，处理旧任务</span><i>→ Join 全部完成</i><span>Stopped<br>关闭返回</span></div>
    <p>图中的三个名字是教学状态；实现使用 stopping 标志和 Join 过程表达它们，没有额外的状态枚举。关闭线性化点是持有队列锁时把 stopping 设为 true：在它前面入队成功的任务会执行，在它后面尝试入队的任务被拒绝。</p>
    <div class="grid"><div class="card"><h3>为什么不能持队列锁 Join</h3><p>关闭者持锁等 worker 退出；worker 需要同一把锁取任务或检查退出条件。两边互等，永远无法前进。实现只持独立关闭锁 Join。</p></div><div class="card"><h3>为什么 worker 不能等待自身空闲</h3><p>它本身就占着 active，只有它返回 active 才会减少。实现使用线程局部标记检测同池 WaitIdle/Shutdown 并抛异常。</p></div></div>
    <h3>嵌套等待仍然可能饿死整个池</h3><p>假设只有 1 个 worker：父任务向同池提交子任务，接着等待子任务结果；子任务还在队列里，唯一 worker 却被父任务占着。N 个 worker 全部这么做也会停住。自身等待检查无法识别任意 future/Task 依赖图；应由池外组织依赖，或使用专门的任务调度机制。</p>
    <h3>捕获的数据必须活到任务使用结束</h3><p>{'按值捕获 [i] 给每个任务独立的输入。按引用捕获 [&data] 要保证 data 在线程执行结束前不销毁，且并发写入区间互不重叠或受同步保护。不要把线程池放入它自己的任务中作为最后一个所有者；在 worker 内析构自身违背所有权契约，会导致终止。' if cpp else 'for 循环内先创建 int index = i，再捕获 index；直接捕获循环变量 i 会共享同一个变量。GC 保证对象仍被引用时不回收，不保证字段访问线程安全；Dispose 的资源也可能提前失效。using 必须覆盖任务使用资源的整个时期。'}</p>
    <div class="note warning"><strong>教学实现边界：</strong>固定线程数、无界队列、同步任务、优雅排空；不提供取消、超时关闭、优先级、工作窃取或背压。任务阻塞永不返回时，关闭也会一直等待。无界队列可能积压内存，生产系统需设计容量与拒绝策略。</div>
    <p>{'异常能通过 future 传播，不代表任务完成了业务操作；必须读取 future 并处理异常。异常任务同样增加 completed，因为它已结束执行。' if cpp else '本实现拒绝常见 Task/ValueTask 返回类型与直接 async void 委托；不要通过 object、同步包装或自定义 awaitable 绕过同步契约。它不流动 ExecutionContext，不等价于 .NET 内置 ThreadPool。日常 CPU 并行优先评估 Task.Run / Parallel；I/O 等待通常使用原生 async API。自定义池用于理解机制和明确需要独立线程的场景。'}</p>''')

    body += section('experiments', '07', '运行三个实验，用结果验证理解', f'''
    <h3>A · 返回值、异常和关闭</h3>
    {code(command+' --demo pool --workers 4 --tasks 17', '实验 A')}
    <p>提交 0² 到 16²，和应为 <strong>1496</strong>。再提交一个故意抛异常的任务，以及一个返回 42 的任务，验证 worker 仍能服务。空闲时 completed=19、queued=0、active=0。关闭后新提交应被拒绝。</p>
    <h3>B · 真实多线程：互斥计数与原子计数</h3>
    {code(command+' --demo counter --workers 4 --tasks 17 --iterations 10000', '实验 B')}
    <p>两个模式都应得到 <strong>17 × 10000 = 170000</strong>。先用 1 个 worker，再改成 4、8 个。共享一个计数器会争用同一位置；原子操作也有代价。可以进一步让每个任务先在局部累计，最后只做一次原子加法，减少共享写入。</p>
    <h3>C · 任务粒度与串行对照</h3>
    {code(command+' --demo benchmark --workers 4 --tasks 128 --iterations 100000', '实验 C')}
    <p>串行和线程池对同一组输入执行相同的无符号整数递推，并比较 XOR 校验值。每个任务都有独立 seed；结果参与输出。计时不含线程池构造，包含提交、排队和等待结果的成本；一个小任务先作预热。{'C++ 使用 steady_clock 测量经过时间。' if cpp else 'C# 使用 Stopwatch；JIT、分层编译和 GC 仍可能影响结果。'}</p>
    <p>把 iterations 从 10、1000 增到 100000，并记录串行毫秒、线程池毫秒及校验结果。任务太小，调度与同步成本可能超过并行收益；任务数少于 worker 数，也用不满工作线程。<strong>不能把一次更快当成普遍规律，也不要用这两个小程序给语言性能排名。</strong></p>
    {table(['参数','默认值','范围 / 含义'],[('--demo','all','all / pool / counter / benchmark'),('--workers','4','1..64，固定线程数'),('--tasks','16','1..10000，提交任务数'),('--iterations','20000','1..1000000，每个任务的工作次数'),('--help','—','查看帮助'+('；--test 运行自检' if not cpp else '；自检使用独立的 thread_pool_tests'))])}
    <div class="sim"><h3>粒度计算器 · 简化数学模型</h3><p class="small">假设任务等长、每个线程占用独立计算资源、调度成本全部串行相加；忽略缓存、竞争和系统抢占。不是本机实测。</p>
    <label>任务数 <input id="calc-tasks" type="number" min="1" max="10000" value="16"></label><label>线程数 <input id="calc-workers" type="number" min="1" max="64" value="4"></label><label>每任务计算 ms <input id="calc-cost" type="number" min="0.001" max="10000" step="0.01" value="1"></label><label>每任务调度 ms <input id="calc-overhead" type="number" min="0" max="10000" step="0.01" value="0.05"></label><p class="mono">Tserial=N×C；Tpool≈ceil(N/P)×C+N×H</p><div id="calc-result" class="output" aria-live="polite"></div></div>
    <h3>可重复的正确性检查</h3>{code('powershell -NoProfile -ExecutionPolicy Bypass -File .\\scripts\\test.ps1', '构建两份工程并运行测试 · 单个进程超时 60 秒')}
    <p>每种语言有 7 组测试：结果/异常、多生产者恰好执行一次、并发上限与空闲等待、队列空但任务仍活跃、并发关闭与拒绝、worker 自身等待保护、生命周期与参数边界。测试使用事件或 promise 协调，不依赖先 sleep 再猜测状态；短超时只用于检查等待未提前返回。日志保存在本课程 <code>validation</code> 目录。</p>''')

    exercises = [
        ('为什么不能只检查 queue.empty()？','worker 会先把任务取出再执行，此时队列可能已空。必须同时检查 active==0；测试 empty queue vs active task 专门覆盖它。'),
        ('把任务体移到队列锁内会怎样？','同一时刻只有一个任务能执行，并行度退化；若任务又提交到本池，C++ 普通 mutex 的递归加锁还会导致死锁风险。无论语言，都应把用户代码放在锁外。'),
        ('completed==submitted 能否随时作为销毁对象的条件？','不能独立读取两个计数就推断稳定空闲；生产者可能继续提交，结果和记账也有短暂时间差。用队列锁下的不变量、WaitIdle 的观察，或 Shutdown 停止接收并 Join；销毁还必须由外部所有者协调其他访问者。'),
        ('如何给队列增加容量上限？','在队列锁内检查容量；满时先选择明确策略：拒绝提交、阻塞生产者或等待超时。阻塞生产者需要另一个“有空位”的条件；关闭必须唤醒它们，worker 提交也要考虑池耗尽死锁。增加测试：满队列、并发生产者、关闭时仍有生产者等待。'),
        ('如何减少计数器上的竞争？','每个任务使用局部变量完成 iterations 次累计，结束时只加一次到共享计数器。锁/原子更新从 tasks×iterations 次降到 tasks 次，但仍要验证总和。'),
        ('为什么关闭不等于取消？','当前契约承诺排空已接收的任务。取消需要另行定义未执行任务的结果状态、运行中任务的协作检查和取消与成功的竞态。不能安全地强杀任意正在执行的线程。')]
    body += section('practice', '08', '练习与常见误区', ''.join(f'<details><summary>{i:02d} · {q}</summary><p>{a}</p></details>' for i,(q,a) in enumerate(exercises,1)) + '''
    <p>建议扩展顺序：增加每任务局部计数 → 实现拒绝策略的有界队列 → 为关闭竞态补测试 → 最后才考虑优先级、取消和更复杂调度。每加一个状态，都要回答它由哪把锁保护、由谁通知、在关闭时如何终结。</p>''')

    sources = ''
    for idx, source_path in enumerate(source_paths):
        raw = (ROOT / source_path).read_bytes()
        source_text = raw.decode('utf-8-sig')
        rendered = ''.join(f'<span class="line" id="source-{idx}-L{n}"><a class="line-number" href="#source-{idx}-L{n}" aria-label="第 {n} 行">{n}</a>{esc(line)}</span>' for n, line in enumerate(source_text.splitlines(), 1))
        relative = Path(source_path).relative_to(lang).as_posix()
        sources += f'<details class="source" id="source-{idx}"><summary>{esc(source_path)} · {len(source_text.splitlines())} 行</summary><div class="signature">SHA-256 {hashlib.sha256(raw).hexdigest()} · <a href="{esc(relative)}">打开原文件</a></div><div class="code"><div class="code-title">完整源码 · 行号可定位</div><pre><code>{rendered}</code></pre></div></details>'
    body += section('sources', '09', '完整源码：实现、示例与验证', '<p>以下内容由真实工程文件生成，不是另外维护的伪代码。修改源码后运行 <code>python scripts/generate_lessons.py</code>，更新两份 HTML 快照。只阅读教材不需要 Python。</p>' + sources)

    refs = [('C++ 标准工作草案 · mutex','https://eel.is/c++draft/thread.mutex.requirements.mutex'),('C++ 标准工作草案 · condition_variable','https://eel.is/c++draft/thread.condition.condvar'),('C++ 标准工作草案 · 原子内存序','https://eel.is/c++draft/atomics.order'),('C++ 标准工作草案 · packaged_task','https://eel.is/c++draft/futures.task')] if cpp else [('Microsoft Learn · lock 语句','https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/statements/lock'),('Microsoft Learn · Monitor.Wait','https://learn.microsoft.com/en-us/dotnet/api/system.threading.monitor.wait?view=net-10.0'),('Microsoft Learn · Interlocked','https://learn.microsoft.com/en-us/dotnet/api/system.threading.interlocked?view=net-10.0'),('Microsoft Learn · TaskCreationOptions','https://learn.microsoft.com/en-us/dotnet/api/system.threading.tasks.taskcreationoptions?view=net-10.0'),('Microsoft Learn · Mutex','https://learn.microsoft.com/en-us/dotnet/standard/threading/mutexes'),('Microsoft Learn · .NET 内置线程池','https://learn.microsoft.com/en-us/dotnet/standard/threading/the-managed-thread-pool')]
    body += section('references', '10', '参考资料与两种语言的对应关系', table(['用途','C++','C#'],[('执行者','std::thread','Thread'),('进程内互斥','std::mutex + lock_guard/unique_lock','lock(object) / Monitor'),('条件等待','condition_variable.wait(predicate)','while + Monitor.Wait'),('通知','notify_one / notify_all','本例采用 Monitor.PulseAll'),('原子计数','atomic.fetch_add','Interlocked.Increment'),('结果与异常','packaged_task / future','TaskCompletionSource / Task'),('资源生命周期','析构 / RAII','using / IDisposable')]) + '<p>概念相通，但不要逐字机械翻译：C++ mutex 不可重入，C# Monitor 可重入；C++ 原子操作可选择内存序，C# Interlocked 没有同名参数。</p><ul>' + ''.join(f'<li><a href="{url}" target="_blank" rel="noopener">{label}</a></li>' for label,url in refs) + '</ul><p class="small">文档查阅日期：2026-10-28。网页参考需要联网，教材正文、交互演示和源码快照均可离线使用。C++ 参考是持续更新的工作草案，本工程仅使用 C++17 已支持的设施。</p>')

    navigation = [('start','00 · 开始与运行'),('model','01 · 线程池模型'),('atomic','02 · 原子操作'),('mutex','03 · mutex 与锁'),('wait','04 · 条件等待'),('implementation','05 · 实现带读'),('lifetime','06 · 关闭与死锁'),('experiments','07 · 可运行实验'),('practice','08 · 练习与答案'),('sources','09 · 完整源码'),('references','10 · 语言对照与参考')]
    return f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="color-scheme" content="light"><meta name="description" content="{esc(title)} 线程池实现课程，包含原子操作、mutex、条件等待、异常传递、优雅关闭、交互演示及完整源码。"><title>{esc(title)} · 从零实现线程池</title><style>{CSS}</style></head>
<body class="{lang}"><a class="skip" href="#start">跳到正文</a><aside><a class="brand" href="#top">{'C++' if cpp else 'C#'} · 并发实验室</a><div class="caption">THREAD POOL / FROM FIRST PRINCIPLES</div><nav aria-label="课程目录">{''.join(f'<a href="#{id_}">{text}</a>' for id_,text in navigation)}</nav><div class="side-footer">独立课程 · 单文件离线教材<br><a href="{other}">切换到另一种语言 ↗</a><br><a href="../README.md">工程运行说明 ↗</a></div></aside>
<main id="top"><div class="toolbar"><div class="search-box"><label for="search" hidden>搜索章节与源码</label><input id="search" type="search" aria-label="搜索章节与源码" placeholder="搜索：原子、mutex、Submit、关闭…" autocomplete="off" aria-controls="search-results"><div id="search-results" class="search-results" hidden></div></div><button id="print">打印 / PDF</button></div>
<header><div class="eyebrow">{esc(title)} / CONCURRENCY WORKSHOP</div><h1>从零实现线程池，<br>看懂每一次同步。</h1><p class="lead">从两个线程的一次加法开始，到一个能返回结果、传递异常、排空任务的固定线程池。用源码与可运行实验，把“为什么要加锁”讲清楚。</p><div class="tags"><span class="tag">{esc(title)}</span><span class="tag">中文带读 · 完整工程</span><span class="tag">无需联网阅读</span><span class="tag">独立于原有课程</span></div><div class="overview"><div><b>3</b><small>可运行对照实验</small></div><div><b>7</b><small>并发契约测试组</small></div><div><b>2 + 1</b><small>状态演示与粒度计算器</small></div></div></header>
<noscript><div class="note">JavaScript 已关闭。正文与源码仍可阅读，搜索、复制及交互实验需启用 JavaScript。</div></noscript>
{body}<footer>{esc(short)} 线程池课程 · 源码为唯一实现依据 · 2026-10-28<br>本页所有样式与脚本均已内嵌，不依赖 CDN、字体下载或本地服务器。</footer></main><script>{JS}</script></body></html>'''


if __name__ == '__main__':
    for language in ('cpp', 'csharp'):
        target = ROOT / language / 'lesson.html'
        target.write_text(make_lesson(language), encoding='utf-8', newline='\n')
        print(f'Generated {target} ({target.stat().st_size:,} bytes)')
