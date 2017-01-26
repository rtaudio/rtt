
#include <ctype.h>
#include <errno.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#include <libgen.h>
#include <sys/wait.h>
#else
#include <fcntl.h>    /* For O_RDWR */
#include<Windows.h>
#include<io.h>
#include<wchar.h>
#include <rtt.h>
#include<thread>

#define sleep(s) Sleep(s*1000)
int
mkstemp(char *tmpl);
#endif

#define err fprintf
//#define dbg(a,b,c) ;
#define dbg ;


#include "stress.h"



stress::runAsync() {
{
		stressStartSetFlag();
		int numHogs = RttThread::GetSystemNumCores()+1;

		LOG(logINFO) << "Stressing systems for " << (runTimeMs) << " ms with " << numHogs << " workers per task. Tasks are: cpu,vm";


		{
			// create measuremnet (non-rt & rt)
			//RttThread mt4([&measure, &hMat]() { measure(&hMat[4], 1); }, false); // sleep 1us
			//RttThread mt0([&measure, &hMat]() { measure(&hMat[0], 0); }, false, "mt0");  // yield			
			

			RttThread rtSleep([&measure, &hMat]() {	measure(&hMat[5], 1); }, true); // sleep 1us
			RttThread rtYield([&measure, &hMat]() {	measure(&hMat[1], 0); }, true, "mt1"); // yield

			//RttThread mt2([&measure, &hMat]() { measure(&hMat[2], -1); }, false);  // no yield/sleep
			RttThread mt3([&measure, &hMat]() {	measure(&hMat[3], -1); }, true);
			

			
			// give the measurement threads time to spin up
			usleep(stressStartDelayUs);

			
			std::vector<RttThread> stressCpu(numHogs, RttThreadPrototype([]() { usleep(stressStartDelayUs); hogcpu(); }, false, "cpu_hog"));
			std::vector<RttThread> stressVm(numHogs, RttThreadPrototype([numHogs]() { usleep(stressStartDelayUs); hogvm(256 / numHogs * 1024 * 1024, 4069, -1, 0); }, false, "vm_hog"));

#if !defined(_WIN32) && 0
			LOG(logWARNING) << "WARNING: Running hoghdd, this might wear your SSD!";
			std::vector<RttThread> stressIo(numHogs, RttThreadPrototype([]() {usleep(stressStartDelayUs);  hogio(); }, false, "io_hog"));
			std::vector<RttThread> stressHdd(2, RttThreadPrototype([]() {
				usleep(stressStartDelayUs);
				hoghdd(1024 * 1024 * 2);
				LOG(logINFO) << "hdd worker finished!";
			}, false, "hdd_hog"));
#endif 
		}

		stressStop();
	}











volatile int g_stress = 0;

void stressStartSetFlag() {
	g_stress = 1;
}

void stressStop() {
	g_stress = 0;
}

int
hogcpu (void)
{
	while (g_stress) {
		sqrt(rand());
		//SwitchToThread();
		//usleep(1);
	}

  return 0;
}

int
hogio ()
{
	while (g_stress)
#if _WIN32
		_flushall();
#else
    sync ();
#endif
  return 0;
}

int
hogvm (long long bytes, long long stride, long long hang, int keep)
{
  long long i;
  char *ptr = 0;
  char c;
  int do_malloc = 1;

  while (g_stress)
    {
      if (do_malloc)
        {
          dbg (stdout, "allocating %lli bytes ...\n", bytes);
          if (!(ptr = (char *) malloc (bytes * sizeof (char))))
            {
              err (stderr, "hogvm malloc failed: %s\n", strerror (errno));
              return 1;
            }
          if (keep)
            do_malloc = 0;
        }

      dbg (stdout, "touching bytes in strides of %lli bytes ...\n", stride);
      for (i = 0; i < bytes; i += stride)
        ptr[i] = 'Z';           /* Ensure that COW happens.  */

      if (hang == 0)
        {
          dbg (stdout, "sleeping forever with allocated memory\n");
          while (1)
            sleep (1024);
        }
      else if (hang > 0)
        {
          dbg (stdout, "sleeping for %llis with allocated memory\n", hang);
          sleep (hang);
        }

      for (i = 0; i < bytes; i += stride)
        {
          c = ptr[i];
          if (c != 'Z')
            {
              err (stderr, "memory corruption at: %p\n", ptr + i);
              return 1;
            }
        }

      if (do_malloc)
        {
          free (ptr);
          dbg (stdout, "freed %lli bytes\n", bytes);
        }
    }

  return 0;
}

int
hoghdd (long long bytes)
{
	g_stress = 1;
  long long i, j;
  int fd;
  int chunk = (1024 * 1024) - 1;        /* Minimize slow writing.  */
  char *buff = new char[chunk];

  dbg (stdout, "seeding %d byte buffer with random data\n", chunk);
  for (i = 0; i < chunk - 1; i++)
    {
      j = rand ();
      j = (j < 0) ? -j : j;
      j %= 95;
      j += 32;
      buff[i] = j;
    }
  buff[i] = '\n';

  while (g_stress)
    {
      char name[] = "./stress.XXXXXX";

      if ((fd = mkstemp (name)) == -1)
        {
          err (stderr, "mkstemp failed: %s\n", strerror (errno));   delete buff;
          return 1;
        }

      dbg (stdout, "opened %s for writing %lli bytes\n", name, bytes);

#ifndef _WIN32
      dbg (stdout, "unlinking %s\n", name);
      if (unlink (name) == -1)
        {
          err (stderr, "unlink of %s failed: %s\n", name, strerror (errno));   delete buff;
          return 1;
        }
#endif

      dbg (stdout, "fast writing to %s\n", name);
      for (j = 0; bytes == 0 || j + chunk < bytes; j += chunk)
        {
          if (write (fd, buff, chunk) == -1)
            {
				err(stderr, "write failed: %s\n", strerror(errno));   delete buff; close(fd); 
              return 1;
            }
        }

      dbg (stdout, "slow writing to %s\n", name);
      for (; bytes == 0 || j < bytes - 1; j++)
        {
          if (write (fd, &buff[j % chunk], 1) == -1)
            {
              err (stderr, "write failed: %s\n", strerror (errno));   delete buff; close(fd);
              return 1;
            }
        }
      if (write (fd, "\n", 1) == -1)
        {
          err (stderr, "write failed: %s\n", strerror (errno));   delete buff; close(fd);
          return 1;
        }
      ++j;

	  dbg(stdout, "closing %s after %lli bytes\n", name, j);
      close (fd);
#ifdef _WIN32
	  //DeleteFileA(name);
#endif
    }
  delete buff;
  return 0;
}


#ifdef _WIN32

static const char letters[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* Generate a temporary file name based on TMPL.  TMPL must match the
rules for mk[s]temp (i.e. end in "XXXXXX").  The name constructed
does not exist at the time of the call to mkstemp.  TMPL is
overwritten with the result.  */
int
mkstemp(char *tmpl)
{
	int len;
	char *XXXXXX;
	static unsigned long long value;
	unsigned long long random_time_bits;
	unsigned int count;
	int fd = -1;
	int save_errno = errno;

	/* A lower bound on the number of temporary files to attempt to
	generate.  The maximum total number of temporary file names that
	can exist for a given template is 62**6.  It should never be
	necessary to try all these combinations.  Instead if a reasonable
	number of names is tried (we define reasonable as 62**3) fail to
	give the system administrator the chance to remove the problems.  */
#define ATTEMPTS_MIN (62 * 62 * 62)

	/* The number of times to attempt to generate a temporary file.  To
	conform to POSIX, this must be no smaller than TMP_MAX.  */
#if ATTEMPTS_MIN < TMP_MAX
	unsigned int attempts = TMP_MAX;
#else
	unsigned int attempts = ATTEMPTS_MIN;
#endif

	len = strlen(tmpl);
	if (len < 6 || strcmp(&tmpl[len - 6], "XXXXXX"))
	{
		errno = EINVAL;
		return -1;
	}

	/* This is where the Xs start.  */
	XXXXXX = &tmpl[len - 6];

	/* Get some more or less random data.  */
	{
		SYSTEMTIME      stNow;
		FILETIME ftNow;

		// get system time
		GetSystemTime(&stNow);
		stNow.wMilliseconds = 500;
		if (!SystemTimeToFileTime(&stNow, &ftNow))
		{
			errno = -1;
			return -1;
		}

		random_time_bits = (((unsigned long long)ftNow.dwHighDateTime << 32)
			| (unsigned long long)ftNow.dwLowDateTime);
	}
	value += random_time_bits ^ (unsigned long long)GetCurrentThreadId();

	for (count = 0; count < attempts; value += 7777, ++count)
	{
		unsigned long long v = value;

		/* Fill in the random bits.  */
		XXXXXX[0] = letters[v % 62];
		v /= 62;
		XXXXXX[1] = letters[v % 62];
		v /= 62;
		XXXXXX[2] = letters[v % 62];
		v /= 62;
		XXXXXX[3] = letters[v % 62];
		v /= 62;
		XXXXXX[4] = letters[v % 62];
		v /= 62;
		XXXXXX[5] = letters[v % 62];

		fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, _S_IREAD | _S_IWRITE);
		if (fd >= 0)
		{
			errno = save_errno;
			return fd;
		}
		else if (errno != EEXIST)
			return -1;
	}

	/* We got out of the loop because we ran out of combinations to try.  */
	errno = EEXIST;
	return -1;
}

#endif