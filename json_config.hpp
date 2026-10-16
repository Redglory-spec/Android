#pragma once
// ============================================================================
// json_config.hpp
// Parser & Serializer JSON ringan (Zero-Dependency) untuk:
// 1. config_toko.json (Database Cabang Toko & Link Google Form IN/OUT)
// 2. user_prefs.json (Preferensi Terakhir Pengguna)
// 3. Komunikasi JSON W3C WebDriver (ChromeDriver REST API)
// ============================================================================

#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace JsonConfig {

struct StoreLinks {
    std::string link_in;
    std::string link_out;
};

using StoreDatabase = std::map<std::string, StoreLinks>;

struct UserPrefs {
    std::string toko;
    std::string jenis;
    std::string nama;
    std::string posisi;
    bool headless = true;
    std::string mode_sumber = "file";
    int jumlah_worker = 1;
    int max_retry = 3;
    bool dark_mode = false;
};

inline StoreDatabase GetDefaultDatabase() {
    StoreDatabase db;
    db["XBTK"] = {
        "https://docs.google.com/forms/d/e/1FAIpQLSetvyJul0k_KWbrm5BHvKqsquJrZtdwqoKwB7WJTIt2zGqH3w/viewform",
        "https://docs.google.com/forms/d/e/1FAIpQLSdmQ-ltvneM9YSKpcS6lgK-a_xPjNEJsNwGSR9WpMseYeLYow/viewform"
    };
    db["XPHD"] = {
        "https://docs.google.com/forms/d/e/1FAIpQLSeR0A3LBGOA2Q50Vh621OqT6xFDm-zyabnQWzjVx_XhU0Sq5Q/viewform",
        "https://docs.google.com/forms/d/e/1FAIpQLSeTe9lLsPjyRk20M2hVYiPFlnTSQqgE780-7_iGksWWE9SVXA/viewform"
    };
    db["XRTO"] = {
        "https://docs.google.com/forms/d/e/1FAIpQLSe7WWrZvRv8QT12wqLnjFUnoeyhEnwfJVAIPBAOiJpu-uxNQA/viewform",
        "https://docs.google.com/forms/d/e/1FAIpQLSewduPWNSS-jYYqV_z_5FbbWHP-T18QIICAncpfTD6CqT9EvA/viewform"
    };
    return db;
}

inline std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

namespace Detail {

inline void SkipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
}

inline bool ParseString(const std::string& s, size_t& i, std::string& out) {
    SkipWs(s, i);
    if (i >= s.size() || s[i] != '\"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        char c = s[i++];
        if (c == '\"') return true;
        if (c == '\\' && i < s.size()) {
            char esc = s[i++];
            switch (esc) {
                case '\"': out += '\"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                    if (i + 4 <= s.size()) {
                        int code = std::stoi(s.substr(i, 4), nullptr, 16);
                        if (code < 0x80) out += static_cast<char>(code);
                        i += 4;
                    }
                    break;
                default: out += esc; break;
            }
        } else {
            out += c;
        }
    }
    return false;
}

} // namespace Detail

// Ekstrak nilai string berdasarkan key JSON: misal "sessionId": "abc"
inline std::string ExtractJsonStringValue(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = 0;
    while ((pos = json.find(pattern, pos)) != std::string::npos) {
        size_t i = pos + pattern.size();
        Detail::SkipWs(json, i);
        if (i < json.size() && json[i] == ':') {
            ++i;
            std::string val;
            if (Detail::ParseString(json, i, val)) {
                return val;
            }
        }
        pos += pattern.size();
    }
    return "";
}

inline bool ExtractJsonBoolValue(const std::string& json, const std::string& key, bool default_val) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;
    size_t i = pos + pattern.size();
    Detail::SkipWs(json, i);
    if (i >= json.size() || json[i] != ':') return default_val;
    ++i;
    Detail::SkipWs(json, i);
    if (json.compare(i, 4, "true") == 0) return true;
    if (json.compare(i, 5, "false") == 0) return false;
    return default_val;
}

inline bool SaveStoreDatabase(const std::string& filepath, const StoreDatabase& db, std::string& err) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) {
        err = "Tidak dapat menulis ke file " + filepath;
        return false;
    }
    ofs << "{\n";
    size_t idx = 0;
    for (const auto& kv : db) {
        ofs << "    \"" << EscapeJsonString(kv.first) << "\": {\n";
        ofs << "        \"IN\": \"" << EscapeJsonString(kv.second.link_in) << "\",\n";
        ofs << "        \"OUT\": \"" << EscapeJsonString(kv.second.link_out) << "\"\n";
        ofs << "    }" << (++idx < db.size() ? "," : "") << "\n";
    }
    ofs << "}\n";
    return true;
}

inline StoreDatabase LoadStoreDatabase(const std::string& filepath, bool& used_default, std::string& warn_msg) {
    used_default = false;
    warn_msg.clear();

    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        StoreDatabase def = GetDefaultDatabase();
        std::string ignore_err;
        SaveStoreDatabase(filepath, def, ignore_err);
        return def;
    }

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    StoreDatabase db;

    size_t i = 0;
    Detail::SkipWs(content, i);
    if (i >= content.size() || content[i] != '{') {
        used_default = true;
        warn_msg = "Format JSON tidak valid.";
        return GetDefaultDatabase();
    }
    ++i;

    while (i < content.size()) {
        Detail::SkipWs(content, i);
        if (i < content.size() && (content[i] == '}' || content[i] == ',')) {
            if (content[i] == '}') break;
            ++i;
            continue;
        }
        std::string store_code;
        if (!Detail::ParseString(content, i, store_code)) break;
        Detail::SkipWs(content, i);
        if (i >= content.size() || content[i] != ':') break;
        ++i;
        Detail::SkipWs(content, i);
        if (i >= content.size() || content[i] != '{') break;

        size_t brace_end = content.find('}', i + 1);
        if (brace_end == std::string::npos) break;

        std::string inner_obj = content.substr(i, brace_end - i + 1);
        StoreLinks links;
        links.link_in = ExtractJsonStringValue(inner_obj, "IN");
        links.link_out = ExtractJsonStringValue(inner_obj, "OUT");
        if (!store_code.empty()) {
            db[store_code] = links;
        }
        i = brace_end + 1;
    }

    if (db.empty()) {
        used_default = true;
        warn_msg = "Database toko kosong atau rusak, memakai default.";
        return GetDefaultDatabase();
    }
    return db;
}

inline int ExtractJsonIntValue(const std::string& json, const std::string& key, int default_val) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return default_val;
    size_t i = pos + pattern.size();
    Detail::SkipWs(json, i);
    if (i >= json.size() || json[i] != ':') return default_val;
    ++i;
    Detail::SkipWs(json, i);
    size_t start = i;
    while (i < json.size() && (std::isdigit(static_cast<unsigned char>(json[i])) || json[i] == '-')) {
        ++i;
    }
    if (i > start) {
        try {
            return std::stoi(json.substr(start, i - start));
        } catch (...) {}
    }
    return default_val;
}

inline UserPrefs LoadUserPrefs(const std::string& filepath) {
    UserPrefs prefs;
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) return prefs;

    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    prefs.toko = ExtractJsonStringValue(content, "toko");
    prefs.jenis = ExtractJsonStringValue(content, "jenis");
    prefs.nama = ExtractJsonStringValue(content, "nama");
    prefs.posisi = ExtractJsonStringValue(content, "posisi");
    prefs.headless = ExtractJsonBoolValue(content, "headless", true);
    std::string mode = ExtractJsonStringValue(content, "mode_sumber");
    if (mode == "file" || mode == "paste") {
        prefs.mode_sumber = mode;
    }
    prefs.jumlah_worker = std::clamp(ExtractJsonIntValue(content, "jumlah_worker", 1), 1, 4);
    prefs.max_retry = std::clamp(ExtractJsonIntValue(content, "max_retry", 3), 1, 10);
    prefs.dark_mode = ExtractJsonBoolValue(content, "dark_mode", false);
    return prefs;
}

inline void SaveUserPrefs(const std::string& filepath, const UserPrefs& prefs) {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return;
    ofs << "{\n"
        << "    \"toko\": \"" << EscapeJsonString(prefs.toko) << "\",\n"
        << "    \"jenis\": \"" << EscapeJsonString(prefs.jenis) << "\",\n"
        << "    \"nama\": \"" << EscapeJsonString(prefs.nama) << "\",\n"
        << "    \"posisi\": \"" << EscapeJsonString(prefs.posisi) << "\",\n"
        << "    \"headless\": " << (prefs.headless ? "true" : "false") << ",\n"
        << "    \"mode_sumber\": \"" << EscapeJsonString(prefs.mode_sumber) << "\",\n"
        << "    \"jumlah_worker\": " << prefs.jumlah_worker << ",\n"
        << "    \"max_retry\": " << prefs.max_retry << ",\n"
        << "    \"dark_mode\": " << (prefs.dark_mode ? "true" : "false") << "\n"
        << "}\n";
}

} // namespace JsonConfig
