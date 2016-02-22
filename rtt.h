#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdlib.h> 
#include <string.h>
#include <string>
#include <signal.h>
#include <functional>

#ifdef WIN32
#include<windows.h>
void usleep(unsigned int usec);
#endif

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#include <sys/resource.h>
#endif


#ifdef WIN32
#define THREAD_FUNC DWORD WINAPI
#else
#define THREAD_FUNC void *
#endif


static void _signal_handler_sigusr1(int sig) {
}

struct PlatformThread {

	enum Priorities {
		Low, Normal, High, RealTime,
	};

#ifdef WIN32
	HANDLE handle;
	DWORD id;
#else
	pthread_t handle;
	struct sched_param param;
#endif

	int joined;
	char name[32];

	typedef std::function<void(void *arg)> Routine;

	Routine func;
	void *arg;

	bool killOnDelete;
	
	
	static void Init()
	{
		static bool init = false;
		if(init) {
			return;
		}
		init = true;
	#ifndef WIN32
		assert(-1 != setpriority(PRIO_PROCESS, 0, -20)); // nice value of -20 is highest!

		signal(SIGUSR1,_signal_handler_sigusr1); 
	/* // TODO
	char cmd[100];
	sprintf(cmd, "chrt -p 40 -f %d", getpid());
	system(cmd);
	*/
	#else
		if(!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
		  DWORD dwError = GetLastError();
		  fprintf(stderr, "Could not set RealTime Priority (%d)\n", dwError);
		  exit(1);
		}

		
	#endif					
	}


#ifdef WIN32
	inline PlatformThread(DWORD(__stdcall *func)(void *), void *arg=NULL, bool rtThread=false)
#else
	inline PlatformThread(void *(*func)(void *), void *arg=NULL, bool rtThread=false)
#endif
	{
		Init();
		
		killOnDelete = true;
		name[0] = '\0';
		joined = false;
#ifdef WIN32
		handle = CreateThread (NULL, 0, func, arg, 0, &id);
		assert(NULL != handle);	
#else
		int rc;
		rc = pthread_create(&handle, NULL, func, arg);
		assert(0 == rc);
#endif
		if (rtThread)
			SetPriority(RealTime);
	}

	void SetPriority(Priorities prio)
	{
#ifdef WIN32
		int winprio;

		switch (prio) {
		case Low: winprio = THREAD_PRIORITY_LOWEST; break;
		case Normal: winprio = THREAD_PRIORITY_NORMAL; break;
		case High: winprio = THREAD_PRIORITY_HIGHEST; break;
		case RealTime: winprio = THREAD_PRIORITY_TIME_CRITICAL; break;
		}
		assert(SetThreadPriority(handle, winprio) != 0);
#else
		int prio_max = sched_get_priority_max(SCHED_RR); // SCHED_FIFO
		int prio_min = sched_get_priority_min(SCHED_RR); // SCHED_FIFO

		printf("sched_get_priority_(min/max) = %d/%d\n", prio_min, prio_max);

		switch (prio) {
		case Low:		param.sched_priority = prio_min; break;
		case Normal:	param.sched_priority = 20; break;
		case High:		param.sched_priority = 40; break;
		case RealTime:	param.sched_priority = prio_max; break;
		}

		int rc = pthread_setschedparam(handle, SCHED_RR, &param);
		if (rc != 0) {
			fprintf(stderr, "pthread_setschedparam failed! (PRIO=%d; RC=%d)\n", prio_max, rc);
		}
#endif
	}

	static THREAD_FUNC _boundFuncMain(void *arg)
	{
		auto pt = (PlatformThread*)arg;
		pt->func(pt->arg);
		return 0;
	}


	inline PlatformThread(Routine &func, void *arg = NULL, bool rtThread = false) : func(func), arg(arg), joined(false)
	{
		Init();
		
		name[0] = '\0';
		killOnDelete = true;

#ifdef WIN32
		handle = CreateThread(NULL, 0, &PlatformThread::_boundFuncMain, this, 0, &id);
		assert(NULL != handle);
#else
		int rc;
		rc = pthread_create(&handle, NULL, &PlatformThread::_boundFuncMain, this);
		assert(0 == rc);
#endif
		if (rtThread)
			SetPriority(RealTime);
	}

#ifndef WIN32
private: inline PlatformThread(pthread_t handle) : handle(handle), arg(0), joined(false)
	{
		name[0] = '\0';
		killOnDelete = false;
		pthread_getname_np(pthread_self(), name, sizeof(name));
	}
public:
#else
private: inline PlatformThread(HANDLE handle) : handle(handle), arg(0), joined(false)
{
	name[0] = '\0';
	killOnDelete = false;
	id = GetThreadId(handle);
}
public:
#endif



	inline ~PlatformThread()
	{
		if (!killOnDelete) return;

		bool ok = joined || Join(500) || Kill() || Join(500);
		if(!ok) {
			fprintf(stderr, "Thread destruction finally failed!\n");
		}
	}

	inline static PlatformThread GetCurrent() 
	{
#ifndef WIN32
		return PlatformThread(pthread_self());
#else
		return PlatformThread(GetCurrentThread());
#endif
	}

#ifdef WIN32
#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
#pragma pack(pop)
#endif

	inline bool SetName(const char *name)
	{
		strcpy(this->name, name);
#ifdef WIN32
		assert(GetCurrentThread() == handle);
				
		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = name;
		info.dwThreadID = id;
		info.dwFlags = 0;

		static const DWORD MS_VC_EXCEPTION = 0x406D1388;

		__try
		{
			RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}

		return true;
#else
		return pthread_setname_np(handle, this->name) == 0;
#endif

	}

	bool Join(long maxMs=0)
	{
		if(joined)
			return true;
#ifdef WIN32
		WaitForSingleObject(handle, maxMs > 0 ? maxMs : INFINITE);
		return true;
#else
		void *res;
		int s;
		
		if(maxMs > 0) {
			timespec to;
			to.tv_nsec = maxMs * (1000L * 1000L);
			s = pthread_timedjoin_np(handle, &res, &to);
		} else {
			s = pthread_join(handle, &res);
		}
        if (s != 0) {
			fprintf(stderr, "Joining thread [%s] failed!\n", this->name);
			return false;
		}

		free(res);
		joined = true;
		return true;
#endif
	}

	inline bool Kill()
	{
#ifdef WIN32
		return false;
#else
		return pthread_kill(handle, SIGUSR1) == 0;
#endif
	}

	
	inline static void Exit()
	{
	#ifndef WIN32
		pthread_exit(NULL);
	#endif					
	}
};


struct PlatformThreadEvent {
private:
#ifndef WIN32
	pthread_mutex_t mtx;
	pthread_cond_t cond;
#else
	HANDLE m_EventHandle;
#endif
	int state;
	bool autoreset;

public:
	PlatformThreadEvent(bool autoreset=true) : autoreset(autoreset), state(0)
	{		
#ifndef WIN32
		pthread_mutex_init(&mtx, 0);
		pthread_cond_init(&cond, 0);
#else
		m_EventHandle = ::CreateEvent(NULL, !autoreset, false, NULL);
#endif
	}

	~PlatformThreadEvent()
	{
#ifndef WIN32
		pthread_cond_destroy(&cond);
		pthread_mutex_destroy(&mtx);
#else
		::CloseHandle(m_EventHandle);
#endif
	}

	void Signal() {
#ifndef WIN32
		pthread_mutex_lock(&mtx);
		state = 1;
		if (autoreset)
			pthread_cond_signal(&cond);
		else
			pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&mtx);
#else
		::SetEvent(m_EventHandle);
#endif
	}

	bool Wait(int msecs=0) {
#ifndef WIN32
		bool ret = true;

		pthread_mutex_lock(&mtx);

		if (state == 0)
		{
			if(msecs == 0) {
				pthread_cond_wait(&cond, &mtx);
			} else {
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
		
				// add delta to nanoseconds
				ts.tv_nsec += long(msecs) * 1000000L;

				// wrap nanoseconds
				int sec_wrap = ts.tv_nsec / 1000000000L;
				ts.tv_sec += sec_wrap;
				ts.tv_nsec = ts.tv_nsec - sec_wrap * 1000000000L;

				if(pthread_cond_timedwait(&cond, &mtx, &ts) == ETIMEDOUT)
					ret = false;
			}
		}
		if (autoreset)
			state = 0;
		pthread_mutex_unlock(&mtx);

		return ret;
#else
	if(WaitForSingleObject(m_EventHandle, msecs == 0 ? INFINITE : msecs)==WAIT_OBJECT_0)
		return true;
	return false;

#endif
	}

	bool Reset() {
#ifndef WIN32
		pthread_mutex_lock(&mtx);
		state = 0;
		pthread_mutex_unlock(&mtx);
		return true;
#else
		return (::ResetEvent(m_EventHandle) == TRUE ? true : false);
#endif
	}
};


#ifdef WIN32
#define EXIT_SIGNAL_HANDLER(h) signal(SIGINT, h); signal(SIGABRT, h); signal(SIGTERM, h);
#else
#define EXIT_SIGNAL_HANDLER(h) signal(SIGQUIT, h); signal(SIGTERM, h); signal(SIGHUP, h); signal(SIGINT, h);
#endif

#ifdef WIN32
#define in_port_t u_short
#define inet_aton(s,b) InetPton(AF_INET,L##s,b)
#define close closesocket
#else
#include <unistd.h>
#define SOCKET int
#endif



#ifdef WIN32
#define MUTEX CRITICAL_SECTION
#define MUTEX_INIT(m) InitializeCriticalSection(&(m));
#define MUTEX_LOCK(m) EnterCriticalSection(&(m));
#define MUTEX_UNLOCK(m) LeaveCriticalSection(&(m));
#define MUTEX_FREE(m) DeleteCriticalSection(&(m));
#else
#define MUTEX pthread_mutex_t
#define MUTEX_INIT(m) m = PTHREAD_MUTEX_INITIALIZER;
#define MUTEX_LOCK(m) pthread_mutex_lock(&(m));
#define MUTEX_UNLOCK(m) pthread_mutex_unlock(&(m));
#define MUTEX_FREE(m) ;
#endif


struct PlatformMutex {
private:
	MUTEX mtx;
public:
	inline PlatformMutex() { MUTEX_INIT(mtx); }
	inline void Lock() { MUTEX_LOCK(mtx); }
	inline void Unlock() { MUTEX_UNLOCK(mtx); }
	inline void Join() { MUTEX_LOCK(mtx); MUTEX_UNLOCK(mtx); }
	inline ~PlatformMutex() { MUTEX_FREE(mtx); }
};

struct PlatformLocalLock {
	PlatformMutex *mtx;
	inline PlatformLocalLock(PlatformMutex *mtx) :mtx(mtx) { mtx->Lock(); }
	inline PlatformLocalLock(PlatformMutex &mtx) :mtx(&mtx) { mtx.Lock(); }
	inline ~PlatformLocalLock() { mtx->Unlock(); }
};