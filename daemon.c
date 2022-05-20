/*!
 * Copyright Notice:
 * Copyright (C) 2008 Call Direct Cellular Solutions Pty. Ltd.
 *
 * This file or portions thereof may not be copied or distributed in any form
 * (including but not limited to printed or electronic forms and binary or object forms)
 * without the expressed written consent of Call Direct Cellular Solutions Pty. Ltd
 * Copyright laws and International Treaties protect the contents of this file.
 * Unauthorized use is prohibited.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY CALL DIRECT CELLULAR SOLUTIONS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL CALL DIRECT
 * CELLULAR SOLUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OFF
 * SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <signal.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1


static void child_handler(int signum)
{
	switch (signum)
	{
		case SIGALRM:
			exit(EXIT_FAILURE);
			break;
		case SIGUSR1:
			exit(EXIT_SUCCESS);
			break;
		case SIGCHLD:
			exit(EXIT_FAILURE);
			break;
	}
}

static int isPIDRunning(pid_t pid)
{
	char achProcPID[128];
	char cbFN;
	struct stat statProc;

	cbFN=sprintf(achProcPID,"/proc/%d",pid);
	return stat(achProcPID,&statProc)>=0;
}

static void ensure_singleton(const char* lockfile)
{
	char achPID[128];
	int fd;
	int cbRead;

	pid_t pid;
	int cbPID;

	fd=open(lockfile, O_RDWR | O_CREAT | O_EXCL, 0640);
	if(fd<0)
	{
		if(errno==EEXIST)
		{
			fd=open(lockfile, O_RDONLY);

			// get PID from lock
			cbRead=read(fd,achPID,sizeof(achPID));
			if(cbRead>0)
				achPID[cbRead]=0;
			else
				achPID[0]=0;

			pid=atoi(achPID);
			if(!pid || !isPIDRunning(pid))
			{
				syslog(LOG_ERR,"deleting the lockfile - %s", lockfile);

				close(fd);
				unlink(lockfile);

				ensure_singleton(lockfile);
				return;
			}
		}

		syslog(LOG_ERR,"another instance already running (because creating lock file %s failed: %s)", lockfile, strerror(errno));
		exit(EXIT_FAILURE);
	}

	pid=getpid();
	cbPID=sprintf(achPID,"%d\n",pid);
	
	write(fd,achPID,cbPID);
	close(fd);
}

void daemonize(const char *lockfile, const char *user)
{
	pid_t pid, sid, parent;

	/* already a daemon ?*/
	if (getppid() == 1) return;

	/* Create the lock file as the current user */
	if (lockfile && lockfile[0])
		ensure_singleton(lockfile);

	/* Drop user if there is one, and we were run as root */
	if (getuid() == 0 || geteuid() == 0)
	{
		struct passwd *pw = getpwnam(user);
		if (pw)
		{
			syslog(LOG_NOTICE, "setting user to %s", user);
			setuid(pw->pw_uid);
		}
	}

	/* Trap signals that we expect to recieve */
	signal(SIGCHLD, child_handler);
	signal(SIGUSR1, child_handler);
	signal(SIGALRM, child_handler);

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0)
	{
		syslog(LOG_ERR, "unable to fork daemon, code=%d (%s)",
		       errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0)
	{

		/* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
		   for two seconds to elapse (SIGALRM).  pause() should not return. */
		alarm(2);
		pause();

		exit(EXIT_FAILURE);
	}

	/* At this point we are executing as the child process */
	parent = getppid();

	/* Initial signal setup */
	signal(SIGCHLD, SIG_DFL); /* A child process dies */
	signal(SIGTSTP, SIG_IGN); /* Various TTY signals */
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGHUP, SIG_IGN); /* Ignore hangup signal */
	signal(SIGTERM, SIG_DFL); /* Die on SIGTERM */

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0)
	{
		syslog(LOG_ERR, "unable to create a new session, code %d (%s)",
		       errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Change the current working directory.  This prevents the current
	   directory from being locked; hence not being able to remove it. */
	/*if ((chdir("/")) < 0)
	 {
	    syslog( LOG_ERR, "unable to change directory to %s, code %d (%s)",
	            "/", errno, strerror(errno) );
	    exit(EXIT_FAILURE);
	}*/

	/* Redirect standard files to /dev/null */
	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

	/* Tell the parent process that we are A-okay */
	kill(parent, SIGUSR1);
}

/*
* vim:ts=4:sw=4:
*/
