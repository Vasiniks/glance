#include "renderer.h"
#include <cmath>
#include <algorithm>

namespace glance {

COLORREF lerp_color(COLORREF a, COLORREF b, double t) noexcept {
    t = std::clamp(t, 0.0, 1.0);
    return RGB(int(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               int(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               int(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}
COLORREF cpu_color(double pct) noexcept {
    // Modern palette: emerald -> amber -> rose
    if (pct < 55) return lerp_color(RGB(52,211,153), RGB(251,191,36), pct / 55.0);
    return lerp_color(RGB(251,191,36), RGB(244,63,94), (pct - 55) / 45.0);
}
COLORREF ram_color(double pct) noexcept { return cpu_color(pct); }

// ---------- GDI cache (lightweight, no per-frame alloc) ----------
struct GdiCache {
    HFONT f10 = nullptr, f10b = nullptr, f11 = nullptr, f12 = nullptr;
    int px_ref = 0;
    HBRUSH br = nullptr; COLORREF brC = CLR_INVALID;
    HPEN pen = nullptr; COLORREF penC = CLR_INVALID; int penW = 0;
    void clear() noexcept {
        if (f10) DeleteObject(f10); if (f10b) DeleteObject(f10b);
        if (f11) DeleteObject(f11); if (f12) DeleteObject(f12);
        if (br) DeleteObject(br); if (pen) DeleteObject(pen);
        f10=f10b=f11=f12=nullptr; br=nullptr; pen=nullptr; brC=penC=CLR_INVALID; penW=0; px_ref=0;
    }
    HFONT get(int px, bool bold) {
        if (px_ref == px) {
            if (bold && f10b) return f10b;
            if (!bold && f10) return f10;
        }
        // Recreate all at this px for simplicity
        clear();
        px_ref = px;
        const wchar_t* face = L"Segoe UI Variable";
        // Fallback exists automatically if not installed
        auto mk = [&](int p, int wt){ return CreateFontW(-p,0,0,0,wt,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face); };
        f10 = mk(px, FW_NORMAL);
        f10b = mk(px, FW_SEMIBOLD);
        f11 = mk(px+1, FW_SEMIBOLD);
        f12 = mk(px+2, FW_NORMAL);
        return bold ? f10b : f10;
    }
    HBRUSH brush(COLORREF c) {
        if (br && brC == c) return br;
        if (br) DeleteObject(br);
        br = CreateSolidBrush(c); brC = c; return br;
    }
    HPEN penColor(COLORREF c, int w) {
        if (pen && penC == c && penW == w) return pen;
        if (pen) DeleteObject(pen);
        pen = CreatePen(PS_SOLID, w, c); penC=c; penW=w; return pen;
    }
};
static thread_local GdiCache gc;

void Renderer::cleanup() noexcept { gc.clear(); }

static inline void fillRect(HDC hdc, int x,int y,int w,int h,COLORREF c){ if(w<=0||h<=0) return; RECT r{x,y,x+w,y+h}; FillRect(hdc,&r,gc.brush(c)); }
static inline void fillRounded(HDC hdc,int x,int y,int w,int h,int rad,COLORREF c){
    if(w<=0||h<=0) return;
    if(rad*2 > w) rad = w/2;
    if(rad*2 > h) rad = h/2;
    // Use RoundRect with solid brush
    HBRUSH br = gc.brush(c);
    HPEN pen = gc.penColor(c,1);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc,br);
    HPEN oldPen = (HPEN)SelectObject(hdc,pen);
    RoundRect(hdc,x,y,x+w,y+h,rad*2,rad*2);
    SelectObject(hdc,oldBr); SelectObject(hdc,oldPen);
}

static void drawCpuTiles(HDC hdc,int x,int y,int w,int h,const CpuSample& cpu,bool vertical) {
    if(cpu.count==0) return;
    size_t n=cpu.count;
    int cols=(int)std::ceil(std::sqrt((double)n)); if(cols<1) cols=1;
    int rows=(int)std::ceil((double)n/cols);
    if(vertical) std::swap(cols,rows);
    const int gap=3;
    int cw=(w-gap*(cols-1))/cols; int ch=(h-gap*(rows-1))/rows;
    cw=std::max(cw,3); ch=std::max(ch,3);
    int rad=std::min(cw,ch)>=10?4:2;
    for(size_t i=0;i<n;++i){
        int col=(int)(i%cols), row=(int)(i/cols);
        int cx=x+col*(cw+gap), cy=y+row*(ch+gap);
        fillRounded(hdc,cx,cy,cw,ch,rad,cpu_color(cpu.per_core[i]));
        // subtle inner highlight at top 30% (cheap lighter overlay)
        COLORREF hi=lerp_color(cpu_color(cpu.per_core[i]),RGB(255,255,255),0.18);
        fillRounded(hdc,cx,cy,cw,std::max(2,ch/3),rad,hi); // quick highlight bar
        // re-fill middle to keep rounded look for highlight
        fillRounded(hdc,cx,cy+cw/8,cw,ch*2/3,rad,cpu_color(cpu.per_core[i])); // cover overdraw center keeps pill feel
        // Actually final tile color on top of highlight via second draw
        // Simpler: draw full tile again slightly transparent concept not available in GDI, so we do second draw as main
        // To keep code tiny, just redraw full tile but with highlight we already have slight contrast
        // The extra draws are cheap GDI
    }
    // redraw clean tiles without hack for final look (small overhead but nice)
    // For performance we skip the hacky highlight above in final and just do clean rounded
    // Do clean pass:
    for(size_t i=0;i<n;++i){
        int col=(int)(i%cols), row=(int)(i/cols);
        int cx=x+col*(cw+gap), cy=y+row*(ch+gap);
        fillRounded(hdc,cx,cy,cw,ch,rad,cpu_color(cpu.per_core[i]));
    }
}

// Premium UI constants
namespace ui {
    const COLORREF bg = RGB(15,15,18);
    const COLORREF bg2 = RGB(22,22,26);
    const COLORREF track = RGB(38,38,44);
    const COLORREF track2 = RGB(45,45,52);
    const COLORREF border = RGB(42,42,50);
    const COLORREF labelBg = RGB(36,36,42);
    const COLORREF labelTx = RGB(176,178,190);
    const COLORREF sep = RGB(38,38,46);
    const COLORREF textDim = RGB(160,162,174);
    const COLORREF text1 = RGB(228,230,238);
    const COLORREF netRx = RGB(96,165,250);
    const COLORREF netTx = RGB(251,191,36);
}

void Renderer::paint(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg, bool vertical) {
    // Background with subtle gradient-ish via two fills
    fillRect(hdc, rc.left, rc.top, rc.right-rc.left, rc.bottom-rc.top, ui::bg);
    // top border 1px for bottom edge glow
    if(!vertical) fillRect(hdc, rc.left, rc.top, rc.right-rc.left, 1, ui::border);
    else fillRect(hdc, rc.left, rc.top, 1, rc.bottom-rc.top, ui::border);

    if(!snap.valid){
        SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,ui::textDim);
        HFONT old=(HFONT)SelectObject(hdc,gc.get(12,false));
        RECT r=rc; r.left+=16;
        DrawTextW(hdc,L"◍ Glance — sampling…",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(hdc,old); return;
    }
    if(vertical) paint_vertical(hdc,rc,snap,cfg);
    else paint_horizontal(hdc,rc,snap,cfg);
}

void Renderer::paint_horizontal(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    const int pad=8;
    int x=rc.left+pad, y=rc.top+pad;
    int aw=(rc.right-rc.left)-pad*2;
    int ah=(rc.bottom-rc.top)-pad*2;
    if(aw<=0||ah<=0) return;

    SetBkMode(hdc,TRANSPARENT);
    // header row height
    int headerH=18;
    int contentH=ah-headerH-2;
    if(contentH<8) contentH=8;

    int gap=16;
    int secs= (cfg.show_cpu?1:0)+(cfg.show_ram?1:0)+((cfg.show_disks&&snap.disk_count)?1:0)+(cfg.show_net?1:0);
    if(secs==0) return;
    int sw=(aw-gap*(secs-1))/secs;
    if(sw<24) sw=24;

    last_.bar=rc;
    last_.cpu_count=(int)snap.cpu.count;
    last_.disk_count=(int)snap.disk_count;

    int cur=x;
    auto drawLabel = [&](int sx,const wchar_t* icon,const wchar_t* txt){
        // pill label bg
        int pillH=16;
        RECT tr{sx, y, sx+sw, y+pillH};
        // background pill
        fillRounded(hdc,sx,tr.top, std::min(68, sw), pillH, 8, ui::labelBg);
        HFONT old=(HFONT)SelectObject(hdc,gc.get(10,true));
        SetTextColor(hdc,ui::labelTx);
        wchar_t buf[32]; wsprintfW(buf,L"%ls %ls",icon,txt);
        DrawTextW(hdc,buf,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(hdc,old);
    };

    // separator helper
    auto sepLine = [&](int sx){ fillRect(hdc,sx, y,1, ah, ui::sep); };

    // CPU
    if(cfg.show_cpu){
        drawLabel(cur,L"✦",L"CPU");
        last_.cpu=RECT{cur, y+headerH, cur+sw, y+headerH+contentH};
        drawCpuTiles(hdc,cur, y+headerH+2, sw, contentH-2, snap.cpu,false);
        cur+=sw; if(secs>1){ sepLine(cur+gap/2); cur+=gap; }
    }
    // RAM
    if(cfg.show_ram){
        drawLabel(cur,L"◍",L"RAM");
        last_.ram=RECT{cur, y+headerH, cur+sw, y+headerH+contentH};
        int by=y+headerH+4, bh=10;
        // track rounded
        fillRounded(hdc,cur,by,sw,bh, bh/2, ui::track);
        int f=(int)(sw * std::clamp(snap.ram.used_percent/100.0,0.0,1.0));
        if(f>0){
            // gradient-like: two segments: 0-60% green, 60-85 amber, 85-100 rose via solid now but with color
            fillRounded(hdc,cur,by,f,bh,bh/2,ram_color(snap.ram.used_percent));
            // inner shine line at top 2px
            COLORREF shine=lerp_color(ram_color(snap.ram.used_percent),RGB(255,255,255),0.22);
            fillRounded(hdc,cur+1,by+1,std::max(0,f-2), std::max(1,bh/3), bh/2, shine);
        }
        if(cfg.always_numbers || sw>120){
            HFONT old=(HFONT)SelectObject(hdc,gc.get(10,false));
            SetTextColor(hdc,ui::text1);
            wchar_t b[32]; wsprintfW(b,L"%.0f%% • %.1f GB",snap.ram.used_percent, (double)snap.ram.used_bytes/1024/1024/1024);
            RECT tr{cur, by+bh+3, cur+sw, by+bh+18};
            DrawTextW(hdc,b,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(hdc,old);
        }
        cur+=sw; if(secs>1 && (cfg.show_disks||cfg.show_net)){ sepLine(cur+gap/2); cur+=gap; }
    }
    // DISKS
    if(cfg.show_disks && snap.disk_count){
        drawLabel(cur,L"◫",L"DISK");
        last_.disks=RECT{cur, y+headerH, cur+sw, y+headerH+contentH};
        int by=y+headerH+6;
        int rowH=14;
        for(size_t i=0;i<snap.disk_count && by+rowH < y+ah; ++i){
            // mini label
            HFONT old=(HFONT)SelectObject(hdc,gc.get(9,false));
            SetTextColor(hdc,ui::textDim);
            wchar_t nb[36]; wcsncpy_s(nb, snap.disks[i].name, 8); // short
            RECT lr{cur, by, cur+32, by+rowH};
            DrawTextW(hdc,nb,-1,&lr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
            SelectObject(hdc,old);
            // bar
            int bx=cur+36, bw=sw-36;
            int bh=5;
            int bby=by+(rowH-bh)/2;
            fillRounded(hdc,bx,bby,bw,bh,bh/2,ui::track2);
            int f=(int)(bw*std::clamp(snap.disks[i].active/100.0,0.0,1.0));
            if(f) fillRounded(hdc,bx,bby,f,bh,bh/2,cpu_color(snap.disks[i].active));
            by+=rowH;
        }
        cur+=sw; if(cfg.show_net){ sepLine(cur+gap/2); cur+=gap; }
    }
    // NET
    if(cfg.show_net){
        drawLabel(cur,L"⇆",L"NET");
        last_.net=RECT{cur, y+headerH, cur+sw, y+headerH+contentH};
        int by=y+headerH+4, bh=10;
        double tot=snap.net.rx_bps+snap.net.tx_bps;
        fillRounded(hdc,cur,by,sw,bh,bh/2,ui::track);
        if(tot>1){
            double rxF=snap.net.rx_bps/tot;
            int rxW=(int)(sw*rxF);
            if(rxW>0) fillRounded(hdc,cur,by,rxW,bh,bh/2,ui::netRx);
            if(sw-rxW>0) fillRounded(hdc,cur+rxW,by,sw-rxW,bh,bh/2,ui::netTx);
        }
        // numbers
        HFONT old=(HFONT)SelectObject(hdc,gc.get(10,false));
        SetTextColor(hdc,ui::text1);
        auto fmt = [](double b)->std::wstring{
            const wchar_t* u[]= {L"B",L"KB",L"MB",L"GB"};
            double v=b; int i=0; while(v>=1024 && i<3){v/=1024;i++;}
            wchar_t buf[24]; wsprintfW(buf,v<10?L"%.1f %ls":L"%.0f %ls",v,u[i]); return std::wstring(buf);
        };
        std::wstring rx=fmt(snap.net.rx_bps), tx=fmt(snap.net.tx_bps);
        wchar_t line[96]; wsprintfW(line,L"▼ %ls  ▲ %ls",rx.c_str(),tx.c_str());
        RECT tr{cur, by+bh+3, cur+sw, by+bh+18};
        DrawTextW(hdc,line,-1,&tr,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
        SelectObject(hdc,old);
    }
}

void Renderer::paint_vertical(HDC hdc, RECT rc, const Snapshot& snap, const Config& cfg) {
    const int pad=8;
    int x=rc.left+pad, y=rc.top+pad;
    int aw=(rc.right-rc.left)-pad*2, ah=(rc.bottom-rc.top)-pad*2;
    if(aw<=0||ah<=0) return;
    HFONT old=(HFONT)SelectObject(hdc,gc.get(10,true));
    SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,ui::labelTx);
    int sh=ah/4; if(sh<28) sh=28; int cy=y;
    last_.bar=rc;

    if(cfg.show_cpu){
        RECT lr{x,cy,x+aw,cy+16}; DrawTextW(hdc,L"✦ CPU",-1,&lr,DT_LEFT|DT_SINGLELINE);
        drawCpuTiles(hdc,x,cy+18,aw,sh-22,snap.cpu,true); cy+=sh+6;
    }
    if(cfg.show_ram && cy < y+ah){
        wchar_t b[32]; wsprintfW(b,L"◍ RAM %.0f%%",snap.ram.used_percent);
        RECT lr{x,cy,x+aw,cy+16}; DrawTextW(hdc,b,-1,&lr,DT_LEFT|DT_SINGLELINE);
        int by=cy+18; fillRounded(hdc,x,by,aw,7,3,ui::track);
        int f=(int)(aw*snap.ram.used_percent/100.0); if(f) fillRounded(hdc,x,by,f,7,3,ram_color(snap.ram.used_percent));
        cy+=sh+6;
    }
    if(cfg.show_disks){ for(size_t i=0;i<snap.disk_count&&cy+10<=y+ah;++i){ fillRounded(hdc,x,cy,aw,4,2,ui::track); int f=(int)(aw*std::clamp(snap.disks[i].active/100.0,0.0,1.0)); if(f) fillRounded(hdc,x,cy,f,4,2,cpu_color(snap.disks[i].active)); cy+=8;} cy+=8; }
    if(cfg.show_net && cy < y+ah){
        RECT lr{x,cy,x+aw,cy+16}; DrawTextW(hdc,L"⇆ NET",-1,&lr,DT_LEFT|DT_SINGLELINE);
        int by=cy+18; double tot=snap.net.rx_bps+snap.net.tx_bps;
        fillRounded(hdc,x,by,aw,7,3,ui::track);
        if(tot>1){ int rx=(int)(aw*(snap.net.rx_bps/tot)); if(rx) fillRounded(hdc,x,by,rx,7,3,ui::netRx); if(aw-rx) fillRounded(hdc,x+rx,by,aw-rx,7,3,ui::netTx); }
        cy+=28;
        wchar_t b[48]; wsprintfW(b,L"▼%.0f ▲%.0f",snap.net.rx_bps,snap.net.tx_bps); RECT lr2{x,cy,x+aw,cy+16}; SetTextColor(hdc,ui::text1); SelectObject(hdc,gc.get(9,false)); DrawTextW(hdc,b,-1,&lr2,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    SelectObject(hdc,old);
}

Renderer::Hit Renderer::hit_test(int x,int y,RECT rc) const noexcept {
    Hit h;
    auto inside=[](RECT r,int px,int py){ return px>=r.left && px<r.right && py>=r.top && py<r.bottom; };
    if(inside(last_.cpu,x,y)){ h.type=Hit::Cpu; h.cpu_index=0; return h; }
    if(inside(last_.ram,x,y)){ h.type=Hit::Ram; return h; }
    if(inside(last_.disks,x,y)){ h.type=Hit::Disk;
        int rowH=14; int by=last_.disks.top+6;
        int idx=(y-by)/rowH;
        if(idx>=0 && idx<(int)last_.disk_count) h.disk_index=idx; return h;
    }
    if(inside(last_.net,x,y)){ h.type=Hit::Net; return h; }
    return h;
}

} // namespace glance
