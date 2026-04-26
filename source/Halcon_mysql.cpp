#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"
#include "HalconCpp.h"
using namespace HalconCpp;

static const char *EmptyToNull(const char *s)
{
	return (s && s[0] != '\0') ? s : NULL;
}

Herror Hmysql_real_connect(Hproc_handle proc_handle)
{
	Hcpar host, user, passwd, db, port, unix_socket, client_flag;
	HAllocStringMem(proc_handle, 1024);
	HGetSPar(proc_handle, 1, STRING_PAR, &host, 1);
	HGetSPar(proc_handle, 2, STRING_PAR, &user, 1);
	HGetSPar(proc_handle, 3, STRING_PAR, &passwd, 1);
	HGetSPar(proc_handle, 4, STRING_PAR, &db, 1);
	HGetSPar(proc_handle, 5, LONG_PAR, &port, 1);
	HGetSPar(proc_handle, 6, STRING_PAR, &unix_socket, 1);
	HGetSPar(proc_handle, 7, LONG_PAR, &client_flag, 1);

	Def_OUTMysqlObject(1, pUserData);

	(*pUserData)->mysqlCtx = mysql_init(NULL);
	if ((*pUserData)->mysqlCtx == NULL)
	{
		const char *error = "mysql_init failed";
		HPutElem(proc_handle, 2, &error, 1, STRING_PAR);
		return H_MSG_TRUE;
	}

	if (mysql_real_connect((*pUserData)->mysqlCtx,
						   EmptyToNull(host.par.s),
						   EmptyToNull(user.par.s),
						   EmptyToNull(passwd.par.s),
						   EmptyToNull(db.par.s),
						   (unsigned int)port.par.l,
						   EmptyToNull(unix_socket.par.s),
						   (unsigned long)client_flag.par.l) == NULL)
	{
		const char *error = mysql_error((*pUserData)->mysqlCtx);
		HPutElem(proc_handle, 2, &error, 1, STRING_PAR);
		return H_MSG_TRUE;
	}

	const char *ok = "OK";
	HPutElem(proc_handle, 2, &ok, 1, STRING_PAR);
	return H_MSG_TRUE;
}

Herror Hmysql_query(Hproc_handle proc_handle)
{
	Hcpar SQL;
	HAllocStringMem(proc_handle, 1024);
	Def_INMysqlObject(1, pUserData);
	HGetSPar(proc_handle, 2, STRING_PAR, &SQL, 1);

	int Ret = mysql_query(pUserData->mysqlCtx, SQL.par.s);
	if (Ret != 0)
	{
		const char *error = mysql_error(pUserData->mysqlCtx);
		HPutElem(proc_handle, 1, &error, 1, STRING_PAR);
		return H_MSG_TRUE;
	}
	const char *ok = "OK";
	HPutElem(proc_handle, 1, &ok, 1, STRING_PAR);
	return H_MSG_TRUE;
}

Herror Hmysql_store_result(Hproc_handle proc_handle)
{
	HAllocStringMem(proc_handle, 1024);
	Def_INMysqlObject(1, pUserData);

	MYSQL_RES *result = mysql_store_result(pUserData->mysqlCtx);
	if (result == NULL)
	{
		const char *SearchResult = "NoData";
		HPutElem(proc_handle, 1, &SearchResult, 1, STRING_PAR);
		return H_MSG_TRUE;
	}

	my_ulonglong rowCount = mysql_num_rows(result);
	unsigned int fieldCount = mysql_num_fields(result);
	size_t total = (size_t)rowCount * (size_t)fieldCount;

	if (total == 0)
	{
		mysql_free_result(result);
		const char *SearchResult = "NoData";
		HPutElem(proc_handle, 1, &SearchResult, 1, STRING_PAR);
		return H_MSG_TRUE;
	}

	char **Buffer = NULL;
	HCkP(HAllocTmp(proc_handle, (char **)&Buffer, (INT4_8)(sizeof(char *) * total)));

	size_t n = 0;
	MYSQL_ROW row;
	while ((row = mysql_fetch_row(result)) != NULL && n < total)
	{
		for (unsigned int c = 0; c < fieldCount && n < total; ++c)
		{
			Buffer[n++] = row[c] ? row[c] : (char *)"";
		}
	}

	HPutElem(proc_handle, 1, Buffer, (INT4_8)n, STRING_PAR);
	mysql_free_result(result);
	return H_MSG_TRUE;
}
