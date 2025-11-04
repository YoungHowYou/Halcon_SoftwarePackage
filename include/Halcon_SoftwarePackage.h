#include "Halcon.h"
#define EXPORTS_API __declspec(dllexport)
#define 正确 2
#ifdef __cplusplus
extern "C" {
#endif
#pragma region sqlite
	extern EXPORTS_API Herror Hsqlite3_open(Hproc_handle proc_handle);
	extern EXPORTS_API Herror Hsqlite3_close(Hproc_handle proc_handle);
	extern EXPORTS_API Herror Hsqlite3_exec(Hproc_handle proc_handle);
	extern EXPORTS_API Herror Hsqlite3_exec_callback(Hproc_handle proc_handle);
	extern EXPORTS_API Herror Hsqlite3_loadOrSaveDb(Hproc_handle proc_handle);
#pragma endregion



#ifdef __cplusplus
}
#endif 


