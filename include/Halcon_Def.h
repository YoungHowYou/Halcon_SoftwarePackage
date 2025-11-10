#pragma once
#include "Halcon.h"
#include "modbus.h"
#include "sqlite3.h"
#include "xcgui.h"
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

#pragma region XGUI

#define H_HXCGUI_TAG 0xC0FFEE50
#define H_HXCGUI_SEM_TYPE "HXCGUI"
extern "C"
{
    typedef struct
    {
        HXCGUI* HXCGUICtx;

    } HXCGUIHUserHandleData;

    static Herror HXCGUIHUserHandleDestructor(Hproc_handle ph, HXCGUIHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HXCGUIHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HXCGUI_TAG, H_HXCGUI_SEM_TYPE, HXCGUIHUserHandleDestructor, NULL, NULL);
}
#define Def_INHXCGUIObject(pos, pUserData) \
    HXCGUIHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HXCGUIHandleTypeUser, &(pUserData))

#define Def_OUTHXCGUIObject(pos, pUserData)                                        \
    HXCGUIHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HXCGUIHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HXCGUIHUserHandleData), (void **)(pUserData)))
#define OUTHXCGUIObject(pUserData) (*(pUserData))



#define H_HWINDOW_TAG 0xC0FFEE60
#define H_HWINDOW_SEM_TYPE "HWINDOW"
extern "C"
{
    typedef struct
    {
        HWINDOW HWINDOWCtx;

    } HWINDOWHUserHandleData;

    static Herror HWINDOWHUserHandleDestructor(Hproc_handle ph, HWINDOWHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HWINDOWHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HWINDOW_TAG, H_HWINDOW_SEM_TYPE, HWINDOWHUserHandleDestructor, NULL, NULL);
}
#define Def_INHWINDOWObject(pos, pUserData) \
    HWINDOWHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HWINDOWHandleTypeUser, &(pUserData))

#define Def_OUTHWINDOWObject(pos, pUserData)                                        \
    HWINDOWHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HWINDOWHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HWINDOWHUserHandleData), (void **)(pUserData)))
#define OUTHWINDOWObject(pUserData) (*(pUserData))

#define H_HELE_TAG 0xC0FFEE70
#define H_HELE_SEM_TYPE "HELE"
extern "C"
{
    typedef struct
    {
        HELE HELECtx;

    } HELEHUserHandleData;

    static Herror HELEHUserHandleDestructor(Hproc_handle ph, HELEHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HELEHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HELE_TAG, H_HELE_SEM_TYPE, HELEHUserHandleDestructor, NULL, NULL);
}
#define Def_INHELEObject(pos, pUserData) \
    HELEHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HELEHandleTypeUser, &(pUserData))

#define Def_OUTHELEObject(pos, pUserData)                                        \
    HELEHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HELEHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HELEHUserHandleData), (void **)(pUserData)))
#define OUTHELEObject(pUserData) (*(pUserData))

#define H_HMENUX_TAG 0xC0FFEE80
#define H_HMENUX_SEM_TYPE "HMENUX"
extern "C"
{
    typedef struct
    {
        HMENUX HMENUXCtx;

    } HMENUXHUserHandleData;

    static Herror HMENUXHUserHandleDestructor(Hproc_handle ph, HMENUXHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HMENUXHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HMENUX_TAG, H_HMENUX_SEM_TYPE, HMENUXHUserHandleDestructor, NULL, NULL);
}
#define Def_INHMENUXObject(pos, pUserData) \
    HMENUXHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HMENUXHandleTypeUser, &(pUserData))

#define Def_OUTHMENUXObject(pos, pUserData)                                        \
    HMENUXHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HMENUXHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HMENUXHUserHandleData), (void **)(pUserData)))
#define OUTHMENUXObject(pUserData) (*(pUserData))

#define H_HIMAGE_TAG 0xC0FFEE90
#define H_HIMAGE_SEM_TYPE "HIMAGE"
extern "C"
{
    typedef struct
    {
        HIMAGE HIMAGECtx;

    } HIMAGEHUserHandleData;

    static Herror HIMAGEHUserHandleDestructor(Hproc_handle ph, HIMAGEHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HIMAGEHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HIMAGE_TAG, H_HIMAGE_SEM_TYPE, HIMAGEHUserHandleDestructor, NULL, NULL);
}
#define Def_INHIMAGEObject(pos, pUserData) \
    HIMAGEHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HIMAGEHandleTypeUser, &(pUserData))

#define Def_OUTHIMAGEObject(pos, pUserData)                                        \
    HIMAGEHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HIMAGEHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HIMAGEHUserHandleData), (void **)(pUserData)))
#define OUTHIMAGEObject(pUserData) (*(pUserData))

#define H_HFONTX_TAG 0xC0FFEEA0
#define H_HFONTX_SEM_TYPE "HFONTX"
extern "C"
{
    typedef struct
    {
        HFONTX HFONTXCtx;

    } HFONTXHUserHandleData;

    static Herror HFONTXHUserHandleDestructor(Hproc_handle ph, HFONTXHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HFONTXHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HFONTX_TAG, H_HFONTX_SEM_TYPE, HFONTXHUserHandleDestructor, NULL, NULL);
}
#define Def_INHFONTXObject(pos, pUserData) \
    HFONTXHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HFONTXHandleTypeUser, &(pUserData))

#define Def_OUTHFONTXObject(pos, pUserData)                                        \
    HFONTXHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HFONTXHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HFONTXHUserHandleData), (void **)(pUserData)))
#define OUTHFONTXObject(pUserData) (*(pUserData))

#define H_HBKM_TAG 0xC0FFEEB0
#define H_HBKM_SEM_TYPE "HBKM"
extern "C"
{
    typedef struct
    {
        HBKM HBKMCtx;

    } HBKMHUserHandleData;

    static Herror HBKMHUserHandleDestructor(Hproc_handle ph, HBKMHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HBKMHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HBKM_TAG, H_HBKM_SEM_TYPE, HBKMHUserHandleDestructor, NULL, NULL);
}
#define Def_INHBKMObject(pos, pUserData) \
    HBKMHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HBKMHandleTypeUser, &(pUserData))

#define Def_OUTHBKMObject(pos, pUserData)                                        \
    HBKMHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HBKMHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HBKMHUserHandleData), (void **)(pUserData)))
#define OUTHBKMObject(pUserData) (*(pUserData))

#define H_HTEMP_TAG 0xC0FFEEC0
#define H_HTEMP_SEM_TYPE "HTEMP"
extern "C"
{
    typedef struct
    {
        HTEMP HTEMPCtx;

    } HTEMPHUserHandleData;

    static Herror HTEMPHUserHandleDestructor(Hproc_handle ph, HTEMPHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HTEMPHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HTEMP_TAG, H_HTEMP_SEM_TYPE, HTEMPHUserHandleDestructor, NULL, NULL);
}
#define Def_INHTEMPObject(pos, pUserData) \
    HTEMPHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HTEMPHandleTypeUser, &(pUserData))

#define Def_OUTHTEMPObject(pos, pUserData)                                        \
    HTEMPHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HTEMPHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HTEMPHUserHandleData), (void **)(pUserData)))
#define OUTHTEMPObject(pUserData) (*(pUserData))

#define H_HSVG_TAG 0xC0FFEED0
#define H_HSVG_SEM_TYPE "HSVG"
extern "C"
{
    typedef struct
    {
        HSVG HSVGCtx;

    } HSVGHUserHandleData;

    static Herror HSVGHUserHandleDestructor(Hproc_handle ph, HSVGHUserHandleData *data)
    {
        
        return HFree(ph, data);
    }
    // 句柄类型描述符
    const HHandleInfo HSVGHandleTypeUser = HANDLE_INFO_INITIALIZER_NOSER(H_HSVG_TAG, H_HSVG_SEM_TYPE, HSVGHUserHandleDestructor, NULL, NULL);
}
#define Def_INHSVGObject(pos, pUserData) \
    HSVGHUserHandleData *(pUserData);    \
    HGetCElemH1(proc_handle, (pos), &HSVGHandleTypeUser, &(pUserData))

#define Def_OUTHSVGObject(pos, pUserData)                                        \
    HSVGHUserHandleData **(pUserData);                                           \
    HCkP(HAllocOutputHandle(proc_handle, 1, &(pUserData), &HSVGHandleTypeUser)); \
    HCkP(HAlloc(proc_handle, sizeof(HSVGHUserHandleData), (void **)(pUserData)))
#define OUTHSVGObject(pUserData) (*(pUserData))
#pragma endregion
