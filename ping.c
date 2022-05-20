#include <stdio.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/ip_icmp.h>
#include <time.h>
#include <syslog.h>

#define PING_TIMEOUT  5

extern clock_t __getSecTickCount(void);
extern int _fTermSigDetected;

static char* timeString(char* buf, unsigned int maxlen)
{
	char* cp;
	char tbuf[30];
	time_t now;
	time(&now);
	ctime_r(&now, tbuf);
	if ((cp = strchr(tbuf, '\n')) != NULL)
		* cp = '\0';
	if ((cp = strchr(tbuf, '\r')) != NULL)
		* cp = '\0';
	strncpy(buf, tbuf, maxlen - 1);
	buf[maxlen - 1] = '\0';
	return buf;
}

static int in_cksum(unsigned short* buf, int sz)
{
	int nleft = sz;
	int sum = 0;
	unsigned short* w = buf;
	unsigned short ans = 0;

	while (nleft > 1)
	{
		sum += * w++;
		nleft -= 2;
	}

	if (nleft == 1)
	{
		*(unsigned char*)(&ans) = *(unsigned char*)w;
		sum += ans;
	}

	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	ans = ~sum;
	return (ans);
}

int ping(const char* host,const char* src)
{
	static const int DEFDATALEN = 56;
	static const int MAXIPLEN = 60;
	static const int MAXICMPLEN = 76;

	struct hostent* h;
	struct sockaddr_in pingaddr;
	struct sockaddr_in pingsrcaddr;
	struct icmp* pkt;
	int pingsock, c;
	char packet[DEFDATALEN+MAXIPLEN+MAXICMPLEN];
	struct sockaddr_in from;
	struct timeval tv;
	fd_set readset;
	int ret;
	struct iphdr* iphdr;
	static int same_bad_result = 0;
	char tbuf[30];

	clock_t pingTime, currTime;
	u_int16_t last_icmp_id, last_icmp_seq;

	int success = 0;
	size_t fromlen = sizeof(from);

	if ((pingsock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1)
	{
		syslog(LOG_ERR,"Could not create ICMP socket, errno is %d\r\n", errno);
		success = -1; // Socket Error
		return success;
	}

	// TT
	// bind into a specific local address if asked
	if(src) 
	{
		if ((h = gethostbyname(src)) == 0)
		{
			syslog(LOG_INFO, "ping: source addr: domain name lookup failed - %s",host);
			close(pingsock);
			success = -2; // DNS lookup failure
			return success;
		}

		memset(&pingsrcaddr, 0, sizeof(struct sockaddr_in));
		pingsrcaddr.sin_family = AF_INET;
		memcpy(&pingsrcaddr.sin_addr, h->h_addr_list[0], sizeof(pingsrcaddr.sin_addr));
      		if(bind(pingsock, &pingsrcaddr, sizeof(struct sockaddr_in)) != 0)
		{
			syslog(LOG_INFO, "ping: source addr: bind failed - %s",host);
			close(pingsock);
			success = -8; // bind failure
			return success;
		}
	}
		

	memset(&pingaddr, 0, sizeof(struct sockaddr_in));

	pingaddr.sin_family = AF_INET;

	if ((h = gethostbyname(host)) == 0)
	{
		syslog(LOG_INFO, "ping: domain name lookup failed - %s",host);
		close(pingsock);
		success = -2; // DNS lookup failure
		return success;
	}

	memcpy(&pingaddr.sin_addr, h->h_addr_list[0], sizeof(pingaddr.sin_addr));

	static unsigned short ntransmitted=0;

	pkt = (struct icmp*)packet;
	memset(pkt, 0, sizeof(packet));
	pkt->icmp_type = ICMP_ECHO;
	pkt->icmp_seq = last_icmp_seq = htons(ntransmitted++);
	pkt->icmp_id = last_icmp_id = htons(getpid());
	pkt->icmp_cksum = in_cksum((unsigned short*)pkt, sizeof(packet));

	c = sendto(pingsock, packet, sizeof(packet), 0, (struct sockaddr*) & pingaddr, sizeof(struct sockaddr_in));
	if (c < 0)
	{
		success = -3; // Sendto failed
	}
	else
	{
		pingTime = __getSecTickCount();

		while(!_fTermSigDetected) {
			currTime = __getSecTickCount();
			if (currTime - pingTime > PING_TIMEOUT) {
				success = -4; // Timeout 
				break;
			}

			FD_ZERO(&readset);
			FD_SET(pingsock, &readset);
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			//fprintf(stderr,"calling select\r\n");
			ret = select(pingsock + 1, &readset, (fd_set*)0, (fd_set*)0, &tv);
			//fprintf(stderr,"called select and ret is %d\r\n",ret);

			// check for a timeout.........
			if (ret == 0)
			{
				continue;
			}
			else if (ret != -1)
			{

				//fprintf(stderr,"calling recvfrom\r\n", host);
				if ((c = recvfrom(pingsock, packet, sizeof(packet), 0, (struct sockaddr*) & from, &fromlen)) < sizeof(struct icmp) + sizeof(struct iphdr) )
				{
					success = -5;
					continue; //not ours, ignore.
				}
				else
				{
					//fprintf(stderr,"c is > 76\r\n");
					iphdr = (struct iphdr*)packet;

					pkt = (struct icmp*)(packet + (iphdr->ihl << 2)); // skip ip hdr

					if (pkt->icmp_type == ICMP_ECHOREPLY && pkt->icmp_id == last_icmp_id && pkt->icmp_seq == last_icmp_seq)
					{
						success = 1;
						break;
					}
					else
					{
						continue; //not ours, ignore.
					}
				}
			}
			else
			{
				success = -7; // >? unknown failure from select()
				break;
			}
		}
	}
	if (success >= 0 || success != same_bad_result)
	{
		syslog(LOG_INFO, "%s ping %s result %d", timeString(tbuf, sizeof(tbuf)), host, success);
		same_bad_result = success;
	}

	close(pingsock);

	return success;
}
