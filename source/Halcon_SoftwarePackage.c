#include <stdio.h>
#include "Halcon_SoftwarePackage.h"
#pragma region StringByImage

Herror HSetStringByImageIn(Hproc_handle proc_handle)
{
	//Hcpar  StrKey;
	Hkey  out_obj_key, out_image_key;

	//HAllocStringMem(proc_handle, 32768);
	//HGetSPar(proc_handle, 1, STRING_PAR, &StrKey, 1);

	char const* const* strPtrArr;   // 指向 HALCON 内部指针数组
	INT4_8           strCount;      // 实际字符串个数
	// 获取整个 STRING 数组（不拷贝）
	HGetPElemS(proc_handle, 1, CONV_NONE, &strPtrArr, &strCount);
	if(strCount != 1 )
	{
		return H__LINE__; // 没有字符串，直接返回
	}

	Himage outimage;
	int Height = 1;
	int Width = 1024;
	//char* II = StrKey.par.s;
	int string_len = strlen(strPtrArr[0]);
	//32768
	if ((string_len + 4) > 1024)
	{
		//Height = HTuple(string_len / 1024).TupleCeil();
		Height = ceil((string_len + 4)/1024.0) ;
	}
	else
	{
		Width = (string_len + 4) + 1;
	}
	HCkP(HNewImage(proc_handle, &outimage, BYTE_IMAGE, Width, Height));
	memcpy(outimage.pixel.b, &string_len, 4);
	memcpy(outimage.pixel.b + 4,strPtrArr[0], string_len);
	/***********************************************/
	HCrObj(proc_handle, 1, &out_obj_key);
	HPutDImage(proc_handle, out_obj_key, 1, &outimage, FALSE, &out_image_key);//图像输出
	HPutRect(proc_handle, out_obj_key, outimage.width, outimage.height);
	return H_MSG_TRUE;
}

Herror HGetStringByImageOut(Hproc_handle proc_handle)
{
	Hkey      in_smallobj_key;
	Himage    insmallimage;
	HAllocStringMem(proc_handle, 32768);
	HGetObj(proc_handle, 1, 1, &in_smallobj_key);
	HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
	int Len;
	memcpy(&Len, insmallimage.pixel.b, 4);
	char* msg;
	HAllocTmp(proc_handle, &msg, Len + 1);
	//memcpy(&msgS, insmallimage.pixel.b+4, Len);
	//String msg = msgS;
	strcpy(msg, (char*)insmallimage.pixel.b + 4);
	HPutElem(proc_handle, 1, &msg, 1, STRING_PAR);
	//HFreeTmp(proc_handle, &msg);

	return H_MSG_TRUE;
}
#pragma endregion
#pragma region sqlite

Herror CHsqlite3_open(Hproc_handle proc_handle)
{

	return Hsqlite3_open( proc_handle);
}
Herror CHsqlite3_close(Hproc_handle proc_handle)
{
   
	return Hsqlite3_close( proc_handle);
}


Herror CHsqlite3_exec(Hproc_handle proc_handle)
{
   
	return Hsqlite3_exec( proc_handle);


}

Herror CHsqlite3_exec_callback(Hproc_handle proc_handle)
{

	return Hsqlite3_exec_callback( proc_handle);


}
Herror CHsqlite3_loadOrSaveDb(Hproc_handle proc_handle)
{

	return Hsqlite3_loadOrSaveDb( proc_handle);


}


#pragma endregion

#pragma region modbus
Herror CHmodbus_rtu_connect(Hproc_handle proc_handle)
{

	return Hmodbus_rtu_connect(proc_handle);
}
Herror CHmodbus_tcp_connect(Hproc_handle proc_handle)
{

	return Hmodbus_tcp_connect(proc_handle);
}
Herror CHmodbus_close(Hproc_handle proc_handle)
{
	return Hmodbus_close(proc_handle);
}
Herror CHmodbus_set_slave_ID(Hproc_handle proc_handle)
{
	return Hmodbus_set_slave_ID(proc_handle);
}

Herror CHmodbus_read_bits(Hproc_handle proc_handle)
{
	return Hmodbus_read_bits(proc_handle);
}

Herror CHmodbus_write_bit(Hproc_handle proc_handle)
{
	return Hmodbus_write_bit(proc_handle);
}

Herror CHmodbus_write_bits(Hproc_handle proc_handle)
{
	return Hmodbus_write_bits(proc_handle);
}

Herror CHmodbus_write_register(Hproc_handle proc_handle)
{
	return Hmodbus_write_register(proc_handle);
}

Herror CHmodbus_write_registers(Hproc_handle proc_handle)
{
	return Hmodbus_write_registers(proc_handle);
}

Herror CHmodbus_read_registers(Hproc_handle proc_handle)
{
	return Hmodbus_read_registers(proc_handle);
}

Herror CHmodbus_write_register_float(Hproc_handle proc_handle)
{
    return Hmodbus_write_register_float(proc_handle);
}

Herror CHmodbus_read_register_float(Hproc_handle proc_handle)
{
    return Hmodbus_read_register_float(proc_handle);
}

Herror CHmodbus_write_register_int(Hproc_handle proc_handle)
{
    return Hmodbus_write_register_int(proc_handle);
}

Herror CHmodbus_read_register_int(Hproc_handle proc_handle)
{
    return Hmodbus_read_register_int(proc_handle);
}
Herror CHmodbus_read_inputbits(Hproc_handle proc_handle)
{
	return Hmodbus_read_inputbits( proc_handle);
}
Herror CHmodbus_strerror(Hproc_handle proc_handle)
{
	return Hmodbus_strerror( proc_handle);
}

#pragma endregion

#pragma region spdlog
Herror CHspdlog_init_thread_pool(Hproc_handle proc_handle)
{
	return Hspdlog_init_thread_pool(proc_handle);
}
Herror CHspdlog_basic_logger_mt(Hproc_handle proc_handle)
{
	return Hspdlog_basic_logger_mt(proc_handle);
}
Herror CHspdlog_rotating_logger_mt(Hproc_handle proc_handle)
{
	return Hspdlog_rotating_logger_mt(proc_handle);
}
Herror CHspdlog_daily_logger_mt(Hproc_handle proc_handle)
{
	return Hspdlog_daily_logger_mt(proc_handle);
}
Herror CHspdlog_stdout_color_mt(Hproc_handle proc_handle)
{
	return Hspdlog_stdout_color_mt(proc_handle);
}
Herror CHspdlog_get(Hproc_handle proc_handle)
{
	return Hspdlog_get(proc_handle);
}
Herror CHspdlog_log(Hproc_handle proc_handle)
{
	return Hspdlog_log(proc_handle);
}
Herror CHspdlog_trace(Hproc_handle proc_handle)
{
	return Hspdlog_trace(proc_handle);
}
Herror CHspdlog_debug(Hproc_handle proc_handle)
{
	return Hspdlog_debug(proc_handle);
}
Herror CHspdlog_info(Hproc_handle proc_handle)
{
	return Hspdlog_info(proc_handle);
}
Herror CHspdlog_warn(Hproc_handle proc_handle)
{
	return Hspdlog_warn(proc_handle);
}
Herror CHspdlog_err(Hproc_handle proc_handle)
{
	return Hspdlog_err(proc_handle);
}
Herror CHspdlog_critical(Hproc_handle proc_handle)
{
	return Hspdlog_critical(proc_handle);
}
Herror CHspdlog_set_level(Hproc_handle proc_handle)
{
	return Hspdlog_set_level(proc_handle);
}
Herror CHspdlog_set_pattern(Hproc_handle proc_handle)
{
	return Hspdlog_set_pattern(proc_handle);
}
Herror CHspdlog_flush(Hproc_handle proc_handle)
{
	return Hspdlog_flush(proc_handle);
}
Herror CHspdlog_flush_on(Hproc_handle proc_handle)
{
	return Hspdlog_flush_on(proc_handle);
}
Herror CHspdlog_drop(Hproc_handle proc_handle)
{
	return Hspdlog_drop(proc_handle);
}
Herror CHspdlog_drop_all(Hproc_handle proc_handle)
{
	return Hspdlog_drop_all(proc_handle);
}
Herror CHspdlog_shutdown(Hproc_handle proc_handle)
{
	return Hspdlog_shutdown(proc_handle);
}
#pragma endregion

#pragma region mysql
Herror CHmysql_real_connect(Hproc_handle proc_handle)
{
	return Hmysql_real_connect(proc_handle);
}
Herror CHmysql_query(Hproc_handle proc_handle)
{
	return Hmysql_query(proc_handle);
}
Herror CHmysql_store_result(Hproc_handle proc_handle)
{
	return Hmysql_store_result(proc_handle);
}
#pragma endregion

#pragma region opencv_exiv2
Herror CHCremap(Hproc_handle proc_handle)
{
    return HCremap(proc_handle);
}

Herror CHPNGOut(Hproc_handle proc_handle)
{
    return HPNGOut(proc_handle);
}

Herror CHPNGIn(Hproc_handle proc_handle)
{
    return HPNGIn(proc_handle);
}

Herror Cadd_roi(Hproc_handle proc_handle)
{
    return HCadd_roi(proc_handle);
}

Herror Cmul_roi(Hproc_handle proc_handle)
{
    return HCmul_roi(proc_handle);
}

Herror Csub_B_roi(Hproc_handle proc_handle)
{
    return HCsub_B_roi(proc_handle);
}

Herror Cdiv_B_roi(Hproc_handle proc_handle)
{
    return HCdiv_B_roi(proc_handle);
}

Herror Cdiv_A_roi(Hproc_handle proc_handle)
{
    return HCdiv_A_roi(proc_handle);
}

Herror Csub_A_roi(Hproc_handle proc_handle)
{
    return HCsub_A_roi(proc_handle);
}

Herror CCLAHE_image(Hproc_handle proc_handle)
{
    return HCCLAHE_image(proc_handle);
}

Herror CWriteImageExif(Hproc_handle proc_handle)
{
    return HCWriteImageExif(proc_handle);
}

Herror Ccv_orb_detect(Hproc_handle proc_handle)
{
    return HCcv_orb_detect(proc_handle);
}

Herror Ccv_akaze_detect(Hproc_handle proc_handle)
{
    return HCcv_akaze_detect(proc_handle);
}

Herror Ccv_bf_knn_match(Hproc_handle proc_handle)
{
    return HCcv_bf_knn_match(proc_handle);
}

Herror Ccv_estimate_affine_partial2d(Hproc_handle proc_handle)
{
    return HCcv_estimate_affine_partial2d(proc_handle);
}

Herror Ccv_write_image(Hproc_handle proc_handle)
{
    return HCcv_write_image(proc_handle);
}
#pragma endregion