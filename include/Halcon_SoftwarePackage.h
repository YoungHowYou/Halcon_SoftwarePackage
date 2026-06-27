#include "Halcon.h"
#if defined(_WIN32) || defined(_WIN64)
#define EXPORTS_API __declspec(dllexport)
#else
#define EXPORTS_API __attribute__((visibility("default")))
#endif
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
extern EXPORTS_API Herror Hmodbus_write_register_float(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_read_register_float(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_write_register_int(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_read_register_int(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmodbus_read_inputbits(Hproc_handle proc_handle);	
extern EXPORTS_API Herror Hmodbus_strerror(Hproc_handle proc_handle);

#pragma endregion

#pragma region spdlog
extern EXPORTS_API Herror Hspdlog_init_thread_pool(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_basic_logger_mt(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_rotating_logger_mt(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_daily_logger_mt(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_stdout_color_mt(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_get(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_log(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_trace(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_debug(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_info(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_warn(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_err(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_critical(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_set_level(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_set_pattern(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_flush(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_flush_on(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_drop(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_drop_all(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hspdlog_shutdown(Hproc_handle proc_handle);
#pragma endregion

#pragma region mysql
extern EXPORTS_API Herror Hmysql_real_connect(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmysql_query(Hproc_handle proc_handle);
extern EXPORTS_API Herror Hmysql_store_result(Hproc_handle proc_handle);
#pragma endregion

#pragma region opencv_exiv2
extern EXPORTS_API Herror HCremap(Hproc_handle proc_handle);
extern EXPORTS_API Herror HPNGOut(Hproc_handle proc_handle);
extern EXPORTS_API Herror HPNGIn(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCadd_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCmul_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCsub_B_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCdiv_B_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCdiv_A_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCsub_A_roi(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCCLAHE_image(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCWriteImageExif(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCcv_orb_detect(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCcv_akaze_detect(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCcv_bf_knn_match(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCcv_estimate_affine_partial2d(Hproc_handle proc_handle);
extern EXPORTS_API Herror HCcv_write_image(Hproc_handle proc_handle);
#pragma endregion


#ifdef __cplusplus
}
#endif 


