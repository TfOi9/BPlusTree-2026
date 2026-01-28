# BPlusTree 实现简介

本项目提供一个带缓冲与磁盘持久化的模板类 B+ 树实现，核心位于 `include/` 目录。

## 主要模块
- `bpt.hpp`: B+ 树主体，提供插入、删除、查找与范围查找；封装缓冲区管理与持久化根节点记录。
- `buffer.hpp`: LRU 缓冲管理器，负责页面缓存、脏页写回、根位置读写（通过 `DiskManager`）。
- `page.hpp`: 页面结构定义（叶子/内部），支持二分查找、邻接指针、父指针等数据。
- `disk.hpp`: 磁盘读写管理器，可以读写定长页面，维护文件头信息（如根位置）。
- `config.hpp`: B+ 树参数设置，包含页面大小、缓冲区大小等。

## 接口概览
- `find(const KeyType& key) -> std::optional<ValueType>`：返回首个匹配值，未找到则空。
- `find_all(const KeyType& key, std::vector<ValueType>& vec)`：收集所有等值键对应的值。
- `insert(const KeyType& key, const ValueType& val)`：插入键值对，必要时分裂页面并自顶向下更新。
- `erase(const KeyType& key, const ValueType& val)`：删除指定键值对，必要时借位或合并并重新平衡。

## 持久化与缓冲
- 构造时读取已持久化的根位置；析构时写回最新根位置。
- 缓冲区采用 LRU 策略，`get_page`取得只读页面，`get_page_mutable` 取得可写页面并标记脏页，`finish_use` 释放使用标记。
- `flush` 写回所有脏页并清空缓存状态，用于安全关闭或重置缓存。

## 键类型
示例程序使用定长字符串。其他定长键类型也可按需替换，需定义比较运算符以支持页面二分查找与顺序维护。