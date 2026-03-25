#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

using namespace std;

#pragma pack(push, 1)
struct Entry {
    uint32_t py_offset;  // 拼音串在py_pool中的偏移
    uint32_t wd_offset;  // 词语串在wd_pool中的偏移
};
#pragma pack(pop)

struct Header {
    uint32_t entry_count;    // 条目总数
    uint32_t wd_pool_size;   // 词语池大小
    uint32_t py_pool_size;   // 拼音池大小
};

// 拼音规范化：移除引号并转为小写
inline string normalize(const string& s) {
    string r;
    r.reserve(s.size());
    for (char c : s) {
        if (c != '\'' && c != '`') r += tolower(c);
    }
    return r;
}

// Windows内存映射封装
class MMap {
public:
    HANDLE f = NULL, m = NULL;
    char* data = nullptr;

    bool open(const string& path) {
        f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) return false;

        m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!m) return false;

        data = (char*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        return data != nullptr;
    }

    ~MMap() {
        if (data) UnmapViewOfFile(data);
        if (m) CloseHandle(m);
        if (f) CloseHandle(f);
    }
};

// 查询引擎
class Engine {
    MMap mm;
    Header* hdr = nullptr;
    Entry* entries = nullptr;
    char* py_pool = nullptr;
    char* wd_pool = nullptr;

public:
    bool load(const string& file) {
        if (!mm.open(file)) return false;

        hdr = (Header*)mm.data;

        // 检查文件完整性
        size_t expected_size = sizeof(Header) + hdr->entry_count * sizeof(Entry) + hdr->py_pool_size + hdr->wd_pool_size;
        DWORD file_size = GetFileSize(mm.f, nullptr);
        if (expected_size > file_size) return false;

        entries = (Entry*)(mm.data + sizeof(Header));
        py_pool = mm.data + sizeof(Header) + hdr->entry_count * sizeof(Entry);
        wd_pool = py_pool + hdr->py_pool_size;
        return true;
    }

    void query_and_print(const string& input) {
        string key = normalize(input);

        // 二分查找第一个匹配项
        int l = 0, r = hdr->entry_count - 1, first = -1;
        while (l <= r) {
            int mid = (l + r) >> 1;
            int cmp = strcmp(py_pool + entries[mid].py_offset, key.c_str());
            if (cmp >= 0) {
                if (cmp == 0) first = mid;
                r = mid - 1;
            }
            else {
                l = mid + 1;
            }
        }

        if (first == -1) {
            cout << "[No Result]\n";
            return;
        }

        // 遍历所有匹配项
        int count = 0, total = 0;
        for (int i = first; i < hdr->entry_count; i++) {
            const char* py = py_pool + entries[i].py_offset;
            if (strcmp(py, key.c_str()) != 0) break;

            if (count < 49) {
                cout << (wd_pool + entries[i].wd_offset) << "\t";
                if (++count % 7 == 0) cout << "\n";
            }
            total++;
        }

        cout << "\n[Total=" << total << "]\n";
    }
};

// 构建索引文件（优化预分配）
void build(const string& csv, const string& bin) {
    ifstream in(csv);
    vector<pair<string, string>> data;
    string line;

    getline(in, line);  // 跳过表头

    size_t total_py = 0, total_wd = 0;
    while (getline(in, line)) {
        if (line.empty()) continue;
        auto pos = line.find(',');
        if (pos == string::npos) continue;

        string py = normalize(line.substr(0, pos));
        string wd = line.substr(pos + 1);
        if (!py.empty() && !wd.empty()) {
            data.emplace_back(py, wd);
            total_py += py.size() + 1;
            total_wd += wd.size() + 1;
        }
    }

    sort(data.begin(), data.end());

    string py_pool, wd_pool;
    py_pool.reserve(total_py);
    wd_pool.reserve(total_wd);

    vector<Entry> entries;
    entries.reserve(data.size());

    for (auto& [py, wd] : data) {
        entries.push_back({ (uint32_t)py_pool.size(), (uint32_t)wd_pool.size() });
        py_pool += py + '\0';
        wd_pool += wd + '\0';
    }

    Header hdr{ (uint32_t)entries.size(), (uint32_t)wd_pool.size(), (uint32_t)py_pool.size() };

    ofstream out(bin, ios::binary);
    out.write((char*)&hdr, sizeof(hdr));
    out.write((char*)entries.data(), entries.size() * sizeof(Entry));
    out.write(py_pool.data(), py_pool.size());
    out.write(wd_pool.data(), wd_pool.size());

    cout << "[Build Done] Entries=" << entries.size() << endl;
}

// 打印内存使用情况
void print_memory() {
    PROCESS_MEMORY_COUNTERS_EX pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));

    cout << "[Memory] " << pmc.WorkingSetSize / 1024.0 / 1024.0 << " MB" << endl;
}

// 裁剪工作集到最小
void trim_working_set() {
    SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    string csv = "dict.csv";
    string bin = "dict.bin";

    ifstream f(bin);
    if (!f.good()) {
        cout << "[INFO] Building index..." << endl;
        build(csv, bin);
    }

    Engine e;
    if (!e.load(bin)) {
        cout << "[ERROR] Load failed" << endl;
        return 0;
    }

    cout << "==== Pinyin Query System (Optimized) ====" << endl;
    cout << "Type pinyin (exit to quit)" << endl;

    print_memory();

    string s;
    while (true) {
        cout << "\n> ";
        cin >> s;
        if (s == "exit") break;

        auto t1 = chrono::high_resolution_clock::now();
        e.query_and_print(s);
        auto t2 = chrono::high_resolution_clock::now();

        cout << "[Time] " << chrono::duration<double, milli>(t2 - t1).count() << " ms" << endl;

        trim_working_set();
        print_memory();
    }
}