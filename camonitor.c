
static char *sccsId = "@(#)camonitor.c	1.22\t11/8/93";

/* camonitor.c 
 *
 *      Author: Janet Anderson
 *      Date:   11-08-93
 * 
 *      Experimental Physics and Industrial Control System (EPICS)
 *
 *      Copyright 1991, the Regents of the University of California,
 *      and the University of Chicago Board of Governors.
 *
 *      This software was produced under  U.S. Government contracts:
 *      (W-7405-ENG-36) at the Los Alamos National Laboratory,
 *      and (W-31-109-ENG-38) at Argonne National Laboratory.
 *
 *      Initial development by:
 *              The Controls and Automation Group (AT-8)
 *              Ground Test Accelerator
 *              Accelerator Technology Division
 *              Los Alamos National Laboratory
 *
 *      Co-developed with
 *              The Controls and Computing Group
 *              Accelerator Systems Division
 *              Advanced Photon Source
 *              Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 *              Argonne National Laboratory
 *
 * Modification Log:
 * -----------------
 * .nn  mm-dd-yy        iii     Comment
 *      ...
 */

#include <stdio.h>
#include <stdlib.h>

#include <fdmgr.h>
#include <cadef.h>
#include <tsDefs.h>
#include <alarm.h>			/* alarm status, severity     */
#include <alarmString.h>

#define FDMGR_SEC_TIMEOUT        10              /* seconds       */
#define FDMGR_USEC_TIMEOUT       0               /* micro-seconds */

#define CA_PEND_EVENT_TIME	0.001

#define TRUE            1
#define FALSE           0

/* globals */
int DEBUG;


/* prototypes for static routines */
#ifdef __STDC__

void processChangeConnectionEvent( struct connection_handler_args args);
void processNewEvent( struct event_handler_args args);

#else

void processChangeConnectionEvent();
void processNewEvent();

#endif /*__STDC__*/





void addMonitor(channelName)
  char *channelName;
{
  int status;
  chid chid;
  evid evid;

  if (DEBUG) printf("addMonitor for [%s]\n",channelName);

  status = ca_search(channelName,&chid);
  if (status != ECA_NORMAL)
    printf("ca_search failed on channel name: [%s]\n",channelName);

  ca_pend_io(3.0);

  status = ca_add_masked_array_event(DBR_TIME_STRING, 1, chid, processNewEvent,
     NULL, (float)0,(float)0,(float)0, &evid, DBE_VALUE|DBE_ALARM);
  if (status != ECA_NORMAL)
    printf("ca_add_masked_array_event failed on channel name: [%s]\n", channelName);

  if (ca_field_type(chid) == TYPENOTCONN)
    printf("[%s] not connected\n",channelName);

  /* add change connection event */
  ca_change_connection_event(chid, processChangeConnectionEvent);

}

void processChangeConnectionEvent(args)
  struct connection_handler_args args;
{
  if (DEBUG) printf("processChangeConnectionEvent for [%s]\n",ca_name(args.chid));

  if (args.op == CA_OP_CONN_DOWN)
     printf ("[%s] not connected\n",ca_name(args.chid));
}


void processNewEvent(args)
  struct event_handler_args args;
{
  struct dbr_time_string *cdData;
  char    timeText[28];

  if (DEBUG) printf("processNewEvent for [%s]\n",ca_name(args.chid));

  cdData = (struct dbr_time_string *) args.dbr;

  (void)tsStampToText(&cdData->stamp, TS_TEXT_MMDDYY, timeText);

  if (cdData->severity)
    printf(" %-30s %s %s %s %s\n",
      ca_name(args.chid), timeText, cdData->value,
      alarmStatusString[cdData->status],
      alarmSeverityString[cdData->severity]); 
  else 
    printf(" %-30s %s %s \n",
      ca_name(args.chid), timeText, cdData->value);

}

void processCA(notused)
  void *notused;
{
  ca_pend_event(CA_PEND_EVENT_TIME);
}


void registerCA(pfdctx,fd,condition)
  void *pfdctx;
  int fd;
  int condition;
{
  if (DEBUG) printf("registerCA with condition: %d\n",condition);

  if (condition) fdmgr_add_fd(pfdctx, fd, processCA, NULL);
  else fdmgr_clear_fd(pfdctx, fd);
}





void main(argc, argv)
     int argc;
     char *argv[];
{
     void *pfdctx;			/* fdmgr context */
     extern char *optarg; /* needed for getopt() */
     extern int optind;   /* needed for getopt() */
     int input_error;
     int c;
     int i;
     static struct timeval timeout = {FDMGR_SEC_TIMEOUT, FDMGR_USEC_TIMEOUT};

     /*  initialize channel access */
     SEVCHK(ca_task_initialize(),
		"initializeCA: error in ca_task_initialize");
 
     /* initialize fdmgr */
     pfdctx = (void *) fdmgr_init();

     /* add CA's fd to fdmgr...  */
     SEVCHK(ca_add_fd_registration(registerCA,pfdctx),
          "initializeCA: error adding CA's fd to X");

     /* get command line options if any  */
     DEBUG = FALSE;
     input_error = 0;
     while (!input_error && (c = getopt(argc, argv, "vc")) != -1)
     {
         switch (c)
         {
             case 'v': DEBUG = TRUE; break;
             case '?': input_error = 1; break;
             default: input_error = 1; break;
         }
     }
     if (input_error || optind >= argc) {
          fprintf(stderr, "\n \tusage: %s PVname PVname ... \n\n",argv[0]);
          exit(1);
     }
 
     /* add ca monitor for each  PVname on the command line */
     for (i = 0;  optind < argc; optind++, i ++){
       if (DEBUG) printf("PVname%d: %s\n",i,argv[optind]);
       addMonitor(argv[optind]);
     }

     ca_pend_event(CA_PEND_EVENT_TIME);

     /* start  events loop */
     while(TRUE) {
          fdmgr_pend_event(pfdctx,&timeout);
     }
}

