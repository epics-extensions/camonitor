#*************************************************************************
# Copyright (c) 2002 The University of Chicago, as Operator of Argonne
# National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
# Operator of Los Alamos National Laboratory.
# This file is distributed subject to a Software License Agreement found
# in the file LICENSE that is included with this distribution. 
#*************************************************************************
#
# $Id$
#
TOP = ../..
include $(TOP)/configure/CONFIG

# Debugging options
ifeq ($(T_A),solaris)
#DEBUGCMD = purify -first-only -chain-length=30
#DEBUGCMD = quantify
#HOST_OPT=NO
endif

USR_LIBS = ca Com
SYS_PROD_LIBS_WIN32 += ws2_32

PROD_DEFAULT = camonitor camonitorpv
PROD_WIN32 = camonitor
PROD_Darwin = camonitor

camonitor_SRCS = camonitor.c
camonitorpv_SRCS = camonitorpv.c

include $(TOP)/configure/RULES

