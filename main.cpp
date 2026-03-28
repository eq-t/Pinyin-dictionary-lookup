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
    uint32_t py_offset;
    uint32_t wd_offset;
};
#pragma pack(pop)

struct Header {
    uint32_t entry_count;
    uint32_t wd_pool_size;
    uint32_t py_pool_size;
};

// ================= 拼音规范化（原地修改，速度最优） =================
inline string normalize(string s) {
    size_t len = 0;
    for (char c : s) {
        if (c != '\'' && c != '`') {
            s[len++] = tolower(static_cast<unsigned char>(c));
        }
    }
    s.resize(len);
    return s;
}

// ================= 内存映射 =================
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

// ================= 查询 =================
class Engine {
    MMap mm;
    Header* hdr;
    Entry* entries;
    char* py_pool;
    char* wd_pool;
public:
    bool load(const string& file) {
        if (!mm.open(file)) return false;
        hdr = (Header*)mm.data;
        entries = (Entry*)(mm.data + sizeof(Header));
        py_pool = mm.data + sizeof(Header) + hdr->entry_count * sizeof(Entry);
        wd_pool = py_pool + hdr->py_pool_size;
        return true;
    }

    void query(const string& input) {
        string key = normalize(input);
        if (key.empty()) {
            cout << "[No Result]" << endl;
            return;
        }

        size_t key_len = key.size();

        // 二分查找第一个精确匹配
        int l = 0, r = hdr->entry_count - 1, first = -1;
        while (l <= r) {
            int mid = (l + r) >> 1;
            const char* p = py_pool + entries[mid].py_offset;
            int cmp = strncmp(p, key.c_str(), key_len);
            if (cmp == 0 && p[key_len] == '\0') {
                first = mid;
                r = mid - 1;        // 继续向左找到最左侧
            }
            else if (cmp < 0) {
                l = mid + 1;
            }
            else {
                r = mid - 1;
            }
        }

        if (first == -1) {
            cout << "[No Result]" << endl;
            return;
        }

        // ================= 计算实际总数 + 输出缓冲 =================
        size_t total = 0;           // 实际匹配到的全部数量
        string output;
        output.reserve(2048);

        int count = 0;              // 用于换行计数
        size_t shown = 0;           // 用于限制显示数量

        for (int i = first; i < (int)hdr->entry_count; i++) {
            const char* py = py_pool + entries[i].py_offset;
            if (strncmp(py, key.c_str(), key_len) != 0 || py[key_len] != '\0')
                break;

            total++;                // 统计实际总数（关键修复）

            if (shown < 49) {       // 只显示前49个
                const char* wd = wd_pool + entries[i].wd_offset;
                output.append(wd);
                output += '\t';
                count++;
                shown++;
                if (count % 7 == 0) output += '\n';
            }
        }

        // 输出结果
        cout << output;
        if (shown % 7 != 0) cout << "\n";   // 如果最后一行没满7个，补换行

        cout << "[Total=" << total << "]\n";   // 显示真实总数
    }
};

// ================= 构建 =================
void build(const string& csv, const string& bin) {
    ifstream in(csv);
    vector<pair<string, string>> data;
    string line;
    getline(in, line); // skip header
    while (getline(in, line)) {
        if (line.empty()) continue;
        auto pos = line.find(',');
        if (pos == string::npos) continue;
        string py = normalize(line.substr(0, pos));
        string wd = line.substr(pos + 1);
        if (!py.empty() && !wd.empty()) data.emplace_back(py, wd);
    }
    sort(data.begin(), data.end());
    string py_pool, wd_pool;
    vector<Entry> entries;
    for (auto& [py, wd] : data) {
        uint32_t py_off = py_pool.size();
        py_pool += py + '\0';
        uint32_t wd_off = wd_pool.size();
        wd_pool += wd + '\0';
        entries.push_back({ py_off, wd_off });
    }
    Header hdr{ (uint32_t)entries.size(), (uint32_t)wd_pool.size(), (uint32_t)py_pool.size() };
    ofstream out(bin, ios::binary);
    out.write((char*)&hdr, sizeof(hdr));
    out.write((char*)entries.data(), entries.size() * sizeof(Entry));
    out.write(py_pool.data(), py_pool.size());
    out.write(wd_pool.data(), wd_pool.size());
    cout << "[Build Done] Entries=" << entries.size() << endl;
}

// ================= 内存统计 =================
void print_memory() {
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    cout << "[Memory] " << pmc.WorkingSetSize / 1024.0 / 1024.0 << " MB" << endl;
}

// ================= 主动降低内存占用 =================
void trim_memory() {
    EmptyWorkingSet(GetCurrentProcess());
    SetProcessWorkingSetSize(GetCurrentProcess(), (SIZE_T)-1, (SIZE_T)-1);
}

// ================= 主函数 =================
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

    // 加载完成后立即压缩内存
    trim_memory();

    cout << "==== Pinyin Query System ====" << endl;
    cout << "Type pinyin (exit to quit)" << endl;
    print_memory();

    string s;
    while (true) {
        cout << "\n> ";
        cin >> s;
        if (s == "exit") break;

        auto t1 = chrono::high_resolution_clock::now();
        e.query(s);
        auto t2 = chrono::high_resolution_clock::now();

        cout << "[Time] "
            << chrono::duration<double, milli>(t2 - t1).count()
            << " ms" << endl;

        print_memory();

        // 每次查询后强制回收内存
        trim_memory();
    }
}