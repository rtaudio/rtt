#include "rtt.h"



#ifdef _WIN32
#include <windows.h>

void usleep(unsigned int usec)
{ 
	if( (usec % 1000) == 0) {
		Sleep(usec /1000);
		return;
	}

    HANDLE timer; 
    LARGE_INTEGER ft; 
  
    ft.QuadPart = -(10 * (__int64)usec); 
  
	//Timer Funktionen ab WINNT verfügbar 
    timer = CreateWaitableTimer(NULL, TRUE, NULL); 
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0); 
    WaitForSingleObject(timer, INFINITE); 
    CloseHandle(timer); 
}


#endif