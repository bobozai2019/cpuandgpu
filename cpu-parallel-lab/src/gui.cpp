#include "engine.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>

namespace {
enum ControlId { MODE=101, WORKERS, SAMPLES, TILE, RUN, COMPARE, CANCEL, EXPORT, HELP, PAGE };
constexpr COLORREF Bg=RGB(12,19,31), Panel=RGB(22,33,49), Ink=RGB(230,238,249), Muted=RGB(151,170,193), Accent=RGB(75,218,191);
COLORREF Color(int i) {
    static constexpr COLORREF colors[]{RGB(68,208,184),RGB(99,166,252),RGB(249,184,83),RGB(196,139,249),
        RGB(244,122,145),RGB(124,208,102),RGB(83,206,232),RGB(235,150,88)};
    return colors[std::max(0,i)%8];
}
std::wstring Number(double n, int precision=2) {std::wostringstream s;s<<std::fixed<<std::setprecision(precision)<<n;return s.str();}
std::wstring ModeLabel(lab::Mode mode) {
    return mode==lab::Mode::Serial ? L"单线程" : mode==lab::Mode::Static ? L"静态分块" : L"动态取任务";
}
void Fill(HDC dc, RECT rect, COLORREF color) {HBRUSH b=CreateSolidBrush(color);FillRect(dc,&rect,b);DeleteObject(b);}
struct App {
    HWND window{};
    HFONT normal{},title{},big{},small{};
    std::unique_ptr<lab::Engine> engine;
    lab::Snapshot snapshot;
    std::vector<uint32_t> image;
    std::optional<lab::Result> result, baseline;
    lab::Config target;
    bool busy=false, compareNext=false;
    int page=0;
    std::wstring message=L"先点“一键对照”：相同画面先单线程计算，再用当前并行策略计算。";
    std::filesystem::path root;
    HWND Control(int id) const {return GetDlgItem(window,id);}
    void Text(HDC dc, int x,int y,int w,int h,const std::wstring& text, COLORREF color=Ink,HFONT font=nullptr) {
        SelectObject(dc,font?font:normal);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);
        RECT r{x,y,x+w,y+h};DrawTextW(dc,text.c_str(),-1,&r,DT_LEFT|DT_TOP|DT_WORDBREAK|DT_NOPREFIX);
    }
    void Add(const wchar_t* klass,const wchar_t* label,int id,int x,int y,int w,int h,DWORD style=0) {
        HWND child=CreateWindowW(klass,label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|style,x,y,w,h,window,reinterpret_cast<HMENU>(INT_PTR(id)),nullptr,nullptr);
        SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(normal),TRUE);
    }
    void Combo(int id,int x,int w,std::initializer_list<const wchar_t*> items,int selected) {
        Add(L"COMBOBOX",L"",id,x,119,w,300,CBS_DROPDOWNLIST|WS_VSCROLL);
        for(auto item:items) SendMessageW(Control(id),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
        SendMessageW(Control(id),CB_SETCURSEL,selected,0);
    }
    int Value(int id) {wchar_t buffer[32]{};GetWindowTextW(Control(id),buffer,32);return _wtoi(buffer);}
    void Init(HWND hwnd) {
        window=hwnd;
        normal=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        small=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        title=CreateFontW(-28,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Microsoft YaHei UI");
        big=CreateFontW(-32,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);
        root=std::filesystem::path(path).parent_path().parent_path().parent_path();
        Combo(MODE,24,168,{L"单线程",L"静态分块",L"动态取任务"},2);
        Combo(WORKERS,208,94,{L"1",L"2",L"3",L"4",L"6",L"8",L"12",L"16",L"32",L"64"},lab::HardwareThreads()>=8?5:lab::HardwareThreads()>=4?3:0);
        Combo(SAMPLES,318,94,{L"4",L"8",L"16",L"32",L"64",L"128",L"256"},5);
        Combo(TILE,428,94,{L"4",L"8",L"16",L"32",L"64",L"128",L"256"},3);
        Add(L"BUTTON",L"开始计算",RUN,542,117,112,32);
        Add(L"BUTTON",L"一键对照",COMPARE,666,117,112,32);
        Add(L"BUTTON",L"取消",CANCEL,790,117,76,32);
        Add(L"BUTTON",L"导出结果",EXPORT,878,117,112,32);
        Add(L"BUTTON",L"教学说明",HELP,1002,117,112,32);
        Add(L"BUTTON",L"下一组线程",PAGE,1126,117,120,32);
        EnableWindow(Control(CANCEL),FALSE);EnableWindow(Control(EXPORT),FALSE);
        SetTimer(window,1,80,nullptr);
    }
    ~App() {engine.reset();for(auto f:{normal,title,big,small})if(f)DeleteObject(f);}
    void Controls(bool enabled) {
        for(int id:{MODE,WORKERS,SAMPLES,TILE,RUN,COMPARE})EnableWindow(Control(id),enabled);
        EnableWindow(Control(CANCEL),!enabled);
        EnableWindow(Control(EXPORT),enabled && result.has_value() && !result->snapshot.cancelled);
    }
    void Start(bool compare) {
        if(busy)return;
        target={};target.samples=Value(SAMPLES);target.tile=Value(TILE);
        target.mode=static_cast<lab::Mode>(SendMessageW(Control(MODE),CB_GETCURSEL,0,0));
        if(compare && target.mode==lab::Mode::Serial) {
            target.mode=lab::Mode::Dynamic;SendMessageW(Control(MODE),CB_SETCURSEL,2,0);
        }
        const unsigned count=unsigned(Value(WORKERS));
        if(!engine || engine->Size()!=count)engine=std::make_unique<lab::Engine>(count);
        compareNext=compare;page=0;result.reset();
        auto c=target;if(compare)c.mode=lab::Mode::Serial;
        Begin(c);
    }
    void Begin(const lab::Config& config) {
        engine->Start(config);busy=true;Controls(false);
        snapshot=engine->Poll();image=engine->Image();
        message=compareNext?L"对照 1 / 2：建立相同工作量的单线程基准……":L"正在计算：彩色边框表示线程归属；已完成的块才会发布到画面。";
    }
    void Tick() {
        if(!busy)return;
        snapshot=engine->Poll();image=engine->Image();
        if(!snapshot.running) {
            result=engine->Finish();snapshot=result->snapshot;image=result->pixels;busy=false;
            if(!snapshot.cancelled && snapshot.config.mode==lab::Mode::Serial)baseline=*result;
            if(compareNext && !snapshot.cancelled) {
                compareNext=false;Begin(target);message=L"对照 2 / 2：计算相同画面，观察线程重叠、尾部等待和最后的加速比。";
            } else {
                compareNext=false;Controls(true);
                message=snapshot.cancelled?L"已取消；未完成的图块不会作为有效结果参与比较。":L"计算完成。时间线保留真实任务区间；可导出图像、指标和逐块 trace。";
                if(baseline && lab::SameWork(baseline->snapshot.config,snapshot.config) && !snapshot.cancelled) {
                    if(baseline->pixels!=result->pixels)throw std::runtime_error("serial/parallel pixel mismatch");
                    message+=L" 逐像素校验：一致。";
                }
            }
        }
        InvalidateRect(window,nullptr,FALSE);
    }
    void Export() {
        if(busy || !result || result->snapshot.cancelled)return;
        SYSTEMTIME t{};GetLocalTime(&t);wchar_t name[80]{};
        swprintf_s(name,L"run-%04u%02u%02u-%02u%02u%02u-%03u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);
        const auto dir=root/L"out"/name;
        lab::SaveImage(*result,dir/L"render.bmp");lab::SaveReport(*result,dir/L"result.json");lab::SaveTrace(*result,dir/L"timeline.trace.json");
        if(baseline && lab::SameWork(baseline->snapshot.config,result->snapshot.config))lab::SaveReport(*baseline,dir/L"serial-baseline.json");
        message=L"已导出到 "+dir.wstring();InvalidateRect(window,nullptr,FALSE);
        ShellExecuteW(window,L"open",dir.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    }
    void Paint(HDC dc,int width,int height) {
        Fill(dc,{0,0,width,height},Bg);
        Text(dc,24,18,940,42,L"CPU 并发实验室  /  看见多核一起工作",Ink,title);
        Text(dc,25,62,1200,26,L"真实 CPU 光线追踪 · 固定输入 · 持久线程池 · 单线程 / 静态分块 / 动态取任务",Muted);
        Text(dc,width-210,28,190,27,std::to_wstring(lab::HardwareThreads())+L" 个逻辑处理器",Accent);
        Text(dc,24,94,170,24,L"调度策略",Muted,small);Text(dc,208,94,100,24,L"工作线程",Muted,small);
        Text(dc,318,94,100,24,L"每像素采样",Muted,small);Text(dc,428,94,100,24,L"图块边长",Muted,small);
        Text(dc,24,165,width-48,44,message,Accent);
        const int rows=std::min(8,std::max(1,snapshot.workers));
        const int imageWidth=std::min({800,width-490,(height-233-46-64-rows*12)*640/360}), imageHeight=imageWidth*360/640;
        const int left=24,top=233,right=left+imageWidth+24,panelWidth=width-right-24;
        Text(dc,left,207,imageWidth,25,L"01  计算结果  ·  块边框颜色 = worker 编号",Ink);
        Fill(dc,{left,top,left+imageWidth,top+imageHeight},Panel);
        if(!image.empty()) {
            BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=snapshot.config.width;
            info.bmiHeader.biHeight=-snapshot.config.height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,COLORONCOLOR);
            StretchDIBits(dc,left,top,imageWidth,imageHeight,0,0,snapshot.config.width,snapshot.config.height,image.data(),&info,DIB_RGB_COLORS,SRCCOPY);
            for(const auto& t:snapshot.tiles) {
                if(!t.state)continue;
                const int x=left+t.x*imageWidth/snapshot.config.width,y=top+t.y*imageHeight/snapshot.config.height;
                const int r=left+(t.x+t.w)*imageWidth/snapshot.config.width,b=top+(t.y+t.h)*imageHeight/snapshot.config.height;
                HPEN pen=CreatePen(PS_SOLID,t.state==1?2:1,Color(t.owner));auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
                Rectangle(dc,x,y,r,b);SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(pen);
            }
        } else Text(dc,left+36,top+imageHeight/2-35,imageWidth-72,80,L"每个像素都要追踪光线、多次反射并求平均。\n增加采样数，会增加有实际产出的 CPU 计算。",Muted);
        Text(dc,right,207,panelWidth,25,L"02  实测指标",Ink);
        Fill(dc,{right,top,width-24,top+imageHeight},Panel);
        const int x=right+20;
        Text(dc,x,top+16,panelWidth-40,24,L"整批计算墙钟时间  /  包含派发与汇合",Muted,small);
        Text(dc,x,top+45,panelWidth-40,46,Number(snapshot.wallMs/1000)+L" s",Ink,big);
        const bool valid=result && !busy && !snapshot.cancelled;
        const bool comparable=valid && baseline && lab::SameWork(baseline->snapshot.config,snapshot.config) && baseline->pixels==result->pixels;
        Text(dc,x,top+102,panelWidth-40,25,L"相对单线程加速",Muted,small);
        Text(dc,x,top+128,panelWidth-40,43,comparable?Number(baseline->snapshot.wallMs/snapshot.wallMs)+L" ×":L"—",Accent,big);
        Text(dc,x,top+182,panelWidth-40,26,L"平均 CPU 并行度  =  Σ线程 CPU 时间 / 墙钟",Muted,small);
        Text(dc,x,top+210,panelWidth-40,38,valid?Number(snapshot.cpuMs/snapshot.wallMs)+L" 个逻辑 CPU":L"完成后统计",Ink,title);
        Text(dc,x,top+262,panelWidth-40,25,L"图块进度  "+std::to_wstring(snapshot.completed)+L" / "+std::to_wstring(snapshot.total),Ink);
        const int barWidth=panelWidth-40;
        Fill(dc,{x,top+296,x+barWidth,top+302},Bg);
        if(snapshot.total)Fill(dc,{x,top+296,x+barWidth*snapshot.completed/snapshot.total,top+302},Accent);
        int active=0;for(const auto& t:snapshot.tiles)if(t.state==1)++active;
        Text(dc,x,top+316,panelWidth-40,60,busy?L"正在执行的任务区间："+std::to_wstring(active)+L"\n线程处于区间内，也可能被操作系统暂停。":
            valid?L"线程 CPU 总时间："+Number(snapshot.cpuMs/1000)+L" s\n"+(comparable?L"相同输入逐像素校验通过":L"先运行单线程或点击一键对照"):L"取消结果不参与性能比较。",Muted,small);
        const int timelineTop=top+imageHeight+46;
        Text(dc,24,timelineTop-32,width-48,27,L"03  线程时间线  ·  横轴为同一批次的真实墙钟时间，空白表示没有图块任务",Ink);
        const int rowHeight=std::clamp((height-timelineTop-64)/rows,10,29);
        const int plotX=95,plotW=width-335;
        Fill(dc,{24,timelineTop,width-24,timelineTop+rows*rowHeight+10},Panel);
        const int first=page*8;
        for(int row=0;row<rows && first+row<std::max(1,snapshot.workers);++row) {
            const int worker=first+row,y=timelineTop+row*rowHeight+4;
            Text(dc,34,y,57,rowHeight,L"W"+std::to_wstring(worker),Color(worker),small);
            Fill(dc,{plotX,y+3,plotX+plotW,y+rowHeight-3},Bg);
            for(const auto& t:snapshot.tiles)if(t.state && t.owner==worker && snapshot.wallMs>0) {
                const int a=plotX+int(t.begin/snapshot.wallMs*plotW),b=plotX+int(t.end/snapshot.wallMs*plotW);
                Fill(dc,{a,y+3,std::max(a+1,b),y+rowHeight-3},Color(worker));
            }
            if(worker<int(snapshot.stats.size()) && !busy)
                Text(dc,plotX+plotW+12,y,210,rowHeight,std::to_wstring(snapshot.stats[worker].tiles)+L" 块 / CPU "+Number(snapshot.stats[worker].cpuMs,0)+L" ms",Muted,small);
        }
        const int footer=timelineTop+rows*rowHeight+17;
        Text(dc,plotX,footer,80,24,L"0 ms",Muted,small);
        Text(dc,plotX+plotW-100,footer,100,24,Number(snapshot.wallMs,0)+L" ms",Muted,small);
        Text(dc,plotX+plotW+12,footer,210,24,L"线程组 "+std::to_wstring(page+1)+L" / "+std::to_wstring(std::max(1,(snapshot.workers+7)/8)),Muted,small);
        Text(dc,24,height-27,width-48,25,L"640 × 360 · 5 次反射上限   |   时间线重叠 ≠ 同时占用物理核心   |   较短任务的 CPU 时间会受系统记账粒度影响",Muted,small);
    }
};
App* app=nullptr;
LRESULT CALLBACK WindowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    try {
        switch(msg) {
        case WM_CREATE:app->Init(hwnd);return 0;
        case WM_GETMINMAXINFO:{auto p=reinterpret_cast<MINMAXINFO*>(lp);p->ptMinTrackSize={1280,960};return 0;}
        case WM_COMMAND:
            switch(LOWORD(wp)) {
            case RUN:app->Start(false);break;
            case COMPARE:app->Start(true);break;
            case CANCEL:if(app->engine){app->compareNext=false;app->engine->Cancel();}break;
            case EXPORT:app->Export();break;
            case HELP:{auto path=app->root/L"lesson.html";ShellExecuteW(hwnd,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
            case PAGE:app->page=(app->page+1)%std::max(1,(app->snapshot.workers+7)/8);break;
            }
            InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_TIMER:app->Tick();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_SIZE:InvalidateRect(hwnd,nullptr,FALSE);return 0;
        case WM_PAINT:{
            PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
            HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,r.right),std::max(1L,r.bottom));
            auto old=SelectObject(buffer,bitmap);app->Paint(buffer,r.right,r.bottom);
            BitBlt(dc,0,0,r.right,r.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(hwnd,&ps);return 0;
        }
        case WM_CLOSE:DestroyWindow(hwnd);return 0;
        case WM_DESTROY:KillTimer(hwnd,1);PostQuitMessage(0);return 0;
        }
    } catch(const std::exception& e) {
        if(app->engine){app->engine->Cancel();try{app->engine->Wait();}catch(...){}}
        app->busy=false;app->compareNext=false;app->Controls(true);
        MessageBoxA(hwnd,e.what(),"CPU lab error",MB_ICONERROR);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
} // namespace
// In-process GUI regression: call the same controller and paint code as the
// interactive window. No desktop capture / injected input / external UI driver.
static int VerifyUi(App& state, HWND hwnd) {
    try {
        auto check=[](bool yes,const char* why){if(!yes)throw std::runtime_error(why);};
#ifdef NDEBUG
        SendMessageW(state.Control(SAMPLES),CB_SETCURSEL,5,0);
        const auto previewName=L"ui-verification";
#else
        SendMessageW(state.Control(SAMPLES),CB_SETCURSEL,0,0);
        const auto previewName=L"ui-verification-debug";
#endif
        SendMessageW(state.Control(WORKERS),CB_SETCURSEL,3,0);
        state.Start(true);
        check(state.busy && !IsWindowEnabled(state.Control(RUN)),"controls must lock while running");
        state.engine->Wait();state.Tick();
        check(state.busy && state.baseline.has_value(),"comparison must advance to parallel phase");
        state.engine->Wait();state.Tick();
        check(!state.busy && state.result && state.baseline->pixels==state.result->pixels,"comparison did not finish correctly");
        check(IsWindowEnabled(state.Control(EXPORT)),"completed result must allow export");
        RECT r{};GetClientRect(hwnd,&r);
        HDC dc=CreateCompatibleDC(nullptr);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;
        info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        void* data=nullptr;HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
        check(bitmap!=nullptr,"DIB allocation failed");auto old=SelectObject(dc,bitmap);
        state.Paint(dc,r.right,r.bottom);
        // Native child controls render themselves into our test canvas.
        for(HWND child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&rect),2);
            const int saved=SaveDC(dc);SetViewportOrgEx(dc,rect.left,rect.top,nullptr);
            SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_NONCLIENT|PRF_CHILDREN);
            RestoreDC(dc,saved);
        }
        // Hidden comboboxes omit their selected text in WM_PRINT on some Windows
        // themes; paint the actual selected string in this offscreen preview.
        for(int id:{MODE,WORKERS,SAMPLES,TILE}) {
            RECT rect{};GetWindowRect(state.Control(id),&rect);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&rect),2);
            wchar_t text[64]{};GetWindowTextW(state.Control(id),text,64);
            check(text[0]!=0,"combobox lost its selection");
            Fill(dc,{rect.left+3,rect.top+3,rect.right-23,rect.bottom-3},Bg);
            state.Text(dc,rect.left+6,rect.top+2,rect.right-rect.left-29,rect.bottom-rect.top-4,text);
        }
        GdiFlush();
        lab::Result preview;preview.snapshot.config.width=r.right;preview.snapshot.config.height=r.bottom;
        auto begin=static_cast<uint32_t*>(data);preview.pixels.assign(begin,begin+size_t(r.right)*r.bottom);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
        lab::SaveImage(preview,state.root/L"out"/(std::wstring(previewName)+L".bmp"));
        lab::SaveReport(*state.result,state.root/L"out"/(std::wstring(previewName)+L".json"));
        SendMessageW(state.Control(SAMPLES),CB_SETCURSEL,6,0);
        state.Start(true);state.compareNext=false;state.engine->Cancel();state.engine->Wait();state.Tick();
        check(!state.busy && state.result->snapshot.cancelled,"cancel must stop the whole comparison");
        check(!IsWindowEnabled(state.Control(EXPORT)),"cancelled result must not export");
        SendMessageW(state.Control(SAMPLES),CB_SETCURSEL,0,0);
        state.Start(false);state.engine->Wait();state.Tick();
        check(!state.busy && !state.result->snapshot.cancelled,"restart after cancel failed");
        return 0;
    } catch(const std::exception& e) {
        OutputDebugStringA(e.what());return 1;
    }
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR arguments,int show) {
    SetProcessDPIAware();
    App state;app=&state;
    WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=L"CpuParallelLab";
    cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    HWND hwnd=CreateWindowW(cls.lpszClassName,L"CPU 并发实验室 · CPU Parallel Lab",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1360,960,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 1;
    if(std::wstring(arguments)==L"--self-test") {const int code=VerifyUi(state,hwnd);DestroyWindow(hwnd);return code;}
    ShowWindow(hwnd,show);UpdateWindow(hwnd);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0) {
        if(!IsDialogMessageW(hwnd,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
    }
    return 0;
}
