// Halcon_ParmUI.cpp —— 参数配置窗口模块 supply 层
//   库：parm_ui（parmsetting/，Dear ImGui v1.92.9b + GLFW 3.4 静态链入，独立 C 接口）
//   本文件只做参数搬运：Json / Competence → parm_ui_json() → JsonOut / Cancelled
//
//   入口选型（只包 parm_ui_json 这一个）：
//     * parm_ui_json        —— 内部自建 UI 线程，任意线程可调，只有当前调用线程阻塞到关窗
//                              ⇒ 与 HALCON 算子的同步语义天然对应（本模块用它）
//     * parm_ui_json_inline —— 要求调用线程自己跑完整个 UI 事件循环，HDevelop 宿主不满足
//     * parm_ui_json_async  —— 回调语义，HALCON 算子拿不到回调期间的返回值指针
//
//   线程/并发：库内用 g_session_mutex 把会话串行化（同一时刻只允许一个配置窗口，
//             后到的调用排队），DEF 已声明 process_mutual: true。

#include "Halcon_SoftwarePackage.h"

#include "parm_ui.h"

#include <string>

/* 自定义错误码（>10000，配 HSetErrText 使用） */
#define PARM_UI_ERR_FAILED 31000 /* 弹窗失败：无图形环境 / 窗口初始化失败 / JSON 语法错误 */
#define PARM_UI_ERR_INPUT  31001 /* 入参非法：Json 不是恰好一个字符串 */

Herror Hparm_ui_show(Hproc_handle proc_handle)
{
    HCkNoObj(proc_handle);
    /* 读字符串参数前分配字符串内存。每算子只调一次（多次调用会泄漏 temp memory 块） */
    HAllocStringMem(proc_handle, 1024);

    /* 输入控制 1 = Json：零拷贝取 HALCON 内部字符串指针
       （配置 JSON 可能几 KB~几十 KB，不走 HGetSPar 的定长拷贝路径） */
    char const *const *strs = nullptr;
    INT4_8              n    = 0;
    HGetPElemS(proc_handle, 1, CONV_NONE, &strs, &n);
    if (n != 1 || strs == nullptr || strs[0] == nullptr)
    {
        HSetErrText(const_cast<char *>("parm_ui_show: input Json must be exactly one string"));
        return PARM_UI_ERR_INPUT;
    }
    const std::string jsonIn(strs[0]);

    /* 输入控制 2 = Competence：当前用户权限等级（库只做数值比较，不限制取值范围） */
    Hcpar competence;
    HGetSPar(proc_handle, 2, LONG_PAR, &competence, 1);

    /* 弹模态窗口：内部起 UI 线程，返回堆上 UTF-8 JSON（须用 parm_ui_free 释放）。
       保存关闭 → 改完的 JSON + cancelled=0；取消 / × → 原 JSON 副本 + cancelled=1。 */
    int   cancelled = 0;
    char *jsonOut   = parm_ui_json(jsonIn.c_str(), (int)competence.par.l, &cancelled);
    if (jsonOut == nullptr)
    {
        const char *err = parm_ui_last_error();
        HSetErrText(const_cast<char *>(err != nullptr && *err != '\0'
                                           ? err
                                           : "parm_ui_show: parameter dialog failed"));
        return PARM_UI_ERR_FAILED;
    }

    /* 输出控制参数独立编号（从 1 起，不接在输入控制之后） */
    HPutElem(proc_handle, 1, &jsonOut, 1, STRING_PAR); /* 必须 char** */
    INT4_8 cancelledOut = cancelled ? 1 : 0;
    HPutElem(proc_handle, 2, &cancelledOut, 1, LONG_PAR);

    parm_ui_free(jsonOut);
    return H_MSG_TRUE;
}
