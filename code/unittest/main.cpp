#include "framework.h"
#include <cstring>

// misakifs 单元测试入口
//
// 用例散落在各 test_*.cpp 中, 通过文件作用域的 Registrar 静态对象
// 在 main() 之前完成注册, 这里只负责解析参数并统一运行。
//
//   ./unittest          运行全部轻量用例
//   ./unittest --full   额外运行重量级用例(如写满整盘)

int main(int argc, char** argv)
{
    bool full = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--full") == 0)
            full = true;
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::cout << "用法: " << argv[0] << " [--full]" << std::endl;
            std::cout << "  --full   额外运行重量级用例 (写满整盘)" << std::endl;
            return 0;
        }
        else
        {
            std::cout << "未知参数: " << argv[i]
                      << "  (可用 --full, --help)" << std::endl;
            return 2;
        }
    }

    std::cout << "misakifs 单元测试" << std::endl;
    std::cout << "共注册 " << ut::registry().size() << " 个用例";
    if (!full)
        std::cout << " (未开启 --full, 重量级用例将跳过)";
    std::cout << std::endl;

    return ut::run_all(full);
}
