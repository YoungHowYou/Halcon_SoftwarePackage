#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"
#include "HalconCpp.h"
using namespace HalconCpp;

Herror Hmodbus_rtu_connect(Hproc_handle proc_handle)
{
	Hcpar ctxrtuIndex;
	Hcpar device;
	Hcpar baud;
	Hcpar parity;
	Hcpar data_bit;
	Hcpar stop_bit;
	Hcpar mode;
	HAllocStringMem(proc_handle, 32);
	HGetSPar(proc_handle, 1, STRING_PAR, &device, 1);
	HGetSPar(proc_handle, 2, LONG_PAR, &baud, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &parity, 1);
	HGetSPar(proc_handle, 4, LONG_PAR, &data_bit, 1);
	HGetSPar(proc_handle, 5, LONG_PAR, &stop_bit, 1);
	HGetSPar(proc_handle, 6, LONG_PAR, &mode, 1);
	Def_OUTModbusObject(1, pUserData);

	// char paritys = parity.par.s[0];
	(*pUserData)->modbusCtx = modbus_new_rtu(device.par.s, baud.par.l, (char)parity.par.l, data_bit.par.l, stop_bit.par.l);

	if (modbus_connect((*pUserData)->modbusCtx) == -1)
	{
		return 10000 * __LINE__;
	}
	if (modbus_rtu_set_serial_mode((*pUserData)->modbusCtx, mode.par.l) == -1)
	{
		return H__LINE__ * 10000;
	}
	return H_MSG_TRUE;
}
Herror Hmodbus_tcp_connect(Hproc_handle proc_handle)
{
	Hcpar ctxtcpIndex; // ID
	Hcpar ServerIP;	   // IP
	Hcpar SeverPort;   // Port

	HAllocStringMem(proc_handle, 16);

	HGetSPar(proc_handle, 1, STRING_PAR, &ServerIP, 1);
	HGetSPar(proc_handle, 2, LONG_PAR, &SeverPort, 1);
	Def_OUTModbusObject(1, pUserData);

	(*pUserData)->modbusCtx = modbus_new_tcp(ServerIP.par.s, SeverPort.par.l);

	// 返回值不为0链接失败
	if (modbus_connect((*pUserData)->modbusCtx) == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}
Herror Hmodbus_close(Hproc_handle proc_handle)
{
	Def_INModbusObject(1, pUserData);
	modbus_close(pUserData->modbusCtx);
	modbus_free(pUserData->modbusCtx);
	return H_MSG_TRUE;
}
Herror Hmodbus_set_slave_ID(Hproc_handle proc_handle)
{
	int set_slave_ID_Result;
	Hcpar ctxtcp_set_slave_ID;
	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &ctxtcp_set_slave_ID, 1);

	set_slave_ID_Result = modbus_set_slave(pUserData->modbusCtx, ctxtcp_set_slave_ID.par.l);
	if (set_slave_ID_Result == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_read_bits(Hproc_handle proc_handle)
{
	Hcpar Addressstrat;	  // 起始地址
	Hcpar AddresssLength; // 读取数据长度

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &AddresssLength, 1);
	uint8_t *Bits_Besult;
	// HMLLocTmp(proc_handle, Bits_Besult, AddresssLength.par.l);//会自动释放
	HAllocTmp(proc_handle, &Bits_Besult, AddresssLength.par.l);

	int Get_Bits_Besult = modbus_read_bits(pUserData->modbusCtx, Addressstrat.par.l, AddresssLength.par.l, Bits_Besult);
	if (Get_Bits_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	long *C;
	HAllocTmp(proc_handle, &C, AddresssLength.par.l * sizeof(long));
	for (size_t i = 0; i < AddresssLength.par.l; i++)
	{
		C[i] = Bits_Besult[i];
	}

	HPutElem(proc_handle, 1, C, AddresssLength.par.l, LONG_PAR);
	return H_MSG_TRUE;
}

Herror Hmodbus_write_bit(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar status;		// 状态

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &status, 1);

	int Write_Bit_Besult = modbus_write_bit(pUserData->modbusCtx, Addressstrat.par.l, status.par.l);
	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_write_bits(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	Hcpar *status;
	INT4_8 num_params;
	HGetPPar(proc_handle, 4, &status, &num_params);
	uint8_t *Bits_Besult;
	HAllocTmp(proc_handle, &Bits_Besult, num_params * sizeof(uint8_t));

	for (INT4_8 i = 0; i < num_params; i++)
	{
		Bits_Besult[i] = (INT4_8)status[i].par.l;
	}

	int Write_Bit_Besult = modbus_write_bits(pUserData->modbusCtx, Addressstrat.par.l, num_params, Bits_Besult);
	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_write_register(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar status;		// 状态

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &status, 1);

	int Write_Bit_Besult = modbus_write_register(pUserData->modbusCtx, Addressstrat.par.l, (uint16_t)status.par.l);
	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_write_registers(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	Hcpar *status;
	INT4_8 num_params;
	HGetPPar(proc_handle, 4, &status, &num_params);
	uint16_t *Bits_Besult;
	HAllocTmp(proc_handle, &Bits_Besult, num_params * sizeof(uint16_t));

	for (INT4_8 i = 0; i < num_params; i++)
	{
		Bits_Besult[i] = (uint16_t)status[i].par.l;
	}

	int Write_Bit_Besult = modbus_write_registers(pUserData->modbusCtx, Addressstrat.par.l, num_params, Bits_Besult);
	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_read_registers(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar Length;		// 起始地址

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &Length, 1);

	uint16_t *Read_Registers_Num;
	HAllocTmp(proc_handle, &Read_Registers_Num, Length.par.l * sizeof(uint16_t)); //

	int Read_Registers_Result = modbus_read_registers(pUserData->modbusCtx, Addressstrat.par.l, Length.par.l, Read_Registers_Num);

	if (Read_Registers_Result == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{

		long *C;
		HAllocTmp(proc_handle, &C, Length.par.l * sizeof(long));
		for (size_t i = 0; i < Length.par.l; i++)
		{
			C[i] = Read_Registers_Num[i];
		}

		HPutElem(proc_handle, 1, C, Length.par.l, LONG_PAR);
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_write_register_float(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar status;		// 状态
	Hcpar EncodingMode;
	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, DOUBLE_PAR, &status, 1);
	HGetSPar(proc_handle, 4, STRING_PAR, &EncodingMode, 1);

	uint16_t dest[2];
	if (strcmp(EncodingMode.par.s, "abcd") == 0)
	{
		modbus_set_float_abcd((float)status.par.d, dest);
	}
	else if (strcmp(EncodingMode.par.s, "dcba") == 0)
	{
		modbus_set_float_dcba((float)status.par.d, dest);
	}
	else if (strcmp(EncodingMode.par.s, "badc") == 0)
	{
		modbus_set_float_badc((float)status.par.d, dest);
	}
	else if (strcmp(EncodingMode.par.s, "cdab") == 0)
	{
		modbus_set_float_cdab((float)status.par.d, dest);
	}
	else
	{
		modbus_set_float((float)status.par.d, dest);
	}

	int Write_Bit_Besult = modbus_write_registers(pUserData->modbusCtx, (int)Addressstrat.par.l, 2, dest);
	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_read_register_float(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar EncodingMode;

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, STRING_PAR, &EncodingMode, 1);

	uint16_t Read_Registers_Num[2];

	int Read_Registers_Result = modbus_read_registers(pUserData->modbusCtx, Addressstrat.par.l, 2, Read_Registers_Num);

	if (Read_Registers_Result == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		float C;
		if (strcmp(EncodingMode.par.s, "abcd") == 0)
		{
			C = modbus_get_float_abcd(Read_Registers_Num);
		}
		else if (strcmp(EncodingMode.par.s, "dcba") == 0)
		{
			C = modbus_get_float_dcba(Read_Registers_Num);
		}
		else if (strcmp(EncodingMode.par.s, "badc") == 0)
		{
			C = modbus_get_float_badc(Read_Registers_Num);
		}
		else if (strcmp(EncodingMode.par.s, "cdab") == 0)
		{
			C = modbus_get_float_cdab(Read_Registers_Num);
		}
		else
		{
			C = modbus_get_float(Read_Registers_Num);
		}

		HPutElem(proc_handle, 1, &C, 1, DOUBLE_PAR);
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_write_register_int(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar status;		// 状态
	Hcpar BitDepth;
	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &status, 1);
	HGetSPar(proc_handle, 4, LONG_PAR, &BitDepth, 1);

	int Write_Bit_Besult;

	if (BitDepth.par.l == 32)
	{
		uint16_t regs32[2];
		MODBUS_SET_INT32_TO_INT16(regs32, 0, status.par.l);
		Write_Bit_Besult = modbus_write_registers(pUserData->modbusCtx, (int)Addressstrat.par.l, 2, regs32);
	}
	else
	{
		uint16_t regs64[4];
		MODBUS_SET_INT64_TO_INT16(regs64, 0, status.par.l);
		Write_Bit_Besult = modbus_write_registers(pUserData->modbusCtx, (int)Addressstrat.par.l, 4, regs64);
	}

	if (Write_Bit_Besult == -1)
	{
		return H__LINE__ * 10000;
	}
	else
	{
		return H_MSG_TRUE;
	}
}

Herror Hmodbus_read_register_int(Hproc_handle proc_handle)
{
	Hcpar Addressstrat; // 起始地址
	Hcpar BitDepth;

	Def_INModbusObject(1, pUserData);
	HGetSPar(proc_handle, 2, LONG_PAR, &Addressstrat, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &BitDepth, 1);
	int Read_Registers_Result;

	if (BitDepth.par.l == 32)
	{
		uint16_t regs32[2];
		Read_Registers_Result = modbus_read_registers(pUserData->modbusCtx, Addressstrat.par.l, 2, regs32);
		if (Read_Registers_Result == -1)
		{
			return H__LINE__ * 10000;
		}
		else
		{
			int32_t val = MODBUS_GET_INT32_FROM_INT16(regs32, 0);
			int64_t C=(int64_t)val;
			HPutElem(proc_handle, 1, &C, 1, LONG_PAR);
			return H_MSG_TRUE;
		}
	}
	else
	{
		uint16_t regs64[4];
		Read_Registers_Result = modbus_read_registers(pUserData->modbusCtx, Addressstrat.par.l, 4, regs64);
		if (Read_Registers_Result == -1)
		{
			return H__LINE__ * 10000;
		}
		else
		{
			int64_t val = MODBUS_GET_INT64_FROM_INT16(regs64, 0);
			HPutElem(proc_handle, 1, &val, 1, LONG_PAR);
			return H_MSG_TRUE;
		}
	}
}
