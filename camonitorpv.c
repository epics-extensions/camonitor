
/*
 * $Id$
 * $Log$
 * Revision 1.1  1997/02/12 18:04:42  jbk
 * added new program to monitor pv and call script when value changes
 *
 *
 * Monitor a PV and call a program/script each time the value changes
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <wait.h>
#include <sys/types.h>
#include <sys/time.h>

#include "cadef.h"
#include "db_access.h"

typedef struct access_rights_handler_args ACCESS_ARGS;
typedef struct connection_handler_args CONNECT_ARGS;
typedef struct exception_handler_args EXCEPT_ARGS;
typedef evargs EVENT_ARGS;

#define REALLY_SMALL 0.0000001

typedef void (*OLD_SIG_FUNC)(int);

fd_set all_fds;
int do_not_exit=1;
char pv_name[50];
char pv_value[50];
char* script_name;
int pv_type;
chid id;
evid event;

static void conCB(CONNECT_ARGS args);
static void exCB(EXCEPT_ARGS args);
static void evCB(evargs args);
static void accCB(ACCESS_ARGS args);
static void fdCB(void* ua, int fd, int opened);
static void getCB(EVENT_ARGS args);

static void sig_func(int x)
{
	do_not_exit=0;
	return;
}

static void sig_chld(int sig)
{
	while(waitpid(-1,NULL,WNOHANG)>0);
	signal(SIGCHLD,sig_chld);
}

int main(int argc, char** argv)
{
	char buf[80];
	fd_set rfds;
	int tot;
	struct timeval tv;

	if(argc!=3)
	{
		fprintf(stderr,"Usage: %s PV_to_monitor script_to_run\n\n",argv[0]);
		fprintf(stderr,"This program monitored PV PV_to_monitor and\n");
		fprintf(stderr,"runs and executes script_to_run, which can be\n");
		fprintf(stderr,"any program or executable script.\n");
		fprintf(stderr,"The script or program gets invoked with the first\n");
		fprintf(stderr,"argument as the PV name and the second argument\n");
		fprintf(stderr,"as the value of the PV\n");
		fprintf(stderr,"The program or shell script script_to_run is\n");
		fprintf(stderr,"run as a separate child process of this program\n");
		fprintf(stderr,"This means that your script or program will not\n");
		fprintf(stderr,"stop this process from running, if fact your\n");
		fprintf(stderr,"script or program can be invoked many times if\n");
		fprintf(stderr,"the value of the PV is changing rapidly and\n");
		fprintf(stderr,"several instances of your program could be running\n");
		fprintf(stderr,"simultaneously.\n");
		return -1;
	}

	strcpy(pv_name,argv[1]);
	script_name=argv[2];
	pv_value[0]='\0';

	if(access(script_name,X_OK)<0)
	{
		fprintf(stderr,"Script %s not found or not executable\n",script_name);
		return -1;
	}

	signal(SIGINT,sig_func);
	signal(SIGQUIT,sig_func);
	signal(SIGTERM,sig_func);
	signal(SIGHUP,sig_func);
	signal(SIGCHLD,sig_chld);

	FD_ZERO(&all_fds);

	SEVCHK(ca_task_initialize(),"task initialize");
	SEVCHK(ca_add_fd_registration(fdCB,&all_fds),"add fd registration");
	SEVCHK(ca_add_exception_event(exCB,NULL),"add exception event");

	SEVCHK(ca_search_and_connect(pv_name,&id,conCB,NULL),
		"search and connect");
	SEVCHK(ca_replace_access_rights_event(id,accCB),
		"replace access rights event");

	/* SEVCHK(ca_add_event(DBR_TIME_STRING,data.id,evCB,&data,&data.event),
		"add event"); */

	ca_pend_event(REALLY_SMALL);

	/* fprintf(stderr,"Monitoring <%s>\n",pv_name); */

	while(do_not_exit)
	{
		rfds=all_fds;
		tv.tv_sec=1;
		tv.tv_usec=0; /*200000*/;

		switch(tot=select(FD_SETSIZE,&rfds,NULL,NULL,&tv))
		{
		/*
		case -1:
			perror("select error - bad");
			break;
		*/
		case 0:
			ca_pend_event(REALLY_SMALL);
			break;
		default:
			/* fprintf(stderr,"select data ready\n"); */
			ca_pend_event(REALLY_SMALL);
			break;
		}
	}

	fprintf(stderr,"PV monitor program is exiting!\n");
	ca_task_exit();
	return 0;
}

static void fdCB(void* ua, int fd, int opened)
{
	fd_set* fds = (fd_set*)ua;

	/* fprintf(stderr,"fdCB: openned=%d, fd=%d\n",opened,fd); */

	if(opened)
		FD_SET(fd,fds);
	else
		FD_CLR(fd,fds);
}

static void conCB(CONNECT_ARGS args)
{
	/* void* node=(void*)ca_puser(args.chid); */
	long rc;
/*
	fprintf(stderr,"exCB: -------------------------------\n");
	fprintf(stderr,"conCB: name=%s\n",ca_name(args.chid));
	fprintf(stderr,"conCB: type=%d\n",ca_field_type(args.chid));
	fprintf(stderr,"conCB: number elements=%d\n",ca_element_count(args.chid));
	fprintf(stderr,"conCB: host name=%s\n",ca_host_name(args.chid));
	fprintf(stderr,"conCB: read access=%d\n",ca_read_access(args.chid));
	fprintf(stderr,"conCB: write access=%d\n",ca_write_access(args.chid));
	fprintf(stderr,"conCB: state=%d\n",ca_state(args.chid));
*/
	if(ca_state(args.chid)==cs_conn)
	{
		/* issue a get */
		if(ca_field_type(args.chid)==DBF_ENUM)
		{
			pv_type=DBF_ENUM;
			rc=ca_array_get_callback(DBR_GR_ENUM,1,id,getCB,NULL);
		}
		else
		{
			pv_type=DBF_STRING;
			rc=ca_array_get_callback(DBR_STRING,1,id,getCB,NULL);
		}
		SEVCHK(rc,"get with callback bad");
	}
	else
		fprintf(stderr,"PV <%s> not connected\n",pv_name);
}

static void getCB(EVENT_ARGS args)
{
	/* void* node=(void*)ca_puser(args.chid); */
	struct dbr_gr_enum* edb;
	char* cdb;
	
	if(args.status==ECA_NORMAL)
	{
		if(pv_type==DBF_ENUM)
		{
			edb=(struct dbr_gr_enum*)args.dbr;
			cdb=&edb->strs[edb->value][0];
			/* strncpy(pv_value,cdb,MAX_ENUM_STRING_SIZE); */
			SEVCHK(ca_add_event(DBR_GR_ENUM,id,evCB,NULL,&event),
				"add event");
		}
		else
		{
			cdb=(char*)args.dbr;
			/* strncpy(pv_value,cdb,MAX_STRING_SIZE); */
			SEVCHK(ca_add_event(DBR_STRING,id,evCB,NULL,&event),
				"add event");
		}
	}
	else
		fprintf(stderr,"PV <%s> get failed\n",pv_name);
}

static void exCB(EXCEPT_ARGS args)
{
	fprintf(stderr,"exCB: -------------------------------\n");
	fprintf(stderr,"exCB: name=%s\n",ca_name(args.chid));
	fprintf(stderr,"exCB: type=%d\n",ca_field_type(args.chid));
	fprintf(stderr,"exCB: number of elements=%d\n",ca_element_count(args.chid));
	fprintf(stderr,"exCB: host name=%s\n",ca_host_name(args.chid));
	fprintf(stderr,"exCB: read access=%d\n",ca_read_access(args.chid));
	fprintf(stderr,"exCB: write access=%d\n",ca_write_access(args.chid));
	fprintf(stderr,"exCB: state=%d\n",ca_state(args.chid));
	fprintf(stderr,"exCB: -------------------------------\n");
	fprintf(stderr,"exCB: type=%d\n",args.type);
	fprintf(stderr,"exCB: count=%d\n",args.count);
	fprintf(stderr,"exCB: status=%d\n",args.stat);
}

static void evCB(evargs args)
{
	/* void* p = (void*)ca_puser(args.chid); */
	struct dbr_gr_enum* edb;
	char* cdb;
	int len;

	if(args.status!=ECA_NORMAL)
	{
		/*
		fprintf(stderr,"evCB: ------NOT-NORMAL---------------\n");
		fprintf(stderr,"evCB: name=%s\n",ca_name(args.chid));
		fprintf(stderr,"evCB: type=%d\n",ca_field_type(args.chid));
		fprintf(stderr,"evCB: number ele=%d\n",ca_element_count(args.chid));
		fprintf(stderr,"evCB: host name=%s\n",ca_host_name(args.chid));
		fprintf(stderr,"evCB: read access=%d\n",ca_read_access(args.chid));
		fprintf(stderr,"evCB: write access=%d\n",ca_write_access(args.chid));
		fprintf(stderr,"evCB: state=%d\n",ca_state(args.chid));
		*/
		fprintf(stderr,"Event receive failure for <%s>\n",pv_name);
	}
	else
	{
		/* fprintf(stderr,"evCB: %s=%s\n",ca_name(args.chid),args.dbr); */
		if(pv_type==DBF_ENUM)
		{
			edb=(struct dbr_gr_enum*)args.dbr;
			cdb=&edb->strs[edb->value][0];
			len=MAX_ENUM_STRING_SIZE;
		}
		else
		{
			cdb=(char*)args.dbr;
			len=MAX_STRING_SIZE;
		}
		if(strcmp(pv_value,cdb)!=0)
		{
			strncpy(pv_value,cdb,len);
			/* run the script now */
			switch(fork())
			{
			case -1: /* error */
				perror("Cannot create gateway processes");
				break;
			case 0: /* child */
				/* script exec */
				execlp(script_name,"user_script",pv_name,pv_value,NULL);
				perror("Execute of script failed");
				exit(1);
				break;
			default: /* parent */
				break;
			}
		}
	}
}

static void accCB(ACCESS_ARGS args)
{
	/*
	fprintf(stderr,"accCB: -------------------------------\n");
	fprintf(stderr,"accCB: name=%s\n",ca_name(args.chid));
	fprintf(stderr,"accCB: type=%d\n",ca_field_type(args.chid));
	fprintf(stderr,"accCB: number elements=%d\n",ca_element_count(args.chid));
	fprintf(stderr,"accCB: host name=%s\n",ca_host_name(args.chid));
	fprintf(stderr,"accCB: read access=%d\n",ca_read_access(args.chid));
	fprintf(stderr,"accCB: write access=%d\n",ca_write_access(args.chid));
	fprintf(stderr,"accCB: state=%d\n",ca_state(args.chid));
	*/
}

