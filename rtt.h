#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdlib.h> 
#include <string.h>
#include <string>
#include <signal.h>
#include <functional>


#ifdef _WIN32
#include<windows.h>
#include<Avrt.h>
#pragma comment(lib, "Avrt.lib") 
void usleep(unsigned int usec);
#endif

void nsleep(int64_t nsec);

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/resource.h>
#include <errno.h>

#define USE_SCHEDULER SCHED_FIFO // SCHED_RR
#endif

#include <iostream>

#ifdef _WIN32
#define THREAD_FUNC DWORD WINAPI
int fork(void);
#else
#define THREAD_FUNC void *
#endif

#ifdef ANDROID
#include <sstream>
#include <android/log.h>
#define SSTR( x ) static_cast< std::ostringstream & >( \
        ( std::ostringstream() << std::dec << x ) ).str()

	/*
void androidLogWrapper(FILE *f, const char *fmt, ...)
{
	int al = ANDROID_LOG_INFO;
	if (f == stderr)
	{
		al = ANDROID_LOG_ERROR;
		
	}
	
	va_list args;
	va_start(args, format);

	if (priority & PRIO_LOG)
		vprintf(format, args);

	va_end(args);
	
	
	
}
*/
#define fprintf(f,fmt, ...) __android_log_print((f == stderr) ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "rtt", fmt, ##__VA_ARGS__);
#else
#define SSTR( X ) std::to_string( X )
#endif

class RttThreadPrototype {
public:
	template<typename Functor>
	RttThreadPrototype(const Functor &lambda, bool rtThread = false, std::string threadNamePrefix = "")
		: func(lambda), rt(rtThread), namePrefix(threadNamePrefix)
	{
		nameIndex = 0;
	}

	std::string nextName() const {
		//return namePrefix + std::to_string(nextNameIndex());
		return namePrefix + SSTR(nextNameIndex());
	}

	int nextNameIndex() const {
		return nameIndex++;
	}

	std::function<void()> const& getFunc() const {
		return func;
	}

	bool isRt() const {	return rt;}

	RttThreadPrototype(const RttThreadPrototype& other) :
		func(other.func), namePrefix(other.namePrefix), rt(other.rt)
	{
		nameIndex = other.nextNameIndex();
	}

private:
	std::function<void(void)> func;
	bool rt;
	std::string namePrefix;
	mutable int nameIndex;


};

static void _signal_handler_sigusr1(int sig) {
}

class RttThread {
public:
	typedef std::function<void(void *arg)> Routine;
	enum Priorities {
		Low, Normal, High, RealTime,
	};

private:

#ifdef _WIN32
	HANDLE handle;
	DWORD id;
#else
	pthread_t handle;
	struct sched_param param;
#endif

	int joined;
	std::string name;

	

	Routine func;
	void *arg;

	bool killOnDelete;
	
	
public:

	static int GetSystemNumCores() {
#ifdef WIN32
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		return  sysinfo.dwNumberOfProcessors;
#else
		return sysconf(_SC_NPROCESSORS_ONLN);
#endif
	}

	static bool Init()
	{
		static bool init = false;
		if(init) {
			return true;
		}
		
    #ifndef _WIN32
        // nice value of -20 is highest!
        int r;
        if(0 != (r=setpriority(PRIO_PROCESS, 0, -20))) {
            fprintf(stderr, "rtt: Could not set process nice value (error %d)!\n", -1);
        }

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
		  return false;
		}

		
	#endif		

init = true;	
return true;
	}


#ifdef _WIN32
	inline RttThread(DWORD(__stdcall *func)(void *), void *arg=NULL, bool rtThread=false)
#else
	inline RttThread(void *(*func)(void *), void *arg=NULL, bool rtThread=false)
#endif
	{
		Init();
		
		killOnDelete = true;
		name = "";
		joined = false;
#ifdef _WIN32
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
#ifdef _WIN32
		int winprio;

		switch (prio) {
		case Low: winprio = THREAD_PRIORITY_LOWEST; break;
		case Normal: winprio = THREAD_PRIORITY_NORMAL; break;
		case High: winprio = THREAD_PRIORITY_HIGHEST; break;
		case RealTime: winprio = THREAD_PRIORITY_TIME_CRITICAL; break;
		}
		assert(SetThreadPriority(handle, winprio) != 0);



#else
		static int prio_max = -1, prio_min = -1;
		if (prio_max == -1 || prio_min == -1) {
			prio_max = sched_get_priority_max(USE_SCHEDULER); // SCHED_FIFO
			prio_min = sched_get_priority_min(USE_SCHEDULER); // SCHED_FIFO
			printf("sched_get_priority_{min|max}(%s) = %d|%d\n", USE_SCHEDULER == SCHED_FIFO ? "SCHED_FIFO" : "SCHED_RR", prio_min, prio_max);
		}

		switch (prio) {
		case Low:		param.sched_priority = prio_min; break;
		case Normal:	param.sched_priority = 20; break;
		case High:		param.sched_priority = 40; break;
		case RealTime:	param.sched_priority = prio_max; break;
		}

		int rc = pthread_setschedparam(handle, USE_SCHEDULER, &param);
		if (rc != 0) {
			static int triedFix = 0;
			if (!triedFix) {
				// fix: disable RT group scheduling http://lists.opensuse.org/opensuse-security/2011-04/msg00015.html
                //system("sysctl - w kernel.sched_rt_runtime_us = -1 &> /dev/null");
                pclose(popen("sysctl -w kernel.sched_rt_runtime_us=-1", "r"));
				triedFix = 1;
				rc = pthread_setschedparam(handle, USE_SCHEDULER, &param);
			} 
			
			if (rc != 0) {
				errno = rc; perror("pthread_setschedparam");
				fprintf(stderr, "pthread_setschedparam failed! The thread will not perform with real-time accuracy! (PRIO=%d; RC=%d, %s)\n", prio_max, rc, USE_SCHEDULER == SCHED_FIFO ? "SCHED_FIFO" : "SCHED_RR");
			}
		}
#endif
	}

	static THREAD_FUNC _boundFuncMain(void *arg)
	{
		auto pt = (RttThread*)arg;

		if (pt->isRt) {
#ifndef _WIN32 
			/*
			int c = sched_getcpu();
			if (c < 0) {
				fprintf(stderr, "sched_getcpu failed!\n");
			}
			else {
				cpu_set_t cpuset; CPU_ZERO(&cpuset);	CPU_SET(c, &cpuset);
				int s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
				if (s != 0)
					fprintf(stderr, "failed sticking RT thread to CPU %d!\n", c);
				else
					fprintf(stderr, "sticked RT thread to CPU %d\n", c);
			} */
#else
				DWORD nTaskIndex = 0;
				HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &nTaskIndex);
				if (NULL == hTask) {
					auto le = GetLastError(); // ERROR_THREAD_ALREADY_IN_TASK
					if (le == ERROR_INVALID_TASK_INDEX) {
						std::cerr << "ERROR_INVALID_TASK_INDEX" << std::endl;
					}
					else if (le == ERROR_INVALID_TASK_NAME) {
						std::cerr << "ERROR_INVALID_TASK_NAME" << std::endl;
					}
					else if (le == ERROR_PRIVILEGE_NOT_HELD) {
						std::cerr << "ERROR_PRIVILEGE_NOT_HELD" << std::endl;
					}
				}
				hTask = hTask;
				assert(AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL) == TRUE);

			//fprintf(stderr, "windows: no cpu-sticking implemeted!\n");
#endif
		}

		//pt->handle = GetCurrentThread();
		if (pt->name.size() > 0)
			pt->SetName(pt->name);

		pt->func(pt->arg);
		return 0;
	}

	bool isRt;

	inline RttThread(Routine &func, void *arg = NULL, bool rtThread = false) : func(func), arg(arg), isRt(rtThread), joined(false)
	{
		Init();
		proto = 0;
		name = "";
		killOnDelete = false;

#ifdef _WIN32
		handle = CreateThread(NULL, 0, &RttThread::_boundFuncMain, this, 0, &id);
		assert(NULL != handle);
#else
		int rc;
		rc = pthread_create(&handle, NULL, &RttThread::_boundFuncMain, this);
		assert(0 == rc);
#endif
		if (rtThread)
			SetPriority(RealTime);
	}

	inline RttThread(std::function<void(void)> &method, bool rtThread = false) : arg(NULL), isRt(rtThread), joined(false)
	{
		Init();
		proto = 0;

		func = [method](void *arg) {
			method();
		};

		name = "";
		killOnDelete = false;

#ifdef _WIN32
		handle = CreateThread(NULL, 0, &RttThread::_boundFuncMain, this, 0, &id);
		assert(NULL != handle);
#else
		int rc;
		rc = pthread_create(&handle, NULL, &RttThread::_boundFuncMain, this);
		assert(0 == rc);
#endif
		if (rtThread)
			SetPriority(RealTime);
	}

	template<typename Functor>
	RttThread(const Functor &lambda, bool rtThread = false, std::string threadName="") : arg(NULL), isRt(rtThread), joined(false)
	{
		Init();
		proto = 0;

		std::function<void(void)> f(lambda);

		func = [f](void *arg) {
			f();
		};

		name = threadName;
		killOnDelete = false;

#ifdef _WIN32
		handle = CreateThread(NULL, 0, &RttThread::_boundFuncMain, this, 0, &id);
		assert(NULL != handle);
#else
		int rc;
		rc = pthread_create(&handle, NULL, &RttThread::_boundFuncMain, this);
		assert(0 == rc);
#endif
		if (rtThread)
			SetPriority(RealTime);
	}

	const RttThreadPrototype *proto;

	RttThread(const RttThread& other) {
		Init();

		isRt = other.proto->isRt();
		arg = 0;
		joined = false;

		if (other.proto) {
			auto &proto(*other.proto);
			name = proto.nextName();
			killOnDelete = false;

			auto &f = proto.getFunc();

			func = [f](void *arg) {
				f();
			};

#ifdef _WIN32
			handle = CreateThread(NULL, 0, &RttThread::_boundFuncMain, this, 0, &id);
			assert(NULL != handle);
#else
			int rc;
			rc = pthread_create(&handle, NULL, &RttThread::_boundFuncMain, this);
			assert(0 == rc);
#endif
			if (proto.isRt())
				SetPriority(RealTime);
			//else
//				SetPriority(Low);
		}
	}

	RttThread(const RttThreadPrototype& proto) {
		Init();

		this->proto = &proto;

        handle = 0;
	}


#ifndef _WIN32
private: inline RttThread(pthread_t handle) : handle(handle), arg(0), joined(false)
	{
		name = "";
		killOnDelete = false;
		char nameBuf[64];
#ifndef ANDROID
		pthread_getname_np(pthread_self(), nameBuf, sizeof(nameBuf)); // TODO
		name = nameBuf;
#endif
	}
public:
#else
private: inline RttThread(HANDLE handle) : handle(handle), arg(0), joined(false)
{
	name = "";
	killOnDelete = false;
	id = GetThreadId(handle);
}
public:
#endif



	inline ~RttThread()
	{
        if (joined || !handle)
			return;

		if (!killOnDelete) {
			Join();
			return;
		}

		bool ok = joined || Join(500) || Kill() || Join(500);
		if(!ok) {
			fprintf(stderr, "Thread destruction finally failed!\n");
		}
	}

	inline static RttThread GetCurrent() 
	{
#ifndef _WIN32
		return RttThread(pthread_self());
#else
		return RttThread(GetCurrentThread());
#endif
	}

#ifdef _WIN32
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

	inline bool SetName(const std::string &name)
	{
		this->name = name;
#ifdef _WIN32
		//assert(GetCurrentThread() == handle);
				
		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = name.c_str();
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
		return pthread_setname_np(handle, this->name.c_str()) == 0;
#endif

	}

	static bool YieldCurrent() {
#ifdef ANDROID
		nsleep(1); // todo
		return true;
#elif !defined(_WIN32)
		return pthread_yield() == 0;
#else
		// on windows this_thread::yield is not performing well!
		usleep(1);
		return true;
#endif
	}

	bool Join(long maxMs=0)
	{
		if(joined)
			return true;
#ifdef _WIN32
		WaitForSingleObject(handle, maxMs > 0 ? maxMs : INFINITE);
		return true;
#else
		void *res;
		int s;

#ifdef ANDROID
		if (maxMs > 0) {
			fprintf(stderr, "Rtt: no timed join on android yet!\n");
			maxMs = 0;
		}
#else		
		if(maxMs > 0) {
			timespec to;
			if (clock_gettime(CLOCK_REALTIME, &to) == -1) {
				fprintf(stderr, "Joining thread [%s] failed at clock_gettime()!\n", this->name.c_str());
				return false;
			}
			long s = maxMs / 1000L;
			to.tv_sec += s;
			to.tv_nsec += (long)(maxMs - s * 1000L) * (1000L * 1000L);
			//fprintf(stderr, "to.tv_sec = %d, to.tv_nsec = %d\n", (int)to.tv_sec,  (int)to.tv_nsec);
			s = pthread_timedjoin_np(handle, &res, &to);
		} else
#endif		
		{
			s = pthread_join(handle, &res);
		}

		if (s == ETIMEDOUT) {
			fprintf(stderr, "Joining thread [%s] timed out!\n", this->name.c_str());
			return false;
		}

        if (s != 0) {
			fprintf(stderr, "Joining thread [%s] failed (%d)!\n", this->name.c_str(), s);
			return false;
		}

		//free(res);
		joined = true;
		return true;
#endif
	}

	inline bool Kill()
	{
#ifdef _WIN32
		return TerminateThread(handle, 0) != 0;
#elif defined(ANDROID)
		return pthread_kill(handle, 0) == 0 && (joined = true); // SIGTERM TODO!
#else		
		return pthread_cancel(handle) == 0 && (joined = true);
#endif
	}

	
	inline static void Exit()
	{
	#ifndef _WIN32
		pthread_exit(NULL);
	#endif					
	}
};


struct RttEvent {
private:
#ifndef _WIN32
	pthread_mutex_t mtx;
	pthread_cond_t cond;
#else
	HANDLE m_EventHandle;
#endif
	int state;
	bool autoreset;

public:
	RttEvent(bool autoreset=true) : autoreset(autoreset), state(0)
	{		
#ifndef _WIN32
		pthread_mutex_init(&mtx, 0);
		pthread_cond_init(&cond, 0);
#else
		m_EventHandle = ::CreateEvent(NULL, !autoreset, false, NULL);
#endif
	}

	~RttEvent()
	{
#ifndef _WIN32
		pthread_cond_destroy(&cond);
		pthread_mutex_destroy(&mtx);
#else
		::CloseHandle(m_EventHandle);
#endif
	}

	void Signal() {
#ifndef _WIN32
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
#ifndef _WIN32
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
#ifndef _WIN32
		pthread_mutex_lock(&mtx);
		state = 0;
		pthread_mutex_unlock(&mtx);
		return true;
#else
		return (::ResetEvent(m_EventHandle) == TRUE ? true : false);
#endif
	}

};


#ifdef _WIN32
#define EXIT_SIGNAL_HANDLER(h) signal(SIGINT, h); signal(SIGABRT, h); signal(SIGTERM, h);
#else
#define EXIT_SIGNAL_HANDLER(h) signal(SIGQUIT, h); signal(SIGTERM, h); signal(SIGHUP, h); signal(SIGINT, h);
#endif



#ifdef _WIN32
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


struct RttMutex {
private:
	MUTEX mtx;
public:
	inline RttMutex() { MUTEX_INIT(mtx); }
	inline void Lock() { MUTEX_LOCK(mtx); }
	inline void Unlock() { MUTEX_UNLOCK(mtx); }
	inline void Join() { MUTEX_LOCK(mtx); MUTEX_UNLOCK(mtx); }
	inline ~RttMutex() { MUTEX_FREE(mtx); }
};

struct RttLocalLock {
	RttMutex *mtx;
	inline RttLocalLock(RttMutex *mtx) :mtx(mtx) { mtx->Lock(); }
	inline RttLocalLock(RttMutex &mtx) :mtx(&mtx) { mtx.Lock(); }
	inline ~RttLocalLock() { mtx->Unlock(); }
};



class RttTimer {
private:
	unsigned long long wakeups_missed;
	RttThread *thread;
	volatile bool m_isRunning;
	std::function<bool(void)> func;
	void start(uint64_t startNs, uint64_t periodNs);
	int timer_fd;
		
public:
	template<typename Functor>
	inline RttTimer(const Functor &lambda, uint64_t periodNs, uint64_t startNs = 0) :
		func(lambda), thread(0), timer_fd(0)
	{
		start(startNs, periodNs);
	}

	~RttTimer();

};