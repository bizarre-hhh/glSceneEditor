#pragma once
#include "types.hpp"
#include <memory>

// 构建数据的读写接口与格式注册表。
namespace pyslm::io {

// 一份完整的构建数据：头部、模型、层列表。
struct BuildDocument {Header header;std::vector<Model> models;std::vector<Layer> layers;};

// 一种文件格式的描述信息：名称、说明、支持的扩展名及读写能力。
struct FormatInfo {std::string name,description;std::vector<std::string> extensions;bool canRead=false,canWrite=false;};

// 文件格式适配器基类。
// 厂商转换器可以放在另一个原生 DLL 中；双方必须使用相同的
// MSVC x64 工具链、CRT 和构建配置。
class PYSLM_API FormatAdapter {
public:
    virtual ~FormatAdapter()=default;
    virtual FormatInfo format() const=0;                         // 返回格式描述
    virtual BuildDocument read(const std::string& filename) const;  // 读取文件
    virtual void write(const std::string& filename,const BuildDocument& build) const; // 写入文件
};

// 格式注册表：管理适配器并按扩展名或格式名读写。
class PYSLM_API FormatRegistry {
public:
    void add(std::shared_ptr<FormatAdapter> adapter);            // 注册适配器
    void remove(const std::string& name);                        // 取消注册
    std::vector<FormatInfo> formats() const;                     // 列出已注册格式
    BuildDocument read(const std::string& filename,const std::string& format={}) const; // 读取
    void write(const std::string& filename,const BuildDocument& build,const std::string& format={}) const; // 写入
private:
    std::vector<std::shared_ptr<FormatAdapter>> adapters_;       // 已注册的适配器
    const FormatAdapter& resolve(const std::string& filename,const std::string& name,bool writing) const; // 选择适配器
};

// 内置的开放 JSON 交换格式适配器（不用于专有设备格式转换）。
PYSLM_API std::shared_ptr<FormatAdapter> jsonAdapter();

} // namespace pyslm::io
