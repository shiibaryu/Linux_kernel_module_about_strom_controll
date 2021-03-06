/*
 * ip_storm.c 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <linux/un.h>
#include <linux/genetlink.h>
#include <storm.h>

#include "libgenl.h"
#include "utils.h"
#include "ip_common.h"
#include "libnetlink.h"

#define TRAFFIC_TYPE_UNKNOWN_UNICAST    0x0001
#define TRAFFIC_TYPE_BROADCAST          0x0002
#define TRAFFIC_TYPE_MULTICAST          0x0004
#define PPS				0x0001
#define BPS				0x0002
#define LEVEL				0x0004
#define FLAG_UP				0x0001
#define FLAG_DOWN			0x0002

static struct rtnl_handle genl_rth;
static int genl_family = -1;

static void usage(void)__attribute__((noreturn));

void usage(void)
{
	fprintf(stderr,
		"Usage: ip storm add dev NAME\n"
		"          type { broadcast | multicast | unknown_unicast }\n"
		"          { pps | bps | level } threshold low_threshold\n"
		"\n"
		"       ip storm del dev NAME\n"
		"\n"
		"       ip storm show\n"
		"\n"
		);

	exit(-1);

}

static int parse_args(int argc,char **argv,struct storm_info *s_info)
{

	memset(s_info,0,sizeof(struct storm_info));
	
	if(argc < 1){
		usage();
	}
	
	while(argc > 0){
		if(strcmp(*argv,"dev") == 0){
			argc--;
			argv++;
			strncpy(s_info->if_name,*argv,STORM_DEVNAME_MAX);
		}
		else if(strcmp(*argv,"type") == 0){
			argc--;
			argv++;
			if(strcmp(*argv,"multicast") == 0){
				s_info->traffic_type |= TRAFFIC_TYPE_MULTICAST;
				s_info->first_flag = FLAG_UP;
				s_info->drop_flag = FLAG_DOWN;
			}
			else if(strcmp(*argv,"broadcast") == 0){
				s_info->traffic_type |= TRAFFIC_TYPE_BROADCAST;
				s_info->first_flag = FLAG_UP;
				s_info->drop_flag = FLAG_DOWN;
			}		
			else if(strcmp(*argv,"unknown_unicast") == 0){
				s_info->traffic_type |= TRAFFIC_TYPE_UNKNOWN_UNICAST;
				s_info->first_flag = FLAG_UP;
				s_info->drop_flag = FLAG_DOWN;
			}
		}
		else if(strcmp(*argv,"pps") == 0){
			s_info->pb_type = PPS;
			argc--;
			argv++;
			s_info->threshold = atoi(*argv);
			argc--;
			argv++;
			if(argc > 0){
				s_info->low_threshold = atoi(*argv);
			}
		}
		else if(strcmp(*argv,"bps") == 0){
			s_info->pb_type = BPS;
			argc--;
			argv++;
			s_info->threshold = atoi(*argv);
			argc--;
			argv++;
			if(argc > 0){
				s_info->low_threshold = atoi(*argv);
			}		
		}
		else{
			fprintf(stderr,
				"Error: Invalid argument \"%s\"\n", *argv);
			usage();
		}
		argc--;
		argv++;
	}

	return 0;
} 

static int do_add(int argc, char **argv)
{
	struct storm_info s_info;

	if(parse_args(argc,argv,&s_info)<0){
		return -1;
	}

	GENL_REQUEST(req,1024, genl_family, 0, STORM_GENL_VERSION,
		     STORM_CMD_ADD_IF, NLM_F_REQUEST | NLM_F_ACK);
	
	addattr_l(&req.n,1024,STORM_ATTR_IF,&s_info,sizeof(s_info));

	if (rtnl_talk(&genl_rth, &req.n, NULL) < 0){
			return -2;
	}

	return 0;


}

static int do_del(int argc, char **argv)
{
	struct storm_info s_info;

	if(parse_args(argc,argv,&s_info)<0){
		return -1;
	}

	GENL_REQUEST(req,1024, genl_family, 0, STORM_GENL_VERSION,
		     STORM_CMD_DEL_IF,NLM_F_REQUEST | NLM_F_ACK);

	addattr_l(&req.n,1024,STORM_ATTR_IF,&s_info,sizeof(s_info));

	if (rtnl_talk(&genl_rth, &req.n, NULL) < 0){
			return -2;
	}

	return 0;

}

static void print_if(struct storm_info *s_info)
{
	unsigned short res=0;

	if((res = (s_info->traffic_type >> 1)) & 1){
		if(s_info->pb_type & PPS){
			printf("%s broadcast pps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
		else if(s_info->pb_type & BPS){
			printf("%s broadcast bps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
	}
	if((res = (s_info->traffic_type >> 2)) & 1){
		if(s_info->pb_type & PPS){
			printf("%s multicast pps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
		else if(s_info->pb_type & BPS){
			printf("%s multicast bps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
	}
	if((res = (s_info->traffic_type >> 0)) & 1){
		if(s_info->pb_type & PPS){
			printf("%s unknown_unicast pps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
		else if(s_info->pb_type & BPS){
			printf("%s unknown_unicast bps %d %d\n",s_info->if_name,s_info->threshold,s_info->low_threshold);
		}
	}
}
static int storm_show(const struct sockaddr_nl *who,struct nlmsghdr *n,void *arg)
{
	struct storm_info s_info;
	struct genlmsghdr *ghdr;
	struct rtattr *attrs[STORM_ATTR_MAX + 1];
	int len;

	if (n->nlmsg_type == NLMSG_ERROR){
		return -EBADMSG;
	}

	ghdr = NLMSG_DATA(n);
	len = n->nlmsg_len - NLMSG_LENGTH(sizeof(*ghdr));
	if (len < 0){
		return -1;
	}

	parse_rtattr(attrs, STORM_ATTR_MAX,
		     (void *)ghdr + GENL_HDRLEN, len);

	if (!attrs[STORM_ATTR_IF]) {
		fprintf(stderr, "%s: ifkviv not found in the nlmsg\n",
			__func__);
		return -EBADMSG;
	}

	memcpy(&s_info, RTA_DATA(attrs[STORM_ATTR_IF]),
	       sizeof(s_info));

	print_if(&s_info);

	return 0;
}

static int do_show(int argc,char **argv){

	GENL_REQUEST(req, 128, genl_family, 0,
		     STORM_GENL_VERSION, STORM_CMD_SHOW_IF,
		     NLM_F_ROOT | NLM_F_MATCH | NLM_F_REQUEST);

	req.n.nlmsg_seq = genl_rth.dump = ++ genl_rth.seq;

	if (rtnl_send(&genl_rth, &req, req.n.nlmsg_len) < 0)
		return -2;

	if (rtnl_dump_filter(&genl_rth,storm_show, NULL) < 0) {
		fprintf(stderr, "Dump terminated\n");
		exit(1);
	}

	return 0;
}

int do_ipstorm(int argc, char **argv){

	if (argc < 1 || !matches(*argv, "help")){
		usage();
	}

	if (genl_init_handle(&genl_rth,STORM_GENL_NAME,&genl_family)){
		exit(-1);
	}
	
	if(matches(*argv,"add") == 0){
		return do_add(argc - 1, argv + 1);
	}
	if(matches(*argv,"del") == 0 ||
		matches(*argv,"delete") == 0){
			return do_del(argc - 1 , argv + 1);
	}
	if(matches(*argv,"show") == 0){
		return do_show(argc - 1,argv + 1);
	}

	fprintf(stderr,
		"Command \"%s\" is unkonw, type \"ip storm help\".\n", *argv);

	exit(-1);
}