#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstdarg>
#include <cmath>

using namespace Gdiplus;

static void Log(const wchar_t* fmt, ...) {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring exe(buf);
    size_t i = exe.find_last_of(L"\\/");
    std::wstring p = (i == std::wstring::npos ? L"" : exe.substr(0, i + 1)) + L"pet.log";
    FILE* f = _wfopen(p.c_str(), L"a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputws(L"\n", f);
    fclose(f);
}

// 布局
static const int TARGET_H = 208;
static const int MARGIN_BOTTOM = 8;
static const int BUBBLE_H = 46;
static const int PAD = 12;
static const double SAD_THRESHOLD = 1800.0;  // 秒 = 30 分钟，无交互后变伤心
static int g_winW = 216;
static int g_winH = 262;

// 帧/动作
struct Action {
    std::wstring name;
    std::vector<Bitmap*> frames;
    std::vector<Bitmap*> flipped;
    int duration = 150;
    float scale = 1.0f;
};
static std::map<std::wstring, Action> g_actions;

struct ActionDef { const wchar_t* name; int count; int duration; float scale; };
static const ActionDef kDefs[] = {
    {L"idle",             6, 200, 0.65f},
    {L"waiting",          6, 150, 0.65f},
    {L"waving",           4, 160, 0.65f},
    {L"jumping",          5, 140, 0.65f},
    {L"review",           6, 160, 0.65f},
    {L"failed",           8, 350, 0.65f},
    {L"running-right",    8, 110, 0.65f},
    {L"running-left",     8, 110, 0.65f},
    {L"cast",             4, 100, 0.78f},
    {L"salchow",         23,  65, 1.00f},
    {L"phantom-thrust",  23,  60, 1.00f},
    {L"aerial-sweep",    21,  68, 1.00f},
};
static const int kDefCount = sizeof(kDefs) / sizeof(kDefs[0]);

struct MenuAction { int id; const wchar_t* action; const wchar_t* label; };
static const MenuAction kMenuActions[] = {
    {10, L"idle",           L"\u5F85\u673A"},
    {11, L"waiting",        L"\u53D1\u5446"},
    {12, L"waving",         L"\u62DB\u624B"},
    {13, L"jumping",        L"\u5C0F\u8DF3"},
    {14, L"review",         L"\u89C2\u5BDF"},
    {15, L"failed",         L"\u5931\u843D"},
    {16, L"running-right",  L"\u5411\u53F3\u8DD1"},
    {17, L"running-left",   L"\u5411\u5DE6\u8DD1"},
    {18, L"cast",           L"\u65BD\u6CD5"},
    {19, L"salchow",        L"\u8428\u970D\u592B\u8DF3"},
    {20, L"phantom-thrust", L"\u865A\u50CF\u7A81\u523A"},
    {21, L"aerial-sweep",   L"\u7A7A\u4E2D\u6A2A\u626B"},
};
static const int kMenuActionCount = sizeof(kMenuActions) / sizeof(kMenuActions[0]);

static const wchar_t* kIdleActions[] = {
    L"waving", L"jumping", L"waiting", L"review", L"running-right", L"running-left"
};
static const int kIdleActionCount = sizeof(kIdleActions) / sizeof(kIdleActions[0]);

static const wchar_t* kClickActions[] = {
    L"waving", L"jumping", L"waiting", L"review",
    L"cast", L"salchow", L"phantom-thrust", L"aerial-sweep"
};
static const int kClickActionCount = sizeof(kClickActions) / sizeof(kClickActions[0]);

static std::wstring g_curAction = L"idle";
static int g_curFrame = 0;
static double g_frameTime = 0.0;
static double g_clock = 0.0;
static double g_nextActionAt = 6.0;
static std::wstring g_bubble;
static double g_bubbleBackAt = -1.0;
static double g_lastInteractAt = 0.0;

static bool g_dragging = false;
static int g_dragDir = 0;
static bool g_manualLoop = false;
static bool g_freeze = false;
static bool g_resetClock = false;
static bool g_resumeFailed = false;
static POINT g_dragStart{0, 0};
static POINT g_winPos{0, 0};
static HWND g_hwnd = nullptr;
static int g_actionMoveX = 0;
static int g_actionMoveY = 0;
static bool g_autoReturning = false;
static int g_returnRemaining = 0;
static int g_returnDirection = 0;
static const int MIN_VISIBLE_W = 48;
static const int SCREEN_RETURN_PAD = 12;
static const wchar_t* kClass = L"JanePetWindow";

static const wchar_t* kLines[] = {
    L"\u522B\u6233\u6211\uFF0C\u5728\u5FD9~",
    L"\u76EF\u7740\u6211\u770B\u5E72\u561B\uFF1F",
    L"\u8981\u4E0D\u8981\u6211\u5E2E\u4F60\u5199\u4EE3\u7801\uFF1F",
    L"\u547C\u2026\u2026"
};
static const int kLineCount = 4;

std::wstring BaseDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t i = p.find_last_of(L"\\/");
    return i == std::wstring::npos ? L"" : p.substr(0, i + 1);
}

Bitmap* FlipBitmap(Bitmap* src) {
    int w = src->GetWidth(), h = src->GetHeight();
    Bitmap* dst = new Bitmap(w, h, PixelFormat32bppPARGB);
    Graphics g(dst);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.TranslateTransform((REAL)w, 0);
    g.ScaleTransform(-1.0f, 1.0f);
    g.DrawImage(src, 0, 0, w, h);
    return dst;
}

void LoadAction(const ActionDef& def) {
    Action act;
    act.name = def.name;
    act.duration = def.duration;
    act.scale = def.scale;
    for (int i = 0; i < def.count; i++) {
        std::wstring file = L"jane_frames\\" + std::wstring(def.name) + L"_" + std::to_wstring(i) + L".png";
        Bitmap* bmp = new Bitmap((BaseDir() + file).c_str());
        if (bmp->GetLastStatus() != Ok) {
            Log(L"load failed: %ls status=%d", file.c_str(), bmp->GetLastStatus());
            delete bmp;
            continue;
        }
        act.frames.push_back(bmp);
        act.flipped.push_back(FlipBitmap(bmp));
    }
    g_actions[def.name] = act;
}

bool ActionReady(const std::wstring& name) {
    auto it = g_actions.find(name);
    return it != g_actions.end() && !it->second.frames.empty();
}

void ResetActionMotion();

void StartAction(const wchar_t* name) {
    ResetActionMotion();
    g_autoReturning = false;
    g_returnRemaining = 0;
    g_returnDirection = 0;
    if (!ActionReady(name)) name = L"idle";
    g_curAction = name;
    g_curFrame = 0;
    g_frameTime = 0.0;
    g_manualLoop = false;
    g_freeze = false;
    g_resumeFailed = false;
}

void StartActionKeepPosition(const wchar_t* name) {
    if (!ActionReady(name)) name = L"idle";
    g_curAction = name;
    g_curFrame = 0;
    g_frameTime = 0.0;
    g_manualLoop = false;
    g_freeze = false;
    g_resumeFailed = false;
    g_actionMoveX = 0;
    g_actionMoveY = 0;
}

void StartRunReturn(int direction, int distance) {
    if (direction == 0 || distance <= 0) return;
    g_autoReturning = true;
    g_returnDirection = direction;
    g_returnRemaining = distance;
    StartActionKeepPosition(direction < 0 ? L"running-left" : L"running-right");
}

const wchar_t* PickReadyAction(const wchar_t* const* actions, int count) {
    std::vector<const wchar_t*> ready;
    for (int i = 0; i < count; i++) {
        if (ActionReady(actions[i])) ready.push_back(actions[i]);
    }
    if (ready.empty()) return L"idle";
    return ready[rand() % ready.size()];
}

POINT MovePetBy(int dx, int dy = 0, bool allowHorizontalOverflow = false) {
    if (!g_hwnd || (dx == 0 && dy == 0)) return POINT{0, 0};
    RECT r;
    GetWindowRect(g_hwnd, &r);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = r.left + dx;
    int y = r.top + dy;
    if (allowHorizontalOverflow) {
        int minX = -g_winW + MIN_VISIBLE_W;
        int maxX = sw - MIN_VISIBLE_W;
        if (x < minX) x = minX;
        if (x > maxX) x = maxX;
    } else {
        if (x < 0) x = 0;
        if (x > sw - g_winW) x = sw - g_winW;
    }
    if (y < 0) y = 0;
    if (y > sh - g_winH) y = sh - g_winH;
    SetWindowPos(g_hwnd, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return POINT{x - r.left, y - r.top};
}

void ResetActionMotion() {
    if (g_actionMoveX == 0 && g_actionMoveY == 0) return;
    MovePetBy(-g_actionMoveX, -g_actionMoveY, true);
    g_actionMoveX = 0;
    g_actionMoveY = 0;
}

bool IsTravelAction(const std::wstring& action) {
    return action == L"salchow" ||
        action == L"phantom-thrust" ||
        action == L"aerial-sweep";
}

void StartDesktopReturnIfNeeded() {
    if (!g_hwnd || g_dragging || g_autoReturning || IsTravelAction(g_curAction)) return;

    RECT r;
    GetWindowRect(g_hwnd, &r);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    int fixY = 0;
    if (r.top < 0) fixY = -r.top;
    if (r.bottom > sh) fixY = sh - r.bottom;
    if (fixY != 0) MovePetBy(0, fixY, true);

    if (r.left < 0) {
        StartRunReturn(1, -r.left + SCREEN_RETURN_PAD);
    } else if (r.right > sw) {
        StartRunReturn(-1, r.right - sw + SCREEN_RETURN_PAD);
    }
}

struct MotionKey {
    float progress;
    int x;
    int y;
};

POINT EvaluateActionMotion(const std::wstring& action, float progress) {
    static const MotionKey salchow[] = {
        {0.00f, 0, 0}, {0.14f, 10, -8}, {0.34f, 34, -34},
        {0.56f, 60, -20}, {0.76f, 72, -4}, {1.00f, 82, 0}
    };
    static const MotionKey phantomThrust[] = {
        {0.00f, 0, 0}, {0.12f, -12, 2}, {0.24f, 0, -4},
        {0.43f, 42, -10}, {0.62f, 92, -5}, {0.78f, 116, 0},
        {0.91f, 120, 2}, {1.00f, 124, 0}
    };
    static const MotionKey aerialSweep[] = {
        {0.00f, 0, 0}, {0.14f, 12, -18}, {0.34f, 40, -42},
        {0.56f, 76, -28}, {0.74f, 102, -8}, {0.90f, 112, 0},
        {1.00f, 116, 0}
    };

    const MotionKey* keys = nullptr;
    int keyCount = 0;
    if (action == L"salchow") {
        keys = salchow;
        keyCount = sizeof(salchow) / sizeof(salchow[0]);
    } else if (action == L"phantom-thrust") {
        keys = phantomThrust;
        keyCount = sizeof(phantomThrust) / sizeof(phantomThrust[0]);
    } else if (action == L"aerial-sweep") {
        keys = aerialSweep;
        keyCount = sizeof(aerialSweep) / sizeof(aerialSweep[0]);
    } else {
        return POINT{0, 0};
    }

    if (progress <= keys[0].progress) return POINT{keys[0].x, keys[0].y};
    for (int i = 1; i < keyCount; i++) {
        if (progress <= keys[i].progress) {
            const MotionKey& a = keys[i - 1];
            const MotionKey& b = keys[i];
            float t = (progress - a.progress) / (b.progress - a.progress);
            t = t * t * (3.0f - 2.0f * t);
            return POINT{
                (int)std::lround(a.x + (b.x - a.x) * t),
                (int)std::lround(a.y + (b.y - a.y) * t)
            };
        }
    }
    return POINT{keys[keyCount - 1].x, keys[keyCount - 1].y};
}

void UpdateActionMotion(const Action& act) {
    if (g_dragging || act.frames.size() < 2) {
        ResetActionMotion();
        return;
    }

    float frameProgress = (float)g_curFrame + (float)(g_frameTime / (act.duration / 1000.0));
    float progress = frameProgress / (float)(act.frames.size() - 1);
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    POINT target = EvaluateActionMotion(g_curAction, progress);
    POINT moved = MovePetBy(target.x - g_actionMoveX, target.y - g_actionMoveY, true);
    g_actionMoveX += moved.x;
    g_actionMoveY += moved.y;
}

void DrawBubble(Graphics& g, const std::wstring& text) {
    FontFamily ff(L"Microsoft YaHei");
    Font font(&ff, 12.0f, FontStyleRegular, UnitPixel);
    RectF layout(0, 0, (REAL)g_winW - 10, 40);
    RectF bound;
    g.MeasureString(text.c_str(), -1, &font, layout, &bound);
    float pad = 7;
    float bw = bound.Width + pad * 2 + 2;
    float bh = bound.Height + pad * 2;
    if (bw > g_winW - 2) bw = (float)g_winW - 2;
    float bx = (g_winW - bw) / 2.0f;
    float by = 1.0f;
    float r = 9.0f;

    GraphicsPath path;
    path.AddArc(bx, by, r * 2, r * 2, 180, 90);
    path.AddArc(bx + bw - r * 2, by, r * 2, r * 2, 270, 90);
    path.AddArc(bx + bw - r * 2, by + bh - r * 2, r * 2, r * 2, 0, 90);
    path.AddArc(bx, by + bh - r * 2, r * 2, r * 2, 90, 90);
    path.CloseFigure();

    SolidBrush fill(Color(255, 255, 255, 255));
    Pen pen(Color(230, 0, 0, 0), 1.0f);
    g.FillPath(&fill, &path);
    g.DrawPath(&pen, &path);

    SolidBrush tb(Color(255, 45, 45, 45));
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    fmt.SetLineAlignment(StringAlignmentCenter);
    RectF tr(bx, by, bw, bh);
    g.DrawString(text.c_str(), -1, &font, tr, &fmt, &tb);
}

HBITMAP RenderFrame(Action& act, int frameIdx, bool flip, const std::wstring& bubble) {
    Bitmap* canvas = new Bitmap(g_winW, g_winH, PixelFormat32bppPARGB);
    Graphics g(canvas);
    g.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g.Clear(Color(0, 0, 0, 0));

    if (frameIdx >= 0 && frameIdx < (int)act.frames.size()) {
        Bitmap* src = flip ? act.flipped[frameIdx] : act.frames[frameIdx];
        int srcW = src->GetWidth();
        int srcH = src->GetHeight();
        float s = ((float)TARGET_H / (float)srcH) * act.scale;
        int dstW = (int)(srcW * s);
        int dstH = (int)(srcH * s);
        int dstX = (g_winW - dstW) / 2;
        int dstY = g_winH - MARGIN_BOTTOM - dstH;
        g.DrawImage(src, dstX, dstY, dstW, dstH);
    }

    if (!bubble.empty()) DrawBubble(g, bubble);

    HBITMAP hbm = nullptr;
    canvas->GetHBITMAP(Color(0, 0, 0, 0), &hbm);
    delete canvas;
    return hbm;
}

void Present(HBITMAP hbm) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP old = (HBITMAP)SelectObject(mem, hbm);
    POINT pt{0, 0};
    SIZE sz{g_winW, g_winH};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(g_hwnd, nullptr, nullptr, &sz, mem, &pt, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    DeleteObject(hbm);
}


void Tick(double dt) {
    g_clock += dt;
    g_frameTime += dt;

    if (!ActionReady(g_curAction)) StartAction(L"idle");
    Action& act = g_actions[g_curAction];
    if (act.frames.empty()) return;

    double frameDur = act.duration / 1000.0;
    if (frameDur <= 0.0) frameDur = 0.15;

    bool frameAdvanced = false;
    while (!g_freeze && g_frameTime >= frameDur) {
        g_frameTime -= frameDur;
        g_curFrame++;
        frameAdvanced = true;

        if (g_curAction == L"failed") {
            if (g_curFrame >= (int)act.frames.size()) {
                StartAction(L"idle");
                break;
            }
            if (!g_resumeFailed && g_curFrame >= 5) {
                g_curFrame = 4;
                g_freeze = true;
                break;
            }
            continue;
        }

        if (g_curFrame >= (int)act.frames.size()) {
            if (IsTravelAction(g_curAction)) {
                int distance = g_actionMoveX < 0 ? -g_actionMoveX : g_actionMoveX;
                if (distance > 0) {
                    StartRunReturn(g_actionMoveX > 0 ? -1 : 1, distance);
                } else {
                    StartAction(L"idle");
                }
                break;
            }

            bool shouldLoop = (g_curAction == L"idle") ||
                (g_autoReturning &&
                 (g_curAction == L"running-right" || g_curAction == L"running-left")) ||
                (g_dragging && (g_curAction == L"running-right" || g_curAction == L"running-left"));
            if (shouldLoop) {
                g_curFrame = 0;
            } else {
                StartAction(L"idle");
                break;
            }
        }
    }

    if (frameAdvanced && !g_dragging) {
        if (g_autoReturning &&
            (g_curAction == L"running-right" || g_curAction == L"running-left")) {
            int step = g_returnRemaining < 10 ? g_returnRemaining : 10;
            POINT moved = MovePetBy(g_returnDirection * step, 0, true);
            g_returnRemaining -= moved.x < 0 ? -moved.x : moved.x;
            if (g_returnRemaining <= 0) {
                StartAction(L"idle");
            }
        } else {
            if (g_curAction == L"running-right") MovePetBy(10, 0, true);
            if (g_curAction == L"running-left") MovePetBy(-10, 0, true);
        }
    }

    if (g_curAction == L"idle" && !g_manualLoop &&
        g_clock - g_lastInteractAt > SAD_THRESHOLD) {
        StartAction(L"failed");
        g_manualLoop = true;
        g_resumeFailed = false;
    }

    if (!g_manualLoop && g_curAction == L"idle" && g_clock >= g_nextActionAt) {
        StartAction(PickReadyAction(kIdleActions, kIdleActionCount));
        g_nextActionAt = g_clock + 6.0 + (rand() % 400) / 100.0;
    }

    if (g_bubbleBackAt >= 0.0 && g_clock >= g_bubbleBackAt) {
        g_bubble.clear();
        g_bubbleBackAt = -1.0;
    }

    if (!ActionReady(g_curAction)) StartAction(L"idle");
    Action& cur = g_actions[g_curAction];
    UpdateActionMotion(cur);
    StartDesktopReturnIfNeeded();

    if (!ActionReady(g_curAction)) StartAction(L"idle");
    Action& renderAct = g_actions[g_curAction];
    HBITMAP hbm = RenderFrame(renderAct, g_curFrame, false, g_bubble);
    if (hbm) Present(hbm);
}

void DoAction(int id) {
    if (id == 1) { DestroyWindow(g_hwnd); return; }
    if (id >= 10 && id <= 21) {
        const wchar_t* actions[] = {
            L"idle", L"waiting", L"waving", L"jumping", L"review", L"failed",
            L"running-right", L"running-left", L"cast", L"salchow",
            L"phantom-thrust", L"aerial-sweep"
        };
        StartAction(actions[id - 10]);
        g_resetClock = true;  // 重置帧时钟，避免菜单阻塞时间被算进新动作
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN: {
        GetCursorPos(&g_dragStart);
        RECT r;
        GetWindowRect(hwnd, &r);
        g_winPos.x = r.left;
        g_winPos.y = r.top;
        g_dragging = true;
        g_dragDir = 0;
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_dragging || !(wp & MK_LBUTTON)) return 0;
        POINT pt;
        GetCursorPos(&pt);
        SetWindowPos(hwnd, HWND_TOPMOST,
                     g_winPos.x + pt.x - g_dragStart.x,
                     g_winPos.y + pt.y - g_dragStart.y,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        int dx = pt.x - g_dragStart.x;
        int newDir = (dx > 6) ? 1 : (dx < -6) ? -1 : 0;
        if (newDir != 0 && newDir != g_dragDir) {
            g_dragDir = newDir;
            g_manualLoop = false;
            g_freeze = false;
            StartAction((newDir == 1) ? L"running-right" : L"running-left");
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (g_dragging) {
            g_dragging = false;
            ReleaseCapture();
            g_lastInteractAt = g_clock;
            bool wasFrozenFailed = (g_freeze && g_curAction == L"failed");
            g_freeze = false;
            POINT pt;
            GetCursorPos(&pt);
            int dx = pt.x - g_dragStart.x;
            int dy = pt.y - g_dragStart.y;
            if (dx * dx + dy * dy < 16) {
                if (wasFrozenFailed) {
                    // 伤心定格中点击 → 继续播完剩余帧再回待机
                    g_resumeFailed = true;
                    g_resumeFailed = true;
                    g_curFrame = 5;
                    g_frameTime = 0.0;
                } else {
                    StartAction(PickReadyAction(kClickActions, kClickActionCount));
                    g_bubble = kLines[rand() % kLineCount];
                    g_bubbleBackAt = g_clock + 3.2;
                }
            } else {
                StartAction(L"idle");
            }
            g_dragDir = 0;
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        HMENU menu = CreatePopupMenu();
        HMENU actionMenu = CreatePopupMenu();
        for (int i = 0; i < kMenuActionCount; i++) {
            if (ActionReady(kMenuActions[i].action)) {
                AppendMenuW(actionMenu, MF_STRING, kMenuActions[i].id, kMenuActions[i].label);
            }
        }
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)actionMenu, L"\u52A8\u4F5C");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 1, L"\u9000\u51FA");
        SetForegroundWindow(hwnd);
        SetTimer(hwnd, 1, 33, nullptr);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
        KillTimer(hwnd, 1);
        DestroyMenu(menu);
        if (cmd != 0) DoAction(cmd);
        return 0;
    }
    case WM_COMMAND:
        DoAction(LOWORD(wp));
        return 0;
    case WM_TIMER:
        if (wp == 1) Tick(1.0 / 30.0);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    SetProcessDPIAware();
    srand(GetTickCount());

    GdiplusStartupInput in;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &in, nullptr) != Ok) return 1;

    for (int i = 0; i < kDefCount; i++) LoadAction(kDefs[i]);

    if (g_actions.find(L"idle") == g_actions.end() || g_actions[L"idle"].frames.empty()) {
        MessageBoxW(nullptr, L"load idle frames failed", L"Jane Pet", MB_OK);
        return 1;
    }

    Bitmap* idle0 = g_actions[L"idle"].frames[0];
    float aspect = (float)idle0->GetWidth() / (float)idle0->GetHeight();
    g_winW = (int)(TARGET_H * aspect) + PAD * 2;
    g_winH = BUBBLE_H + TARGET_H + MARGIN_BOTTOM;

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32646));
    RegisterClassW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = sw - g_winW - 40;
    int y = sh - g_winH - 90;

    g_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                             kClass, L"Jane Pet", WS_POPUP,
                             x, y, g_winW, g_winH,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    MSG msg{};
    LARGE_INTEGER freq, last;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
    const double frameDt = 1.0 / 30.0;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_resetClock) {
            g_resetClock = false;
            last = now;
        }
        double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
        if (dt >= frameDt) {
            last = now;
            Tick(dt);
        } else {
            Sleep(1);
        }
    }

    for (auto& kv : g_actions) {
        for (Bitmap* b : kv.second.frames) delete b;
        for (Bitmap* b : kv.second.flipped) delete b;
    }
    GdiplusShutdown(token);
    return 0;
}
