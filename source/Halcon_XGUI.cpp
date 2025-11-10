#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"
#include "HalconCpp.h"
using namespace HalconCpp;
Herror HXWnd_Create(Hproc_handle proc_handle)
{
    Hcpar windows_title;
    Hcpar XCStyle;
    Hcpar X;
    Hcpar Y;
    Hcpar CX;
    Hcpar CY;
    HGetSPar(proc_handle, 1, LONG_PAR, &X, 1);
    HGetSPar(proc_handle, 2, LONG_PAR, &Y, 1);
    HGetSPar(proc_handle, 3, LONG_PAR, &CX, 1);
    HGetSPar(proc_handle, 4, LONG_PAR, &CY, 1);
    HGetSPar(proc_handle, 5, STRING_PAR, &windows_title, 1);
    HGetSPar(proc_handle, 6, LONG_PAR, &XCStyle, 1);
    // Def_INHWINDOWObject(7, pHWINDOW);
    Def_OUTHWINDOWObject(1, pUserData);
    const wchar_t *title = XC_utf8tow(windows_title.par.s);
    (*pUserData)->HWINDOWCtx = XWnd_Create(X.par.l, Y.par.l, CX.par.l, CY.par.l, title, NULL, XCStyle.par.l); // 创建窗口
    if ((*pUserData)->HWINDOWCtx){return H_MSG_TRUE;} else{return H__LINE__;}
}