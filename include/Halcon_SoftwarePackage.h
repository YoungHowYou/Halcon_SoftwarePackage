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
#pragma region modbus

extern EXPORTS_API Herror Hmodbus_rtu_connect(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_tcp_connect(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_close(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_set_slave_ID(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_write_bit(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_write_bits(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_write_register(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_write_registers(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_read_registers(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_read_bits(Hproc_handle proc_handle);

#pragma endregion


#ifdef __cplusplus
}
#endif 


