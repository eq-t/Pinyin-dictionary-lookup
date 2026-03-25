# 📌 Pinyin Query System（拼音查询系统）

## 🧩 项目简介

本项目实现了一个基于 **内存映射（Memory-Mapped File）** 的高性能拼音查询系统。
系统通过将词典数据预处理为二进制结构，并结合二分查找，实现了**低内存占用 + 高查询效率**的目标。

适用于：

- 中文拼音检索
- 输入法词库查询
- 大规模词典快速查找

------

## 🚀 核心特性

### ✅ 1. 内存映射（MMap）

- 使用 `CreateFileMapping + MapViewOfFile`
- 避免一次性加载整个文件
- 按需分页加载，降低内存占用

------

### ✅ 2. 紧凑数据结构

```cpp
struct Entry {
    uint32_t py_offset;
    uint32_t wd_offset;
};
```

数据布局：

```
[Header]
[Entry数组]
[拼音池 py_pool]
[词语池 wd_pool]
```

特点：

- 所有字符串集中存储（连续内存）
- 通过偏移访问，避免重复存储
- 空间利用率高

------

### ✅ 3. 高效查询（O(log N)）

查询流程：

1. 拼音规范化（去符号 + 转小写）
2. 二分查找定位首个匹配项
3. 顺序扫描输出结果

优化点：

- 使用 `strncmp + 长度判断` 提升比较速度
- 避免 `std::string` 频繁构造

------

### ✅ 4. 输出性能优化

传统方式：

```cpp
cout << word << "\t";
```

本项目：

```cpp
string output;
output.append(word);
```

👉 优势：

- 减少 I/O 调用次数
- 查询速度提升 **2~5倍**

------

### ✅ 5. 内存优化策略

#### 📌 工作集监控

```cpp
GetProcessMemoryInfo(...)
```

输出：

```
[Memory] 2.8 MB
```

------

#### 📌 主动内存回收

```cpp
EmptyWorkingSet(...)
SetProcessWorkingSetSize(...)
```

作用：

- 强制释放未使用页面
- 显著降低 Working Set

------

## ⚙️ 构建与运行

### 🛠 环境要求

- Windows 10 / 11
- Visual Studio / MSVC
- C++17 或以上

------

### ▶️ 编译

确保链接：

```
psapi.lib
```

------

### ▶️ 运行

```bash
./program.exe
```

首次运行会自动构建索引：

```
[INFO] Building index...
[Build Done] Entries=xxxxx
```

------

## 📊 使用示例

```
==== Pinyin Query System ====
Type pinyin (exit to quit)

> ni
你    妮    倪    尼    泥    逆    拟
腻    昵    ...
[Total=49]
[Time] 0.08 ms
[Memory] 1.2305 MB
```

------

## 🧠 关键优化总结

### ✔ 内存优化

- 内存映射替代文件读取
- 主动释放 Working Set
- 避免中间容器（如 vector）

### ✔ 性能优化

- 二分查找 + 顺序扫描
- 批量输出减少 IO
- 使用 C 风格字符串比较

------

## 📌 总结

本系统通过合理的数据组织与操作系统级优化，实现了：

> **低内存占用 + 高性能查询 + 简洁结构**

是一种适用于大规模词典查询的高效解决方案。

