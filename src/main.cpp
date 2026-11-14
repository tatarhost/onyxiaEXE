#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <commdlg.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")

namespace {
constexpr wchar_t kClassName[] = L"OnyxiaEXE.Native.Window";
constexpr wchar_t kTitle[] = L"OnyxiaEXE";
constexpr COLORREF kPanelBg = RGB(239, 246, 252);
constexpr COLORREF kEditorBg = RGB(252, 254, 255);
constexpr COLORREF kText = RGB(24, 49, 73);

enum ControlId : int {
    ID_NEW = 1001, ID_OPEN, ID_SAVE, ID_SAVE_AS, ID_EXPORT,
    ID_COPY, ID_SCAN, ID_STUDIO, ID_TABS, ID_PROCESS_LIST,
    ID_PROCESS_INFO, ID_OUTPUT, ID_STATUS, ID_EDITOR, ID_ANALYZE, ID_RUN_LOCAL
};

struct ProcessEntry {
    DWORD pid{};
    std::wstring name;
    std::wstring path;
    SIZE_T workingSet{};
};

struct ScriptTab {
    std::wstring name;
    std::wstring path;
    HWND editor{};
    bool dirty{};
};

HINSTANCE g_instance{};
HWND g_main{};
HWND g_toolbar{};
HWND g_tabs{};
HWND g_processList{};
HWND g_processInfo{};
HWND g_output{};
HWND g_status{};
HWND g_editorLabel{};
HFONT g_uiFont{};
HFONT g_titleFont{};
HFONT g_editorFont{};
HBRUSH g_panelBrush{};
std::vector<ProcessEntry> g_processes;
std::vector<ScriptTab> g_scripts;
int g_activeTab{-1};
bool g_formatting{};

std::wstring Lower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring BaseName(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

std::wstring FormatBytes(SIZE_T bytes) {
    wchar_t buffer[64]{};
    if (bytes >= 1024ULL * 1024ULL * 1024ULL)
        swprintf_s(buffer, L"%.1f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    else
        swprintf_s(buffer, L"%.0f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

void Log(const std::wstring& message) {
    if (!g_output) return;
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t prefix[32]{};
    swprintf_s(prefix, L"[%02u:%02u:%02u] ", now.wHour, now.wMinute, now.wSecond);
    const int length = GetWindowTextLengthW(g_output);
    SendMessageW(g_output, EM_SETSEL, length, length);
    const std::wstring line = std::wstring(prefix) + message + L"\r\n";
    SendMessageW(g_output, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
    SendMessageW(g_output, EM_SCROLLCARET, 0, 0);
}

void SetStatus(const std::wstring& message) {
    if (g_status) SetWindowTextW(g_status, message.c_str());
}

std::wstring ReadUtf8File(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return {};
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) bytes.erase(0, 3);
    if (bytes.empty()) return {};

    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = MultiByteToWideChar(page, flags, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (count <= 0) {
        page = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(page, flags, bytes.data(),
            static_cast<int>(bytes.size()), nullptr, 0);
    }
    if (count <= 0) return {};
    std::wstring result(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(page, flags, bytes.data(), static_cast<int>(bytes.size()),
        result.data(), count);
    return result;
}

bool WriteUtf8File(const std::wstring& path, const std::wstring& text) {
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed < 0) return false;
    std::string bytes(static_cast<size_t>(needed), '\0');
    if (needed > 0) {
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
            bytes.data(), needed, nullptr, nullptr);
    }
    std::ofstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}

std::wstring GetEditorText(HWND editor) {
    const int length = GetWindowTextLengthW(editor);
    std::wstring text(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(editor, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

void SetSelectionColor(HWND editor, LONG start, LONG end, COLORREF color) {
    SendMessageW(editor, EM_SETSEL, start, end);
    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = color;
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
}

bool IsWordChar(wchar_t ch) {
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
        (ch >= L'0' && ch <= L'9') || ch == L'_' || ch >= 128;
}

void ApplySyntax(HWND editor) {
    if (!editor || g_formatting) return;
    const std::wstring source = GetEditorText(editor);
    if (source.size() > 180000) return;

    g_formatting = true;
    DWORD oldStart{}, oldEnd{};
    SendMessageW(editor, EM_GETSEL, reinterpret_cast<WPARAM>(&oldStart),
        reinterpret_cast<LPARAM>(&oldEnd));

    SendMessageW(editor, EM_SETSEL, 0, -1);
    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_COLOR | CFM_FACE | CFM_SIZE;
    base.crTextColor = RGB(23, 43, 61);
    base.yHeight = 200;
    wcscpy_s(base.szFaceName, L"Consolas");
    SendMessageW(editor, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&base));

    static const wchar_t* keywords[] = {
        L"and",L"break",L"do",L"else",L"elseif",L"end",L"false",L"for",
        L"function",L"if",L"in",L"local",L"nil",L"not",L"or",L"repeat",
        L"return",L"then",L"true",L"until",L"while",L"continue",L"export",L"type"
    };
    static const wchar_t* builtins[] = {
        L"game",L"workspace",L"script",L"print",L"warn",L"require",
        L"Instance",L"Vector3",L"Vector2",L"CFrame",L"Color3",L"UDim2",
        L"Enum",L"task",L"math",L"string",L"table",L"pairs",L"ipairs",
        L"pcall",L"tostring",L"tonumber",L"typeof"
    };

    size_t i = 0;
    while (i < source.size()) {
        if (source[i] == L'-' && i + 1 < source.size() && source[i + 1] == L'-') {
            const size_t start = i;
            while (i < source.size() && source[i] != L'\n') ++i;
            SetSelectionColor(editor, static_cast<LONG>(start), static_cast<LONG>(i), RGB(123, 143, 160));
            continue;
        }
        if (source[i] == L'"' || source[i] == L'\'') {
            const wchar_t quote = source[i];
            const size_t start = i++;
            while (i < source.size()) {
                if (source[i] == L'\\' && i + 1 < source.size()) { i += 2; continue; }
                if (source[i++] == quote) break;
            }
            SetSelectionColor(editor, static_cast<LONG>(start), static_cast<LONG>(i), RGB(48, 119, 66));
            continue;
        }
        if (source[i] >= L'0' && source[i] <= L'9') {
            const size_t start = i++;
            while (i < source.size() && ((source[i] >= L'0' && source[i] <= L'9') || source[i] == L'.')) ++i;
            SetSelectionColor(editor, static_cast<LONG>(start), static_cast<LONG>(i), RGB(173, 91, 40));
            continue;
        }
        if ((source[i] >= L'a' && source[i] <= L'z') ||
            (source[i] >= L'A' && source[i] <= L'Z') || source[i] == L'_') {
            const size_t start = i++;
            while (i < source.size() && IsWordChar(source[i])) ++i;
            const std::wstring word = source.substr(start, i - start);
            COLORREF color = RGB(23, 43, 61);
            bool colored = false;
            for (const auto* key : keywords) if (word == key) { color = RGB(123, 62, 157); colored = true; break; }
            if (!colored) for (const auto* key : builtins) if (word == key) { color = RGB(36, 93, 155); colored = true; break; }
            if (colored) SetSelectionColor(editor, static_cast<LONG>(start), static_cast<LONG>(i), color);
            continue;
        }
        ++i;
    }
    SendMessageW(editor, EM_SETSEL, oldStart, oldEnd);
    g_formatting = false;
}

void UpdateTabTitle(size_t index) {
    if (index >= g_scripts.size()) return;
    std::wstring title = g_scripts[index].name + (g_scripts[index].dirty ? L" *" : L"");
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = title.data();
    TabCtrl_SetItem(g_tabs, static_cast<int>(index), &item);
}

void ShowActiveEditor() {
    for (size_t i = 0; i < g_scripts.size(); ++i)
        ShowWindow(g_scripts[i].editor, static_cast<int>(i) == g_activeTab ? SW_SHOW : SW_HIDE);
    if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_scripts.size())) {
        const auto& tab = g_scripts[static_cast<size_t>(g_activeTab)];
        const std::wstring label = L"Luau source  |  " + tab.name;
        SetWindowTextW(g_editorLabel, label.c_str());
    } else {
        SetWindowTextW(g_editorLabel, L"Luau source");
    }
}

void NewTab(const std::wstring& name = L"Script.client.lua", const std::wstring& source = L"") {
    ScriptTab tab{};
    tab.name = name;
    tab.editor = CreateWindowExW(WS_EX_CLIENTEDGE, L"RICHEDIT20W", source.c_str(),
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
        ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
        0, 0, 100, 100, g_main, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_EDITOR)),
        g_instance, nullptr);
    if (!tab.editor) { Log(L"Failed to create the Luau editor control."); return; }
    SendMessageW(tab.editor, WM_SETFONT, reinterpret_cast<WPARAM>(g_editorFont), TRUE);
    SendMessageW(tab.editor, EM_SETBKGNDCOLOR, 0, kEditorBg);
    g_scripts.push_back(std::move(tab));

    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = g_scripts.back().name.data();
    TabCtrl_InsertItem(g_tabs, static_cast<int>(g_scripts.size() - 1), &item);
    g_activeTab = static_cast<int>(g_scripts.size() - 1);
    TabCtrl_SetCurSel(g_tabs, g_activeTab);
    ShowActiveEditor();
    ApplySyntax(g_scripts.back().editor);
    SetFocus(g_scripts.back().editor);
    SetStatus(L"New script created.");
}

bool SaveTab(size_t index, bool askPath) {
    if (index >= g_scripts.size()) return false;
    auto& tab = g_scripts[index];
    if (askPath || tab.path.empty()) {
        wchar_t path[MAX_PATH]{};
        const std::wstring suggested = tab.name.size() < MAX_PATH ? tab.name : L"Script.lua";
        wcscpy_s(path, suggested.c_str());
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = g_main;
        dialog.lpstrFilter = L"Lua / Luau source (*.lua)\0*.lua\0All files (*.*)\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"lua";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameW(&dialog)) return false;
        tab.path = path;
    }
    if (!WriteUtf8File(tab.path, GetEditorText(tab.editor))) {
        MessageBoxW(g_main, L"Could not write the selected file.", kTitle, MB_ICONERROR | MB_OK);
        return false;
    }
    tab.name = BaseName(tab.path);
    tab.dirty = false;
    UpdateTabTitle(index);
    ShowActiveEditor();
    Log(L"Saved " + tab.path);
    SetStatus(L"Saved " + tab.path);
    return true;
}

void OpenScript() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_main;
    dialog.lpstrFilter = L"Lua / Luau source (*.lua)\0*.lua\0Text files (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) return;
    NewTab(BaseName(path), ReadUtf8File(path));
    auto& tab = g_scripts.back();
    tab.path = path;
    tab.dirty = false;
    UpdateTabTitle(g_scripts.size() - 1);
    Log(L"Opened " + tab.path);
}

void CopyCurrentScript() {
    if (g_activeTab < 0 || g_activeTab >= static_cast<int>(g_scripts.size())) return;
    const std::wstring text = GetEditorText(g_scripts[static_cast<size_t>(g_activeTab)].editor);
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) return;
    void* data = GlobalLock(memory);
    if (!data) { GlobalFree(memory); return; }
    memcpy(data, text.c_str(), bytes);
    GlobalUnlock(memory);
    if (OpenClipboard(g_main)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
        CloseClipboard();
        Log(L"Script copied. Paste it into a Script or LocalScript in Roblox Studio.");
    } else {
        GlobalFree(memory);
        Log(L"Could not open the Windows clipboard.");
    }
}

std::wstring GetProcessPath(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL ok = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!ok) return {};
    path.resize(length);
    return path;
}

SIZE_T GetWorkingSet(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process) return 0;
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    const BOOL ok = GetProcessMemoryInfo(process, &counters, sizeof(counters));
    CloseHandle(process);
    return ok ? counters.WorkingSetSize : 0;
}

void SetProcessInfo(const ProcessEntry* entry) {
    if (!g_processInfo) return;
    std::wstring info;
    if (!entry) info = L"Select a process to inspect its PID and executable path.";
    else {
        info = L"PID: " + std::to_wstring(entry->pid) + L"\r\n";
        info += L"Process: " + entry->name + L"\r\n";
        info += L"RAM: " + (entry->workingSet ? FormatBytes(entry->workingSet) : L"Unavailable") + L"\r\n";
        info += L"Path: " + (entry->path.empty() ? L"Access denied / unavailable" : entry->path);
    }
    SetWindowTextW(g_processInfo, info.c_str());
}

void ScanProcesses() {
    g_processes.clear();
    ListView_DeleteAllItems(g_processList);
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { Log(L"Could not enumerate Windows processes."); return; }

    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    if (Process32FirstW(snapshot, &process)) {
        do {
            const DWORD pid = process.th32ProcessID;
            const std::wstring name = process.szExeFile;
            const std::wstring path = GetProcessPath(pid);
            if (Lower(name).find(L"roblox") == std::wstring::npos &&
                Lower(path).find(L"roblox") == std::wstring::npos) continue;

            ProcessEntry entry{};
            entry.pid = pid;
            entry.name = name;
            entry.path = path;
            entry.workingSet = GetWorkingSet(pid);
            g_processes.push_back(std::move(entry));
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);

    for (size_t i = 0; i < g_processes.size(); ++i) {
        const auto& entry = g_processes[i];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        wchar_t pidBuffer[24]{};
        swprintf_s(pidBuffer, L"%lu", entry.pid);
        item.pszText = pidBuffer;
        item.lParam = static_cast<LPARAM>(entry.pid);
        const int row = ListView_InsertItem(g_processList, &item);
        ListView_SetItemText(g_processList, row, 1, const_cast<wchar_t*>(entry.name.c_str()));
        const std::wstring memory = entry.workingSet ? FormatBytes(entry.workingSet) : L"—";
        ListView_SetItemText(g_processList, row, 2, const_cast<wchar_t*>(memory.c_str()));
    }
    SetStatus(L"Roblox process matches: " + std::to_wstring(g_processes.size()));
    if (g_processes.empty()) Log(L"No process with 'Roblox' in its executable name or path was found.");
    else Log(L"Found " + std::to_wstring(g_processes.size()) + L" Roblox process(es).");
    SetProcessInfo(nullptr);
}

void OpenStudio() {
    wchar_t localAppData[MAX_PATH]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        Log(L"LOCALAPPDATA is unavailable.");
        return;
    }
    const std::filesystem::path versions = std::filesystem::path(localAppData) / L"Roblox" / L"Versions";
    std::error_code error;
    if (std::filesystem::exists(versions, error)) {
        for (const auto& entry : std::filesystem::directory_iterator(versions, error)) {
            const auto exe = entry.path() / L"RobloxStudioBeta.exe";
            if (std::filesystem::exists(exe, error)) {
                ShellExecuteW(g_main, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                Log(L"Roblox Studio launched.");
                return;
            }
        }
    }
    MessageBoxW(g_main, L"Roblox Studio was not found in the default installation folder.",
        kTitle, MB_ICONINFORMATION | MB_OK);
}


std::wstring FindLuauTool(const wchar_t* filename) {
    wchar_t path[32768]{};
    const DWORD length = SearchPathW(nullptr, filename, nullptr,
        static_cast<DWORD>(std::size(path)), path, nullptr);
    if (!length || length >= std::size(path)) return {};
    return path;
}

void RunLuauTool(bool analyze) {
    if (g_activeTab < 0 || g_activeTab >= static_cast<int>(g_scripts.size())) return;

    const wchar_t* toolName = analyze ? L"luau-analyze.exe" : L"luau.exe";
    const std::wstring toolPath = FindLuauTool(toolName);
    if (toolPath.empty()) {
        const wchar_t* message = analyze
            ? L"luau-analyze.exe was not found on PATH.\nInstall the official Luau CLI tools and restart OnyxiaEXE."
            : L"luau.exe was not found on PATH.\nInstall the official Luau CLI tools and restart OnyxiaEXE.\n\nThis runs Luau locally; Roblox APIs such as game and workspace exist only inside Studio.";
        MessageBoxW(g_main, message, kTitle, MB_ICONINFORMATION | MB_OK);
        Log(std::wstring(toolName) + L" not found on PATH.");
        return;
    }

    wchar_t tempDirectory[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tempDirectory)) {
        Log(L"Could not locate the Windows temporary directory.");
        return;
    }
    wchar_t tempName[MAX_PATH]{};
    if (!GetTempFileNameW(tempDirectory, L"ony", 0, tempName)) {
        Log(L"Could not create a temporary source file.");
        return;
    }

    std::filesystem::path sourcePath(tempName);
    DeleteFileW(sourcePath.c_str());
    sourcePath.replace_extension(L".lua");
    const std::filesystem::path outputPath = sourcePath.wstring() + L".log";

    if (!WriteUtf8File(sourcePath.wstring(),
        GetEditorText(g_scripts[static_cast<size_t>(g_activeTab)].editor))) {
        Log(L"Could not write temporary Luau source.");
        return;
    }

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE outputFile = CreateFileW(outputPath.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (outputFile == INVALID_HANDLE_VALUE) {
        DeleteFileW(sourcePath.c_str());
        Log(L"Could not create Luau output capture file.");
        return;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = outputFile;
    startup.hStdError = outputFile;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring command = L"\"" + toolPath + L"\" \"" + sourcePath.wstring() + L"\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(toolPath.c_str(), mutableCommand.data(),
        nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);

    CloseHandle(outputFile);
    if (!created) {
        DeleteFileW(sourcePath.c_str());
        DeleteFileW(outputPath.c_str());
        Log(std::wstring(L"Could not start ") + toolName + L". Win32 error: " +
            std::to_wstring(GetLastError()));
        return;
    }

    Log(std::wstring(analyze ? L"Analyzing " : L"Running locally: ") +
        g_scripts[static_cast<size_t>(g_activeTab)].name);

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 15000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 2000);
        Log(L"Luau tool stopped after the 15-second time limit.");
    } else if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        GetExitCodeProcess(process.hProcess, &exitCode);
        Log(L"Luau tool exit code: " + std::to_wstring(exitCode));
    } else {
        Log(L"Wait for Luau tool failed.");
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    const std::wstring output = ReadUtf8File(outputPath.wstring());
    if (output.empty()) Log(L"(no output)");
    else {
        size_t start = 0;
        while (start < output.size()) {
            const size_t end = output.find_first_of(L"\r\n", start);
            const std::wstring line = output.substr(start,
                end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!line.empty()) Log(line);
            if (end == std::wstring::npos) break;
            start = end + 1;
            while (start < output.size() && (output[start] == L'\r' || output[start] == L'\n')) ++start;
        }
    }

    DeleteFileW(sourcePath.c_str());
    DeleteFileW(outputPath.c_str());
}

void LayoutChildren(HWND window) {
    RECT rc{};
    GetClientRect(window, &rc);
    const int width = rc.right;
    const int height = rc.bottom;
    const int leftWidth = std::max(285, std::min(340, width / 3));
    const int top = 126;
    const int statusHeight = 22;
    const int outputHeight = 112;
    const int bottom = height - statusHeight - 10;
    const int editorBottom = bottom - outputHeight - 28;
    const int rightX = leftWidth + 20;
    const int rightWidth = std::max(300, width - rightX - 10);

    MoveWindow(g_toolbar, 10, 78, width - 20, 38, TRUE);
    MoveWindow(g_processList, 16, top + 27, leftWidth - 22, editorBottom - top - 34, TRUE);
    MoveWindow(g_processInfo, 16, editorBottom - 1, leftWidth - 22, 92, TRUE);
    MoveWindow(g_tabs, rightX, top, rightWidth, 28, TRUE);
    MoveWindow(g_editorLabel, rightX, top + 29, rightWidth, 20, TRUE);
    for (auto& tab : g_scripts)
        MoveWindow(tab.editor, rightX, top + 50, rightWidth, editorBottom - top - 51, TRUE);
    MoveWindow(g_output, rightX, editorBottom + 24, rightWidth, outputHeight, TRUE);
    MoveWindow(g_status, 0, height - statusHeight, width, statusHeight, TRUE);
}

void AddButton(HWND parent, const wchar_t* text, int id, int x, int width) {
    HWND button = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        x, 82, width, 28, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_instance, nullptr);
    SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);
}

void CreateControls(HWND window) {
    LoadLibraryW(L"Riched20.dll");
    g_uiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_editorFont = CreateFontW(-19, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");

    g_toolbar = CreateWindowExW(WS_EX_CLIENTEDGE, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_WHITERECT, 10, 78, 1000, 38,
        window, nullptr, g_instance, nullptr);
    AddButton(window, L"New", ID_NEW, 15, 62);
    AddButton(window, L"Open", ID_OPEN, 82, 62);
    AddButton(window, L"Save", ID_SAVE, 149, 62);
    AddButton(window, L"Save As", ID_SAVE_AS, 216, 76);
    AddButton(window, L"Export .lua", ID_EXPORT, 297, 88);
    AddButton(window, L"Copy to Studio", ID_COPY, 390, 112);
    AddButton(window, L"Scan Roblox", ID_SCAN, 507, 96);
    AddButton(window, L"Open Studio", ID_STUDIO, 608, 96);
    AddButton(window, L"Analyze Luau", ID_ANALYZE, 709, 96);
    AddButton(window, L"Run Local", ID_RUN_LOCAL, 810, 88);

    g_processList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        16, 153, 310, 400, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS_LIST)), g_instance, nullptr);
    ListView_SetExtendedListViewStyle(g_processList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT | LVCF_WIDTH;
    column.pszText = const_cast<wchar_t*>(L"PID"); column.cx = 58;
    ListView_InsertColumn(g_processList, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"PROCESS"); column.cx = 150;
    ListView_InsertColumn(g_processList, 1, &column);
    column.pszText = const_cast<wchar_t*>(L"RAM"); column.cx = 76;
    ListView_InsertColumn(g_processList, 2, &column);

    g_processInfo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        L"Select a process to inspect its PID and executable path.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        16, 560, 310, 90, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_PROCESS_INFO)), g_instance, nullptr);

    g_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_TABS | TCS_SINGLELINE,
        350, 126, 700, 28, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TABS)), g_instance, nullptr);
    g_editorLabel = CreateWindowExW(0, L"STATIC", L"Luau source",
        WS_CHILD | WS_VISIBLE | SS_LEFT, 350, 155, 700, 20,
        window, nullptr, g_instance, nullptr);
    g_output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
        350, 580, 700, 112, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_OUTPUT)), g_instance, nullptr);
    g_status = CreateWindowExW(0, L"STATIC", L"Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_SUNKEN,
        0, 0, 1000, 22, window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_STATUS)), g_instance, nullptr);

    for (HWND control : {g_processInfo, g_editorLabel, g_output, g_status})
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_uiFont), TRUE);

    g_panelBrush = CreateSolidBrush(kPanelBg);
    NewTab(L"Starter.client.lua",
        L"-- OnyxiaEXE | Roblox Studio Luau workspace\r\n"
        L"-- Write scripts for experiences you create or edit in Roblox Studio.\r\n\r\n"
        L"local Players = game:GetService(\"Players\")\r\n\r\n"
        L"print(\"Hello from Roblox Studio!\")\r\n\r\n"
        L"-- Example for a LocalScript:\r\n"
        L"-- local player = Players.LocalPlayer\r\n"
        L"-- print(player and player.Name)\r\n");
    Log(L"OnyxiaEXE native C++ workbench started.");
    Log(L"Process discovery is read-only. Export/copy scripts into Roblox Studio to run them there.");
    ScanProcesses();
    SetTimer(window, 1, 4000, nullptr);
    LayoutChildren(window);
}

void PaintHeader(HWND window) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window, &ps);
    RECT client{};
    GetClientRect(window, &client);
    const int headerHeight = 70;
    const COLORREF topColor = RGB(22, 58, 94);
    const COLORREF bottomColor = RGB(75, 132, 181);
    for (int y = 0; y < headerHeight; ++y) {
        const double ratio = static_cast<double>(y) / headerHeight;
        const int red = static_cast<int>(GetRValue(topColor) * (1 - ratio) + GetRValue(bottomColor) * ratio);
        const int green = static_cast<int>(GetGValue(topColor) * (1 - ratio) + GetGValue(bottomColor) * ratio);
        const int blue = static_cast<int>(GetBValue(topColor) * (1 - ratio) + GetBValue(bottomColor) * ratio);
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(red, green, blue));
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, 0, y, nullptr);
        LineTo(dc, client.right, y);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    HGDIOBJ oldFont = SelectObject(dc, g_titleFont);
    TextOutW(dc, 19, 8, L"Onyxia", 6);
    SelectObject(dc, g_uiFont);
    SetTextColor(dc, RGB(220, 238, 255));
    TextOutW(dc, 22, 43, L"ROBLOX STUDIO WORKBENCH", 23);
    SelectObject(dc, oldFont);
    EndPaint(window, &ps);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateControls(window);
        return 0;
    case WM_SIZE:
        if (g_toolbar) LayoutChildren(window);
        return 0;
    case WM_PAINT:
        PaintHeader(window);
        return 0;
    case WM_ERASEBKGND: {
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(reinterpret_cast<HDC>(wParam), &rect,
            g_panelBrush ? g_panelBrush : GetSysColorBrush(COLOR_BTNFACE));
        return 1;
    }
    case WM_TIMER:
        if (wParam == 1) ScanProcesses();
        return 0;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == g_tabs && header->code == TCN_SELCHANGE) {
            g_activeTab = TabCtrl_GetCurSel(g_tabs);
            ShowActiveEditor();
            return 0;
        }
        if (header->hwndFrom == g_processList && header->code == LVN_ITEMCHANGED) {
            const auto* change = reinterpret_cast<NMLISTVIEW*>(lParam);
            if ((change->uNewState & LVIS_SELECTED) && change->iItem >= 0 &&
                change->iItem < static_cast<int>(g_processes.size()))
                SetProcessInfo(&g_processes[static_cast<size_t>(change->iItem)]);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        if (id == ID_NEW) NewTab();
        else if (id == ID_OPEN) OpenScript();
        else if (id == ID_SAVE && g_activeTab >= 0) SaveTab(static_cast<size_t>(g_activeTab), false);
        else if (id == ID_SAVE_AS && g_activeTab >= 0) SaveTab(static_cast<size_t>(g_activeTab), true);
        else if ((id == ID_EXPORT) && g_activeTab >= 0) SaveTab(static_cast<size_t>(g_activeTab), true);
        else if (id == ID_COPY) CopyCurrentScript();
        else if (id == ID_SCAN) ScanProcesses();
        else if (id == ID_STUDIO) OpenStudio();
        else if (id == ID_ANALYZE) RunLuauTool(true);
        else if (id == ID_RUN_LOCAL) RunLuauTool(false);
        else if (id == ID_EDITOR && HIWORD(wParam) == EN_CHANGE && !g_formatting) {
            if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_scripts.size())) {
                auto& tab = g_scripts[static_cast<size_t>(g_activeTab)];
                tab.dirty = true;
                UpdateTabTitle(static_cast<size_t>(g_activeTab));
                ApplySyntax(tab.editor);
            }
        }
        return 0;
    }
    case WM_DESTROY:
        KillTimer(window, 1);
        for (auto& tab : g_scripts) if (tab.editor) DestroyWindow(tab.editor);
        if (g_uiFont) DeleteObject(g_uiFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_editorFont) DeleteObject(g_editorFont);
        if (g_panelBrush) DeleteObject(g_panelBrush);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    g_instance = instance;
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return 1;

    g_main = CreateWindowExW(0, kClassName, kTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1160, 770,
        nullptr, nullptr, instance, nullptr);
    if (!g_main) return 1;
    ShowWindow(g_main, showCommand);
    UpdateWindow(g_main);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
