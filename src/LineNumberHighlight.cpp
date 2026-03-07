#include <windows.h>
#include <commdlg.h>

#define SCI_GETLINECOUNT          2154
#define SCI_GETCURRENTPOS         2008
#define SCI_LINEFROMPOSITION      2166
#define SCI_SETMARGINTYPEN        2240
#define SCI_SETMARGINWIDTHN       2242
#define SCI_MARGINSETTEXT         2530
#define SCI_MARGINSETSTYLE        2532
#define SCI_STYLESETFORE          2051
#define SCI_STYLESETBACK          2052
#define SCI_STYLESETFONT          2056
#define SCI_STYLESETSIZE          2055
#define SCI_STYLEGETBACK          2482
#define SCI_STYLEGETSIZE          2485
#define SCI_STYLEGETFONT          2486
#define SCI_STYLEGETBOLD          2493
#define SCI_STYLEGETITALIC        2494
#define SCI_STYLEGETCHARACTERSET  2490
#define SCI_STYLESETCHARACTERSET  2066
#define SCI_STYLESETBOLD          2053
#define SCI_STYLESETITALIC        2054
#define SCI_TEXTWIDTH             2276
#define SCI_GETDIRECTFUNCTION     2184
#define SCI_GETDIRECTPOINTER      2185

#define SC_MARGIN_RTEXT           5
#define STYLE_LINENUMBER          33
#define STYLE_ACTIVE              40
#define STYLE_NORMAL              41
#define OUR_MARGIN                0
#define NPPN_BUFFERACTIVATED      1010
#define SCN_UPDATEUI              2007

#define COLOR_ACTIVE_DARK    RGB(0xCC, 0xCC, 0xCC)
#define COLOR_NORMAL_DARK    RGB(0x6E, 0x76, 0x81)
#define COLOR_ACTIVE_LIGHT   RGB(0x1A, 0x1A, 0x1A)
#define COLOR_NORMAL_LIGHT   RGB(0x99, 0x99, 0x99)

#define IDC_BTN_ACTIVE   101
#define IDC_BTN_NORMAL   102
#define IDC_BTN_OK       103
#define IDC_BTN_CANCEL   104
#define IDC_PREVIEW_A    105
#define IDC_PREVIEW_N    106
#define IDC_BTN_RESET_A  107
#define IDC_BTN_RESET_N  108

typedef LRESULT (*SciFn)(void* ptr, UINT msg, WPARAM wp, LPARAM lp);

struct ShortcutKey { bool isAlt; bool isCtrl; bool isShift; UCHAR key; };
struct FuncItem {
    WCHAR _itemName[64]; void (*_pFunc)(); int _cmdID;
    bool _init2Check; ShortcutKey* _pShKey;
};
struct SCNotification {
    NMHDR nmhdr; int position, ch, modifiers, modificationType;
    const char* text; int length, linesAdded, message;
    WPARAM wParam; LPARAM lParam;
    int line, foldLevelNow, foldLevelPrev, margin, listType, x, y;
    int token, annotationLinesAdded, updated, listCompletionMethod;
};

struct DlgData {
    COLORREF active, normal, custom[16];
    int result; // 0=cancel, 1=ok
};

static SciFn     g_fn          = NULL;
static void*     g_ptr         = NULL;
static HWND      g_hSci        = NULL;
static HWND      g_hNpp        = NULL;
static HINSTANCE g_hInst       = NULL;
static HWND      g_hDlg        = NULL;  // settings dialog handle
static int       g_prevLine    = -1;
static bool      g_ready       = false;
static COLORREF  g_colorActiveDark  = COLOR_ACTIVE_DARK;
static COLORREF  g_colorNormalDark  = COLOR_NORMAL_DARK;
static COLORREF  g_colorActiveLight = COLOR_ACTIVE_LIGHT;
static COLORREF  g_colorNormalLight = COLOR_NORMAL_LIGHT;
// Active pointers - updated on theme change
static COLORREF* g_pColorActive = &g_colorActiveDark;
static COLORREF* g_pColorNormal = &g_colorNormalDark;
#define g_colorActive (*g_pColorActive)
#define g_colorNormal (*g_pColorNormal)
static COLORREF  g_customColors[16] = {};
static char      g_iniPath[MAX_PATH] = {};
static FuncItem  g_funcItems[2];

// ── INI ───────────────────────────────────────────────────────────────────────
static void BuildIniPath(WCHAR* configDir) {
    // Use %APPDATA%\Notepad++\plugins\Config
    char appdata[MAX_PATH]={};
    if(GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH)){
        lstrcpyA(g_iniPath, appdata);
        lstrcatA(g_iniPath, "\\Notepad++\\plugins\\Config\\LineNumberHighlight.ini");
        return;
    }
    // Fallback: next to DLL
    char dllPath[MAX_PATH]={};
    GetModuleFileNameA(g_hInst, dllPath, MAX_PATH);
    int len=lstrlenA(dllPath), dot=len;
    for(int i=len-1;i>=0;i--) if(dllPath[i]=='.'){dot=i;break;}
    for(int i=0;i<dot;i++) g_iniPath[i]=dllPath[i];
    lstrcatA(g_iniPath,".ini");
}
static void WriteColorToIni(const char* key, COLORREF c) {
    char buf[16]={}; DWORD v=(DWORD)c; int i=0;
    if(!v){buf[0]='0';buf[1]=0;}
    else{char t[12];int l=0;while(v){t[l++]='0'+v%10;v/=10;}for(int j=0;j<l;j++)buf[j]=t[l-1-j];buf[l]=0;}
    WritePrivateProfileStringA("Settings",key,buf,g_iniPath);
}
static void LoadSettings() {
    g_colorActiveDark =(COLORREF)GetPrivateProfileIntA("Settings","ActiveColorDark", (int)COLOR_ACTIVE_DARK, g_iniPath);
    g_colorNormalDark =(COLORREF)GetPrivateProfileIntA("Settings","NormalColorDark", (int)COLOR_NORMAL_DARK, g_iniPath);
    g_colorActiveLight=(COLORREF)GetPrivateProfileIntA("Settings","ActiveColorLight",(int)COLOR_ACTIVE_LIGHT,g_iniPath);
    g_colorNormalLight=(COLORREF)GetPrivateProfileIntA("Settings","NormalColorLight",(int)COLOR_NORMAL_LIGHT,g_iniPath);
}
static void SaveSettings() {
    WriteColorToIni("ActiveColorDark",  g_colorActiveDark);
    WriteColorToIni("NormalColorDark",  g_colorNormalDark);
    WriteColorToIni("ActiveColorLight", g_colorActiveLight);
    WriteColorToIni("NormalColorLight", g_colorNormalLight);
}

// ── Scintilla ─────────────────────────────────────────────────────────────────
static LRESULT Sci(UINT msg,WPARAM wp=0,LPARAM lp=0){
    if(g_fn&&g_ptr) return g_fn(g_ptr,msg,wp,lp);
    return SendMessage(g_hSci,msg,wp,lp);
}
static int Digits(int n){int d=1;while(n>=10){n/=10;d++;}return d<4?4:d;}
static void FmtLine(int idx,int digits,char* buf){
    int n=idx+1;
    for(int i=digits-1;i>=0;i--){buf[i]=(n>0)?('0'+n%10):' ';n/=10;}
    buf[digits]=0;
}
static bool IsSci(HWND h){
    char c[32]={}; GetClassNameA(h,c,31);
    return c[0]=='S'&&c[1]=='c'&&c[2]=='i'&&c[3]=='n';
}
static void ApplyStyles() {
    char fontBuf[256]={};
    Sci(SCI_STYLEGETFONT,STYLE_LINENUMBER,(LPARAM)fontBuf);
    int sz=(int)Sci(SCI_STYLEGETSIZE,STYLE_LINENUMBER);
    int bold=(int)Sci(SCI_STYLEGETBOLD,STYLE_LINENUMBER);
    int ital=(int)Sci(SCI_STYLEGETITALIC,STYLE_LINENUMBER);
    int cs=(int)Sci(SCI_STYLEGETCHARACTERSET,STYLE_LINENUMBER);
    LRESULT bg=Sci(SCI_STYLEGETBACK,STYLE_LINENUMBER);
    #define SET_STYLE(s,fg) \
        if(fontBuf[0]) Sci(SCI_STYLESETFONT,s,(LPARAM)fontBuf); \
        Sci(SCI_STYLESETSIZE,s,sz>0?sz:10); \
        Sci(SCI_STYLESETBOLD,s,bold); \
        Sci(SCI_STYLESETITALIC,s,ital); \
        Sci(SCI_STYLESETCHARACTERSET,s,cs); \
        Sci(SCI_STYLESETFORE,s,fg); \
        Sci(SCI_STYLESETBACK,s,bg);
    SET_STYLE(STYLE_ACTIVE, g_colorActive)
    SET_STYLE(STYLE_NORMAL, g_colorNormal)
    #undef SET_STYLE
    Sci(SCI_SETMARGINTYPEN,OUR_MARGIN,SC_MARGIN_RTEXT);
}
static void RenderLine(int idx,int digits,bool active){
    char text[32]; FmtLine(idx,digits,text);
    Sci(SCI_MARGINSETTEXT,idx,(LPARAM)text);
    Sci(SCI_MARGINSETSTYLE,idx,active?STYLE_ACTIVE:STYLE_NORMAL);
}
static void RenderAll(int curLine){
    int total=(int)Sci(SCI_GETLINECOUNT);
    int digits=Digits(total);
    LRESULT cw=Sci(SCI_TEXTWIDTH,STYLE_NORMAL,(LPARAM)"0");
    if(cw<=0) cw=8;
    Sci(SCI_SETMARGINWIDTHN,OUR_MARGIN,(int)cw*(digits+1));
    for(int i=0;i<total;i++) RenderLine(i,digits,i==curLine);
}
static void DoInit(HWND sci){
    g_hSci=sci;
    g_fn=(SciFn)SendMessage(sci,SCI_GETDIRECTFUNCTION,0,0);
    g_ptr=(void*)SendMessage(sci,SCI_GETDIRECTPOINTER,0,0);
    ApplyStyles();
    int cur=(int)Sci(SCI_LINEFROMPOSITION,Sci(SCI_GETCURRENTPOS));
    RenderAll(cur);
    g_prevLine=cur; g_ready=true;
}

// Re-apply styles and repaint all lines after theme change
static void RefreshMargin(){
    if(!g_ready || !g_hSci) return;
    ApplyStyles();
    int cur=(int)Sci(SCI_LINEFROMPOSITION,Sci(SCI_GETCURRENTPOS));
    RenderAll(cur);
    g_prevLine=cur;
}

// ── Settings dialog ───────────────────────────────────────────────────────────
static void PaintSwatch(HWND hWnd, COLORREF color){
    HDC dc=GetDC(hWnd); RECT r; GetClientRect(hWnd,&r);
    HBRUSH br=CreateSolidBrush(color);
    FillRect(dc,&r,br); DeleteObject(br); ReleaseDC(hWnd,dc);
}
static bool PickColor(HWND owner, COLORREF* color, COLORREF* custom){
    CHOOSECOLORA cc={};
    cc.lStructSize=sizeof(cc); cc.hwndOwner=owner;
    cc.rgbResult=*color; cc.lpCustColors=custom;
    cc.Flags=CC_FULLOPEN|CC_RGBINIT;
    if(ChooseColorA(&cc)){*color=cc.rgbResult;return true;}
    return false;
}

static DlgData g_dlgData; // global — no stack issues

#define NPPN_DARKMODECHANGED         1027

// Active theme colors (updated from Npp or fallback)
static bool     g_darkMode   = false;
static COLORREF g_clrBg      = RGB(0x24,0x24,0x24);
static COLORREF g_clrBorder  = RGB(0x40,0x40,0x40);
static COLORREF g_clrBtn     = RGB(0x31,0x31,0x31);
static COLORREF g_clrBtnHot  = RGB(0x36,0x36,0x36);
static COLORREF g_clrBtnDn   = RGB(0x2A,0x2A,0x2A);
static COLORREF g_clrText    = RGB(0xFF,0xFF,0xFF);
// Light fallback colors

#define BTN_RADIUS    4

static HBRUSH g_brBg  = NULL;
static HBRUSH g_brBtn = NULL;

static HWND GetNppHwnd() {
    // g_hNpp from setInfo is a Scintilla handle, not Npp main window
    // Use FindWindow to get the real Notepad++ main window
    HWND h = FindWindowW(L"Notepad++", NULL);
    return h ? h : g_hNpp;
}

static bool IsDarkMode() {
    HWND hNpp = GetNppHwnd();
    if(SendMessageW(hNpp, (WM_USER+1107), 0, 0)) return true; // NPPM_ISDARKMODEENABLED
    // Fallback: editor background brightness (comparePlus approach)
    LRESULT bg = SendMessageW(hNpp, (WM_USER+1091), 0, 0); // NPPM_GETEDITORDEFAULTBACKGROUNDCOLOR
    int r=(int)(bg&0xFF), g=(int)((bg>>8)&0xFF), b=(int)((bg>>16)&0xFF);
    return ((r+g+b)/3) < 128;
}

static void UpdateThemeColors() {
    g_darkMode = IsDarkMode();
    // Switch active color pointers to current theme
    g_pColorActive = g_darkMode ? &g_colorActiveDark  : &g_colorActiveLight;
    g_pColorNormal = g_darkMode ? &g_colorNormalDark  : &g_colorNormalLight;
    if(g_darkMode) {
        g_clrBg     = RGB(0x24,0x24,0x24);
        g_clrBorder = RGB(0x40,0x40,0x40);
        g_clrBtn    = RGB(0x31,0x31,0x31);
        g_clrBtnHot = RGB(0x3C,0x3C,0x3C);
        g_clrBtnDn  = RGB(0x28,0x28,0x28);
        g_clrText   = RGB(0xFF,0xFF,0xFF);
    } else {
        g_clrBg     = RGB(0xF0,0xF0,0xF0);
        g_clrBorder = RGB(0xAD,0xAD,0xAD);
        g_clrBtn    = RGB(0xE1,0xE1,0xE1);
        g_clrBtnHot = RGB(0xD0,0xD0,0xD0);
        g_clrBtnDn  = RGB(0xC0,0xC0,0xC0);
        g_clrText   = RGB(0x00,0x00,0x00);
    }
    // Recreate brushes
    if(g_brBg)  { DeleteObject(g_brBg);  g_brBg=NULL;  }
    if(g_brBtn) { DeleteObject(g_brBtn); g_brBtn=NULL; }
}

static void EnsureDarkBrushes(){
    if(!g_brBg)  g_brBg  = CreateSolidBrush(g_clrBg);
    if(!g_brBtn) g_brBtn = CreateSolidBrush(g_clrBtn);
}

// Per-button hover tracking
#define MAX_BTNS 8
static HWND  g_btnHwnd[MAX_BTNS] = {};
static bool  g_btnHot [MAX_BTNS] = {};

static int BtnIdx(HWND h){
    for(int i=0;i<MAX_BTNS;i++) if(g_btnHwnd[i]==h) return i;
    return -1;
}
static int BtnIdxOrAdd(HWND h){
    int i=BtnIdx(h); if(i>=0) return i;
    for(int j=0;j<MAX_BTNS;j++) if(!g_btnHwnd[j]){g_btnHwnd[j]=h;return j;}
    return -1;
}

static void DrawDarkButton(DRAWITEMSTRUCT* di){
    HDC dc  = di->hDC;
    RECT r  = di->rcItem;
    int idx = BtnIdx(di->hwndItem);
    bool hot = (idx>=0) ? g_btnHot[idx] : false;
    bool dn  = (di->itemState & ODS_SELECTED) != 0;

    COLORREF bg = dn ? g_clrBtnDn : hot ? g_clrBtnHot : g_clrBtn;

    // Fill rounded rect background
    HDC memDC = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
    HBITMAP old = (HBITMAP)SelectObject(memDC, bmp);

    // Background (erase with dialog bg first for anti-alias edges)
    HBRUSH brBg = CreateSolidBrush(g_clrBg);
    FillRect(memDC, &r, brBg);
    DeleteObject(brBg);

    // Rounded button fill
    HBRUSH brBtn = CreateSolidBrush(bg);
    HPEN   penBorder = CreatePen(PS_SOLID,1,g_clrBorder);
    HBRUSH oldBr = (HBRUSH)SelectObject(memDC, brBtn);
    HPEN   oldPen = (HPEN)SelectObject(memDC, penBorder);
    RoundRect(memDC, r.left, r.top, r.right, r.bottom, BTN_RADIUS*2, BTN_RADIUS*2);
    SelectObject(memDC, oldBr);
    SelectObject(memDC, oldPen);
    DeleteObject(brBtn);
    DeleteObject(penBorder);

    // Text
    char txt[64]={}; GetWindowTextA(di->hwndItem, txt, 63);
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, g_clrText);
    HFONT hf = (HFONT)SendMessageA(di->hwndItem, WM_GETFONT, 0, 0);
    HFONT oldf = hf ? (HFONT)SelectObject(memDC, hf) : NULL;
    DrawTextA(memDC, txt, -1, &r, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(oldf) SelectObject(memDC, oldf);

    BitBlt(dc, r.left, r.top, r.right-r.left, r.bottom-r.top, memDC, r.left, r.top, SRCCOPY);
    SelectObject(memDC, old);
    DeleteObject(bmp);
    DeleteDC(memDC);
}

// Subclass proc for hover tracking on owner-draw buttons
static WNDPROC g_origBtnProc = NULL;
static LRESULT CALLBACK BtnSubclassProc(HWND h, UINT m, WPARAM w, LPARAM l){
    int idx = BtnIdxOrAdd(h);
    if(m==WM_MOUSEMOVE && idx>=0 && !g_btnHot[idx]){
        g_btnHot[idx]=true;
        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,h,0};
        TrackMouseEvent(&tme);
        InvalidateRect(h,NULL,FALSE);
    }
    if(m==WM_MOUSELEAVE && idx>=0){
        g_btnHot[idx]=false;
        InvalidateRect(h,NULL,FALSE);
    }
    return CallWindowProcA(g_origBtnProc,h,m,w,l);
}

static LRESULT CALLBACK SettingsWndProc(HWND h,UINT m,WPARAM w,LPARAM l){
    EnsureDarkBrushes();
    switch(m){
    case WM_COMMAND:
        switch(LOWORD(w)){
        case IDC_BTN_ACTIVE:
            if(PickColor(h,&g_dlgData.active,g_dlgData.custom))
                PaintSwatch(GetDlgItem(h,IDC_PREVIEW_A),g_dlgData.active);
            break;
        case IDC_BTN_NORMAL:
            if(PickColor(h,&g_dlgData.normal,g_dlgData.custom))
                PaintSwatch(GetDlgItem(h,IDC_PREVIEW_N),g_dlgData.normal);
            break;
        case IDC_BTN_RESET_A:
            g_dlgData.active = g_darkMode ? COLOR_ACTIVE_DARK : COLOR_ACTIVE_LIGHT;
            PaintSwatch(GetDlgItem(h,IDC_PREVIEW_A),g_dlgData.active);
            break;
        case IDC_BTN_RESET_N:
            g_dlgData.normal = g_darkMode ? COLOR_NORMAL_DARK : COLOR_NORMAL_LIGHT;
            PaintSwatch(GetDlgItem(h,IDC_PREVIEW_N),g_dlgData.normal);
            break;
        case IDC_BTN_OK:
            g_dlgData.result=1;
            EnableWindow(GetParent(h),TRUE);
            DestroyWindow(h);
            break;
        case IDC_BTN_CANCEL:
            g_dlgData.result=0;
            EnableWindow(GetParent(h),TRUE);
            DestroyWindow(h);
            break;
        }
        break;
    case WM_ERASEBKGND: {
        HDC dc=(HDC)w; RECT r; GetClientRect(h,&r);
        FillRect(dc,&r,g_brBg);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HWND hCtl=(HWND)l;
        int ctlId=GetDlgCtrlID(hCtl);
        // Swatches paint themselves - don't interfere
        if(ctlId==IDC_PREVIEW_A||ctlId==IDC_PREVIEW_N) break;
        HDC dc=(HDC)w;
        SetTextColor(dc,g_clrText);
        SetBkColor(dc,g_clrBg);
        return (LRESULT)g_brBg;
    }
    case WM_CTLCOLORBTN: {
        // For owner-draw buttons WM_CTLCOLORBTN still fires — return bg brush
        HDC dc=(HDC)w;
        SetBkColor(dc,g_clrBtn);
        return (LRESULT)g_brBtn;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di=(DRAWITEMSTRUCT*)l;
        if(di->CtlType==ODT_BUTTON){ DrawDarkButton(di); return TRUE; }
        break;
    }
    case WM_CLOSE:
        g_dlgData.result=0;
        EnableWindow(GetParent(h),TRUE);
        DestroyWindow(h);
        break;
    case WM_DESTROY:
        g_hDlg = NULL;
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(h,m,w,l);
}

static void ShowSettings(){
    static bool registered=false;
    if(!registered){
        WNDCLASSA wc={};
        wc.lpfnWndProc=SettingsWndProc;
        wc.hInstance=g_hInst;
        wc.hbrBackground=CreateSolidBrush(g_clrBg);
        wc.lpszClassName="LNHSettings";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        RegisterClassA(&wc);
        registered=true;
    }

    UpdateThemeColors();
    // Init data
    g_dlgData.active=g_colorActive;
    g_dlgData.normal=g_colorNormal;
    g_dlgData.result=0;
    for(int i=0;i<16;i++) g_dlgData.custom[i]=g_customColors[i];

    HWND owner=GetForegroundWindow();
    RECT rp; GetWindowRect(owner,&rp);
    // Layout: margin=10, label=155, gap=8, swatch=26, gap=8, pick=70, gap=8, reset=70
    // Total: 10+155+8+26+8+70+8+70+10 = 365 client width
    int cw=395, ch=132;
    RECT wr={0,0,cw,ch};
    AdjustWindowRectEx(&wr,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int ww=wr.right-wr.left, wh=wr.bottom-wr.top;
    int x=rp.left+(rp.right-rp.left-ww)/2;
    int y=rp.top+(rp.bottom-rp.top-wh)/2;

    HWND hDlg=CreateWindowExA(WS_EX_DLGMODALFRAME,
        "LNHSettings","Line Number Highlight - Settings",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        x,y,ww,wh, owner,NULL,g_hInst,NULL);
    if(!hDlg) return;
    g_hDlg = hDlg;

    // x positions: label=10, swatch=173, pick=207, reset=285
    #define ROW(y) \
        WS_CHILD|WS_VISIBLE, 10,(y)+3, 155,20

    // Row 1: Active
    CreateWindowExA(0,"STATIC","Active line number:",
        WS_CHILD|WS_VISIBLE,10,17,155,20,hDlg,(HMENU)0,g_hInst,NULL);
    HWND hPA=CreateWindowExA(0,"STATIC","",
        WS_CHILD|WS_VISIBLE,173,12,26,26,hDlg,(HMENU)IDC_PREVIEW_A,g_hInst,NULL);
    CreateWindowExA(0,"BUTTON","Pick color",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,207,11,90,28,hDlg,(HMENU)IDC_BTN_ACTIVE,g_hInst,NULL);
    CreateWindowExA(0,"BUTTON","Reset",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,303,11,75,28,hDlg,(HMENU)IDC_BTN_RESET_A,g_hInst,NULL);

    // Row 2: Normal
    CreateWindowExA(0,"STATIC","Inactive line numbers:",
        WS_CHILD|WS_VISIBLE,10,57,155,20,hDlg,(HMENU)0,g_hInst,NULL);
    HWND hPN=CreateWindowExA(0,"STATIC","",
        WS_CHILD|WS_VISIBLE,173,52,26,26,hDlg,(HMENU)IDC_PREVIEW_N,g_hInst,NULL);
    CreateWindowExA(0,"BUTTON","Pick color",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,207,51,90,28,hDlg,(HMENU)IDC_BTN_NORMAL,g_hInst,NULL);
    CreateWindowExA(0,"BUTTON","Reset",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,303,51,75,28,hDlg,(HMENU)IDC_BTN_RESET_N,g_hInst,NULL);
    #undef ROW

    // OK / Cancel — right-aligned
    CreateWindowExA(0,"BUTTON","OK",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,207,95,90,28,hDlg,(HMENU)IDC_BTN_OK,g_hInst,NULL);
    CreateWindowExA(0,"BUTTON","Cancel",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,303,95,75,28,hDlg,(HMENU)IDC_BTN_CANCEL,g_hInst,NULL);

    // Subclass all buttons for hover tracking
    auto subBtn = [](HWND btn){
        WNDPROC p = (WNDPROC)GetWindowLongPtrA(btn, GWLP_WNDPROC);
        if(!g_origBtnProc) g_origBtnProc = p;
        SetWindowLongPtrA(btn, GWLP_WNDPROC, (LONG_PTR)BtnSubclassProc);
    };
    subBtn(GetDlgItem(hDlg,IDC_BTN_ACTIVE));
    subBtn(GetDlgItem(hDlg,IDC_BTN_RESET_A));
    subBtn(GetDlgItem(hDlg,IDC_BTN_NORMAL));
    subBtn(GetDlgItem(hDlg,IDC_BTN_RESET_N));
    subBtn(GetDlgItem(hDlg,IDC_BTN_OK));
    subBtn(GetDlgItem(hDlg,IDC_BTN_CANCEL));
    // Reset hover state
    for(int i=0;i<MAX_BTNS;i++){g_btnHwnd[i]=NULL;g_btnHot[i]=false;}

    EnableWindow(owner,FALSE); // modal
    ShowWindow(hDlg,SW_SHOW);
    UpdateWindow(hDlg);
    PaintSwatch(hPA,g_dlgData.active);
    PaintSwatch(hPN,g_dlgData.normal);

    // Message loop
    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)){
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if(g_dlgData.result==1){
        g_colorActive=g_dlgData.active;
        g_colorNormal=g_dlgData.normal;
        for(int i=0;i<16;i++) g_customColors[i]=g_dlgData.custom[i];
        SaveSettings();
        if(g_ready&&g_hSci){
            ApplyStyles();
            int cur=(int)Sci(SCI_LINEFROMPOSITION,Sci(SCI_GETCURRENTPOS));
            RenderAll(cur); g_prevLine=cur;
        }
    }
}

// ── About dialog ─────────────────────────────────────────────────────────────
#define IDC_ABOUT_OK  201

static LRESULT CALLBACK AboutWndProc(HWND h, UINT m, WPARAM w, LPARAM l){
    EnsureDarkBrushes();
    switch(m){
    case WM_COMMAND:
        if(LOWORD(w)==IDC_ABOUT_OK||LOWORD(w)==IDOK){
            EnableWindow(GetParent(h),TRUE);
            DestroyWindow(h);
        }
        break;
    case WM_ERASEBKGND: {
        HDC dc=(HDC)w; RECT r; GetClientRect(h,&r);
        FillRect(dc,&r,g_brBg);
        // Draw 1px separator line
        HPEN pen=CreatePen(PS_SOLID,1,g_clrBorder);
        HPEN op=(HPEN)SelectObject(dc,pen);
        MoveToEx(dc,20,42,NULL); LineTo(dc,340,42);
        SelectObject(dc,op); DeleteObject(pen);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)w;
        SetTextColor(dc,g_clrText);
        SetBkColor(dc,g_clrBg);
        return (LRESULT)g_brBg;
    }
    case WM_CTLCOLORBTN:
        SetBkColor((HDC)w,g_clrBtn);
        return (LRESULT)g_brBtn;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di=(DRAWITEMSTRUCT*)l;
        if(di->CtlType==ODT_BUTTON){ DrawDarkButton(di); return TRUE; }
        break;
    }
    case WM_CLOSE:
        EnableWindow(GetParent(h),TRUE);
        DestroyWindow(h);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcA(h,m,w,l);
}

static void ShowAbout(){
    UpdateThemeColors();
    // Register class once
    static bool reg=false;
    if(!reg){
        WNDCLASSA wc={};
        wc.lpfnWndProc=AboutWndProc;
        wc.hInstance=g_hInst;
        wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); // WM_ERASEBKGND handles actual painting
        wc.lpszClassName="LNHAbout";
        wc.hCursor=LoadCursor(NULL,IDC_ARROW);
        RegisterClassA(&wc);
        reg=true;
    }

    // Size: 360x180
    int cw=360, ch=180;
    RECT wr={0,0,cw,ch};
    AdjustWindowRectEx(&wr,WS_POPUP|WS_CAPTION|WS_SYSMENU,FALSE,WS_EX_DLGMODALFRAME);
    int ww=wr.right-wr.left, wh=wr.bottom-wr.top;
    HWND owner=GetForegroundWindow();
    RECT rp; GetWindowRect(owner,&rp);
    int x=rp.left+(rp.right-rp.left-ww)/2;
    int y=rp.top+(rp.bottom-rp.top-wh)/2;

    HWND hDlg=CreateWindowExA(WS_EX_DLGMODALFRAME,
        "LNHAbout","About Line Number Highlight",
        WS_POPUP|WS_CAPTION|WS_SYSMENU,
        x,y,ww,wh,owner,NULL,g_hInst,NULL);
    if(!hDlg) return;

    // Title label - нужен обычный шрифт (не bold)
    HWND hTitle=CreateWindowExA(0,"STATIC","Line Number Highlight v1.0",
        WS_CHILD|WS_VISIBLE|SS_LEFT,
        20,16,320,20,hDlg,(HMENU)0,g_hInst,NULL);
    // Copyright
    HWND hCopy=CreateWindowExA(0,"STATIC","Copyright VitalS 2026",
        WS_CHILD|WS_VISIBLE|SS_LEFT,
        20,150,200,18,hDlg,(HMENU)0,g_hInst,NULL);
    // Apply non-bold font to all statics
    NONCLIENTMETRICSA ncm={sizeof(ncm)};
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,sizeof(ncm),&ncm,0);
    ncm.lfMessageFont.lfWeight=FW_NORMAL;
    static HFONT hFont=NULL; // created once, lives as long as plugin
    if(!hFont) hFont=CreateFontIndirectA(&ncm.lfMessageFont);
    SendMessageA(hTitle,WM_SETFONT,(WPARAM)hFont,TRUE);
    SendMessageA(hCopy,WM_SETFONT,(WPARAM)hFont,TRUE);
    // Description
    HWND hDesc=CreateWindowExA(0,"STATIC",
        "Highlights the active line number in the editor margin.\r\n\r\n"
        "Use Plugins > Line Number Highlight > Settings\r\n"
        "to change active and inactive line number colors.",
        WS_CHILD|WS_VISIBLE|SS_LEFT,
        20,54,320,88,hDlg,(HMENU)0,g_hInst,NULL);
    if(hFont) SendMessageA(hDesc,WM_SETFONT,(WPARAM)hFont,TRUE);
    // OK button
    CreateWindowExA(0,"BUTTON","OK",
        WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
        270,140,70,28,hDlg,(HMENU)IDC_ABOUT_OK,g_hInst,NULL);

    // Subclass OK for hover
    HWND hOk=GetDlgItem(hDlg,IDC_ABOUT_OK);
    if(!g_origBtnProc) g_origBtnProc=(WNDPROC)GetWindowLongPtrA(hOk,GWLP_WNDPROC);
    SetWindowLongPtrA(hOk,GWLP_WNDPROC,(LONG_PTR)BtnSubclassProc);
    for(int i=0;i<MAX_BTNS;i++){g_btnHwnd[i]=NULL;g_btnHot[i]=false;}

    EnableWindow(owner,FALSE);
    ShowWindow(hDlg,SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)){
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

// ── Plugin exports ────────────────────────────────────────────────────────────
extern "C" {
__declspec(dllexport)
void setInfo(HWND npp,HWND,HWND,WCHAR*,WCHAR* pluginConfigDir){
    g_hNpp=npp;
    BuildIniPath(pluginConfigDir);
    LoadSettings();
    lstrcpyW(g_funcItems[0]._itemName,L"Settings");
    g_funcItems[0]._pFunc=ShowSettings; g_funcItems[0]._cmdID=0;
    g_funcItems[0]._init2Check=false;   g_funcItems[0]._pShKey=NULL;
    lstrcpyW(g_funcItems[1]._itemName,L"About");
    g_funcItems[1]._pFunc=ShowAbout;    g_funcItems[1]._cmdID=0;
    g_funcItems[1]._init2Check=false;   g_funcItems[1]._pShKey=NULL;
}
__declspec(dllexport) const WCHAR* getName(){return L"Line Number Highlight";}
__declspec(dllexport) FuncItem* getFuncsArray(int* n){*n=2;return g_funcItems;}
__declspec(dllexport)
void beNotified(SCNotification* n){
    UINT code=n->nmhdr.code;
    if(code==(UINT)SCN_UPDATEUI){
        HWND src=n->nmhdr.hwndFrom;
        if(!IsSci(src)) return;
        if(!g_ready||g_hSci!=src){DoInit(src);return;}
        int cur=(int)Sci(SCI_LINEFROMPOSITION,Sci(SCI_GETCURRENTPOS));
        if(cur==g_prevLine) return;
        int digits=Digits((int)Sci(SCI_GETLINECOUNT));
        if(g_prevLine>=0) RenderLine(g_prevLine,digits,false);
        RenderLine(cur,digits,true);
        g_prevLine=cur; return;
    }
    if(code==NPPN_BUFFERACTIVATED) g_ready=false;
    if(code==1002) UpdateThemeColors(); // NPPN_TBMODIFICATION - after Npp init
    if(code==NPPN_DARKMODECHANGED||code==1012){
        bool wasDark=g_darkMode;
        UpdateThemeColors();
        if(g_darkMode!=wasDark || code==NPPN_DARKMODECHANGED) RefreshMargin();
    }
}
__declspec(dllexport) BOOL messageProc(UINT,WPARAM,LPARAM){return FALSE;}
__declspec(dllexport) BOOL isUnicode(){return TRUE;}
}

BOOL WINAPI DllMain(HINSTANCE hInst,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        g_hInst=hInst;
    }
    if(reason==DLL_PROCESS_DETACH){
        if(g_brBg)  { DeleteObject(g_brBg);  g_brBg=NULL;  }
        if(g_brBtn) { DeleteObject(g_brBtn); g_brBtn=NULL; }
    }
    return TRUE;
}
