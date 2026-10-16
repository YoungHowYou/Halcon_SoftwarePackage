#pragma once
#include "Halcon.h"

/* FitModel 句柄析构需要 cv_flow 的模型类型（仅 C++ 编译单元包含本头） */
#include "cvflow/fit.hpp"
#include "cvflow/ransac.hpp"

#include "modbus.h"
#include "sqlite3.h"
#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "mysql.h"
#include "hdict_core.h"     // 扩展包内启用 hdict_core（字典算子，见 §HDict region）
#include "hparams_core.h"   // 扩展包内启用 hparams_core（参数树算子，见 §HParam region）
#include <memory>

#pragma region Sqlite
#define H_Sqlite_TAG 0xC0FFEE40
#define H_Sqlite_SEM_TYPE "Sqlite"
// 这是一个用于加载或保存数据库的函数。它接受一个内存数据库指针 pInMemory、一个数据库文件名 zFilename 和一个标志位 isSave。
//  若 isSave 为真，则将内存数据库保存到文件；若 isSave 为假，则将文件加载到内存数据库。
int loadOrSaveDb(sqlite3 *pInMemeory, const char *zFilename, int isSave);
extern "C"
{
    typedef struct
    {
        sqlite3 *SQLLiteDB;
        char DBPath[128];

    } SqliteHUserHandleData;

    static Herror SqliteHUserHandleDestructor(Hproc_handle ph, SqliteHUserHandleData *data)
    {
        // 防 double close：sqlite3_close 算子已把 SQLLiteDB 置 NULL，此处必须判空，
        // 否则对已关闭的 sqlite3* 再做 backup / sqlite3_close 直接 0xC0000005
        if (data->SQLLiteDB)
        {
            if (strcmp(data->DBPath, ":memory:") == 0)
            {
                loadOrSaveDb(data->SQLLiteDB, "./memory.db", 1);
            }
            else
            {
                sqlite3_close(data->SQLLiteDB);
            }
            data->SQLLiteDB = NULL;
        }

        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo SqliteHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Sqlite_TAG, H_Sqlite_SEM_TYPE, SqliteHUserHandleDestructor, NULL, NULL);
}
#define Def_INSqliteObject(pos, pUserData) \
    SqliteHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &SqliteHandleTypeUser, &(pUserData))

#define Def_OUTSqliteObject(pos, pUserData)                                        \
    SqliteHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &SqliteHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(SqliteHUserHandleData), (void **)(pUserData)))
#define OUTSqliteObject(pUserData) (*(pUserData))

#pragma endregion

#pragma region FitModel
// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEED0（拟合模型句柄，2026-09-30）
#define H_FitModel_TAG 0xC0FFEED0
#define H_FitModel_SEM_TYPE "FitModel"
extern "C"
{
    typedef struct
    {
        int   kind;   // cvflow::FitModelKind：0=LM / 1=Linear / 2=Geom(RansacHandle)
        void* ptr;    // 句柄拥有的模型对象；cv_fit_clear 后置 NULL，析构不再重复释放
        long  aux;    // geom：ResidualType（0=vertical / 1=geometric）
    } FitModelHUserHandleData;

    /* 唯一实体定义在 Halcon_Math.cpp（跨 TU 使用：create 与 clear 在不同模块，
     * 若按本文件其他句柄的 static 每 TU 一份写法，句柄类型比对会失败 2404） */
    extern const HHandleInfo FitModelHandleTypeUser;
}
#define Def_INFitModel(pos, pUserData) \
    FitModelHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &FitModelHandleTypeUser, &(pUserData))

#define Def_OUTFitModel(pos, pUserData)                                        \
    FitModelHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &FitModelHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(FitModelHUserHandleData), (void **)(pUserData)))
#define OUTFitModel(pUserData) (*(pUserData))

#pragma endregion

#pragma region Modbus
// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEEC0（曾误用 0xC0FFEE40 与 sqlite 撞号，已纠正）
#define H_Modbus_TAG 0xC0FFEEC0
#define H_Modbus_SEM_TYPE "Modbus"
extern "C"
{
    typedef struct
    {
        modbus_t* modbusCtx;

    } ModbusHUserHandleData;

    static Herror ModbusHUserHandleDestructor(Hproc_handle ph, ModbusHUserHandleData *data)
    {
        modbus_close(data->modbusCtx);
        modbus_free(data->modbusCtx);
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo ModbusHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Modbus_TAG, H_Modbus_SEM_TYPE, ModbusHUserHandleDestructor, NULL, NULL);
}
#define Def_INModbusObject(pos, pUserData) \
    ModbusHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &ModbusHandleTypeUser, &(pUserData))

#define Def_OUTModbusObject(pos, pUserData)                                        \
    ModbusHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &ModbusHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(ModbusHUserHandleData), (void **)(pUserData)))
#define OUTModbusObject(pUserData) (*(pUserData))
#pragma endregion

#pragma region Spdlog
// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEEA0（曾误用大恒相机的 0xC0FFEE80，已纠正）
#define H_Spdlog_TAG 0xC0FFEEA0
#define H_Spdlog_SEM_TYPE "Spdlog"
extern "C"
{
    typedef struct
    {
        void *loggerPtr; // std::shared_ptr<spdlog::logger>*
        char loggerName[128];
    } SpdlogHUserHandleData;

    static Herror SpdlogHUserHandleDestructor(Hproc_handle ph, SpdlogHUserHandleData *data)
    {
        if (data->loggerPtr)
        {
            auto *pLogger = static_cast<std::shared_ptr<spdlog::logger> *>(data->loggerPtr);
            delete pLogger;
            data->loggerPtr = NULL;
        }
        return HFree(ph, data);
    }
    const HHandleInfo SpdlogHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Spdlog_TAG, H_Spdlog_SEM_TYPE, SpdlogHUserHandleDestructor, NULL, NULL);
}
#define Def_INSpdlogObject(pos, pUserData) \
    SpdlogHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &SpdlogHandleTypeUser, &(pUserData))

#define Def_OUTSpdlogObject(pos, pUserData)                                        \
    SpdlogHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &SpdlogHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(SpdlogHUserHandleData), (void **)(pUserData)))
#define OUTSpdlogObject(pUserData) (*(pUserData))

// Helper: get the shared_ptr<logger> from handle data
inline std::shared_ptr<spdlog::logger> SpdlogGetLogger(SpdlogHUserHandleData *data)
{
    if (data && data->loggerPtr)
    {
        return *static_cast<std::shared_ptr<spdlog::logger> *>(data->loggerPtr);
    }
    return nullptr;
}

// Helper: store a shared_ptr<logger> into handle data
inline void SpdlogSetLogger(SpdlogHUserHandleData *data, std::shared_ptr<spdlog::logger> logger)
{
    data->loggerPtr = new std::shared_ptr<spdlog::logger>(logger);
    strncpy(data->loggerName, logger->name().c_str(), 127);
    data->loggerName[127] = '\0';
}
#pragma endregion

#pragma region Mysql
#define H_Mysql_TAG 0xC0FFEEB0
#define H_Mysql_SEM_TYPE "Mysql"
extern "C"
{
    typedef struct
    {
        MYSQL *mysqlCtx;
    } MysqlHUserHandleData;

    static Herror MysqlHUserHandleDestructor(Hproc_handle ph, MysqlHUserHandleData *data)
    {
        if (data->mysqlCtx)
        {
            mysql_close(data->mysqlCtx);
            data->mysqlCtx = NULL;
        }
        return HFree(ph, data);
    }
    const HHandleInfo MysqlHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Mysql_TAG, H_Mysql_SEM_TYPE, MysqlHUserHandleDestructor, NULL, NULL);
}
#define Def_INMysqlObject(pos, pUserData) \
    MysqlHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &MysqlHandleTypeUser, &(pUserData))

#define Def_OUTMysqlObject(pos, pUserData)                                        \
    MysqlHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &MysqlHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(MysqlHUserHandleData), (void **)(pUserData)))
#define OUTMysqlObject(pUserData) (*(pUserData))
#pragma endregion

#pragma region HDict
// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEEE0（HDict 字典句柄，2026-10-10）
// 句柄持有 hdict_core 的 DictPtr（shared_ptr<Dict>）；Dict 内含 std::shared_mutex ⇒ 不可拷贝，
// 故这里存 shared_ptr 的指针（与 Spdlog 存 shared_ptr<logger> 同一套路），析构时 delete。
#define H_HDict_TAG 0xC0FFEEE0
#define H_HDict_SEM_TYPE "HDict"
extern "C"
{
    typedef struct
    {
        void *dictPtr;   // std::shared_ptr<MLIQMetrics::hdict::Dict>*
    } HDictHUserHandleData;

    static Herror HDictHUserHandleDestructor(Hproc_handle ph, HDictHUserHandleData *data)
    {
        if (data->dictPtr)
        {
            delete static_cast<std::shared_ptr<MLIQMetrics::hdict::Dict> *>(data->dictPtr);
            data->dictPtr = NULL;
        }
        return HFree(ph, data);
    }
    const HHandleInfo HDictHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HDict_TAG, H_HDict_SEM_TYPE, HDictHUserHandleDestructor, NULL, NULL);
}
#define Def_INHDictObject(pos, pUserData) \
    HDictHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HDictHandleTypeUser, &(pUserData))

// 跨模块读 HDict：只取句柄里的数据指针，**不校验类型**。
//   HALCON 的用户句柄类型是按**模块（DLL）**注册的（H_HANDLE_TYPE 里内嵌本模块的析构函数
//   指针），拿本包的描述符去校验别的包建的 HDict 会得到 2404 Invalid handle type。
//   本包算子里只*读*字典（不创建、不释放句柄 ⇒ 不涉及析构），故 expected_type = NULL 安全。
#define Def_INHDictObjectShared(pos, pUserData)                                      \
    HDictHUserHandleData *(pUserData) = NULL;                                        \
    HCkP(HPGetPElemH(proc_handle, (pos), NULL, 1, (void **)&(pUserData), NULL, false))

#define Def_OUTHDictObject(pos, pUserData)                                        \
    HDictHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HDictHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HDictHUserHandleData), (void **)(pUserData))); \
    (*(pUserData))->dictPtr = NULL
#define OUTHDictObject(pUserData) (*(pUserData))

// Helper：从 handle data 取出 DictPtr（共享句柄；生命周期由 handle data 管理）
inline MLIQMetrics::hdict::DictPtr HDictGetDict(HDictHUserHandleData *data)
{
    if (data && data->dictPtr)
    {
        return *static_cast<std::shared_ptr<MLIQMetrics::hdict::Dict> *>(data->dictPtr);
    }
    return nullptr;
}

// Helper：把 DictPtr 存进 handle data（先释放旧值，支持重复赋值）
inline void HDictSetDict(HDictHUserHandleData *data, MLIQMetrics::hdict::DictPtr dict)
{
    if (data->dictPtr)
    {
        delete static_cast<std::shared_ptr<MLIQMetrics::hdict::Dict> *>(data->dictPtr);
        data->dictPtr = NULL;
    }
    data->dictPtr = new std::shared_ptr<MLIQMetrics::hdict::Dict>(std::move(dict));
}
#pragma endregion

#pragma region HParam
// TAG 分配见 README.md「句柄 TAG 分配表」：0xC0FFEEF0（json 参数树句柄，2026-10-10）
// 句柄持有 hparams_core 的 ParamTreePtr（shared_ptr<ParamTree>）—— ParamTree 内嵌 shared_mutex，
// 所以**多线程共享同一句柄也是线程安全的**（load 写锁 / to_json 读锁）。
// 之所以不复用 HDict 句柄：LoadParam 是 json 值语义，塞进 Dict 需要双向转换。
#define H_HParam_TAG 0xC0FFEEF0
#define H_HParam_SEM_TYPE "HParam"
extern "C"
{
    typedef struct
    {
        void *treePtr;   // std::shared_ptr<MLIQMetrics::ParamTree>*
    } HParamHUserHandleData;

    static Herror HParamHUserHandleDestructor(Hproc_handle ph, HParamHUserHandleData *data)
    {
        if (data->treePtr)
        {
            delete static_cast<std::shared_ptr<MLIQMetrics::ParamTree> *>(data->treePtr);
            data->treePtr = NULL;
        }
        return HFree(ph, data);
    }
    const HHandleInfo HParamHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HParam_TAG, H_HParam_SEM_TYPE, HParamHUserHandleDestructor, NULL, NULL);
}
#define Def_INHParamObject(pos, pUserData) \
    HParamHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HParamHandleTypeUser, &(pUserData))

#define Def_OUTHParamObject(pos, pUserData)                                        \
    HParamHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HParamHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HParamHUserHandleData), (void **)(pUserData))); \
    (*(pUserData))->treePtr = NULL
#define OUTHParamObject(pUserData) (*(pUserData))

// Helper：取参数树（未初始化返回 nullptr）
inline MLIQMetrics::ParamTreePtr HParamGetTree(HParamHUserHandleData *data)
{
    if (data && data->treePtr)
    {
        return *static_cast<std::shared_ptr<MLIQMetrics::ParamTree> *>(data->treePtr);
    }
    return nullptr;
}

// Helper：把参数树存进 handle data（先释放旧值，支持重复赋值）
inline void HParamSetTree(HParamHUserHandleData *data, MLIQMetrics::ParamTreePtr tree)
{
    if (data->treePtr)
    {
        delete static_cast<std::shared_ptr<MLIQMetrics::ParamTree> *>(data->treePtr);
        data->treePtr = NULL;
    }
    data->treePtr = new std::shared_ptr<MLIQMetrics::ParamTree>(std::move(tree));
}
#pragma endregion

// #pragma region IUP
// #define H_IUP_TAG 0xC0FFEE50
// #define H_IUP_SEM_TYPE "IUP"
// extern "C"
// {
//     typedef struct
//     {
//        Ihandle *  IUPCtx;

//     } IUPHUserHandleData;

//     static Herror IUPHUserHandleDestructor(Hproc_handle ph, IUPHUserHandleData *data)
//     {
//         IupDestroy(data->IUPCtx);
//         return HFree(ph, data);
//     }
//     // 句柄类型描述符
//     const HHandleInfo IUPHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_IUP_TAG, H_IUP_SEM_TYPE, IUPHUserHandleDestructor, NULL, NULL);
// }
// #define Def_INIUPObject(pos, pUserData) \
//     IUPHUserHandleData *(pUserData);    \
//     HGetCElemH1(proc_handle, (pos), &IUPHandleTypeUser, &(pUserData))

// #define Def_OUTIUPObject(pos, pUserData)                                        \
//     IUPHUserHandleData **(pUserData);                                           \
//     HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &IUPHandleTypeUser)); \
//     HCkP(HAlloc(proc_handle, sizeof(IUPHUserHandleData), (void **)(pUserData)))
// #define OUTIUPObject(pUserData) (*(pUserData))
// #pragma endregion
