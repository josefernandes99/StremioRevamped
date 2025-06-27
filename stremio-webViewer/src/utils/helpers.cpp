#include "helpers.h"

#include <iostream>
#include <shellscalingapi.h>
#include <tlhelp32.h>
#include <VersionHelpers.h>
#include <winhttp.h>
#include <curl/curl.h>

#include "../core/globals.h"

std::string WStringToUtf8(const std::wstring &wstr)
{
    if (wstr.empty()) {
        return {};
    }
    int neededSize = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (neededSize <= 0) {
        return {};
    }
    std::string result(neededSize, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], neededSize, nullptr, nullptr);
    // remove trailing null
    while(!result.empty() && result.back()=='\0') {
        result.pop_back();
    }
    return result;
}

std::wstring Utf8ToWstring(const std::string& utf8Str)
{
    if (utf8Str.empty()) {
        return std::wstring();
    }
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), NULL, 0);
    if (size_needed == 0) {
        return std::wstring();
    }
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wstr[0], size_needed);
    return wstr;
}

bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES &&
            !(attributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool DirectoryExists(const std::wstring& dirPath)
{
    DWORD attributes = GetFileAttributesW(dirPath.c_str());
    return (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY));
}

bool IsDuplicateProcessRunning(const std::vector<std::wstring>& targetProcesses)
{
    DWORD currentPid = GetCurrentProcessId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32W);
    if (!Process32FirstW(hSnapshot, &processEntry)) {
        CloseHandle(hSnapshot);
        return false;
    }
    do {
        if (processEntry.th32ProcessID == currentPid) {
            continue;
        }
        std::wstring exeName(processEntry.szExeFile);
        for (const auto& target : targetProcesses) {
            if (_wcsicmp(exeName.c_str(), target.c_str()) == 0) {
                CloseHandle(hSnapshot);
                return true;
            }
        }
    } while (Process32NextW(hSnapshot, &processEntry));

    CloseHandle(hSnapshot);
    return false;
}

// For local files
std::string decodeURIComponent(const std::string& encoded) {
    std::string result;
    result.reserve(encoded.size());

    for (size_t i = 0; i < encoded.size(); ++i) {
        char c = encoded[i];
        if (c == '%' && i + 2 < encoded.size() &&
            std::isxdigit(static_cast<unsigned char>(encoded[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(encoded[i + 2]))) {
            // Convert the two hex digits to a character
            std::string hex = encoded.substr(i + 1, 2);
            char decodedChar = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            result.push_back(decodedChar);
            i += 2;
            } else {
                result.push_back(c);
            }
    }
    return result;
}

std::wstring GetExeDirectory()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr,buf,MAX_PATH);
    std::wstring path(buf);
    size_t pos=path.find_last_of(L"\\/");
    if(pos!=std::wstring::npos)
        path.erase(pos);
    return path;
}

bool isSubtitle(const std::wstring& filePath) {
    std::wstring lowerFilePath = filePath;
    std::transform(lowerFilePath.begin(), lowerFilePath.end(), lowerFilePath.begin(), towlower);
    return std::any_of(g_subtitleExtensions.begin(), g_subtitleExtensions.end(),
        [&](const std::wstring& ext) { return lowerFilePath.ends_with(ext); });
}

bool IsEndpointReachable(const std::wstring& url)
{
    std::string urlUtf8 = WStringToUtf8(url);
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        curl_easy_cleanup(curl);
        return false;
    }

    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    return (response_code >= 200 && response_code < 300);
}

std::wstring GetFirstReachableUrl() {
    for (const auto& url : g_webuiUrls) {
        if (IsEndpointReachable(url)) {
            return url;
        }
    }
    // Fallback to first URL or handle error
    return g_webuiUrls.empty() ? L"" : g_webuiUrls[0];
}

bool URLContainsAny(const std::wstring& url) {
    if (std::find(g_domainWhitelist.begin(), g_domainWhitelist.end(), g_webuiUrl) == g_domainWhitelist.end()) {
        g_domainWhitelist.push_back(g_webuiUrl);
    }
    return std::any_of(g_domainWhitelist.begin(), g_domainWhitelist.end(), [&](const std::wstring& sub) {
        return url.find(sub) != std::wstring::npos;
    });
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t totalSize = size * nmemb;
    std::string* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

bool FetchAndParseWhitelist()
{
    std::string urlUtf8 = WStringToUtf8(g_extensionsDetailsUrl);
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, urlUtf8.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
        return false;

    try {
        json j = json::parse(response);
        if (j.contains("domains") && j["domains"].is_array()) {
            g_domainWhitelist.clear();
            for (const auto& domain : j["domains"]) {
                if (domain.is_string()) {
                    g_domainWhitelist.push_back(Utf8ToWstring(domain.get<std::string>()));
                }
            }
            return true;
        }
    } catch (...) {
        std::wcout << L"[HELPER]: Failed json parsing of domain whitelist for extensions..." << std::endl;
    }

    return false;
}

void ScaleWithDPI() {
    if (!g_hWnd) return;
    UINT dpi = 96;
    HMONITOR hMonitor = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTOPRIMARY);
    HRESULT hr = GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpi, nullptr);
    if (FAILED(hr)) {
        // Fall back to using g_hWnd's DPI if GetDpiForMonitor is unavailable
        if (IsWindowsVersionOrGreater(10, 0, 1607))
        {
            dpi = GetDpiForWindow(g_hWnd);
        }
        else
        {
            HDC hdc = GetDC(g_hWnd);
            dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(g_hWnd, hdc);
        }
    }
    // Lambda to scale a value based on current DPI
    auto ScaleValue = [dpi](int value) -> int {
        return MulDiv(value, dpi, 96);
    };
    g_tray_itemH = ScaleValue(g_tray_itemH);
    g_tray_sepH  = ScaleValue(g_tray_sepH);
    g_tray_w     = ScaleValue(g_tray_w);
    g_font_height = ScaleValue(g_font_height);
}