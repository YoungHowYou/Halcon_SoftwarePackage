#pragma once
#include "Halcon.h"
#include "modbus.h"
#include "sqlite3.h"
#include "xcgui.h"

#define H_Sqlite_TAG 0xC0FFEE40
#define H_Sqlite_SEM_TYPE "Sqlite"
//这是一个用于加载或保存数据库的函数。它接受一个内存数据库指针 pInMemory、一个数据库文件名 zFilename 和一个标志位 isSave。
// 若 isSave 为真，则将内存数据库保存到文件；若 isSave 为假，则将文件加载到内存数据库。
int loadOrSaveDb(sqlite3* pInMemeory, const char* zFilename, int isSave);
extern "C"
{
    typedef struct
    {
        sqlite3* SQLLiteDB;
        char DBPath[128];

    } SqliteHUserHandleData;

    static Herror SqliteHUserHandleDestructor(Hproc_handle ph, SqliteHUserHandleData *data)
    {
        int rev;
        if(strcmp(data->DBPath, ":memory:")==0)
        {
            rev= loadOrSaveDb(data->SQLLiteDB, "./memory.db", 1);
        }
        else
        {
            rev= sqlite3_close(data->SQLLiteDB);
        }


        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo SqliteHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_Sqlite_TAG, H_Sqlite_SEM_TYPE, SqliteHUserHandleDestructor, NULL, NULL);
}
#define pModule SQLLiteDB
#define Def_INSqliteObject(pos, pUserData)  SqliteHUserHandleData *(pUserData);      \
                                            HGetCElemH1(proc_handle, (pos), &SqliteHandleTypeUser, &(pUserData))
//#define INSqliteObject(pUserData)           ((pUserData)->pModule)

#define Def_OUTSqliteObject(pos, pUserData) SqliteHUserHandleData **(pUserData);                                           \
                                            HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &SqliteHandleTypeUser)); \
                                            HCkP(HAlloc(proc_handle, sizeof(SqliteHUserHandleData), (void **)(pUserData)))
#define OUTSqliteObject(pUserData)          (*(pUserData))
