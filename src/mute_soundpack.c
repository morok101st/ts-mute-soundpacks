/*
 * Mute Soundpack - TeamSpeak 3 Plugin
 *
 * Senkt die Lautstaerke der TeamSpeak-Soundpack-Sounds ("volume_factor_wave"),
 * solange man sich in einem konfigurierten Channel befindet, und stellt den
 * vorherigen Wert beim Verlassen wieder her. Sprach-Audio wird nicht angefasst.
 *
 * Gebaut ohne C-Laufzeitbibliothek (freestanding), importiert nur kernel32 und user32.
 */

#include "ts3_functions.h"
#include "plugin_definitions.h"
#include "teamspeak/public_errors.h"
#include "teamspeak/public_definitions.h"

#define EXPORT __declspec(dllexport)
#define PLUGIN_API_VERSION 26
#define PLUGIN_NAME "Mute Soundpack"
#define PLUGIN_VERSION "1.4"
#define INI_NAME "mute_soundpack.ini"
#define STATE_NAME "mute_soundpack.state"

/* ---------------- minimale Win32-Deklarationen ---------------- */
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
#define INVALID_HANDLE_VALUE ((HANDLE)(long long)-1)
#define GENERIC_READ 0x80000000UL
#define GENERIC_WRITE 0x40000000UL
#define FILE_SHARE_READ 0x1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x80
#define CP_UTF8 65001

__declspec(dllimport) HANDLE CreateFileW(const unsigned short*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE);
__declspec(dllimport) BOOL ReadFile(HANDLE, void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*);
__declspec(dllimport) BOOL CloseHandle(HANDLE);
__declspec(dllimport) BOOL DeleteFileW(const unsigned short*);
typedef void (__stdcall *WAITORTIMERCALLBACK)(void*, unsigned char);
#define WT_EXECUTEONLYONCE 0x8
__declspec(dllimport) HANDLE __stdcall CreateTimerQueue(void);
__declspec(dllimport) BOOL __stdcall CreateTimerQueueTimer(HANDLE*, HANDLE, WAITORTIMERCALLBACK, void*, DWORD, DWORD, unsigned long);
__declspec(dllimport) BOOL __stdcall DeleteTimerQueueTimer(HANDLE, HANDLE, HANDLE);
__declspec(dllimport) BOOL __stdcall DeleteTimerQueueEx(HANDLE, HANDLE);
typedef struct { void* a; long b; long c; void* d; void* e; unsigned long long f; } CRITSEC;
__declspec(dllimport) void __stdcall InitializeCriticalSection(CRITSEC*);
__declspec(dllimport) void __stdcall EnterCriticalSection(CRITSEC*);
__declspec(dllimport) void __stdcall LeaveCriticalSection(CRITSEC*);
__declspec(dllimport) void __stdcall DeleteCriticalSection(CRITSEC*);
__declspec(dllimport) int MultiByteToWideChar(unsigned int, DWORD, const char*, int, unsigned short*, int);
__declspec(dllimport) int WideCharToMultiByte(unsigned int, DWORD, const unsigned short*, int, char*, int, const char*, BOOL*);

/* user32 (nur fuer den Einstellungsdialog) */
typedef void* HWND;
typedef long long INT_PTR;
typedef unsigned long long WPARAM;
typedef long long LPARAM;
typedef INT_PTR (__stdcall *DLGPROC)(HWND, unsigned int, WPARAM, LPARAM);
__declspec(dllimport) INT_PTR __stdcall DialogBoxIndirectParamW(void*, const void*, HWND, DLGPROC, LPARAM);
__declspec(dllimport) BOOL __stdcall EndDialog(HWND, INT_PTR);
__declspec(dllimport) BOOL __stdcall SetDlgItemTextW(HWND, int, const unsigned short*);
__declspec(dllimport) unsigned int __stdcall GetDlgItemTextW(HWND, int, unsigned short*, int);
__declspec(dllimport) BOOL __stdcall CheckDlgButton(HWND, int, unsigned int);
__declspec(dllimport) unsigned int __stdcall IsDlgButtonChecked(HWND, int);
__declspec(dllimport) HWND __stdcall GetDlgItem(HWND, int);
__declspec(dllimport) HWND __stdcall SetFocus(HWND);
__declspec(dllimport) HWND __stdcall GetActiveWindow(void);
__declspec(dllimport) int __stdcall MessageBoxW(HWND, const unsigned short*, const unsigned short*, unsigned int);

/* ---------------- CRT-Ersatz ---------------- */
int _fltused = 0;

void* memset(void* d, int c, size_t n) {
    volatile unsigned char* p = (volatile unsigned char*)d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void* memcpy(void* d, const void* s, size_t n) {
    volatile unsigned char* a = (volatile unsigned char*)d;
    const volatile unsigned char* b = (const volatile unsigned char*)s;
    while (n--) *a++ = *b++;
    return d;
}

BOOL __stdcall DllEntry(void* h, DWORD reason, void* reserved) {
    (void)h; (void)reason; (void)reserved;
    return 1;
}

static size_t s_len(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

static void s_cat(char* dst, size_t cap, const char* src) {
    size_t n = s_len(dst);
    while (*src && n + 1 < cap) dst[n++] = *src++;
    dst[n] = 0;
}

static int s_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int contains(const char* hay, const char* needle, int ignoreCase) {
    size_t i, j, hl = s_len(hay), nl = s_len(needle);
    if (nl == 0 || nl > hl) return 0;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++) {
            char a = hay[i + j], b = needle[j];
            if (ignoreCase) { a = lower(a); b = lower(b); }
            if (a != b) break;
        }
        if (j == nl) return 1;
    }
    return 0;
}

static void fmt_float(char* out, float v) { /* "%.2f" */
    char tmp[24]; int n = 0, i;
    unsigned long long scaled;
    char* o = out;
    if (v < 0) { *o++ = '-'; v = -v; }
    scaled = (unsigned long long)(v * 100.0f + 0.5f);
    do { tmp[n++] = (char)('0' + scaled % 10); scaled /= 10; } while (scaled || n < 3);
    for (i = n - 1; i >= 0; i--) { *o++ = tmp[i]; if (i == 2) *o++ = '.'; }
    *o = 0;
}

static float parse_float(const char* s, float def) {
    float sign = 1.0f, val = 0.0f, frac = 0.1f; int any = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1.0f; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10.0f + (float)(*s - '0'); s++; any = 1; }
    if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9') { val += frac * (float)(*s - '0'); frac *= 0.1f; s++; any = 1; } }
    return any ? sign * val : def;
}

/* ---------------- Plugin-Zustand ---------------- */
static struct TS3Functions ts3;

#define MAX_PATTERNS 8
#define PATTERN_LEN 128
#define DEFAULT_CHANNEL "ACRE"
#define STR_(x) #x
#define STR(x) STR_(x)
static struct {
    char patterns[MAX_PATTERNS][PATTERN_LEN];
    int count;
    int ignoreCase;
    int messages;
    float volume;
} cfg;

#define MAX_HANDLERS 32
static struct {
    uint64 sch;
    int active;
    float saved;
    HANDLE timer;      /* geplantes, verzoegertes Absenken */
    unsigned int gen;  /* entwertet veraltete Timer */
} slots[MAX_HANDLERS];

/* Verzoegerung beim Betreten eines Mute-Channels, damit der
   "Channel switched"-Sound noch vollstaendig abgespielt wird. */
#define ACTIVATE_DELAY_MS 500

/* Events, Einstellungsdialog und Timer laufen in verschiedenen Threads. */
static CRITSEC lock;
static HANDLE timer_queue;
static int shutting_down;
#define LOCK() EnterCriticalSection(&lock)
#define UNLOCK() LeaveCriticalSection(&lock)

static int slot_for(uint64 sch, int create) {
    int i;
    for (i = 0; i < MAX_HANDLERS; i++) if (slots[i].sch == sch) return i;
    if (!create) return -1;
    for (i = 0; i < MAX_HANDLERS; i++) if (slots[i].sch == 0) { slots[i].sch = sch; slots[i].active = 0; return i; }
    return -1;
}

/* ---------------- Konfiguration ---------------- */
static void utf8_to_wide(const char* in, unsigned short* out, int cap) {
    int n = MultiByteToWideChar(CP_UTF8, 0, in, -1, out, cap);
    if (n <= 0) out[0] = 0;
}

static void config_file(unsigned short* wout, int wcap, const char* name) {
    char out[1024];
    out[0] = 0;
    ts3.getConfigPath(out, sizeof(out));
    {
        size_t n = s_len(out);
        if (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/') s_cat(out, sizeof(out), "\\");
    }
    s_cat(out, sizeof(out), name);
    utf8_to_wide(out, wout, wcap);
}

static int save_config(void);

static void load_config(void) {
    static char buf[8192];
    unsigned short wpath[1024];
    HANDLE h;
    DWORD got = 0;
    char* p;

    /* Standardwerte fuer fehlende Schluessel */
    cfg.count = 0; cfg.ignoreCase = 0; cfg.messages = 1; cfg.volume = -100.0f;

    config_file(wpath, 1024, INI_NAME);

    h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) {
        /* Erster Start: ini mit Standard-Channel anlegen */
        cfg.patterns[0][0] = 0;
        s_cat(cfg.patterns[0], PATTERN_LEN, DEFAULT_CHANNEL);
        cfg.count = 1;
        save_config();
        return;
    }
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, 0)) got = 0;
    CloseHandle(h);
    buf[got] = 0;

    p = buf;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    while (*p) {
        char* line = p; char* end; char* eq; char* key; char* val; char* k;
        while (*p && *p != '\n' && *p != '\r') p++;
        end = p;
        while (*p == '\n' || *p == '\r') p++;
        *end = 0;

        while (*line == ' ' || *line == '\t') line++;
        if (*line == ';' || *line == '#' || *line == 0) continue;
        eq = line; while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        key = line; val = eq + 1;
        k = eq - 1; while (k >= key && (*k == ' ' || *k == '\t')) *k-- = 0;
        while (*val == ' ' || *val == '\t') val++;
        k = val + s_len(val) - 1; while (k >= val && (*k == ' ' || *k == '\t')) *k-- = 0;
        for (k = key; *k; k++) *k = lower(*k);

        if (s_eq(key, "channel")) {
            if (cfg.count < MAX_PATTERNS && s_len(val) > 0) {
                cfg.patterns[cfg.count][0] = 0;
                s_cat(cfg.patterns[cfg.count], PATTERN_LEN, val);
                cfg.count++;
            }
        } else if (s_eq(key, "ignorecase")) {
            cfg.ignoreCase = (val[0] == '1');
        } else if (s_eq(key, "volume")) {
            cfg.volume = parse_float(val, -100.0f);
        } else if (s_eq(key, "messages")) {
            cfg.messages = (val[0] == '1');
        }
    }
}

static int save_config(void) {
    static char out[4096];
    char num[32];
    unsigned short w[1024];
    HANDLE h;
    DWORD n;
    int i;
    out[0] = 0;
    s_cat(out, sizeof(out),
        "; " PLUGIN_NAME " - Einstellungen\r\n"
        "; Am einfachsten ueber TeamSpeak > Optionen > Addons > " PLUGIN_NAME " > Einstellungen.\r\n"
        "; Bei direkter Bearbeitung gelten Aenderungen beim naechsten Channel-Wechsel.\r\n"
        ";\r\n"
        "; channel    = Teil des Channel-Namens, in dem die TS-Sounds leise sein sollen.\r\n"
        ";              Mehrere 'channel='-Zeilen sind erlaubt (max. " STR(MAX_PATTERNS) ").\r\n"
        "; ignorecase = 1: Gross-/Kleinschreibung egal, 0: muss exakt passen\r\n"
        "; volume     = Soundpack-Lautstaerke in dB im Channel (-100 = praktisch unhoerbar)\r\n"
        "; messages   = 1: Hinweis im Chat beim Absenken/Zuruecksetzen\r\n"
        "\r\n");
    for (i = 0; i < cfg.count; i++) {
        s_cat(out, sizeof(out), "channel=");
        s_cat(out, sizeof(out), cfg.patterns[i]);
        s_cat(out, sizeof(out), "\r\n");
    }
    s_cat(out, sizeof(out), cfg.ignoreCase ? "ignorecase=1\r\n" : "ignorecase=0\r\n");
    fmt_float(num, cfg.volume);
    s_cat(out, sizeof(out), "volume=");
    s_cat(out, sizeof(out), num);
    s_cat(out, sizeof(out), "\r\n");
    s_cat(out, sizeof(out), cfg.messages ? "messages=1\r\n" : "messages=0\r\n");

    config_file(w, 1024, INI_NAME);
    h = CreateFileW(w, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!WriteFile(h, out, (DWORD)s_len(out), &n, 0)) n = 0;
    CloseHandle(h);
    return n == s_len(out);
}

/* ---------------- Zustandsdatei (Absturzschutz) ----------------
 * Solange mindestens eine Verbindung abgesenkt ist, liegt neben der ini eine
 * Datei mit dem Original-Wert von volume_factor_wave. Wird TeamSpeak beendet,
 * ohne dass das Plugin zuruecksetzen konnte (Absturz, Task-Manager), stellt
 * das Plugin beim naechsten Start diesen Wert wieder her. */
static int state_read(float* v) {
    unsigned short w[1024]; char buf[32]; DWORD got = 0; HANDLE h;
    config_file(w, 1024, STATE_NAME);
    h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return 0;
    if (!ReadFile(h, buf, sizeof(buf) - 1, &got, 0)) got = 0;
    CloseHandle(h);
    buf[got] = 0;
    if (got == 0) return 0;
    *v = parse_float(buf, 0.0f);
    return 1;
}

static void state_write(float v) {
    unsigned short w[1024]; char buf[32]; DWORD n; HANDLE h;
    config_file(w, 1024, STATE_NAME);
    fmt_float(buf, v);
    h = CreateFileW(w, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, buf, (DWORD)s_len(buf), &n, 0);
    CloseHandle(h);
}

static void state_clear(void) {
    unsigned short w[1024];
    config_file(w, 1024, STATE_NAME);
    DeleteFileW(w);
}

static int any_active(void) {
    int i;
    for (i = 0; i < MAX_HANDLERS; i++) if (slots[i].sch && slots[i].active) return 1;
    return 0;
}

/* ---------------- Kernlogik ---------------- */
static void say(uint64 sch, const char* msg) {
    char buf[256];
    if (!cfg.messages) return;
    buf[0] = 0;
    s_cat(buf, sizeof(buf), "[b][color=#888888]" PLUGIN_NAME ":[/color][/b] ");
    s_cat(buf, sizeof(buf), msg);
    ts3.printMessage(sch, buf, PLUGIN_MESSAGE_TARGET_SERVER);
}

static void deactivate(uint64 sch, int announce) {
    char val[32];
    int i = slot_for(sch, 0);
    if (i < 0 || !slots[i].active) return;
    fmt_float(val, slots[i].saved);
    ts3.setPlaybackConfigValue(sch, "volume_factor_wave", val);
    slots[i].active = 0;
    if (!any_active()) state_clear();
    if (announce) {
        char m[96]; m[0] = 0;
        s_cat(m, sizeof(m), "TS-Sounds wieder normal (");
        s_cat(m, sizeof(m), val);
        s_cat(m, sizeof(m), " dB)");
        say(sch, m);
    }
}

static void activate(uint64 sch) {
    char val[32];
    float old = 0.0f;
    int i = slot_for(sch, 1);
    if (i < 0) return;
    if (slots[i].active) {
        /* Schon abgesenkt: nur eine evtl. geaenderte Ziel-Lautstaerke uebernehmen. */
        fmt_float(val, cfg.volume);
        ts3.setPlaybackConfigValue(sch, "volume_factor_wave", val);
        return;
    }
    if (ts3.getPlaybackConfigValueAsFloat(sch, "volume_factor_wave", &old) != ERROR_ok) old = 0.0f;
    if (!any_active()) {
        /* Rest eines Absturzes: gespeicherten Original-Wert bevorzugen,
           sonst nie den abgesenkten Wert als "Original" merken. */
        float s;
        if (state_read(&s) && old < s - 0.01f) old = s;
        else if (old <= cfg.volume + 0.01f) old = 0.0f;
        state_write(old);
    } else if (old <= cfg.volume + 0.01f) {
        old = 0.0f;
    }
    slots[i].saved = old;
    fmt_float(val, cfg.volume);
    ts3.setPlaybackConfigValue(sch, "volume_factor_wave", val);
    slots[i].active = 1;
    {
        char m[96]; m[0] = 0;
        s_cat(m, sizeof(m), "Channel erkannt, TS-Sounds abgesenkt (");
        s_cat(m, sizeof(m), val);
        s_cat(m, sizeof(m), " dB)");
        say(sch, m);
    }
}

/* Nicht im Mute-Channel und nichts aktiv: Wert eines abgestuerzten Laufs zuruecksetzen. */
static void recover(uint64 sch) {
    float s, cur = 0.0f;
    if (any_active() || !state_read(&s)) return;
    if (ts3.getPlaybackConfigValueAsFloat(sch, "volume_factor_wave", &cur) == ERROR_ok && cur < s - 0.01f) {
        char val[32], m[128];
        fmt_float(val, s);
        ts3.setPlaybackConfigValue(sch, "volume_factor_wave", val);
        m[0] = 0;
        s_cat(m, sizeof(m), "TS-Sounds nach unsauberem Beenden wiederhergestellt (");
        s_cat(m, sizeof(m), val);
        s_cat(m, sizeof(m), " dB)");
        say(sch, m);
    }
    state_clear();
}

static int channel_matches(uint64 sch, uint64 channelID) {
    char* name = 0;
    int i, hit = 0;
    if (ts3.getChannelVariableAsString(sch, channelID, CHANNEL_NAME, &name) != ERROR_ok || !name) return 0;
    for (i = 0; i < cfg.count; i++) {
        if (contains(name, cfg.patterns[i], cfg.ignoreCase)) { hit = 1; break; }
    }
    ts3.freeMemory(name);
    return hit;
}

static int my_id(uint64 sch, anyID* me) {
    return ts3.getClientID(sch, me) == ERROR_ok;
}

static int in_muted_channel(uint64 sch) {
    anyID me; uint64 ch = 0;
    if (!my_id(sch, &me)) return -1;
    if (ts3.getChannelOfClient(sch, me, &ch) != ERROR_ok) return -1;
    return channel_matches(sch, ch);
}

static void cancel_pending(int i) {
    if (slots[i].timer) {
        DeleteTimerQueueTimer(timer_queue, slots[i].timer, 0); /* nicht blockierend */
        slots[i].timer = 0;
    }
    slots[i].gen++;
}

static void __stdcall delayed_activate(void* param, unsigned char fired) {
    unsigned long long v = (unsigned long long)param;
    int i = (int)(v & 0xFF);
    unsigned int gen = (unsigned int)(v >> 8);
    (void)fired;
    LOCK();
    if (!shutting_down && slots[i].gen == gen && slots[i].timer) {
        DeleteTimerQueueTimer(timer_queue, slots[i].timer, 0);
        slots[i].timer = 0;
        if (in_muted_channel(slots[i].sch) == 1) activate(slots[i].sch);
    }
    UNLOCK();
}

static void schedule_activate(uint64 sch) {
    int i = slot_for(sch, 1);
    if (i < 0) return;
    if (slots[i].active) { activate(sch); return; } /* nur Lautstaerke aktualisieren */
    if (slots[i].timer) return;                     /* schon geplant */
    slots[i].gen++;
    if (!timer_queue || !CreateTimerQueueTimer(&slots[i].timer, timer_queue, delayed_activate,
            (void*)(((unsigned long long)slots[i].gen << 8) | (unsigned long long)i),
            ACTIVATE_DELAY_MS, 0, WT_EXECUTEONLYONCE)) {
        slots[i].timer = 0;
        activate(sch); /* Fallback: sofort */
    }
}

static void evaluate(uint64 sch) {
    int m;
    load_config();
    m = in_muted_channel(sch);
    if (m < 0) return;
    if (m) {
        schedule_activate(sch);
    } else {
        int i = slot_for(sch, 0);
        if (i >= 0) cancel_pending(i);
        if (i >= 0 && slots[i].active) deactivate(sch, 1);
        else recover(sch);
    }
}

static void on_own_move(uint64 sch, anyID clientID) {
    anyID me;
    LOCK();
    if (my_id(sch, &me) && me == clientID) evaluate(sch);
    UNLOCK();
}

static void evaluate_all(void) {
    uint64* list = 0;
    if (ts3.getServerConnectionHandlerList(&list) == ERROR_ok && list) {
        int i;
        for (i = 0; list[i]; i++) {
            int status = 0;
            if (ts3.getConnectionStatus(list[i], &status) == ERROR_ok && status == STATUS_CONNECTION_ESTABLISHED)
                evaluate(list[i]);
        }
        ts3.freeMemory(list);
    }
}

/* ---------------- Einstellungsdialog ---------------- */
#define IDOK 1
#define IDCANCEL 2
#define IDC_CHANNELS 101
#define IDC_IGNORECASE 102
#define IDC_VOLUME 103
#define IDC_MESSAGES 104
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define MB_OK_WARN 0x30

#define WS_CHILD 0x40000000L
#define WS_VISIBLE 0x10000000L
#define WS_TABSTOP 0x00010000L
#define WS_BORDER 0x00800000L
#define WS_VSCROLL 0x00200000L
#define WS_POPUP 0x80000000L
#define WS_CAPTION 0x00C00000L
#define WS_SYSMENU 0x00080000L
#define DS_SETFONT 0x40L
#define DS_MODALFRAME 0x80L
#define DS_CENTER 0x0800L
#define ES_MULTILINE 0x0004L
#define ES_AUTOVSCROLL 0x0040L
#define ES_AUTOHSCROLL 0x0080L
#define ES_WANTRETURN 0x1000L
#define BS_DEFPUSHBUTTON 0x1L
#define BS_AUTOCHECKBOX 0x3L
#define WS_EX_CLIENTEDGE 0x200L

#define CLS_BUTTON 0x0080
#define CLS_EDIT 0x0081
#define CLS_STATIC 0x0082

static DWORD tpl_buf[1024];
static unsigned short* tpl_p;

static void tpl_word(unsigned short w) { *tpl_p++ = w; }
static void tpl_dword(DWORD d) { tpl_word((unsigned short)(d & 0xFFFF)); tpl_word((unsigned short)(d >> 16)); }
static void tpl_str(const unsigned short* t) { do { tpl_word(*t); } while (*t++); }
static void tpl_align(void) { if (((unsigned long long)tpl_p) & 2) tpl_word(0); }

static void tpl_item(DWORD style, short x, short y, short cx, short cy, unsigned short id, unsigned short cls, const unsigned short* text) {
    tpl_align();
    tpl_dword(style | WS_CHILD | WS_VISIBLE);
    tpl_dword(cls == CLS_EDIT ? WS_EX_CLIENTEDGE : 0);
    tpl_word((unsigned short)x); tpl_word((unsigned short)y);
    tpl_word((unsigned short)cx); tpl_word((unsigned short)cy);
    tpl_word(id);
    tpl_word(0xFFFF); tpl_word(cls);
    tpl_str(text);
    tpl_word(0);
}

static const void* build_template(void) {
    tpl_p = (unsigned short*)tpl_buf;
    tpl_dword(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    tpl_dword(0);
    tpl_word(8);                       /* Anzahl Controls */
    tpl_word(0); tpl_word(0);          /* x, y */
    tpl_word(230); tpl_word(172);      /* cx, cy */
    tpl_word(0); tpl_word(0);          /* kein Menue, Standard-Klasse */
    tpl_str(L"" PLUGIN_NAME " – Einstellungen");
    tpl_word(9); tpl_str(L"Segoe UI");

    tpl_item(0, 7, 7, 216, 18, 0xFFFF, CLS_STATIC,
             L"Channels, in denen die TS-Sounds abgesenkt werden.\nTeil des Channel-Namens, einer pro Zeile (max. " STR(MAX_PATTERNS) "):");
    tpl_item(WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
             7, 28, 216, 62, IDC_CHANNELS, CLS_EDIT, L"");
    tpl_item(WS_TABSTOP | BS_AUTOCHECKBOX, 7, 96, 216, 10, IDC_IGNORECASE, CLS_BUTTON,
             L"Groß-/Kleinschreibung ignorieren");
    tpl_item(0, 7, 115, 160, 10, 0xFFFF, CLS_STATIC,
             L"Lautstärke im Channel in dB (-100 = unhörbar):");
    tpl_item(WS_TABSTOP | ES_AUTOHSCROLL, 170, 113, 53, 13, IDC_VOLUME, CLS_EDIT, L"");
    tpl_item(WS_TABSTOP | BS_AUTOCHECKBOX, 7, 132, 216, 10, IDC_MESSAGES, CLS_BUTTON,
             L"Hinweis im Chat anzeigen");
    tpl_item(WS_TABSTOP | BS_DEFPUSHBUTTON, 119, 151, 50, 14, IDOK, CLS_BUTTON, L"OK");
    tpl_item(WS_TABSTOP, 173, 151, 50, 14, IDCANCEL, CLS_BUTTON, L"Abbrechen");
    return tpl_buf;
}

static void set_text_utf8(HWND dlg, int id, const char* utf8) {
    static unsigned short w[2048];
    utf8_to_wide(utf8, w, 2048);
    SetDlgItemTextW(dlg, id, w);
}

static void get_text_utf8(HWND dlg, int id, char* out, int cap) {
    static unsigned short w[2048];
    w[0] = 0;
    GetDlgItemTextW(dlg, id, w, 2048);
    if (WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, 0, 0) <= 0) out[0] = 0;
}

/* Kopiert hoechstens cap-1 Bytes, ohne ein UTF-8-Zeichen zu zerschneiden. */
static void copy_utf8(char* dst, size_t cap, const char* src, size_t n) {
    size_t i;
    if (n > cap - 1) {
        n = cap - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    for (i = 0; i < n; i++) dst[i] = src[i];
    dst[n] = 0;
}

static void dlg_init(HWND dlg) {
    static char chans[MAX_PATTERNS * (PATTERN_LEN + 2) + 1];
    char num[32];
    int i;
    load_config();
    chans[0] = 0;
    for (i = 0; i < cfg.count; i++) {
        if (i) s_cat(chans, sizeof(chans), "\r\n");
        s_cat(chans, sizeof(chans), cfg.patterns[i]);
    }
    set_text_utf8(dlg, IDC_CHANNELS, chans);
    fmt_float(num, cfg.volume);
    set_text_utf8(dlg, IDC_VOLUME, num);
    CheckDlgButton(dlg, IDC_IGNORECASE, cfg.ignoreCase ? 1 : 0);
    CheckDlgButton(dlg, IDC_MESSAGES, cfg.messages ? 1 : 0);
}

static int dlg_apply(HWND dlg) {
    static char chans[8192];
    char num[64];
    char* p;
    float vol;

    get_text_utf8(dlg, IDC_VOLUME, num, sizeof(num));
    vol = parse_float(num, 1000.0f);
    if (vol < -100.0f || vol > 20.0f) {
        MessageBoxW(dlg, L"Bitte eine Lautstärke zwischen -100 und 20 dB eingeben.", L"" PLUGIN_NAME, MB_OK_WARN);
        SetFocus(GetDlgItem(dlg, IDC_VOLUME));
        return 0;
    }

    get_text_utf8(dlg, IDC_CHANNELS, chans, sizeof(chans));
    cfg.count = 0;
    p = chans;
    while (*p && cfg.count < MAX_PATTERNS) {
        char* a = p; char* e;
        while (*p && *p != '\r' && *p != '\n') p++;
        e = p;
        while (*p == '\r' || *p == '\n') p++;
        while (a < e && (*a == ' ' || *a == '\t')) a++;
        while (e > a && (e[-1] == ' ' || e[-1] == '\t')) e--;
        if (e > a) copy_utf8(cfg.patterns[cfg.count++], PATTERN_LEN, a, (size_t)(e - a));
    }
    cfg.ignoreCase = IsDlgButtonChecked(dlg, IDC_IGNORECASE) == 1;
    cfg.messages = IsDlgButtonChecked(dlg, IDC_MESSAGES) == 1;
    cfg.volume = vol;

    if (!save_config()) {
        MessageBoxW(dlg, L"Die Einstellungen konnten nicht gespeichert werden.", L"" PLUGIN_NAME, MB_OK_WARN);
        return 0;
    }
    return 1;
}

static INT_PTR __stdcall dlg_proc(HWND dlg, unsigned int msg, WPARAM wp, LPARAM lp) {
    (void)lp;
    if (msg == WM_INITDIALOG) { LOCK(); dlg_init(dlg); UNLOCK(); return 1; }
    if (msg == WM_COMMAND) {
        int id = (int)(wp & 0xFFFF);
        if (id == IDOK) {
            int ok;
            LOCK(); ok = dlg_apply(dlg); UNLOCK();
            if (ok) EndDialog(dlg, 1);
            return 1;
        }
        if (id == IDCANCEL) { EndDialog(dlg, 0); return 1; }
    }
    return 0;
}

/* ---------------- Pflicht-Exports ---------------- */
EXPORT const char* ts3plugin_name(void) { return PLUGIN_NAME; }
EXPORT const char* ts3plugin_version(void) { return PLUGIN_VERSION; }
EXPORT int ts3plugin_apiVersion(void) { return PLUGIN_API_VERSION; }
EXPORT const char* ts3plugin_author(void) { return "Airborne Devs"; }
EXPORT const char* ts3plugin_description(void) {
    return "Verringert die Lautstärke des TS-Soundpacks oder schaltet es komplett aus, sobald man einen "
           "in der Konfiguration definierten Channel betritt. Einstellungen über den Button 'Einstellungen'.";
}
EXPORT void ts3plugin_setFunctionPointers(const struct TS3Functions funcs) { ts3 = funcs; }

EXPORT int ts3plugin_init(void) {
    memset(slots, 0, sizeof(slots));
    shutting_down = 0;
    InitializeCriticalSection(&lock);
    timer_queue = CreateTimerQueue();
    LOCK();
    load_config();
    /* Plugin waehrend laufender Verbindung aktiviert: sofort pruefen */
    evaluate_all();
    UNLOCK();
    return 0;
}

EXPORT void ts3plugin_shutdown(void) {
    int i;
    LOCK(); shutting_down = 1; UNLOCK();
    /* Alle Timer abbrechen und auf laufende Callbacks warten (ohne Sperre,
       sonst Deadlock), damit nach dem Entladen nichts mehr feuert. */
    if (timer_queue) { DeleteTimerQueueEx(timer_queue, INVALID_HANDLE_VALUE); timer_queue = 0; }
    LOCK();
    for (i = 0; i < MAX_HANDLERS; i++) {
        slots[i].timer = 0;
        if (slots[i].sch && slots[i].active) deactivate(slots[i].sch, 0);
    }
    UNLOCK();
    DeleteCriticalSection(&lock);
}

EXPORT void ts3plugin_registerPluginID(const char* id) { (void)id; }
EXPORT void ts3plugin_freeMemory(void* data) { (void)data; }
EXPORT int ts3plugin_offersConfigure(void) { return PLUGIN_OFFERS_CONFIGURE_QT_THREAD; }

EXPORT void ts3plugin_configure(void* handle, void* qParentWidget) {
    static int open = 0;
    (void)handle; (void)qParentWidget;
    if (open) return;
    open = 1;
    if (DialogBoxIndirectParamW(0, build_template(), GetActiveWindow(), dlg_proc, 0) == 1) {
        LOCK(); evaluate_all(); UNLOCK(); /* neue Einstellungen sofort anwenden */
    }
    open = 0;
}

/* ---------------- Events ---------------- */
EXPORT void ts3plugin_onConnectStatusChangeEvent(uint64 sch, int newStatus, unsigned int errorNumber) {
    (void)errorNumber;
    LOCK();
    if (newStatus == STATUS_CONNECTION_ESTABLISHED) {
        evaluate(sch);
    } else if (newStatus == STATUS_DISCONNECTED) {
        int i = slot_for(sch, 0);
        if (i >= 0) cancel_pending(i);
        deactivate(sch, 0);
        if (i >= 0) slots[i].sch = 0;
    }
    UNLOCK();
}

EXPORT void ts3plugin_onClientMoveEvent(uint64 sch, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, const char* moveMessage) {
    (void)oldChannelID; (void)newChannelID; (void)visibility; (void)moveMessage;
    on_own_move(sch, clientID);
}

EXPORT void ts3plugin_onClientMoveMovedEvent(uint64 sch, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID moverID, const char* moverName, const char* moverUniqueIdentifier, const char* moveMessage) {
    (void)oldChannelID; (void)newChannelID; (void)visibility; (void)moverID; (void)moverName; (void)moverUniqueIdentifier; (void)moveMessage;
    on_own_move(sch, clientID);
}

EXPORT void ts3plugin_onClientKickFromChannelEvent(uint64 sch, anyID clientID, uint64 oldChannelID, uint64 newChannelID, int visibility, anyID kickerID, const char* kickerName, const char* kickerUniqueIdentifier, const char* kickMessage) {
    (void)oldChannelID; (void)newChannelID; (void)visibility; (void)kickerID; (void)kickerName; (void)kickerUniqueIdentifier; (void)kickMessage;
    on_own_move(sch, clientID);
}

EXPORT void ts3plugin_onUpdateChannelEditedEvent(uint64 sch, uint64 channelID, anyID invokerID, const char* invokerName, const char* invokerUniqueIdentifier) {
    anyID me; uint64 ch = 0;
    (void)invokerID; (void)invokerName; (void)invokerUniqueIdentifier;
    LOCK();
    if (my_id(sch, &me) && ts3.getChannelOfClient(sch, me, &ch) == ERROR_ok && ch == channelID) evaluate(sch);
    UNLOCK();
}
