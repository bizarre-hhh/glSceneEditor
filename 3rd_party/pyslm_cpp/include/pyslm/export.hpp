#pragma once

// DLL 导入/导出宏。
// - 构建 pyslm_cpp.dll 时定义 PYSLM_CPP_BUILD，PYSLM_API 表示导出；
// - 使用 DLL 时未定义该宏，PYSLM_API 表示导入；
// - 非 Windows 平台使用默认可见性。
#if defined(_WIN32)
#  if defined(PYSLM_CPP_BUILD)
#    define PYSLM_API __declspec(dllexport)
#  else
#    define PYSLM_API __declspec(dllimport)
#  endif
#else
#  define PYSLM_API __attribute__((visibility("default")))
#endif
