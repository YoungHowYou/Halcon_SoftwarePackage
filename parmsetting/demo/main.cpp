// =============================================================================
// parm_ui_demo —— 命令行调用示例
//
//   parm_ui_demo                                用内置全类型示例，结果打到 stdout
//   parm_ui_demo in.json                        从文件读，结果打到 stdout
//   parm_ui_demo in.json out.json               从文件读，结果写回 out.json
//   parm_ui_demo in.json out.json 2             指定当前用户权限等级（默认 3）
//
//   测试钩子环境变量（正常使用不需要）：
//     PARM_UI_SMOKE_MS=1500        窗口显示 1.5 秒后自动关闭（等同点“取消”）
//     PARM_UI_SMOKE_SAVE=1         自动关闭时走“保存并关闭”分支
//     PARM_UI_SMOKE_SHOT=a.bmp     第 3 帧截图存成 BMP（相对路径即可）
//     PARM_UI_SMOKE_CLICK_SAVE=20  第 20 帧模拟点“保存并关闭”（用来截校验弹窗）
//     PARM_UI_SMOKE_SHOT2=b.bmp    上面那次点击之后第 3 帧截图
//     PARM_UI_FONT=fonts/xx.ttc    指定中文字体（相对／绝对路径都可以）
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <parm_ui.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <shellapi.h>
#endif

namespace {

const char* kDefaultJson = R"JSON({
  "基础参数": {
    "整数示例":  {"Competence": 1, "Limit": [0, 25000000], "Type": "int",         "Value": 1000},
    "浮点示例":  {"Competence": 1, "Limit": [0.0, 1.0],    "Type": "float",       "Value": 0.5},
    "布尔示例":  {"Competence": 1, "Limit": "",            "Type": "bool",        "Value": "TRUE"},
    "字符串示例":{"Competence": 1, "Limit": "",            "Type": "string",      "Value": "Breakage|Cracks"},
    "字符串枚举":{"Competence": 2, "Limit": ["CPU", "GPU"],"Type": "string_enum", "Value": "GPU"},
    "整数枚举":  {"Competence": 2, "Limit": [1, 2, 4, 8],  "Type": "int_enum",    "Value": 4},
    "浮点枚举":  {"Competence": 2, "Limit": [0.5, 1.5],    "Type": "float_enum",  "Value": 1.5},
    "整数数组":  {"Competence": 2, "Limit": [10, 100],     "Type": "int[]",       "Value": [10, 20, 30]},
    "浮点数组":  {"Competence": 2, "Limit": [0.1, 2.5],    "Type": "float[]",     "Value": [0.5, 1.5]},
    "字符串数组":{"Competence": 2, "Limit": "",            "Type": "string[]",    "Value": ["A", "B"]}
  },
  "路径参数": {
    "目录示例": {"Competence": 1, "Limit": "", "Type": "directory", "Value": "./json"},
    "文件示例": {"Competence": 1, "Limit": "", "Type": "path",      "Value": ".\\mask.hobj"}
  },
  "权限演示": {
    "操作员可改": {"Competence": 0, "Limit": [0, 100], "Type": "int", "Value": 10},
    "产线可改":   {"Competence": 1, "Limit": [0, 100], "Type": "int", "Value": 20},
    "系统可改":   {"Competence": 2, "Limit": [0, 100], "Type": "int", "Value": 30},
    "管理员可改": {"Competence": 3, "Limit": [0, 100], "Type": "int", "Value": 40}
  }
})JSON";

#if defined(_WIN32)
std::string wide_to_utf8(const wchar_t* w) {
    if (w == nullptr) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return std::wstring();
    std::wstring w(static_cast<std::size_t>(n - 1), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#endif

std::string read_text(const std::string& path, bool& ok) {
    std::ifstream in;
#if defined(_WIN32)
    // CRT 的窄字符文件 API 走本地代码页，UTF-8 中文路径必须转宽字符
    in.open(utf8_to_wide(path).c_str(), std::ios::binary);
#else
    in.open(path.c_str(), std::ios::binary);
#endif
    if (!in) {
        ok = false;
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

bool write_text(const std::string& path, const std::string& text) {
    std::ofstream out;
#if defined(_WIN32)
    out.open(utf8_to_wide(path).c_str(), std::ios::binary);
#else
    out.open(path.c_str(), std::ios::binary);
#endif
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

// 取命令行参数。Windows 下 main 的 argv 是本地代码页（GBK），
// 中文文件名会乱码，所以这里改成拿宽字符命令行再转 UTF-8。
std::vector<std::string> get_args(int argc, char** argv) {
    std::vector<std::string> args;
#if defined(_WIN32)
    int     nargs = 0;
    LPWSTR* wargv = ::CommandLineToArgvW(::GetCommandLineW(), &nargs);
    if (wargv != nullptr) {
        for (int i = 0; i < nargs; ++i) args.push_back(wide_to_utf8(wargv[i]));
        ::LocalFree(wargv);
    }
    if (args.empty()) args.push_back("parm_ui_demo");
#else
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
#endif
    return args;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);  // 让控制台能正常显示中文
#endif

    const std::vector<std::string> args = get_args(argc, argv);

    std::string input;
    if (args.size() >= 2) {
        bool ok = false;
        input   = read_text(args[1], ok);
        if (!ok) {
            std::fprintf(stderr, "[demo] 读不到输入文件: %s\n", args[1].c_str());
            return 2;
        }
    } else {
        input = kDefaultJson;
        std::printf("[demo] 未指定输入文件，使用内置全类型示例\n");
    }

    const int competence = (args.size() >= 4) ? std::atoi(args[3].c_str()) : 3;

    int major = 0, minor = 0, patch = 0;
    parm_ui_version(&major, &minor, &patch);
    std::printf("[demo] parm_ui v%d.%d.%d —— 弹出参数配置窗口（当前权限等级 %d）...\n", major, minor,
                patch, competence);

    int   cancelled = 0;
    char* out       = parm_ui_json(input.c_str(), competence, &cancelled);
    if (out == nullptr) {
        std::fprintf(stderr, "[demo] 调用失败: %s\n", parm_ui_last_error());
        return 1;
    }

    if (cancelled) std::printf("[demo] 用户取消（窗口被关闭），返回的是原 JSON\n");

    if (args.size() >= 3) {
        if (write_text(args[2], out))
            std::printf("[demo] 已写入 %s\n", args[2].c_str());
        else
            std::fprintf(stderr, "[demo] 写文件失败: %s\n", args[2].c_str());
    } else {
        std::printf("[demo] 返回的 JSON：\n%s\n", out);
    }

    parm_ui_free(out);
    return 0;
}
