/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution. 
\*************************************************************************/

static char *sccsId = "@(#) $Id$";

/* camonitor.c 
 *
 *      Author: Janet Anderson
 *      Date:   11-08-93
 * 
 *      Experimental Physics and Industrial Control System (EPICS)
 *
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

#define CAM_ADD            1  
#define CAM_REMOVE         2
#define MAX_CHAN_MON     100
#define MAX_CHAN_NAM_LEN 100

#define TRUE            1
#define FALSE           0

/* globals */
int DEBUG;

struct chanDB_s {      /* global database of channels and associated names */
  chid chid;            /* these are the channels we currently have mon's on */
  char chanNam[MAX_CHAN_MON];
} DB_as[MAX_CHAN_NAM_LEN] = {{0}};    /* zero chid means slot not used */

/* forward declarations */
static void processAccessRightsEvent(struct access_rights_handler_args args);
void processChangeConnectionEvent( struct connection_handler_args args);
void processNewEvent( struct event_handler_args args);

/*
 * Channel Database.   Called by addMonitor and remMonitor to add or remove
 * a channel to it's local database (table).  
 * func is either ADD or REMOVE
 * Returns 0 for failure. 1 for success.
 */
int chanDB (chid* chid, char* channelName, int func)
{ 
  int ii;   /* loop counter */

  if (func == CAM_ADD) {
    for ( ii = 0; ii<MAX_CHAN_MON; ii++) {
      if (DB_as[ii].chid == 0) {             /* slot is available */
	DB_as[ii].chid = *chid;
        strncpy (DB_as[ii].chanNam, channelName, MAX_CHAN_NAM_LEN);
	break;
      }
      if (ii == MAX_CHAN_MON -1) {
        printf("ERROR: Array overflow in chanDB\n");
        return (0);
      }
    }
  }
  else if (func == CAM_REMOVE) {
    for ( ii = 0; ii<MAX_CHAN_MON; ii++) {
      if (strcmp(channelName, DB_as[ii].chanNam) == 0) { /* found chanName*/
        *chid = DB_as[ii].chid;                      /* return chid to caller*/ 
        strcpy(DB_as[ii].chanNam,"     ");           /* free up slot */
	DB_as[ii].chid = 0;                             
        break;
      }
      if (ii == MAX_CHAN_MON -1) {
        printf("ERROR: Channel not found in chanDB Database\n");
        return (0);
      }
    }
  }
  else {                                            /* else bad func code */
    return(0);
  }

  return(1);
}

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
  else {
    chanDB(&chid, channelName, CAM_ADD);  /* save chid so we can remove */
  }
}

/*
 * Remove a monitor on input channelName 
 */
void remMonitor(char *channelName)
{
  int status;
  chid chid;

  if (DEBUG) printf("remMonitor for [%s]\n",channelName);

  /* Find the chid associated with the channelName */
  if (!chanDB(&chid, channelName, CAM_REMOVE)) {
    if (DEBUG) printf ("ERROR: Channel not found in database \n");
    return;
  }

  status = ca_clear_channel(chid);
  SEVCHK(status,"ca_clear_channel failed\n");
  if (status != ECA_NORMAL) return;
  ca_pend_event(.1);
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
    ca_set_puser(args.chid,(void *)TRUE);
    if (DEBUG) {
        printf ("Number of elements  for [%s] is %ld\n",
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

  if ( args.status != ECA_NORMAL ) {
    printf ( "camonitor: update failed because \"%s\"\n",
        ca_message ( args.status ) );
    return;
  }

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

/* This is called when the stdin file descr has input ready 
 * Input from the user looks like this for example:
 *  LI31:XCOR:41:BDES START
 *  LI31:QUAD:21:BDES STOP
 */

void processSTDIN(void *notused)
{
 char input_line[80];

 if (gets(input_line)==NULL) return;
 if (strstr(input_line,"START") != NULL) {  /* if contains start cmd */
   if (DEBUG) printf("recvd START cmd\n");
   if (strchr(input_line, ' ') !=NULL) {    /* if space found */
     memset (strchr(input_line, ' '), 0, 1);  /* null terminate after PV */
     if (strlen(input_line)) addMonitor(input_line);
   }
   else          /* else, command didn't parse, blank not found */
     return;
 }
 else {          /* else, stop command */
   if (DEBUG) printf("recvd STOP cmd\n");
   if (strchr(input_line, ' ') !=NULL) {    /* if space found */ 
     memset (strchr(input_line, ' '), 0, 1);  /* null terminate after PV */
     if (strlen(input_line)) {
       if (DEBUG) printf("calling remMonitor for %s \n",input_line);  
       remMonitor(input_line); 
     }
   }
   else          /* else, command didn't parse, blank not found */
     return;
 }
}

int main(int argc,char *argv[])
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

   /* add stdin's fd, 0, to fdmgr...  */
   fdmgr_add_fd(pfdctx, 0, processSTDIN, NULL);

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
      fprintf(stderr, "\n \tusage: %s \n",argv[0]);
      fprintf(stderr, "\tPV1 START\n");
      fprintf(stderr, "\tPV2 START\n");
      fprintf(stderr, "\tPV1 STOP\n");
      fprintf(stderr, "\tPV2 STOP\n\n");
   
      fprintf(stderr, "\n \tusage: %s PVname PVname ... \n\n",argv[0]);

      fprintf(stderr, "\tOr, you can mix and match those two usages \n\n");

      exit(1);
   }
 
   if(DEBUG) printf("pvcount=%d\n",pvcount);

   /**
   if(!pvcount) {
      exit(0);
   }
   **/

   ca_pend_event(CA_PEND_EVENT_TIME);

   /* start  events loop */
   while(TRUE) {
      fdmgr_pend_event(pfdctx,&timeout);
   }
}
