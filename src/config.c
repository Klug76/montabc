#include "config.h"
#include "util.h"
#include <shlobj.h>

static MonitorCfg s_mon[APP_MAX_MONITORS];
static int s_count;
static DockEdge s_defEdge = DOCK_RIGHT;
static double s_defWidth = CFG_DEFAULT_WIDTH;
static WCHAR s_dir[MAX_PATH];
static WCHAR s_path[MAX_PATH];

static void BuildPath(void)
{
    WCHAR base[MAX_PATH];

    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, base) != S_OK)
        return;
    wsprintfW(s_path, L"%s\\montabc\\settings.ini", base);
    wsprintfW(s_dir, L"%s\\montabc", base);
}

static int StartsWith(const WCHAR *s, const WCHAR *prefix)
{
    while (*prefix)
    {
        if (*s++ != *prefix++)
            return 0;
    }
    return 1;
}

static void ReadMonitorSection(const WCHAR *section)
{
    MonitorCfg *m;
    WCHAR buf[32];

    if (s_count >= APP_MAX_MONITORS)
        return;

    m = &s_mon[s_count++];
    lstrcpynW(m->device, section + 8, CCHDEVICENAME); /* после "monitor." */
    GetPrivateProfileStringW(section, L"edge", L"right", buf, 32, s_path);
    m->edge = (buf[0] == L'l') ? DOCK_LEFT : DOCK_RIGHT;
    GetPrivateProfileStringW(section, L"width", L"", buf, 32, s_path);
    m->widthPct = buf[0]
                      ? Util_ClampD(Util_WtoD(buf), CFG_MIN_WIDTH, CFG_MAX_WIDTH)
                      : s_defWidth;
    m->enabled = GetPrivateProfileIntW(section, L"enabled", 1, s_path) != 0;
}

void Cfg_Load(void)
{
    WCHAR names[2048];
    WCHAR *p;
    WCHAR buf[32];

    BuildPath();

    GetPrivateProfileStringW(L"default", L"edge", L"right", buf, 32, s_path);
    s_defEdge = (buf[0] == L'l') ? DOCK_LEFT : DOCK_RIGHT;
    GetPrivateProfileStringW(L"default", L"width", L"", buf, 32, s_path);
    if (buf[0])
        s_defWidth = Util_ClampD(Util_WtoD(buf), CFG_MIN_WIDTH, CFG_MAX_WIDTH);

    GetPrivateProfileSectionNamesW(names, 2048, s_path);
    for (p = names; *p; p += lstrlenW(p) + 1)
    {
        if (StartsWith(p, L"monitor."))
            ReadMonitorSection(p);
    }
}

MonitorCfg *Cfg_For(const WCHAR *device)
{
    int i;
    MonitorCfg *m;

    for (i = 0; i < s_count; i++)
        if (lstrcmpW(s_mon[i].device, device) == 0)
            return &s_mon[i];

    if (s_count >= APP_MAX_MONITORS)
        return NULL;

    m = &s_mon[s_count++];
    lstrcpynW(m->device, device, CCHDEVICENAME);
    if (s_count > 1)
    {
        /* новый монитор наследует геометрию уже настроенной панели */
        m->edge = s_mon[0].edge;
        m->widthPct = s_mon[0].widthPct;
    }
    else
    {
        m->edge = s_defEdge;
        m->widthPct = s_defWidth;
    }
    m->enabled = TRUE;
    return m;
}

void Cfg_Save(void)
{
    WCHAR buf[32];
    WCHAR sect[CCHDEVICENAME + 16];
    int i;

    if (!s_path[0])
        return;

    CreateDirectoryW(s_dir, NULL);

    WritePrivateProfileStringW(L"default", L"edge",
                               s_defEdge == DOCK_LEFT ? L"left" : L"right", s_path);
    Util_DtoW(buf, s_defWidth);
    WritePrivateProfileStringW(L"default", L"width", buf, s_path);

    for (i = 0; i < s_count; i++)
    {
        wsprintfW(sect, L"monitor.%s", s_mon[i].device);
        WritePrivateProfileStringW(sect, L"edge",
                                   s_mon[i].edge == DOCK_LEFT ? L"left" : L"right", s_path);
        Util_DtoW(buf, s_mon[i].widthPct);
        WritePrivateProfileStringW(sect, L"width", buf, s_path);
        WritePrivateProfileStringW(sect, L"enabled",
                                   s_mon[i].enabled ? L"1" : L"0", s_path);
    }
}
