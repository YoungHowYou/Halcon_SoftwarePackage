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
	//Hcpar  StrKey;

	Hkey      in_smallobj_key;
	Himage    insmallimage;
	//HAllocStringMem(proc_handle, 32768);
	HGetObj(proc_handle, 1, 1, &in_smallobj_key);
	HGetDImage(proc_handle, in_smallobj_key, 1, &insmallimage);
	int Len=*(int*)insmallimage.pixel.b;
	//memcpy(&Len, insmallimage.pixel.b, 4);
	char* msg;
	//HAllocTmp(proc_handle, &msg, Len + 1);
	HAlloc(proc_handle, Len + 1, &msg);
	//memcpy(&msgS, insmallimage.pixel.b+4, Len);
	//String msg = msgS;
	strcpy(msg, (char*)insmallimage.pixel.b + 4);
	//HPutElem(proc_handle, 1, &msg, 1, STRING_PAR);
	//HFreeTmp(proc_handle, &msg);
    HPutPElem(proc_handle, 1, &msg, 1, STRING_PAR);

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
#pragma endregion