#include "Halcon_Def.h"
#include "Halcon_SoftwarePackage.h"
#include "HalconCpp.h"
using namespace HalconCpp;

int loadOrSaveDb(sqlite3* pInMemeory, const char* zFilename, int isSave)
{

	int rc;

	sqlite3* pFile;

	sqlite3_backup* pBackup;

	sqlite3* pTo;

	sqlite3* pFrom;

	rc = sqlite3_open(zFilename, &pFile);

	if (rc == SQLITE_OK)

	{

		pFrom = (isSave ? pInMemeory : pFile);
		pTo = (isSave ? pFile : pInMemeory);

		pBackup = sqlite3_backup_init(pTo, "main", pFrom, "main");

		if (pBackup)
		{
			(void)sqlite3_backup_step(pBackup, -1);
			(void)sqlite3_backup_finish(pBackup);
		}

		rc = sqlite3_errcode(pTo);

	}

	(void)sqlite3_close(pFile);

	return rc;
	//
}

Herror Hsqlite3_open(Hproc_handle proc_handle)

{
	Hcpar filename;
	HAllocStringMem(proc_handle, 128);
	HGetSPar(proc_handle, 1, STRING_PAR, &filename, 1);
    Def_OUTSqliteObject(2, pUserData);
    //HGetSPar(proc_handle, 2, LONG_PAR, &DbNumber, 1);
	sqlite3* Db;
	int rev = sqlite3_open(filename.par.s, &Db);
	//SQLLiteDB[DbNumber.par.l] = Db;
    OUTSqliteObject(pUserData)->SQLLiteDB = Db; 
    //OUTSqliteObject(pUserData).DBPath= HStrDup(proc_handle, filename.par.s);
    strcpy(OUTSqliteObject(pUserData)->DBPath, filename.par.s); 
	return H_MSG_TRUE + rev * 10000;
}

Herror Hsqlite3_close(Hproc_handle proc_handle)
{
    Def_INSqliteObject(1,pUserData);
	int rev= sqlite3_close(pUserData->SQLLiteDB);
	return H_MSG_TRUE + rev * 10000;
}

Herror Hsqlite3_exec(Hproc_handle proc_handle)
{
	Hcpar sql, DbNumber;
	HAllocStringMem(proc_handle, 1024*5);
    Def_INSqliteObject(1,pUserData);
	HGetSPar(proc_handle, 2, STRING_PAR, &sql, 1);

	int rev;
	char* errmsg;
	rev = sqlite3_exec(pUserData->SQLLiteDB, sql.par.s, NULL, NULL, &errmsg);
	if (errmsg != NULL)
	{
	}
	else
	{
		errmsg = (char*)"OK";

	}
	HPutElem(proc_handle, 1, &errmsg, 1, STRING_PAR);

	return H_MSG_TRUE;

}

Herror Hsqlite3_exec_callback(Hproc_handle proc_handle)
{
	Hcpar sql, DbNumber, data;
	HAllocStringMem(proc_handle, 1024*5);
    Def_INSqliteObject(1,pUserData);
	HGetSPar(proc_handle, 2, STRING_PAR, &sql, 1);
	INT4_8 rc;
	char** table_names = NULL;
	//static 
	int Y=0;
	 //static 
	int X = 0;
	char* errmsg;
	rc = sqlite3_get_table(pUserData->SQLLiteDB, sql.par.s, &table_names, &Y, &X, &errmsg);
	 if (rc != SQLITE_OK) 
	{
		 HPutElem(proc_handle, 4, &errmsg, 1, STRING_PAR);
		 sqlite3_free_table(table_names);
		 return H_MSG_TRUE;
	}
	else
	{
		
			errmsg = (char*)"OK";
	}

	 if (X*Y > 0)
	 {
		 HPutElem(proc_handle, 1, table_names, (X * Y)+1, STRING_PAR);
		 INT64 Xs = (INT64)X;
		 INT64 Ys = (INT64)Y;

		 HPutElem(proc_handle, 2, &Xs, 1, INT_PAR);
		 HPutElem(proc_handle, 3, &Ys, 1, INT_PAR);
		 HPutElem(proc_handle, 4, &errmsg, 1, STRING_PAR);
		 sqlite3_free_table(table_names);
		 return H_MSG_TRUE;
	 }
	 else
	 {
		 errmsg = (char*)u8"无数据";
		 HPutElem(proc_handle, 4, &errmsg, 1, STRING_PAR);
		 sqlite3_free_table(table_names);
		 return H_MSG_TRUE;


	 }
	 return H_MSG_TRUE;

}

Herror Hsqlite3_loadOrSaveDb(Hproc_handle proc_handle)
{
	Hcpar zFilename, DbNumber, isSave;
	char* errmsg;
	HAllocStringMem(proc_handle,128);
    Def_INSqliteObject(1,pUserData);
	HGetSPar(proc_handle, 2, STRING_PAR, &zFilename, 1);
	HGetSPar(proc_handle, 3, LONG_PAR, &isSave, 1);
	if (DbNumber.par.l = 0) { return 99990; }
	INT4_8 rev;
	rev = loadOrSaveDb(pUserData->SQLLiteDB, zFilename.par.s, isSave.par.l);
	return H_MSG_TRUE + rev * 10000;


}