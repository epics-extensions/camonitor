
static char *sccsId = "@(#) $Id$";

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
#include <string.h>

#include "fdmgr.h"
#include "cvtFast.h"
#include "cadef.h"
#include "tsDefs.h"
#include "alarm.h"			/* alarm status, severity     */
#include "alarmString.h"

#include "camonitorVersion.h"

#define FDMGR_SEC_TIMEOUT        10              /* seconds       */
#define FDMGR_USEC_TIMEOUT       0               /* micro-seconds */

#define CA_PEND_EVENT_TIME	0.001
#define CONNECTION_WAIT_SECONDS	3.0

#define TRUE            1
#define FALSE           0

/* globals */
int DEBUG;

/* forward declarations */
static void processAccessRightsEvent(struct access_rights_handler_args args);
void processChangeConnectionEvent( struct connection_handler_args args);
void processNewEvent( struct event_handler_args args);


void addMonitor(char *channelName)
{
  int status;
  chid chid;
  time_t startTime, currentTime;


  if (DEBUG) printf("addMonitor for [%s]\n",channelName);

  status = ca_search_and_connect(channelName,&chid,processChangeConnectionEvent,NULL);
  SEVCHK(status,"ca_search_and_connect failed\n");
  if (status != ECA_NORMAL) return;
  ca_set_puser(chid,FALSE);

  currentTime = time(&startTime);
  while ((ca_field_type(chid) == TYPENOTCONN) &&
    (difftime(currentTime, startTime)<CONNECTION_WAIT_SECONDS) ){
    ca_pend_event(.1);
    time(&currentTime);
  }
  if (ca_field_type(chid) == TYPENOTCONN){
    printf("[%s] not connected\n",channelName);
  }
}

static void processAccessRightsEvent(struct access_rights_handler_args args)
{
  if (DEBUG) printf("processAccessRightsEvent for [%s]\n",ca_name(args.chid));

  if (ca_field_type(args.chid) == TYPENOTCONN) return;
  if (!ca_read_access(args.chid)) {
     printf (" %s  no read access\n",ca_name(args.chid));
  }
  if (!ca_write_access(args.chid)) {
     printf (" %s  no write access\n",ca_name(args.chid));
  }
}

void startMonitor (chid chan, dbr_short_t *pprecision)
{
    int request_type;
    int status;
    evid evid;

    if (ca_field_type(chan) == DBF_ENUM ) {
        request_type = DBR_TIME_STRING;
    }
    else {
        request_type = dbf_type_to_DBR_TIME(ca_field_type(chan));
    }

    status = ca_add_masked_array_event (request_type, 
        ca_element_count(chan), chan, processNewEvent,
       pprecision, 0.0f, 0.0f, 0.0f, &evid, DBE_VALUE|DBE_ALARM);
    SEVCHK(status,"ca_add_masked_array_event failed\n");
}

void getPrecisionCallBack (struct event_handler_args args)
{
    const struct dbr_gr_float *pvalue = args.dbr;
    dbr_short_t *pprecision;

    if (args.status!=ECA_NORMAL) {
        fprintf (stderr, "dbr_gr_float get call back failed on analog channel \"%s\" because \"%s\"\n",
                ca_name(args.chid), ca_message(args.status));
        fprintf (stderr, "Unable to monitor PV\n");
        return;
    }
    pprecision = (dbr_short_t *)calloc(1,sizeof(dbr_short_t));
    if (!pprecision) {
        fprintf (stderr, "memory allocation failed\n");
        fprintf (stderr, "Unable to monitor PV\n");
    }
    *pprecision = pvalue->precision;
    startMonitor (args.chid, pprecision);
}

void processChangeConnectionEvent(struct connection_handler_args args)
{
  int status;

  if (DEBUG) printf("processChangeConnectionEvent for [%s]\n",ca_name(args.chid));

  if (args.op == CA_OP_CONN_DOWN) {
     printf ("[%s] not connected\n",ca_name(args.chid));
  } 
  else {
    if (ca_puser(args.chid) == (READONLY void *)TRUE) return;
    ca_set_puser(args.chid,TRUE);
    if (DEBUG) {
        printf ("Number of elements  for [%s] is %d\n",
            ca_name(args.chid), ca_element_count(args.chid));
    }

    if (ca_field_type(args.chid) == DBF_DOUBLE ||
        ca_field_type(args.chid) == DBF_FLOAT ) {
        status = ca_get_callback (DBR_GR_FLOAT, args.chid, getPrecisionCallBack, NULL);
        SEVCHK(status,"ca_get_callback() for precision failed\n");
    }
    else {
        startMonitor (args.chid, NULL);
    }

    status = ca_replace_access_rights_event(args.chid, processAccessRightsEvent);
    SEVCHK (status, "ca_replace_access_rights_event failed\n");
  }
}

void processNewEvent(struct event_handler_args args)
{
  struct dbr_time_string *cdData;
  char    timeText[28];
  int i;
  int count;
  int type;
  void *pbuffer;

  if (DEBUG) printf("processNewEvent for [%s]\n",ca_name(args.chid));

  cdData = (struct dbr_time_string *) args.dbr;
  (void)tsStampToText(&cdData->stamp, TS_TEXT_MMDDYY, timeText);
  printf(" %-30s %s ", ca_name(args.chid), timeText);

  count = args.count;
  pbuffer = (void *)args.dbr;
  type = args.type;

  switch(type){
  case (DBR_TIME_STRING):
  {
    struct dbr_time_string *pvalue 
      = (struct dbr_time_string *) pbuffer;

    printf("%s ",pvalue->value);
    break;
  }
  case (DBR_TIME_ENUM):
  {
    struct dbr_time_enum *pvalue
      = (struct dbr_time_enum *)pbuffer;
    dbr_enum_t *pshort = &pvalue->value;

    for (i = 0; i < count; i++,pshort++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      printf("%d ",*pshort);
    }
    break;
  }
  case (DBR_TIME_SHORT):
  {
    struct dbr_time_short *pvalue
      = (struct dbr_time_short *)pbuffer;
    dbr_short_t *pshort = &pvalue->value;

    for (i = 0; i < count; i++,pshort++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      printf("%d ",*pshort);
    }
    break;
  }
  case (DBR_TIME_FLOAT):
  {
    struct dbr_time_float *pvalue
      = (struct dbr_time_float *)pbuffer;
    dbr_float_t *pfloat = &pvalue->value;
    char string[MAX_STRING_SIZE];
    dbr_short_t *pprecision =(short*)args.usr;

    for (i = 0; i < count; i++,pfloat++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      cvtFloatToString(*pfloat,string,*pprecision);
      printf("%s ",string);
    }
    break;
  }
  case (DBR_TIME_CHAR):
  {
    struct dbr_time_char *pvalue
      = (struct dbr_time_char *)pbuffer;
    dbr_char_t *pchar = &pvalue->value;

    for (i = 0; i < count; i++,pchar++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      printf("%d ",(short)(*pchar));
    }
    break;
  }
  case (DBR_TIME_LONG):
  {
    struct dbr_time_long *pvalue
      = (struct dbr_time_long *)pbuffer;
    dbr_long_t *plong = &pvalue->value;

    for (i = 0; i < count; i++,plong++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      printf("%d ",*plong);
    }
    break;
  }
  case (DBR_TIME_DOUBLE):
  {
    struct dbr_time_double *pvalue
      = (struct dbr_time_double *)pbuffer;
    dbr_double_t *pdouble = &pvalue->value;
    char string[MAX_STRING_SIZE];
    dbr_short_t *pprecision =(short*)args.usr;

    for (i = 0; i < count; i++,pdouble++){
      if(count!=1 && (i%10 == 0)) printf("\n");
      cvtDoubleToString(*pdouble,string,*pprecision);
      printf("%s ",string);
    }
    break;
  }
  }

  if (cdData->severity)
    printf(" %s %s",
      alarmStatusString[cdData->status],
      alarmSeverityString[cdData->severity]); 

  printf("\n");
  fflush(0);
}

void processCA(void *notused)
{
  ca_pend_event(CA_PEND_EVENT_TIME);
}


void registerCA(void *pfdctx,int fd,int condition)
{
  if (DEBUG)  printf("registerCA with condition: %d\n",condition);

  if (condition){
    fdmgr_add_fd(pfdctx, fd, processCA, NULL);
  } else {
    fdmgr_clear_fd(pfdctx, fd);
  }
}

void main(int argc,char *argv[])
{
   void *pfdctx;			/* fdmgr context */
   int printHelp=FALSE;
   int printVersion=FALSE;
   int i=1;
   int pvcount=0;
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
   while (i < argc)
   {
      if (strcmp(argv[i],"-debug")== 0 ) { DEBUG = TRUE;}
      else if (strcmp(argv[i],"\\debug")  ==0 ){ DEBUG = TRUE;}
      else if (strcmp(argv[i],"-v")       ==0 ) {printVersion=TRUE; break; }
      else if (strcmp(argv[i],"\\v")      ==0 ) {printVersion=TRUE; break; }
      else if (strcmp(argv[i],"-version") ==0 ) {printVersion=TRUE; break; }
      else if (strcmp(argv[i],"\\version")==0 ) {printVersion=TRUE; break; }
      else if (strcmp(argv[i],"?")        ==0 ) {printHelp=TRUE; break; }
      else if (strncmp(argv[i],"-",1)     ==0 ) {printHelp=TRUE; break; }
      else if (strncmp(argv[i],"\\",1)    ==0 ) {printHelp=TRUE; break; }
      else  {
        /* add ca monitor for each  PVname on the command line */
        if (DEBUG) printf("PVname%d: %s\n",i,argv[i]);
        addMonitor(argv[i]);
        pvcount++;
      }
     i++;
   }

   if (printVersion) {
      fprintf(stderr, "%s\n",camonitorVersion);
      exit(1);
   }
 
   if (printHelp) {
      fprintf(stderr, "\n \tusage: %s PVname PVname ... \n\n",argv[0]);
      exit(1);
   }
 
   if(DEBUG) printf("pvcount=%d\n",pvcount);

   if(!pvcount) {
      exit(0);
   }

   ca_pend_event(CA_PEND_EVENT_TIME);

   /* start  events loop */
   while(TRUE) {
      fdmgr_pend_event(pfdctx,&timeout);
   }
}

