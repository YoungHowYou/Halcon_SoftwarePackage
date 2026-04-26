#pragma once
#include "Halcon.h"
#include "modbus.h"
#include "sqlite3.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "mysql.h"
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
        int rev;
        if (strcmp(data->DBPath, ":memory:") == 0)
        {
            rev = loadOrSaveDb(data->SQLLiteDB, "./memory.db", 1);
        }
        else
        {
            rev = sqlite3_close(data->SQLLiteDB);
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

#pragma region Modbus
#define H_Modbus_TAG 0xC0FFEE40
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
#define H_Spdlog_TAG 0xC0FFEE80
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
