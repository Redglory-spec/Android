#pragma once
// ============================================================================
// xlsx_reader.hpp
// Pembaca file Excel (.xlsx) mandiri (Zero-Dependency) menggunakan C++17 murni.
// Mendukung dekompresi ZIP (Stored & Deflate RFC 1951) + parser XML worksheet.
// ============================================================================

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace XlsxReader {

struct StockRow {
    std::string keep_stock;
    std::string sku;
    std::string sku_qty;
    std::string gondola;
};

// Persis seperti fungsi bersihkan_angka(val) di Python
inline std::string BersihkanAngka(const std::string& val) {
    size_t first = val.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = val.find_last_not_of(" \t\r\n");
    std::string txt = val.substr(first, last - first + 1);

    std::string lower = txt;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "nan" || lower == "none" || lower == "null") {
        return "";
    }

    if (txt.size() > 2 && txt.substr(txt.size() - 2) == ".0") {
        return txt.substr(0, txt.size() - 2);
    }

    // Jika angka desimal murni hasil representasi float Excel (misal 12345.000000)
    if (txt.find('.') != std::string::npos) {
        bool is_num = true;
        size_t start_idx = (txt[0] == '-' || txt[0] == '+') ? 1 : 0;
        int dot_count = 0;
        for (size_t i = start_idx; i < txt.size(); ++i) {
            if (txt[i] == '.') {
                dot_count++;
                if (dot_count > 1) { is_num = false; break; }
            } else if (txt[i] < '0' || txt[i] > '9') {
                is_num = false;
                break;
            }
        }
        if (is_num && dot_count == 1) {
            std::string trimmed = txt;
            while (!trimmed.empty() && trimmed.back() == '0') {
                trimmed.pop_back();
            }
            if (!trimmed.empty() && trimmed.back() == '.') {
                trimmed.pop_back();
            }
            return trimmed;
        }
    }

    return txt;
}

// ============================================================================
// RFC 1951 Deflate Decompressor (Self-Contained)
// ============================================================================
namespace Detail {

struct BitStream {
    const uint8_t* data;
    size_t size;
    size_t byte_pos = 0;
    uint32_t bit_buf = 0;
    int bit_cnt = 0;

    BitStream(const uint8_t* d, size_t s) : data(d), size(s) {}

    void EnsureBits(int n) {
        while (bit_cnt < n) {
            uint8_t b = (byte_pos < size) ? data[byte_pos++] : 0;
            bit_buf |= (static_cast<uint32_t>(b) << bit_cnt);
            bit_cnt += 8;
        }
    }

    uint32_t PeekBits(int n) {
        EnsureBits(n);
        return bit_buf & ((1u << n) - 1u);
    }

    void DropBits(int n) {
        bit_buf >>= n;
        bit_cnt -= n;
    }

    uint32_t ReadBits(int n) {
        if (n == 0) return 0;
        uint32_t v = PeekBits(n);
        DropBits(n);
        return v;
    }

    void AlignByte() {
        int drop = bit_cnt & 7;
        DropBits(drop);
    }
};

struct HuffmanTable {
    static constexpr int MAX_BITS = 15;
    int counts[MAX_BITS + 1] = {0};
    int symbols[288] = {0};

    void Build(const int* lengths, int num_syms) {
        std::memset(counts, 0, sizeof(counts));
        for (int i = 0; i < num_syms; ++i) {
            if (lengths[i] > 0 && lengths[i] <= MAX_BITS) {
                counts[lengths[i]]++;
            }
        }
        int offsets[MAX_BITS + 1];
        offsets[0] = 0;
        for (int bits = 1; bits <= MAX_BITS; ++bits) {
            offsets[bits] = offsets[bits - 1] + counts[bits - 1];
        }
        for (int i = 0; i < num_syms; ++i) {
            int len = lengths[i];
            if (len > 0 && len <= MAX_BITS) {
                symbols[offsets[len]++] = i;
            }
        }
    }

    int Decode(BitStream& bs) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= MAX_BITS; ++len) {
            code |= static_cast<int>(bs.ReadBits(1));
            int count = counts[len];
            if (code - count < first) {
                return symbols[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw std::runtime_error("Kode Huffman tidak valid dalam file XLSX.");
    }
};

inline bool InflateRaw(const uint8_t* src, size_t src_len, std::vector<uint8_t>& out) {
    static const int len_base[29] = {
        3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
        35,43,51,59,67,83,99,115,131,163,195,227,258
    };
    static const int len_extra[29] = {
        0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
        3,3,3,3,4,4,4,4,5,5,5,5,0
    };
    static const int dist_base[30] = {
        1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
        257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
    };
    static const int dist_extra[30] = {
        0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
        7,7,8,8,9,9,10,10,11,11,12,12,13,13
    };
    static const int clen_order[19] = {
        16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
    };

    try {
        BitStream bs(src, src_len);
        int bfinal = 0;
        do {
            bfinal = static_cast<int>(bs.ReadBits(1));
            int btype = static_cast<int>(bs.ReadBits(2));

            if (btype == 0) {
                bs.AlignByte();
                uint16_t len = static_cast<uint16_t>(bs.ReadBits(16));
                uint16_t nlen = static_cast<uint16_t>(bs.ReadBits(16));
                (void)nlen;
                for (uint16_t i = 0; i < len; ++i) {
                    out.push_back(static_cast<uint8_t>(bs.ReadBits(8)));
                }
            } else if (btype == 1 || btype == 2) {
                HuffmanTable litlen_tab, dist_tab;
                if (btype == 1) {
                    int lit_lengths[288];
                    for (int i = 0; i <= 143; ++i) lit_lengths[i] = 8;
                    for (int i = 144; i <= 255; ++i) lit_lengths[i] = 9;
                    for (int i = 256; i <= 279; ++i) lit_lengths[i] = 7;
                    for (int i = 280; i <= 287; ++i) lit_lengths[i] = 8;
                    litlen_tab.Build(lit_lengths, 288);

                    int dist_lengths[32];
                    for (int i = 0; i < 32; ++i) dist_lengths[i] = 5;
                    dist_tab.Build(dist_lengths, 32);
                } else {
                    int hlit = static_cast<int>(bs.ReadBits(5)) + 257;
                    int hdist = static_cast<int>(bs.ReadBits(5)) + 1;
                    int hclen = static_cast<int>(bs.ReadBits(4)) + 4;

                    int clen_lengths[19] = {0};
                    for (int i = 0; i < hclen; ++i) {
                        clen_lengths[clen_order[i]] = static_cast<int>(bs.ReadBits(3));
                    }
                    HuffmanTable clen_tab;
                    clen_tab.Build(clen_lengths, 19);

                    std::vector<int> all_lengths(hlit + hdist, 0);
                    int idx = 0;
                    while (idx < hlit + hdist) {
                        int sym = clen_tab.Decode(bs);
                        if (sym < 16) {
                            all_lengths[idx++] = sym;
                        } else if (sym == 16) {
                            int rep = static_cast<int>(bs.ReadBits(2)) + 3;
                            int prev = (idx > 0) ? all_lengths[idx - 1] : 0;
                            while (rep-- > 0 && idx < hlit + hdist) all_lengths[idx++] = prev;
                        } else if (sym == 17) {
                            int rep = static_cast<int>(bs.ReadBits(3)) + 3;
                            while (rep-- > 0 && idx < hlit + hdist) all_lengths[idx++] = 0;
                        } else if (sym == 18) {
                            int rep = static_cast<int>(bs.ReadBits(7)) + 11;
                            while (rep-- > 0 && idx < hlit + hdist) all_lengths[idx++] = 0;
                        }
                    }
                    litlen_tab.Build(all_lengths.data(), hlit);
                    dist_tab.Build(all_lengths.data() + hlit, hdist);
                }

                while (true) {
                    int sym = litlen_tab.Decode(bs);
                    if (sym < 256) {
                        out.push_back(static_cast<uint8_t>(sym));
                    } else if (sym == 256) {
                        break;
                    } else {
                        int len_idx = sym - 257;
                        if (len_idx < 0 || len_idx >= 29) return false;
                        int length = len_base[len_idx] + static_cast<int>(bs.ReadBits(len_extra[len_idx]));
                        int dist_sym = dist_tab.Decode(bs);
                        if (dist_sym < 0 || dist_sym >= 30) return false;
                        size_t dist = static_cast<size_t>(dist_base[dist_sym] + bs.ReadBits(dist_extra[dist_sym]));
                        if (dist > out.size() || dist == 0) return false;
                        for (int i = 0; i < length; ++i) {
                            out.push_back(out[out.size() - dist]);
                        }
                    }
                }
            } else {
                return false;
            }
        } while (!bfinal);
        return true;
    } catch (...) {
        return false;
    }
}

inline uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Membaca seluruh file di dalam arsip ZIP (melalui Central Directory di akhir file)
inline bool ExtractZipEntries(const std::vector<uint8_t>& zip_bytes,
                              std::map<std::string, std::string>& files_out) {
    if (zip_bytes.size() < 22) return false;

    // Cari End of Central Directory (EOCD) signature 0x06054b50 dari belakang
    size_t eocd_pos = std::string::npos;
    size_t min_pos = (zip_bytes.size() > 65557) ? (zip_bytes.size() - 65557) : 0;
    for (size_t i = zip_bytes.size() - 22; ; --i) {
        if (ReadU32(&zip_bytes[i]) == 0x06054b50) {
            eocd_pos = i;
            break;
        }
        if (i == min_pos) break;
    }
    if (eocd_pos == std::string::npos) return false;

    uint16_t total_entries = ReadU16(&zip_bytes[eocd_pos + 10]);
    uint32_t cd_offset = ReadU32(&zip_bytes[eocd_pos + 16]);

    size_t pos = cd_offset;
    for (uint16_t e = 0; e < total_entries && pos + 46 <= zip_bytes.size(); ++e) {
        if (ReadU32(&zip_bytes[pos]) != 0x02014b50) break;

        uint16_t method = ReadU16(&zip_bytes[pos + 10]);
        uint32_t comp_size = ReadU32(&zip_bytes[pos + 20]);
        uint32_t uncomp_size = ReadU32(&zip_bytes[pos + 24]);
        uint16_t name_len = ReadU16(&zip_bytes[pos + 28]);
        uint16_t extra_len = ReadU16(&zip_bytes[pos + 30]);
        uint16_t comment_len = ReadU16(&zip_bytes[pos + 32]);
        uint32_t local_hdr_off = ReadU32(&zip_bytes[pos + 42]);

        if (pos + 46 + name_len > zip_bytes.size()) break;
        std::string filename(reinterpret_cast<const char*>(&zip_bytes[pos + 46]), name_len);
        pos += 46 + name_len + extra_len + comment_len;

        // Kita hanya butuh sharedStrings.xml dan sheet1.xml
        if (filename != "xl/sharedStrings.xml" &&
            filename != "xl/worksheets/sheet1.xml") {
            continue;
        }

        if (local_hdr_off + 30 > zip_bytes.size()) continue;
        if (ReadU32(&zip_bytes[local_hdr_off]) != 0x04034b50) continue;

        uint16_t l_name_len = ReadU16(&zip_bytes[local_hdr_off + 26]);
        uint16_t l_extra_len = ReadU16(&zip_bytes[local_hdr_off + 28]);
        size_t data_off = local_hdr_off + 30 + l_name_len + l_extra_len;
        if (data_off + comp_size > zip_bytes.size()) continue;

        if (method == 0) {
            files_out[filename] = std::string(
                reinterpret_cast<const char*>(&zip_bytes[data_off]), comp_size);
        } else if (method == 8) {
            std::vector<uint8_t> decompressed;
            decompressed.reserve(uncomp_size > 0 ? uncomp_size : comp_size * 2);
            if (InflateRaw(&zip_bytes[data_off], comp_size, decompressed)) {
                files_out[filename] = std::string(
                    reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
            }
        }
    }
    return files_out.count("xl/worksheets/sheet1.xml") > 0;
}

inline std::string DecodeXmlEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            size_t semi = s.find(';', i + 1);
            if (semi != std::string::npos && semi - i <= 10) {
                std::string ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") { out += '&'; i = semi; continue; }
                if (ent == "lt") { out += '<'; i = semi; continue; }
                if (ent == "gt") { out += '>'; i = semi; continue; }
                if (ent == "quot") { out += '"'; i = semi; continue; }
                if (ent == "apos") { out += '\''; i = semi; continue; }
                if (!ent.empty() && ent[0] == '#') {
                    int code = 0;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                        code = std::stoi(ent.substr(2), nullptr, 16);
                    } else {
                        code = std::stoi(ent.substr(1));
                    }
                    if (code > 0 && code < 128) {
                        out += static_cast<char>(code);
                        i = semi;
                        continue;
                    }
                }
            }
        }
        out += s[i];
    }
    return out;
}

// Ambil atribut dari tag XML, misal r="A1" atau t="s"
inline std::string GetXmlAttr(const std::string& tag, const std::string& attr_name) {
    std::string pattern = attr_name + "=\"";
    size_t pos = tag.find(pattern);
    if (pos == std::string::npos) {
        // Coba dengan spasi di depannya agar tidak salah cocok (misal "r=" vs "spans=")
        return "";
    }
    // Pastikan karakter sebelum attr_name adalah spasi atau awal tag
    while (pos != std::string::npos) {
        if (pos == 0 || tag[pos - 1] == ' ' || tag[pos - 1] == '\t' || tag[pos - 1] == '\n') {
            size_t val_start = pos + pattern.size();
            size_t val_end = tag.find('"', val_start);
            if (val_end != std::string::npos) {
                return tag.substr(val_start, val_end - val_start);
            }
            break;
        }
        pos = tag.find(pattern, pos + 1);
    }
    return "";
}

// Konversi referensi sel Excel (misal "A1" -> kolom 0, "D15" -> kolom 3, "AA2" -> kolom 26)
inline int ColRefToIndex(const std::string& cell_ref) {
    int col = 0;
    bool has_letters = false;
    for (char c : cell_ref) {
        if (c >= 'A' && c <= 'Z') {
            col = col * 26 + (c - 'A' + 1);
            has_letters = true;
        } else if (c >= 'a' && c <= 'z') {
            col = col * 26 + (c - 'a' + 1);
            has_letters = true;
        } else {
            break;
        }
    }
    return has_letters ? (col - 1) : -1;
}

// Ekstrak seluruh isi <t>...</t> di dalam sebuah blok XML (misal <si> atau <is>)
inline std::string ExtractAllTextTags(const std::string& block) {
    std::string result;
    size_t pos = 0;
    while (pos < block.size()) {
        size_t t_open = block.find("<t", pos);
        if (t_open == std::string::npos) break;
        char next_ch = (t_open + 2 < block.size()) ? block[t_open + 2] : '\0';
        if (next_ch != '>' && next_ch != ' ') {
            pos = t_open + 2;
            continue;
        }
        size_t tag_end = block.find('>', t_open + 2);
        if (tag_end == std::string::npos) break;
        if (block[tag_end - 1] == '/') {
            // Self-closing <t/>
            pos = tag_end + 1;
            continue;
        }
        size_t t_close = block.find("</t>", tag_end + 1);
        if (t_close == std::string::npos) break;
        result += DecodeXmlEntities(block.substr(tag_end + 1, t_close - tag_end - 1));
        pos = t_close + 4;
    }
    return result;
}

inline std::vector<std::string> ParseSharedStrings(const std::string& xml) {
    std::vector<std::string> strings;
    size_t pos = 0;
    while (pos < xml.size()) {
        size_t si_open = xml.find("<si", pos);
        if (si_open == std::string::npos) break;
        char next_ch = (si_open + 3 < xml.size()) ? xml[si_open + 3] : '\0';
        if (next_ch != '>' && next_ch != ' ') {
            pos = si_open + 3;
            continue;
        }
        size_t si_tag_end = xml.find('>', si_open + 3);
        if (si_tag_end == std::string::npos) break;
        size_t si_close = xml.find("</si>", si_tag_end + 1);
        if (si_close == std::string::npos) break;

        std::string si_block = xml.substr(si_tag_end + 1, si_close - si_tag_end - 1);
        strings.push_back(ExtractAllTextTags(si_block));
        pos = si_close + 5;
    }
    return strings;
}

inline std::vector<std::vector<std::string>> ParseSheetRows(
    const std::string& xml,
    const std::vector<std::string>& shared_strings) {

    std::vector<std::vector<std::string>> table;
    size_t pos = 0;
    while (pos < xml.size()) {
        size_t row_open = xml.find("<row", pos);
        if (row_open == std::string::npos) break;
        char next_ch = (row_open + 4 < xml.size()) ? xml[row_open + 4] : '\0';
        if (next_ch != '>' && next_ch != ' ') {
            pos = row_open + 4;
            continue;
        }
        size_t row_tag_end = xml.find('>', row_open + 4);
        if (row_tag_end == std::string::npos) break;
        if (xml[row_tag_end - 1] == '/') {
            pos = row_tag_end + 1;
            continue;
        }
        size_t row_close = xml.find("</row>", row_tag_end + 1);
        if (row_close == std::string::npos) break;

        std::string row_xml = xml.substr(row_tag_end + 1, row_close - row_tag_end - 1);
        std::vector<std::string> row_cells;

        size_t c_pos = 0;
        int next_col_idx = 0;
        while (c_pos < row_xml.size()) {
            size_t c_open = row_xml.find("<c", c_pos);
            if (c_open == std::string::npos) break;
            char c_next = (c_open + 2 < row_xml.size()) ? row_xml[c_open + 2] : '\0';
            if (c_next != '>' && c_next != ' ' && c_next != '/') {
                c_pos = c_open + 2;
                continue;
            }
            size_t c_tag_end = row_xml.find('>', c_open + 2);
            if (c_tag_end == std::string::npos) break;

            std::string c_tag = row_xml.substr(c_open, c_tag_end - c_open + 1);
            std::string cell_ref = GetXmlAttr(c_tag, "r");
            std::string cell_type = GetXmlAttr(c_tag, "t");

            int col_idx = ColRefToIndex(cell_ref);
            if (col_idx < 0) col_idx = next_col_idx;
            next_col_idx = col_idx + 1;

            if (static_cast<int>(row_cells.size()) <= col_idx) {
                row_cells.resize(col_idx + 1, "");
            }

            if (c_tag.size() >= 2 && c_tag[c_tag.size() - 2] == '/') {
                // Self-closing <c .../> (sel kosong)
                c_pos = c_tag_end + 1;
                continue;
            }

            size_t c_close = row_xml.find("</c>", c_tag_end + 1);
            if (c_close == std::string::npos) break;

            std::string c_body = row_xml.substr(c_tag_end + 1, c_close - c_tag_end - 1);
            std::string cell_val;

            if (cell_type == "inlineStr") {
                cell_val = ExtractAllTextTags(c_body);
            } else {
                size_t v_open = c_body.find("<v>");
                size_t v_close = c_body.find("</v>");
                if (v_open != std::string::npos && v_close != std::string::npos && v_close > v_open + 3) {
                    std::string raw_v = DecodeXmlEntities(c_body.substr(v_open + 3, v_close - v_open - 3));
                    if (cell_type == "s") {
                        try {
                            int ss_idx = std::stoi(raw_v);
                            if (ss_idx >= 0 && ss_idx < static_cast<int>(shared_strings.size())) {
                                cell_val = shared_strings[ss_idx];
                            }
                        } catch (...) {}
                    } else {
                        cell_val = raw_v;
                    }
                }
            }

            row_cells[col_idx] = cell_val;
            c_pos = c_close + 4;
        }

        table.push_back(std::move(row_cells));
        pos = row_close + 6;
    }

    return table;
}

} // namespace Detail

// Membaca file .xlsx dan memvalidasi 4 kolom wajib:
// ['Alamat Keep Stock', 'SKU', 'SKU QTY', 'Alamat Gondola']
inline bool ReadExcelFile(const std::string& filepath,
                          std::vector<StockRow>& out_rows,
                          std::string& out_error) {
    out_rows.clear();
    out_error.clear();

    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        out_error = "Gagal membuka file Excel: " + filepath;
        return false;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                                std::istreambuf_iterator<char>());
    if (bytes.size() < 4) {
        out_error = "File Excel kosong atau rusak.";
        return false;
    }

    std::map<std::string, std::string> zip_files;
    if (!Detail::ExtractZipEntries(bytes, zip_files)) {
        out_error = "Gagal mengekstrak struktur .xlsx (Pastikan file berformat .xlsx standar).";
        return false;
    }

    std::vector<std::string> shared_strings;
    auto it_ss = zip_files.find("xl/sharedStrings.xml");
    if (it_ss != zip_files.end()) {
        shared_strings = Detail::ParseSharedStrings(it_ss->second);
    }

    auto raw_table = Detail::ParseSheetRows(zip_files["xl/worksheets/sheet1.xml"], shared_strings);
    if (raw_table.empty()) {
        return true; // 0 baris
    }

    // Baris pertama adalah Header
    const auto& header = raw_table[0];
    int idx_keep = -1, idx_sku = -1, idx_qty = -1, idx_gondola = -1;

    auto trim_str = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return std::string("");
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    };

    for (size_t c = 0; c < header.size(); ++c) {
        std::string h = trim_str(header[c]);
        if (h == "Alamat Keep Stock") idx_keep = static_cast<int>(c);
        else if (h == "SKU") idx_sku = static_cast<int>(c);
        else if (h == "SKU QTY") idx_qty = static_cast<int>(c);
        else if (h == "Alamat Gondola") idx_gondola = static_cast<int>(c);
    }

    const char* wajib[4] = {"Alamat Keep Stock", "SKU", "SKU QTY", "Alamat Gondola"};
    int indices[4] = {idx_keep, idx_sku, idx_qty, idx_gondola};
    for (int i = 0; i < 4; ++i) {
        if (indices[i] < 0) {
            out_error = std::string("Kolom '") + wajib[i] + "' tidak ditemukan di file Excel!";
            return false;
        }
    }

    for (size_t r = 1; r < raw_table.size(); ++r) {
        const auto& row = raw_table[r];
        auto get_col = [&](int col_idx) -> std::string {
            if (col_idx >= 0 && col_idx < static_cast<int>(row.size())) {
                return BersihkanAngka(row[col_idx]);
            }
            return "";
        };

        StockRow sr;
        sr.keep_stock = get_col(idx_keep);
        sr.sku = get_col(idx_sku);
        sr.sku_qty = get_col(idx_qty);
        sr.gondola = get_col(idx_gondola);

        // Lewati baris yang benar-benar kosong di akhir sheet jika semua kolom di baris itu kosong
        bool all_empty = true;
        for (const auto& cell : row) {
            if (!BersihkanAngka(cell).empty()) {
                all_empty = false;
                break;
            }
        }
        if (all_empty && row.empty()) continue;

        out_rows.push_back(std::move(sr));
    }

    return true;
}

} // namespace XlsxReader
