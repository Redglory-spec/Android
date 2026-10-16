#pragma once
// ============================================================================
// webdriver_client.hpp
// Klien W3C WebDriver Native C++ (WinHTTP) untuk mengontrol ChromeDriver
// tanpa dependensi library eksternal.
// Mendukung pencarian otomatis chromedriver.exe (lokal, PATH, cache Selenium,
// hingga auto-download sesuai versi Chrome yang terinstal di Windows).
// ============================================================================

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <shlwapi.h>

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <functional>
#include <atomic>
#include <mutex>
#include "json_config.hpp"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace WebDriver {

inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &res[0], sz);
    return res;
}

inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &res[0], sz, nullptr, nullptr);
    return res;
}

// Rekursif mencari chromedriver.exe di dalam folder (misal ~/.cache/selenium/chromedriver)
inline std::wstring FindFileRecursive(const std::wstring& dir, const std::wstring& target_name, int max_depth = 5) {
    if (max_depth < 0) return L"";
    WIN32_FIND_DATAW fd;
    std::wstring search_mask = dir + L"\\*";
    HANDLE hFind = FindFirstFileW(search_mask.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";

    std::wstring found_path;
    std::vector<std::wstring> subdirs;

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            subdirs.push_back(dir + L"\\" + name);
        } else if (_wcsicmp(name.c_str(), target_name.c_str()) == 0) {
            found_path = dir + L"\\" + name;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (!found_path.empty()) return found_path;

    // Prioritaskan subdirektori terbaru (urut terbalik agar versi tertinggi dipilih dulu)
    for (auto it = subdirs.rbegin(); it != subdirs.rend(); ++it) {
        found_path = FindFileRecursive(*it, target_name, max_depth - 1);
        if (!found_path.empty()) return found_path;
    }
    return L"";
}

// Deteksi versi Google Chrome yang terinstal di Windows (misal "131.0.6778.86")
inline std::string DetectInstalledChromeVersion() {
    HKEY hKey;
    wchar_t buf[128] = {0};
    DWORD buf_sz = sizeof(buf);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Google\\Chrome\\BLBeacon", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"version", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &buf_sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return WideToUtf8(buf);
        }
        RegCloseKey(hKey);
    }
    buf_sz = sizeof(buf);
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Google Chrome", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"DisplayVersion", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &buf_sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return WideToUtf8(buf);
        }
        RegCloseKey(hKey);
    }
    return "";
}

// Unduh konten URL via HTTPS menggunakan WinHTTP
inline bool HttpGetUrl(const std::wstring& host, const std::wstring& path, std::string& out_body) {
    out_body.clear();
    HINTERNET hSession = WinHttpOpen(L"BotKeepStock/5.9", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool ok = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD status_code = 0;
        DWORD sz = sizeof(status_code);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status_code == 200) {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
                std::vector<char> chunk(avail);
                DWORD read_bytes = 0;
                if (WinHttpReadData(hRequest, chunk.data(), avail, &read_bytes)) {
                    out_body.append(chunk.data(), read_bytes);
                }
            }
            ok = true;
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return ok;
}

// Cari chromedriver.exe atau unduh otomatis jika belum ada (Thread-Safe untuk Multi-Worker)
inline std::wstring LocateOrDownloadChromeDriver(std::function<void(const std::string&)> log_fn) {
    static std::mutex s_driverMutex;
    std::lock_guard<std::mutex> lock(s_driverMutex);

    wchar_t exe_dir[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    PathRemoveFileSpecW(exe_dir);

    // 1. Cek di folder yang sama dengan aplikasi (.exe)
    std::wstring local_cd = std::wstring(exe_dir) + L"\\chromedriver.exe";
    if (PathFileExistsW(local_cd.c_str())) {
        return local_cd;
    }

    // 2. Cek di Working Directory saat ini
    if (PathFileExistsW(L"chromedriver.exe")) {
        wchar_t full_buf[MAX_PATH] = {0};
        GetFullPathNameW(L"chromedriver.exe", MAX_PATH, full_buf, nullptr);
        return full_buf;
    }

    // 3. Cek di PATH environment variable
    wchar_t path_buf[MAX_PATH] = {0};
    if (SearchPathW(nullptr, L"chromedriver.exe", nullptr, MAX_PATH, path_buf, nullptr) > 0) {
        return path_buf;
    }

    // 4. Cek di cache Selenium Manager (%USERPROFILE%\.cache\selenium\chromedriver)
    wchar_t user_profile[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"USERPROFILE", user_profile, MAX_PATH) > 0) {
        std::wstring selenium_cache = std::wstring(user_profile) + L"\\.cache\\selenium\\chromedriver";
        if (PathFileExistsW(selenium_cache.c_str())) {
            std::wstring cached = FindFileRecursive(selenium_cache, L"chromedriver.exe");
            if (!cached.empty()) {
                return cached;
            }
        }
    }

    // 5. Auto-Download dari repositori resmi Chrome for Testing sesuai versi Chrome pengguna
    std::string chrome_ver = DetectInstalledChromeVersion();
    if (!chrome_ver.empty()) {
        size_t dot = chrome_ver.find('.');
        std::string major_ver = (dot != std::string::npos) ? chrome_ver.substr(0, dot) : chrome_ver;
        if (log_fn) {
            log_fn("chromedriver.exe belum ditemukan. Mendeteksi Chrome v" + chrome_ver + ", mengunduh otomatis...");
        }

        std::string resolved_ver;
        std::wstring api_path = L"/chrome-for-testing/LATEST_RELEASE_" + Utf8ToWide(major_ver);
        if (HttpGetUrl(L"googlechromelabs.github.io", api_path, resolved_ver)) {
            // Trim whitespace
            while (!resolved_ver.empty() && (resolved_ver.back() == '\r' || resolved_ver.back() == '\n' || resolved_ver.back() == ' ')) {
                resolved_ver.pop_back();
            }
            std::wstring dl_path = L"/chrome-for-testing-public/" + Utf8ToWide(resolved_ver) + L"/win64/chromedriver-win64.zip";
            std::string zip_bytes;
            if (HttpGetUrl(L"storage.googleapis.com", dl_path, zip_bytes) && zip_bytes.size() > 1000) {
                std::wstring zip_file = std::wstring(exe_dir) + L"\\chromedriver_temp.zip";
                {
                    std::ofstream ofs(WideToUtf8(zip_file), std::ios::binary);
                    ofs.write(zip_bytes.data(), static_cast<std::streamsize>(zip_bytes.size()));
                }
                // Ekstrak menggunakan tar.exe bawaan Windows 10/11
                std::wstring cmd = L"tar.exe -xf \"" + zip_file + L"\" --strip-components=1 -C \"" + exe_dir + L"\" chromedriver-win64/chromedriver.exe";
                STARTUPINFOW si = {sizeof(si)};
                si.dwFlags = STARTF_USESHOWWINDOW;
                si.wShowWindow = SW_HIDE;
                PROCESS_INFORMATION pi = {0};
                if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, exe_dir, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 15000);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                }
                DeleteFileW(zip_file.c_str());
                if (PathFileExistsW(local_cd.c_str())) {
                    if (log_fn) log_fn("chromedriver.exe v" + resolved_ver + " berhasil diunduh otomatis!");
                    return local_cd;
                }
            }
        }
    }

    return L"";
}

class ChromeSession {
public:
    ChromeSession() = default;
    ~ChromeSession() {
        Quit();
    }

    void Start(bool headless, std::function<void(const std::string&)> log_fn = nullptr) {
        std::wstring driver_path = LocateOrDownloadChromeDriver(log_fn);
        if (driver_path.empty()) {
            throw std::runtime_error(
                "File chromedriver.exe tidak ditemukan! Pastikan Google Chrome terinstal atau letakkan chromedriver.exe satu folder dengan aplikasi ini.");
        }

        // Gunakan port unik per sesi agar Multi-Browser Paralel tidak bentrok
        static std::atomic<int> s_portCounter{0};
        int offset = s_portCounter.fetch_add(1) % 500;
        port_ = 9515 + ((GetCurrentProcessId() % 200) * 10) + offset;

        // Buat Job Object agar jika aplikasi ditutup paksa, chromedriver.exe ikut tertutup otomatis
        hJob_ = CreateJobObjectW(nullptr, nullptr);
        if (hJob_) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
            jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(hJob_, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        }

        std::wstring cmd = L"\"" + driver_path + L"\" --port=" + std::to_wstring(port_);
        STARTUPINFOW si = {sizeof(si)};
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &piDriver_)) {
            throw std::runtime_error("Gagal menjalankan proses chromedriver.exe.");
        }

        if (hJob_) {
            AssignProcessToJobObject(hJob_, piDriver_.hProcess);
        }
        ResumeThread(piDriver_.hThread);

        // Tunggu ChromeDriver siap menerima koneksi HTTP
        hHttpSession_ = WinHttpOpen(L"BotKeepStock-WebDriver/5.9",
                                    WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hHttpSession_) {
            throw std::runtime_error("Gagal menginisialisasi WinHTTP.");
        }
        WinHttpSetTimeouts(hHttpSession_, 5000, 5000, 30000, 30000);

        hHttpConnect_ = WinHttpConnect(hHttpSession_, L"127.0.0.1", static_cast<INTERNET_PORT>(port_), 0);
        if (!hHttpConnect_) {
            throw std::runtime_error("Gagal menghubungkan ke port lokal ChromeDriver.");
        }

        bool ready = false;
        for (int i = 0; i < 40; ++i) {
            std::string resp;
            int status = SendHttpRequest(L"GET", L"/status", "", resp);
            if (status == 200 && resp.find("\"ready\":true") != std::string::npos) {
                ready = true;
                break;
            }
            Sleep(200);
        }
        if (!ready) {
            throw std::runtime_error("Waktu habis menunggu kesiapan chromedriver.exe.");
        }

        // Buat sesi Chrome baru (sama persis dengan opsi_chrome di Python)
        std::ostringstream payload;
        payload << "{"
                << "\"capabilities\":{"
                << "\"alwaysMatch\":{"
                << "\"browserName\":\"chrome\","
                << "\"goog:chromeOptions\":{"
                << "\"args\":[";
        if (headless) {
            payload << "\"--headless=new\",";
        }
        payload << "\"--disable-gpu\",\"--window-size=1920,1080\"],"
                << "\"excludeSwitches\":[\"enable-logging\"]"
                << "}"
                << "}}}";

        std::string session_resp;
        int code = SendHttpRequest(L"POST", L"/session", payload.str(), session_resp);
        sessionId_ = JsonConfig::ExtractJsonStringValue(session_resp, "sessionId");
        if (code != 200 || sessionId_.empty()) {
            std::string err_msg = JsonConfig::ExtractJsonStringValue(session_resp, "message");
            throw std::runtime_error("Gagal membuka browser Chrome: " + (err_msg.empty() ? session_resp : err_msg));
        }
    }

    void Quit() {
        if (!sessionId_.empty() && hHttpConnect_) {
            std::string dummy;
            std::wstring path = L"/session/" + Utf8ToWide(sessionId_);
            SendHttpRequest(L"DELETE", path, "", dummy);
            sessionId_.clear();
        }
        if (hHttpConnect_) {
            WinHttpCloseHandle(hHttpConnect_);
            hHttpConnect_ = nullptr;
        }
        if (hHttpSession_) {
            WinHttpCloseHandle(hHttpSession_);
            hHttpSession_ = nullptr;
        }
        if (piDriver_.hProcess) {
            TerminateProcess(piDriver_.hProcess, 0);
            CloseHandle(piDriver_.hProcess);
            CloseHandle(piDriver_.hThread);
            piDriver_ = {0};
        }
        if (hJob_) {
            CloseHandle(hJob_);
            hJob_ = nullptr;
        }
    }

    void NavigateTo(const std::string& url) {
        std::string body = "{\"url\":\"" + JsonConfig::EscapeJsonString(url) + "\"}";
        std::string resp;
        int code = SendSessionRequest(L"POST", L"/url", body, resp);
        if (code != 200) {
            throw std::runtime_error("Gagal membuka URL form: " + ExtractDriverError(resp));
        }
    }

    // Setara dengan driver.find_element(By.XPATH, xpath)
    std::string FindElementByXPath(const std::string& xpath) {
        std::string body = "{\"using\":\"xpath\",\"value\":\"" + JsonConfig::EscapeJsonString(xpath) + "\"}";
        std::string resp;
        int code = SendSessionRequest(L"POST", L"/element", body, resp);
        if (code != 200) {
            throw std::runtime_error("Elemen tidak ditemukan: " + xpath);
        }
        std::string elem_id = ExtractElementId(resp);
        if (elem_id.empty()) {
            throw std::runtime_error("ID elemen kosong untuk XPath: " + xpath);
        }
        return elem_id;
    }

    // Setara dengan WebDriverWait(driver, timeout_sec).until(EC.element_to_be_clickable((By.XPATH, xpath)))
    std::string WaitElementClickable(const std::string& xpath, int timeout_ms, const volatile bool* stop_flag = nullptr) {
        DWORD start_tick = GetTickCount();
        std::string body = "{\"using\":\"xpath\",\"value\":\"" + JsonConfig::EscapeJsonString(xpath) + "\"}";

        while (true) {
            if (stop_flag && *stop_flag) {
                throw std::runtime_error("Proses dihentikan pengguna.");
            }

            std::string resp;
            int code = SendSessionRequest(L"POST", L"/element", body, resp);
            if (code == 200) {
                std::string elem_id = ExtractElementId(resp);
                if (!elem_id.empty() && IsElementClickable(elem_id)) {
                    return elem_id;
                }
            }

            if (GetTickCount() - start_tick >= static_cast<DWORD>(timeout_ms)) {
                throw std::runtime_error("Timeout menunggu elemen siap diklik: " + xpath);
            }
            Sleep(80);
        }
    }

    // Setara dengan WebDriverWait(driver, 15).until(EC.invisibility_of_element_located((By.XPATH, xpath)))
    void WaitElementInvisible(const std::string& xpath, int timeout_ms, const volatile bool* stop_flag = nullptr) {
        DWORD start_tick = GetTickCount();
        std::string body = "{\"using\":\"xpath\",\"value\":\"" + JsonConfig::EscapeJsonString(xpath) + "\"}";

        while (true) {
            if (stop_flag && *stop_flag) return;

            std::string resp;
            int code = SendSessionRequest(L"POST", L"/element", body, resp);
            if (code != 200) {
                // Elemen sudah tidak ada di DOM -> invisible!
                return;
            }
            std::string elem_id = ExtractElementId(resp);
            if (elem_id.empty() || !IsElementDisplayed(elem_id)) {
                return;
            }

            if (GetTickCount() - start_tick >= static_cast<DWORD>(timeout_ms)) {
                throw std::runtime_error("Timeout menunggu pengiriman form selesai.");
            }
            Sleep(80);
        }
    }

    void ClearElement(const std::string& elem_id) {
        std::wstring path = L"/element/" + Utf8ToWide(elem_id) + L"/clear";
        std::string resp;
        for (int attempt = 0; attempt < 3; ++attempt) {
            int code = SendSessionRequest(L"POST", path, "{}", resp);
            if (code == 200) return;
            Sleep(150);
        }
        throw std::runtime_error("Gagal mengosongkan kolom input: " + ExtractDriverError(resp));
    }

    void SendKeysToElement(const std::string& elem_id, const std::string& text) {
        std::wstring path = L"/element/" + Utf8ToWide(elem_id) + L"/value";
        std::string body = "{\"text\":\"" + JsonConfig::EscapeJsonString(text) + "\"}";
        std::string resp;
        for (int attempt = 0; attempt < 3; ++attempt) {
            int code = SendSessionRequest(L"POST", path, body, resp);
            if (code == 200) return;
            Sleep(150);
        }
        throw std::runtime_error("Gagal mengetik teks ke elemen: " + ExtractDriverError(resp));
    }

    void ClickElement(const std::string& elem_id) {
        std::wstring path = L"/element/" + Utf8ToWide(elem_id) + L"/click";
        std::string resp;
        for (int attempt = 0; attempt < 3; ++attempt) {
            int code = SendSessionRequest(L"POST", path, "{}", resp);
            if (code == 200) return;
            Sleep(150);
        }
        throw std::runtime_error("Gagal mengklik elemen: " + ExtractDriverError(resp));
    }

private:
    int port_ = 9515;
    HANDLE hJob_ = nullptr;
    PROCESS_INFORMATION piDriver_ = {0};
    HINTERNET hHttpSession_ = nullptr;
    HINTERNET hHttpConnect_ = nullptr;
    std::string sessionId_;

    static std::string ExtractElementId(const std::string& json) {
        // W3C WebDriver standard element key
        std::string id = JsonConfig::ExtractJsonStringValue(json, "element-6066-11e4-a52e-4f735466cecf");
        if (id.empty()) {
            id = JsonConfig::ExtractJsonStringValue(json, "ELEMENT");
        }
        return id;
    }

    static std::string ExtractDriverError(const std::string& json) {
        std::string msg = JsonConfig::ExtractJsonStringValue(json, "message");
        if (msg.empty()) msg = json;
        size_t nl = msg.find('\n');
        if (nl != std::string::npos) msg = msg.substr(0, nl);
        if (msg.size() > 70) msg = msg.substr(0, 70);
        return msg;
    }

    bool IsElementDisplayed(const std::string& elem_id) {
        // Gunakan endpoint /displayed atau execute/sync
        std::wstring path = L"/element/" + Utf8ToWide(elem_id) + L"/displayed";
        std::string resp;
        int code = SendSessionRequest(L"GET", path, "", resp);
        if (code == 200) {
            return JsonConfig::ExtractJsonBoolValue(resp, "value", false);
        }
        // Fallback via JS execute/sync jika endpoint /displayed tidak tersedia
        std::string js_body =
            "{\"script\":\"var e=arguments[0];if(!e)return false;var r=e.getBoundingClientRect();"
            "var s=window.getComputedStyle(e);return r.width>0&&r.height>0&&s.visibility!=='hidden'&&s.display!=='none';\","
            "\"args\":[{\"element-6066-11e4-a52e-4f735466cecf\":\"" + JsonConfig::EscapeJsonString(elem_id) + "\"}]}";
        code = SendSessionRequest(L"POST", L"/execute/sync", js_body, resp);
        return (code == 200 && JsonConfig::ExtractJsonBoolValue(resp, "value", false));
    }

    bool IsElementClickable(const std::string& elem_id) {
        if (!IsElementDisplayed(elem_id)) return false;
        std::wstring path = L"/element/" + Utf8ToWide(elem_id) + L"/enabled";
        std::string resp;
        int code = SendSessionRequest(L"GET", path, "", resp);
        return (code == 200 && JsonConfig::ExtractJsonBoolValue(resp, "value", false));
    }

    int SendSessionRequest(const std::wstring& method, const std::wstring& subpath,
                           const std::string& body, std::string& out_resp) {
        std::wstring full_path = L"/session/" + Utf8ToWide(sessionId_) + subpath;
        return SendHttpRequest(method, full_path, body, out_resp);
    }

    int SendHttpRequest(const std::wstring& method, const std::wstring& path,
                        const std::string& body, std::string& out_resp) {
        out_resp.clear();
        if (!hHttpConnect_) return 0;

        HINTERNET hReq = WinHttpOpenRequest(hHttpConnect_, method.c_str(), path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (!hReq) return 0;

        std::wstring headers = L"Content-Type: application/json; charset=utf-8\r\n";
        LPVOID body_ptr = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
        DWORD body_len = static_cast<DWORD>(body.size());

        int status_code = 0;
        if (WinHttpSendRequest(hReq, headers.c_str(), static_cast<DWORD>(headers.size()),
                               body_ptr, body_len, body_len, 0) &&
            WinHttpReceiveResponse(hReq, nullptr)) {

            DWORD code = 0;
            DWORD sz = sizeof(code);
            WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            status_code = static_cast<int>(code);

            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                std::vector<char> buf(avail);
                DWORD read_bytes = 0;
                if (WinHttpReadData(hReq, buf.data(), avail, &read_bytes)) {
                    out_resp.append(buf.data(), read_bytes);
                }
            }
        }

        WinHttpCloseHandle(hReq);
        return status_code;
    }
};

} // namespace WebDriver

#endif // _WIN32
