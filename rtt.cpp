#include "rtt.h"



#ifdef _WIN32
#include <windows.h>

#include <time.h>

void usleep(unsigned int usec)
{
	if ((usec % 1000) == 0) {
		Sleep(usec / 1000);
		return;
	}

	HANDLE timer;
	LARGE_INTEGER ft;

	ft.QuadPart = -(10 * (__int64)usec);

	//Timer Funktionen ab WINNT verf�gbar 
	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}

void nsleep(int64_t nsec)
{
	HANDLE timer;
	LARGE_INTEGER ft;

	if (nsec < 100) nsec = 100;

	ft.QuadPart = -(nsec/100);

	//Timer Funktionen ab WINNT verf�gbar 
	timer = CreateWaitableTimer(NULL, TRUE, NULL);
	SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
	WaitForSingleObject(timer, INFINITE);
	CloseHandle(timer);
}


#else
#include <sys/time.h>

#if defined(__linux__) || defined( __ANDROID_API__) >= 21
#include <sys/timerfd.h>
#endif

void nsleep(int64_t nsec)
{
	struct timespec t;
	t.tv_sec = 0;
	t.tv_nsec = nsec;
	if (nanosleep(&t, NULL) != 0)
		fprintf(stderr, "nanosleep failed!\n");
}
#endif


#ifdef _WIN32
static LARGE_INTEGER timerFreq;
#endif

inline static uint64_t getMicroSeconds()
{
#ifndef _WIN32
	uint64_t t;
	struct timeval tv;
	gettimeofday(&tv, 0);
	t = (uint64_t)tv.tv_sec * (uint64_t)1e6 + (uint64_t)tv.tv_usec;
	return t;
#else
	LARGE_INTEGER t1;
	QueryPerformanceCounter(&t1);
	return (uint64_t)(((double)t1.QuadPart) / ((double)timerFreq.QuadPart) * 1000000.0);
#endif
}


void RttTimer::init(uint64_t startNs, uint64_t periodNs) {
	m_isRunning = true;
#ifndef _WIN32

	int ret;

	struct itimerspec itval;

	/* Create the timer */
	// MONOTONIC is more than CLOCK_REALTIME accurate!
	timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (timer_fd == -1)
		throw "Creating timerfd failed!";

	if (startNs < 1e3)
		startNs = 1e3;

	itval.it_interval.tv_sec = periodNs / 1000000000;
	itval.it_interval.tv_nsec = periodNs % 1000000000;
	itval.it_value.tv_sec = (periodNs + startNs) / 1000000000;
	itval.it_value.tv_nsec = (periodNs + startNs) % 1000000000;
	ret = timerfd_settime(timer_fd, 0, &itval, NULL);

	if (ret < 0) {
		throw "Creating timerfd failed!";
	}
#else

	if (timerFreq.LowPart == 0)
		QueryPerformanceFrequency(&timerFreq);


	LARGE_INTEGER ft;
	ft.QuadPart = -(LONGLONG)(startNs / 100UL);
	timer_fd = CreateWaitableTimer(NULL, FALSE, NULL);
	if (periodNs < 1000000) {
		std::cerr << "Warning: windows timers minimum period is 1 ms, requested " << periodNs << " ns!" << std::endl;
		periodNs = 1000000;
	}
	SetWaitableTimer(timer_fd, &ft, (LONG)(periodNs / 1000000UL), NULL, NULL, TRUE);
#endif

}

void RttTimer::startThread(uint64_t startNs, uint64_t periodNs)
{
	init(startNs, periodNs);
	thread = new RttThread([this]() {
		while (m_isRunning && waitNextPeriod() && func()) { }
		m_isRunning = false;
	}, true);
}

 RttTimer::~RttTimer() {
	m_isRunning = false; 
#ifndef _WIN32
	if (timer_fd) {
		//struct itimerspec itval;
		//memset(&itval, 0, sizeof(itval));
		//timerfd_settime(timer_fd, 0, &itval, NULL);

		close(timer_fd);
	}
#else
	CloseHandle(timer_fd);
#endif

	if (thread) delete thread;
}

 bool RttTimer::waitNextPeriod() {
	 if (!m_isRunning)
		 return false;

#ifdef _WIN32
	 if (WaitForSingleObject(timer_fd, INFINITE) != WAIT_OBJECT_0) {
		 std::cerr << "Timer cancelled!" << std::endl;
		 m_isRunning = false;
		 return false;
	 }
#else
	 unsigned long long missed;
	 int ret;

	 /* Wait for the next timer event. If we have missed any the
	 number is written to "missed" */
	 //printf("reading...\n");
	 ret = read(timer_fd, &missed, sizeof(missed));
	 //printf("!\n");
	 if (ret == -1)
	 {
		 perror("read timer");
		 m_isRunning = false;
		 return false;
	 }
	 if (missed > 0)
		 this->wakeups_missed += (missed - 1);	 
#endif
	 return true;
 }