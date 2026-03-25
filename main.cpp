#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cctype>
#include <memory>

#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

using namespace std;

// ================= 压缩存储结构 =================
#pragma pack(push, 1)
struct CompressedEntry {
    uint32_t py_offset;
    uint32_t wd_offset;
    uint16_t py_len;
};
#pragma pack(pop)

struct Header {
    uint32_t entry_count;
    uint32_t wd_pool_size;
    uint32_t py_pool_size;
    uint32_t hash_table_size;
    uint32_t hash_seed;
};

// ================= 拼音规范化 =================
class SimpleHash {
public:
    static string normalize(const string& pinyin) {
        string result;
        for (char c : pinyin) {
            if (c == '\'' || c == '`') {
                continue;
            }
            result += tolower(c);
        }
        return result;
    }
};

// ================= UTF-8文件读取 =================
class UTF8FileReader {
public:
    static bool readLine(ifstream& file, string& line) {
        if (!getline(file, line)) return false;

        // 移除BOM
        if (line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF) {
            line = line.substr(3);
        }

        // 移除回车
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        return true;
    }
};

// ================= 内存映射文件 =================
class MMapFile {
public:
    HANDLE hFile = NULL;
    HANDLE hMap = NULL;
    char* data = nullptr;
    size_t size = 0;

    bool open(const string& path) {
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        LARGE_INTEGER fileSize;
        if (!GetFileSizeEx(hFile, &fileSize)) return false;
        size = fileSize.QuadPart;

        hMap = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) return false;

        data = (char*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
        return data != nullptr;
    }

    ~MMapFile() {
        if (data) UnmapViewOfFile(data);
        if (hMap) CloseHandle(hMap);
        if (hFile) CloseHandle(hFile);
    }
};

// ================= 查询引擎 =================
class CompactQueryEngine {
private:
    MMapFile mm;
    Header* hdr;
    CompressedEntry* entries;
    char* py_pool;
    char* wd_pool;

    int binary_search_by_pinyin(const string& pinyin) {
        if (hdr->entry_count == 0) return -1;

        int left = 0;
        int right = hdr->entry_count - 1;
        int result = -1;

        while (left <= right) {
            int mid = left + (right - left) / 2;
            const char* mid_py = py_pool + entries[mid].py_offset;
            int cmp = strcmp(mid_py, pinyin.c_str());

            if (cmp < 0) {
                left = mid + 1;
            }
            else if (cmp > 0) {
                right = mid - 1;
            }
            else {
                result = mid;
                right = mid - 1;
            }
        }

        return result;
    }

public:
    bool load(const string& file) {
        if (!mm.open(file)) return false;

        if (mm.size < sizeof(Header)) return false;

        hdr = (Header*)mm.data;
        entries = (CompressedEntry*)(mm.data + sizeof(Header));
        py_pool = mm.data + sizeof(Header) + hdr->entry_count * sizeof(CompressedEntry);
        wd_pool = py_pool + hdr->py_pool_size;

        return true;
    }

    vector<string> query(const string& input) {
        vector<string> results;
        string pinyin = SimpleHash::normalize(input);
        if (pinyin.empty()) return results;

        int start_idx = binary_search_by_pinyin(pinyin);
        if (start_idx != -1) {
            for (int i = start_idx; i < hdr->entry_count; i++) {
                const char* current_py = py_pool + entries[i].py_offset;
                if (strcmp(current_py, pinyin.c_str()) != 0) break;

                const char* word = wd_pool + entries[i].wd_offset;
                results.emplace_back(word);
            }
        }

        return results;
    }

    void print_sample() {
        if (hdr->entry_count > 0) {
            cout << "\nIndex sample (first 10):" << endl;
            for (int i = 0; i < min(10, (int)hdr->entry_count); i++) {
                cout << "  " << (py_pool + entries[i].py_offset)
                    << " -> " << (wd_pool + entries[i].wd_offset) << endl;
            }
        }
    }

    size_t get_mapped_memory() { return mm.size; }

    void print_stats() {
        if (hdr) {
            cout << "Statistics:" << endl;
            cout << "  Entries: " << hdr->entry_count << endl;
            cout << "  Pinyin pool: " << hdr->py_pool_size / 1024.0 << " KB" << endl;
            cout << "  Word pool: " << hdr->wd_pool_size / 1024.0 << " KB" << endl;
        }
    }
};

// ================= 构建索引 =================
void build_compact_bin(const string& csv, const string& bin) {
    ifstream in(csv, ios::binary);
    if (!in.is_open()) {
        cout << "Cannot open file: " << csv << endl;
        return;
    }

    string line;
    vector<pair<string, string>> raw;
    int line_num = 0;

    // 读取第一行（跳过BOM和表头）
    if (!UTF8FileReader::readLine(in, line)) {
        cout << "File is empty" << endl;
        return;
    }
    line_num++;

    // 读取数据行
    while (UTF8FileReader::readLine(in, line)) {
        line_num++;
        if (line.empty()) continue;

        size_t pos = line.find(',');
        if (pos == string::npos) continue;

        string pinyin = line.substr(0, pos);
        string word = line.substr(pos + 1);

        // 规范化拼音
        string normalized;
        for (char c : pinyin) {
            if (c == '\'' || c == '`') {
                continue;
            }
            normalized += tolower(c);
        }

        if (!normalized.empty() && !word.empty()) {
            raw.emplace_back(normalized, word);
        }
    }

    if (raw.empty()) {
        cout << "No valid data" << endl;
        return;
    }

    // 按拼音排序
    sort(raw.begin(), raw.end());

    // 构建拼音池和汉字池
    string py_pool, wd_pool;
    vector<CompressedEntry> entries;
    unordered_map<string, uint32_t> py_offset_map;
    unordered_map<string, uint32_t> wd_offset_map;

    for (auto& item : raw) {
        string& py = item.first;
        string& wd = item.second;

        uint32_t py_off;
        auto py_it = py_offset_map.find(py);
        if (py_it != py_offset_map.end()) {
            py_off = py_it->second;
        }
        else {
            py_off = (uint32_t)py_pool.size();
            py_pool += py;
            py_pool += '\0';
            py_offset_map[py] = py_off;
        }

        uint32_t wd_off;
        auto wd_it = wd_offset_map.find(wd);
        if (wd_it != wd_offset_map.end()) {
            wd_off = wd_it->second;
        }
        else {
            wd_off = (uint32_t)wd_pool.size();
            wd_pool += wd;
            wd_pool += '\0';
            wd_offset_map[wd] = wd_off;
        }

        CompressedEntry entry;
        entry.py_offset = py_off;
        entry.wd_offset = wd_off;
        entry.py_len = (uint16_t)py.length();
        entries.push_back(entry);
    }

    Header hdr{
        (uint32_t)entries.size(),
        (uint32_t)wd_pool.size(),
        (uint32_t)py_pool.size(),
        0,
        0
    };

    ofstream out(bin, ios::binary);
    out.write((char*)&hdr, sizeof(hdr));
    out.write((char*)entries.data(), entries.size() * sizeof(CompressedEntry));
    out.write(py_pool.data(), py_pool.size());
    out.write(wd_pool.data(), wd_pool.size());

    cout << "\nBuild completed" << endl;
    cout << "  Entries: " << entries.size() << endl;
    cout << "  Unique pinyin: " << py_offset_map.size() << endl;
    cout << "  Unique words: " << wd_offset_map.size() << endl;
    cout << "  File size: " << (sizeof(hdr) + entries.size() * sizeof(CompressedEntry) +
        py_pool.size() + wd_pool.size()) / 1024.0 << " KB" << endl;
}

// ================= 内存统计 =================
void print_memory() {
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    cout << "[Memory] " << pmc.WorkingSetSize / 1024.0 / 1024.0 << " MB" << endl;
}

// ================= 主函数 =================
int main() {
    // 设置控制台为UTF-8编码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    string csv_file = "dict.csv";
    string bin_file = "dict_compact.bin";

    // 检查是否需要重新构建
    ifstream test(bin_file, ios::binary);
    if (!test.good()) {
        cout << "Building index..." << endl;
        build_compact_bin(csv_file, bin_file);
        cout << endl;
    }

    CompactQueryEngine engine;
    if (!engine.load(bin_file)) {
        cout << "Load failed" << endl;
        system("pause");
        return 0;
    }

    cout << "Pinyin Query System" << endl;
    engine.print_stats();
    engine.print_sample();
    cout << "\nMemory mapped: " << engine.get_mapped_memory() / 1024.0 / 1024.0 << " MB" << endl;
    print_memory();

    string input;
    while (true) {
        cout << "\n> ";
        cin >> input;

        if (input == "exit") break;

        auto start = chrono::high_resolution_clock::now();
        vector<string> results = engine.query(input);
        auto end = chrono::high_resolution_clock::now();

        if (results.empty()) {
            cout << "No results" << endl;
        }
        else {
            // 限制输出数量
            size_t limit = min(results.size(), (size_t)50);
            for (size_t i = 0; i < limit; i++) {
                cout << results[i] << endl;
            }
            if (results.size() > limit) {
                cout << "... total " << results.size() << " results" << endl;
            }
        }

        cout << "[Time] " << chrono::duration<double, milli>(end - start).count() << " ms" << endl;
    }

    return 0;
}