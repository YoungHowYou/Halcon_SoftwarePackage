#include <stdio.h>
#include "Halcon_SoftwarePackage.h"

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