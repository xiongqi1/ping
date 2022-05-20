#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/times.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <libgen.h>
#include <errno.h>

#include "ping.h"
#include "daemon.h"

#ifdef PLATFORM_PLATYPUS
#include "nvram.h"
#include <limits.h>
#endif

#include "rdb_ops.h"

#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
const char _cmdOpts[] = "p:s:t:i:f:r:x:c:dP:S:";
#else
const char _cmdOpts[] = "p:s:t:i:f:r:c:dP:S:";
#endif

struct
{
	char szProgramFileName[PATH_MAX];

	// variables for ip address options
	char* szIpAddrTable[2];
	int cIpAddrTable;
	char* szSrcIpAddrTable[2];
	int cSrcIpAddrTable;

	int nPeriodSec;
	int nAccPeriodSec;
	int nFailCount;
	int nForceResetMin;
#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
	int nPH2SuccessCount;
#endif

	int fDaemon;

	char* szRebootCmd;
} _config;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static clock_t _clkTicksPerSec;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static clock_t __getTicksPerSecond(void)
{
	return sysconf(_SC_CLK_TCK);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static clock_t __getTickCount(void)
{
	struct tms tm;

	return times(&tm);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void initTickCount(void)
{
	_clkTicksPerSec = __getTicksPerSecond();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
clock_t __getSecTickCount(void)
{
	return __getTickCount() / _clkTicksPerSec;
}
///////////////////////////////////////////////////////////////////////////////////////////////////
static void printUsageExit(void)
{
	fprintf(stderr, "%s - periodic ping daemon\n", _config.szProgramFileName);

	fprintf(stderr, "\n");
	fprintf(stderr, "usage>\n");

	fprintf(stderr, "\t -p <ip address>\t primary ping address\n");
	fprintf(stderr, "\t -s <ip address>\t secondary ping address\n");
	fprintf(stderr, "\t -P <ip address>\t primary ping source address\n");
	fprintf(stderr, "\t -S <ip address>\t secondary ping source address\n");
	fprintf(stderr, "\t -t <seconds>   \t ping interval period\n");
	fprintf(stderr, "\t -i <seconds>   \t ping interval period while it is failing\n");
	fprintf(stderr, "\t -f <count>     \t failure count until reboot\n");
#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
	fprintf(stderr, "\t -x <count>     \t ping host 2 success count until reboot\n");
#endif
	fprintf(stderr, "\n");

	fprintf(stderr, "\t -r <minutes>   \t periodic reboot minutes\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "\t -c <reboot cmd>\t reboot command\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "\t -d             \t disable daemonization\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "example>\n");
	fprintf(stderr, "\t# %s -p <ip address> -t <seconds> -f <count> [options]\n", _config.szProgramFileName);
	fprintf(stderr, "or\n");
	fprintf(stderr, "\t# %s -r <minutes> [options]\n",_config.szProgramFileName);
	fprintf(stderr, "\n");

	exit(1);
}


int _fTermSigDetected=0;
///////////////////////////////////////////////////////////////////////////////////////////////////
static void sigHandlerTerm(int signum)
{
	_fTermSigDetected=1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char** argv)
{
	int stat;
	int childstat;

	////////////////////////////
	// command line analyzing //
	////////////////////////////

	memset(&_config, 0, sizeof(_config));

	// get program file name
	char* szPName = basename(argv[0]);
	if (szPName)
		strcpy(_config.szProgramFileName, szPName);
	else
		strcpy(_config.szProgramFileName, "");

	// default option
	_config.fDaemon=1;
	_config.szRebootCmd = "reboot";

	/* On Bovine platform, when periodicpingd is called via periodicping template using
	 * rdb_manager_t, passing command argument "/usr/bin/rdb_set service.system.reset 1"
	 * is parsed as two argument. To reboot properly, compare argc and optind and if
	 * not match, merge remaining arguments to command argument.
	 */
	int ii;
	/*
	syslog(LOG_INFO, "argc = %d",argc);
	for (ii=0; ii<argc; ii++)
		syslog(LOG_INFO, "arg[%d] = '%s'",ii, argv[ii]);
	syslog(LOG_INFO, "optind = %d",optind);
	*/

	// get options
	int opt;
	while ((opt = getopt(argc, argv, _cmdOpts)) != EOF)
	{
		// "p:s:p:i:f:r:";
		switch (opt)
		{
			case 'd':
				_config.fDaemon=0;
				break;

			case 'c':
				_config.szRebootCmd = optarg;
				/*syslog(LOG_INFO, "szRebootCmd = '%s'",_config.szRebootCmd);*/
				break;

			case 'p':
			case 's':
				if(_config.cIpAddrTable < 2) 
				_config.szIpAddrTable[_config.cIpAddrTable++] = optarg;
				break;

			case 'P':
			case 'S':
				if(_config.cSrcIpAddrTable < 2) 
				_config.szSrcIpAddrTable[_config.cSrcIpAddrTable++] = optarg;
				break;

			case 't':
				_config.nPeriodSec = atoi(optarg);
				break;

			case 'f':
				_config.nFailCount = atoi(optarg);
				break;

#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
			case 'x':
				_config.nPH2SuccessCount = atoi(optarg);
				break;
#endif

			case 'r':
				_config.nForceResetMin = atoi(optarg);
				break;

			case 'i':
				_config.nAccPeriodSec = atoi(optarg);
				break;

			case '?':
			default:
				printUsageExit();
				break;
		}
	}

	/* On Bovine platform, when periodicpingd is called via periodicping template using
	 * rdb_manager_t, passing command argument "/usr/bin/rdb_set service.system.reset 1"
	 * is parsed as two argument. To reboot properly, compare argc and optind and if
	 * not match, merge remaining arguments to command argument.
	 */
	/*syslog(LOG_INFO, "optind = %d",optind);*/
	for (ii=optind; ii<argc; ii++)
		sprintf(_config.szRebootCmd,"%s %s",_config.szRebootCmd, argv[ii]);
	/*syslog(LOG_INFO, "szRebootCmd = '%s'",_config.szRebootCmd);*/

	//////////////////////////////
	// check compulsory options //
	//////////////////////////////

#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
	if( !(_config.nFailCount && _config.nPH2SuccessCount && _config.nPeriodSec && _config.cIpAddrTable) && !_config.nForceResetMin )
#else
	if( !(_config.nFailCount && _config.nPeriodSec && _config.cIpAddrTable) && !_config.nForceResetMin )
#endif
		printUsageExit();


	const char szLockFName[]="/var/lock/subsys/periodicping";
	if(_config.fDaemon)
	{
		daemonize(szLockFName, "pingdaemon");
		syslog(LOG_INFO, "starting...");
	}

	signal(SIGINT, sigHandlerTerm);
	signal(SIGTERM, sigHandlerTerm);

	// open database
	if (rdb_open_db() < 0)
	{
		syslog(LOG_ERR,"failed to open database");
		exit(-1);
	}

	initTickCount();

	//////////
	// ping //
	//////////

	clock_t secS = __getSecTickCount();

	int cTotalFailCnt = 0;
#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
	int pingHost2SuccessCnt = 0;
#endif
	int succ = 0;

	clock_t secPing = secS;
	int nPeriodSec = _config.nPeriodSec;

	clock_t secResetPeriod = _config.nForceResetMin * 60;

	int iTry;

	while (1)
	{
		clock_t secNow = __getSecTickCount();

		if (_config.cIpAddrTable && (secNow - secPing > nPeriodSec))
		{
			int iIpAddr;

			// update ping sec.
			secPing = secNow;

			for(iIpAddr=0;iIpAddr<_config.cIpAddrTable;iIpAddr++)
			{
				for(iTry=0;iTry<3;iTry++)
				{
					// ping
					succ = ping(_config.szIpAddrTable[iIpAddr],_config.szSrcIpAddrTable[iIpAddr]);
					if(succ>=0)
					{
						#if defined (PLATFORM_ANTELOPE) || \
                            defined (PLATFORM_ARACHNID) || \
                            defined (V_PINGMONITOR_nbn)
						//For every successful ping to host 2, increment success count
						if(iIpAddr == 1)
						{
							pingHost2SuccessCnt++;
							//Check if count of successful pings to host 2 is already
							//greater than the configured value, then force reboot due to fail
							if(pingHost2SuccessCnt >= _config.nPH2SuccessCount)
								goto fail_reboot;
						}
						else
						{
							//reset host 2 success count if host 1 ping is successful		
							pingHost2SuccessCnt = 0;
						}

						#endif
						goto ping_succ;
					}
	
					// ping - switch if it fails
					const char* szReason="";
					switch(succ)
					{
						case -1:
							szReason="Socket Error";
							break;

						case -2:
							szReason="DNS lookup failure";
							break;

						case -3:
							szReason="Sendto failed";
							break;

						case -4:
							szReason="Timeout";
							break;

						case -5:
							szReason="Too short response";
							break;

						case -6:
							szReason="Unknown response";
							break;

						case -7:
							szReason="unknown failure from select()";
							break;

						case -8:
							szReason="Bind (source) failed";
							break;

						default:
							szReason="unknown failure";
							break;
					}
					syslog(LOG_ERR,"Failed to ping %s - %s", _config.szIpAddrTable[iIpAddr], szReason);
				}
			}
ping_succ:

			// reload period sec.
			nPeriodSec = _config.nPeriodSec;

			// increase fail count if failed
			if (succ < 0)
			{
				cTotalFailCnt++;
				
				#if defined(BOARD_nhd1w)
				{
					char rdb_buf[32];
					int docked;
					int wan_mode;
					
					
					// get docking status
					if(rdb_get_single("wwan.0.mhs.docked",rdb_buf,sizeof(rdb_buf))<0)
						rdb_buf[0]=0;
					docked=atoi(rdb_buf);
					
					// get wan mode
					if(rdb_get_single("wwan.0.mhs.docked",rdb_buf,sizeof(rdb_buf))<0)
						rdb_buf[0]=0;
					wan_mode=!strcmp(rdb_buf,"wan");
					
					// we do not reboot if lan mode and no MHS available
					if(!wan_mode && !docked) {
						syslog(LOG_INFO,"periodicping is suspended - wan_mode=%d,docked=%d",wan_mode,docked);
						cTotalFailCnt=0;
					}
					else {
						syslog(LOG_INFO, "fail count increased - %d",cTotalFailCnt);
					}
				}
				#else		
				syslog(LOG_INFO, "fail count increased - %d",cTotalFailCnt);
				#endif		
			}
			else
			{
				cTotalFailCnt=0;
			}

			// accelate period sec if failed
			if (succ<0 && _config.nAccPeriodSec>0)
				nPeriodSec = _config.nAccPeriodSec;

#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
fail_reboot:		//goto points here as c doesn't allow a goto to declarations directly below
			;
#endif

		}

		int fForceReboot = secResetPeriod && (secNow - secS) > secResetPeriod;
#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
		// fail if either TotalFailCnt exceeds _config.nFailCount or if pingHost2SuccessCnt exceeds _config.nPH2SuccessCount
		int fFailReboot = (_config.nFailCount && cTotalFailCnt >= _config.nFailCount) || (_config.nPH2SuccessCount && pingHost2SuccessCnt >= _config.nPH2SuccessCount);
#else
		int fFailReboot = _config.nFailCount && cTotalFailCnt >= _config.nFailCount;
#endif

		if(_fTermSigDetected || sleep(1))
		{
			syslog(LOG_INFO, "signal detected\n");
			break;
		}

		
		if (fForceReboot || fFailReboot)
		{
#if defined(PLATFORM_PLATYPUS) && (defined (BOARD_3g8wv) || defined (BOARD_3g38wv) || defined (BOARD_3g38wv2) || defined (BOARD_3g36wv) || defined (BOARD_3g39w))

			// periodicping is not a platform independent package any more.
			// In Platypus, we have to check WAN and configuration.
			// we do not do periodic ping if 3G is disabled and WAN link is down

			char buf[64];

			char* wwan_mode;

			int wwan_disabled;
			int wwan_3gbackup;
			int wan_link_up;

			int wwan_active;

			const char* recovery_cmd;

			// get wan link status
			buf[0]=0;
			rdb_get_single("wan.link",buf,sizeof(buf));
			wan_link_up=!strcmp(buf,"up") || !strlen(buf);
			
			// get wwan configuration
			wwan_mode = nvram_get(RT2860_NVRAM, "wwan_opmode");
			wwan_disabled=!strcmp(wwan_mode,"Manual");
			wwan_3gbackup=!strcmp(wwan_mode,"wanBackup");
			nvram_strfree(wwan_mode);

			// get active connection
			buf[0]=0;
			rdb_get_single("wan.connection",buf,sizeof(buf));
			wwan_active=!strcmp("wan.connection","3g");

			
			if(wwan_disabled && !wan_link_up)
			{
				syslog(LOG_INFO, "wan link is down in wan mode - periodic ping is ignored");
			}
			else
			{
				if(wwan_disabled || (wwan_3gbackup && !wwan_active && wan_link_up))
					recovery_cmd="/sbin/reboot";
				else
					recovery_cmd="/sbin/reboot_module.sh";

#if defined (BOARD_3g36wv) // this define is only for Videotron
				{
					char buf[32] = {0,};
					char * reboot_count = nvram_get(RT2860_NVRAM, "periodic_rebootcount");
					int int_count = 0;
					if(reboot_count) {
						int_count = atoi(reboot_count);
					}
					nvram_strfree(reboot_count);
					
					if (int_count > (INT_MAX - 1))
						int_count = -1;
					int_count++;
					sprintf(buf, "%d", int_count);
					nvram_set(RT2860_NVRAM, "periodic_rebootcount", buf);
				}
#endif
				syslog(LOG_INFO, "run cmd='%s'",recovery_cmd);
				system(recovery_cmd);
			}

            if(fForceReboot)
            {
                char achCmd[256];
                sprintf(achCmd,"%s %d",_config.szRebootCmd, fFailReboot?1:0);
                syslog(LOG_INFO, "run cmd='%s'",achCmd);
                system(achCmd);
            }
#else

			if(fForceReboot)
				syslog(LOG_INFO, "launching by periodic force rebooting... cmd='%s'",_config.szRebootCmd);
			else
				syslog(LOG_INFO, "launching by ping failure... failcnt=%d, cmd='%s'",cTotalFailCnt,_config.szRebootCmd);

			char achCmd[256];
			sprintf(achCmd,"%s %d",_config.szRebootCmd, fFailReboot?1:0);
			syslog(LOG_INFO, "run cmd='%s'",achCmd);
			
			
			stat=system(achCmd);
			
			// if fail
			if(stat<0) {
				syslog(LOG_ERR,"systam() call failed - %s",strerror(errno));
			}
			// if success
			else {
				childstat=WEXITSTATUS(stat);
				if(childstat==0) {
					syslog(LOG_ERR,"reboot command succeeded - reset reboot timer ");
					secS=secNow;
				}
				else {
					syslog(LOG_ERR,"reboot command returned error(%d) - retrying again 1 minute later",childstat);
				}
			}
#endif
			// wait until christmas
			sleep(60);
			cTotalFailCnt=0;
#if defined (PLATFORM_ANTELOPE) || defined (PLATFORM_ARACHNID) || \
    defined (V_PINGMONITOR_nbn)
			pingHost2SuccessCnt=0;
#endif

			nPeriodSec = _config.nPeriodSec;
		}
	}

	rdb_close_db();

	if(_config.fDaemon)
	{
		unlink(szLockFName);
		syslog(LOG_INFO, "exit");
	}

	return 0;
}


///////////////////////////////////////////////////////////////////////////////////////////////////
