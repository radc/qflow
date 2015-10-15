/*--------------------------------------------------------------*/
/* tclqrouter.c:						*/
/*	Tcl routines for qrouter command-line functions		*/
/* Copyright (c) 2013  Tim Edwards, Open Circuit Design, Inc.	*/
/*--------------------------------------------------------------*/

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <tk.h>

#include <X11/Intrinsic.h>
#include <X11/StringDefs.h>

#include "qrouter.h"
#include "maze.h"
#include "qconfig.h"
#include "lef.h"

/* Global variables */

Tcl_HashTable QrouterTagTable;
Tcl_Interp *qrouterinterp;
Tcl_Interp *consoleinterp;

/* This hash table speeds up DEF file reading */

Tcl_HashTable InstanceTable;

/* Command structure */

typedef struct {
   const char	*cmdstr;
   void		(*func)();
} cmdstruct;

/* Forward declarations of commands */

extern int Tk_SimpleObjCmd();
extern int redraw();
extern int qrouter_map();
extern int qrouter_start();
extern int qrouter_stage1();
extern int qrouter_stage2();
extern int qrouter_writedef();
extern int qrouter_readdef();
extern int qrouter_readlef();
extern int qrouter_readconfig();
extern int qrouter_failing();
extern int qrouter_cost();
extern int qrouter_tag();
extern int qrouter_remove();
extern int qrouter_obs();
extern int qrouter_layerinfo();
extern int qrouter_priority();
extern int qrouter_ignore();
extern int qrouter_via();
extern int qrouter_resolution();
extern int qrouter_congested();
extern int qrouter_layers();
extern int qrouter_passes();
extern int qrouter_vdd();
extern int qrouter_gnd();
extern int qrouter_verbose();
extern int QuitCallback();

static cmdstruct qrouter_commands[] =
{
   {"tag", (void *)qrouter_tag},
   {"start", (void *)qrouter_start},
   {"stage1", (void *)qrouter_stage1},
   {"stage2", (void *)qrouter_stage2},
   {"write_def", (void *)qrouter_writedef},
   {"read_def", (void *)qrouter_readdef},
   {"read_lef", (void *)qrouter_readlef},
   {"read_config", (void *)qrouter_readconfig},
   {"layer_info", (void *)qrouter_layerinfo},
   {"obstruction", (void *)qrouter_obs},
   {"ignore", (void *)qrouter_ignore},
   {"priority", (void *)qrouter_priority},
   {"via", (void *)qrouter_via},
   {"resolution", (void *)qrouter_resolution},
   {"congested", (void *)qrouter_congested},
   {"layers", (void *)qrouter_layers},
   {"passes", (void *)qrouter_passes},
   {"vdd", (void *)qrouter_vdd},
   {"gnd", (void *)qrouter_gnd},
   {"failing", (void *)qrouter_failing},
   {"remove", (void *)qrouter_remove},
   {"cost", (void *)qrouter_cost},
   {"map", (void *)qrouter_map},
   {"verbose", (void *)qrouter_verbose},
   {"redraw", (void *)redraw},
   {"quit", (void *)QuitCallback},
   {"", NULL}  /* sentinel */
};

/*-----------------------*/
/* Tcl 8.4 compatibility */
/*-----------------------*/

#ifndef CONST84
#define CONST84
#endif

/*----------------------------------------------------------------------*/
/* Deal with systems which don't define va_copy().			*/
/*----------------------------------------------------------------------*/

#ifndef HAVE_VA_COPY
  #ifdef HAVE___VA_COPY
    #define va_copy(a, b) __va_copy(a, b)
  #else
    #define va_copy(a, b) a = b
  #endif
#endif

#ifdef ASG
   extern int SetDebugLevel(int *level);
#endif

/*----------------------------------------------------------------------*/
/* Reimplement strdup() to use Tcl_Alloc().				*/
/*----------------------------------------------------------------------*/

char *Tcl_Strdup(const char *s)
{
   char *snew;
   int slen;

   slen = 1 + strlen(s);
   snew = Tcl_Alloc(slen);
   if (snew != NULL)
      memcpy(snew, s, slen);

   return snew;
}

/*----------------------------------------------------------------------*/
/* Reimplement vfprintf() as a call to Tcl_Eval().			*/
/*									*/
/* Since the string goes through the interpreter, we need to escape	*/
/* various characters like brackets, braces, dollar signs, etc., that	*/
/* will otherwise be modified by the interpreter.			*/
/*----------------------------------------------------------------------*/

void tcl_vprintf(FILE *f, const char *fmt, va_list args_in)
{
   va_list args;
   static char outstr[128] = "puts -nonewline std";
   char *outptr, *bigstr = NULL, *finalstr = NULL;
   int i, nchars, result, escapes = 0, limit;

   /* If we are printing an error message, we want to bring attention	*/
   /* to it by mapping the console window and raising it, as necessary.	*/
   /* I'd rather do this internally than by Tcl_Eval(), but I can't	*/
   /* find the right window ID to map!					*/

   if ((f == stderr) && (consoleinterp != qrouterinterp)) {
      Tk_Window tkwind;
      tkwind = Tk_MainWindow(consoleinterp);
      if ((tkwind != NULL) && (!Tk_IsMapped(tkwind)))
	 result = Tcl_Eval(consoleinterp, "wm deiconify .\n");
      result = Tcl_Eval(consoleinterp, "raise .\n");
   }

   strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");
   outptr = outstr;

   /* This mess circumvents problems with systems which do not have	*/
   /* va_copy() defined.  Some define __va_copy();  otherwise we must	*/
   /* assume that args = args_in is valid.				*/

   va_copy(args, args_in);
   nchars = vsnprintf(outptr + 24, 102, fmt, args);
   va_end(args);

   if (nchars >= 102) {
      va_copy(args, args_in);
      bigstr = Tcl_Alloc(nchars + 26);
      strncpy(bigstr, outptr, 24);
      outptr = bigstr;
      vsnprintf(outptr + 24, nchars + 2, fmt, args);
      va_end(args);
    }
    else if (nchars == -1) nchars = 126;

    for (i = 24; *(outptr + i) != '\0'; i++) {
       if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	  	*(outptr + i) == ']' || *(outptr + i) == '\\' ||
		*(outptr + i) == '$')
	  escapes++;
    }

    if (escapes > 0) {
      finalstr = Tcl_Alloc(nchars + escapes + 26);
      strncpy(finalstr, outptr, 24);
      escapes = 0;
      for (i = 24; *(outptr + i) != '\0'; i++) {
	  if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
	    		*(outptr + i) == ']' || *(outptr + i) == '\\' ||
			*(outptr + i) == '$') {
	     *(finalstr + i + escapes) = '\\';
	     escapes++;
	  }
	  *(finalstr + i + escapes) = *(outptr + i);
      }
      outptr = finalstr;
    }

    *(outptr + 24 + nchars + escapes) = '\"';
    *(outptr + 25 + nchars + escapes) = '\0';

    result = Tcl_Eval(consoleinterp, outptr);

    if (bigstr != NULL) Tcl_Free(bigstr);
    if (finalstr != NULL) Tcl_Free(finalstr);
}
    
/*------------------------------------------------------*/
/* Console output flushing which goes along with the	*/
/* routine tcl_vprintf() above.				*/
/*------------------------------------------------------*/

void tcl_stdflush(FILE *f)
{   
   Tcl_SavedResult state;
   static char stdstr[] = "::flush stdxxx";
   char *stdptr = stdstr + 11;
    
   Tcl_SaveResult(qrouterinterp, &state);
   strcpy(stdptr, (f == stderr) ? "err" : "out");
   Tcl_Eval(qrouterinterp, stdstr);
   Tcl_RestoreResult(qrouterinterp, &state);
}

/*----------------------------------------------------------------------*/
/* Reimplement fprintf() as a call to Tcl_Eval().			*/
/*----------------------------------------------------------------------*/

void tcl_printf(FILE *f, const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  tcl_vprintf(f, format, ap);
  va_end(ap);
}

/*----------------------------------------------------------------------*/
/* Implement tag callbacks on functions					*/
/* Find any tags associated with a command and execute them.		*/
/*----------------------------------------------------------------------*/

int QrouterTagCallback(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int objidx, result = TCL_OK;
    char *postcmd, *substcmd, *newcmd, *sptr, *sres;
    char *croot = Tcl_GetString(objv[0]);
    Tcl_HashEntry *entry;
    Tcl_SavedResult state;
    int reset = FALSE;
    int i, llen, cmdnum;

    entry = Tcl_FindHashEntry(&QrouterTagTable, croot);
    postcmd = (entry) ? (char *)Tcl_GetHashValue(entry) : NULL;

    if (postcmd)
    {
	substcmd = (char *)Tcl_Alloc(strlen(postcmd) + 1);
	strcpy(substcmd, postcmd);
	sptr = substcmd;

	/*--------------------------------------------------------------*/
	/* Parse "postcmd" for Tk-substitution escapes			*/
	/* Allowed escapes are:						*/
	/* 	%W	substitute the tk path of the calling window	*/
	/*	%r	substitute the previous Tcl result string	*/
	/*	%R	substitute the previous Tcl result string and	*/
	/*		reset the Tcl result.				*/
	/*	%[0-5]  substitute the argument to the original command	*/
	/*	%N	substitute all arguments as a list		*/
	/*	%%	substitute a single percent character		*/
	/*	%*	(all others) no action: print as-is.		*/
	/*--------------------------------------------------------------*/

	while ((sptr = strchr(sptr, '%')) != NULL)
	{
	    switch (*(sptr + 1))
	    {
		case 'W': {
		    char *tkpath = NULL;
		    Tk_Window tkwind = Tk_MainWindow(interp);
		    if (tkwind != NULL) tkpath = Tk_PathName(tkwind);
		    if (tkpath == NULL)
			newcmd = (char *)Tcl_Alloc(strlen(substcmd));
		    else
			newcmd = (char *)Tcl_Alloc(strlen(substcmd) + strlen(tkpath));

		    strcpy(newcmd, substcmd);

		    if (tkpath == NULL)
			strcpy(newcmd + (int)(sptr - substcmd), sptr + 2);
		    else
		    {
			strcpy(newcmd + (int)(sptr - substcmd), tkpath);
			strcat(newcmd, sptr + 2);
		    }
		    Tcl_Free(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    } break;

		case 'R':
		    reset = TRUE;
		case 'r':
		    sres = (char *)Tcl_GetStringResult(interp);
		    newcmd = (char *)Tcl_Alloc(strlen(substcmd)
				+ strlen(sres) + 1);
		    strcpy(newcmd, substcmd);
		    sprintf(newcmd + (int)(sptr - substcmd), "\"%s\"", sres);
		    strcat(newcmd, sptr + 2);
		    Tcl_Free(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    break;

		case '0': case '1': case '2': case '3': case '4': case '5':
		    objidx = (int)(*(sptr + 1) - '0');
		    if ((objidx >= 0) && (objidx < objc))
		    {
		        newcmd = (char *)Tcl_Alloc(strlen(substcmd)
				+ strlen(Tcl_GetString(objv[objidx])));
		        strcpy(newcmd, substcmd);
			strcpy(newcmd + (int)(sptr - substcmd),
				Tcl_GetString(objv[objidx]));
			strcat(newcmd, sptr + 2);
			Tcl_Free(substcmd);
			substcmd = newcmd;
			sptr = substcmd;
		    }
		    else if (objidx >= objc)
		    {
		        newcmd = (char *)Tcl_Alloc(strlen(substcmd) + 1);
		        strcpy(newcmd, substcmd);
			strcpy(newcmd + (int)(sptr - substcmd), sptr + 2);
			Tcl_Free(substcmd);
			substcmd = newcmd;
			sptr = substcmd;
		    }
		    else sptr++;
		    break;

		case 'N':
		    llen = 1;
		    for (i = 1; i < objc; i++)
		       llen += (1 + strlen(Tcl_GetString(objv[i])));
		    newcmd = (char *)Tcl_Alloc(strlen(substcmd) + llen);
		    strcpy(newcmd, substcmd);
		    strcpy(newcmd + (int)(sptr - substcmd), "{");
		    for (i = 1; i < objc; i++) {
		       strcat(newcmd, Tcl_GetString(objv[i]));
		       if (i < (objc - 1))
			  strcat(newcmd, " ");
		    }
		    strcat(newcmd, "}");
		    strcat(newcmd, sptr + 2);
		    Tcl_Free(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    break;

		case '%':
		    newcmd = (char *)Tcl_Alloc(strlen(substcmd) + 1);
		    strcpy(newcmd, substcmd);
		    strcpy(newcmd + (int)(sptr - substcmd), sptr + 1);
		    Tcl_Free(substcmd);
		    substcmd = newcmd;
		    sptr = substcmd;
		    break;

		default:
		    break;
	    }
	}

	/* Fprintf(stderr, "Substituted tag callback is \"%s\"\n", substcmd); */
	/* Flush(stderr); */

	Tcl_SaveResult(interp, &state);
	result = Tcl_Eval(interp, substcmd);
	if ((result == TCL_OK) && (reset == FALSE))
	    Tcl_RestoreResult(interp, &state);
	else
	    Tcl_DiscardResult(&state);

	Tcl_Free(substcmd);
    }
    return result;
}

/*--------------------------------------------------------------*/
/* Add a command tag callback					*/
/*--------------------------------------------------------------*/

int qrouter_tag(ClientData clientData,
        Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_HashEntry *entry;
    char *hstring;
    int new;

    if (objc != 2 && objc != 3)
	return TCL_ERROR;

    entry = Tcl_CreateHashEntry(&QrouterTagTable, Tcl_GetString(objv[1]), &new);
    if (entry == NULL) return TCL_ERROR;

    hstring = (char *)Tcl_GetHashValue(entry);
    if (objc == 2)
    {
	Tcl_SetResult(interp, hstring, NULL);
	return TCL_OK;
    }

    if (strlen(Tcl_GetString(objv[2])) == 0)
    {
	Tcl_DeleteHashEntry(entry);
    }
    else
    {
	hstring = Tcl_Strdup(Tcl_GetString(objv[2]));
	Tcl_SetHashValue(entry, hstring);
    }
    return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Cell macro lookup based on the hash table			*/
/*--------------------------------------------------------------*/

GATE
DefFindInstance(char *name)
{
    GATE ginst;
    Tcl_HashEntry *entry;

    entry = Tcl_FindHashEntry(&InstanceTable, name);
    ginst = (entry) ? (GATE)Tcl_GetHashValue(entry) : NULL;
    return ginst;
}

/*--------------------------------------------------------------*/
/* Cell macro hash table generation				*/
/* Given an instance record, create an entry in the hash table	*/
/* for the instance name, with the record entry pointing to the	*/
/* instance record.						*/
/*--------------------------------------------------------------*/

void
DefHashInstance(GATE gateginfo)
{
    int new;
    Tcl_HashEntry *entry;

    entry = Tcl_CreateHashEntry(&InstanceTable,
		gateginfo->gatename, &new);
    if (entry != NULL)
	Tcl_SetHashValue(entry, (ClientData)gateginfo);
}

/*--------------------------------------------------------------*/
/* Initialization procedure for Tcl/Tk				*/
/*--------------------------------------------------------------*/

int
Qrouter_Init(Tcl_Interp *interp)
{
   int cmdidx;
   Tk_Window tktop;
   char *tmp_s;
   char command[256];
   char version_string[20];

   /* Interpreter sanity checks */
   if (interp == NULL) return TCL_ERROR;

   /* Remember the interpreter */
   qrouterinterp = interp;

   if (Tcl_InitStubs(interp, "8.1", 0) == NULL) return TCL_ERROR;

   strcpy(command, "qrouter::");
   
   /* Create the start command */

   tktop = Tk_MainWindow(interp);

   /* Create all of the commands (except "simple") */

   for (cmdidx = 0; qrouter_commands[cmdidx].func != NULL; cmdidx++) {
      sprintf(command + 9, "%s", qrouter_commands[cmdidx].cmdstr);
      Tcl_CreateObjCommand(interp, command,
		(Tcl_ObjCmdProc *)qrouter_commands[cmdidx].func,
		(ClientData)tktop, (Tcl_CmdDeleteProc *) NULL);
   }

   /* Command which creates a "simple" window.		*/

   Tcl_CreateObjCommand(interp, "simple",
		(Tcl_ObjCmdProc *)Tk_SimpleObjCmd,
		(ClientData)tktop, (Tcl_CmdDeleteProc *) NULL);

   Tcl_Eval(interp, "lappend auto_path .");

   sprintf(version_string, "%s", VERSION);
   Tcl_SetVar(interp, "QROUTER_VERSION", version_string, TCL_GLOBAL_ONLY);

   Tcl_Eval(interp, "namespace eval qrouter namespace export *");
   Tcl_PkgProvide(interp, "Qrouter", version_string);

   /* Initialize the console interpreter, if there is one. */

   if ((consoleinterp = Tcl_GetMaster(interp)) == NULL)
      consoleinterp = interp;

   /* Initialize the macro hash table */

   Tcl_InitHashTable(&InstanceTable, TCL_STRING_KEYS);

   /* Initialize the command tag table */

   Tcl_InitHashTable(&QrouterTagTable, TCL_STRING_KEYS);

   return TCL_OK;
}

/*------------------------------------------------------*/
/* Command "start"					*/
/*------------------------------------------------------*/

int qrouter_start(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int i, j, result, argc;
    char *scriptfile = NULL;
    char **argv;

    /* For compatibility with the original C code, convert Tcl	*/
    /* object arguments to strings.  Break out "-s <name>",	*/
    /* which is not handled by runqrouter(), and source the	*/
    /* script <name> between runqrouter() and read_def().	*/

    argv = (char **)malloc(objc * sizeof(char *));
    argc = 0;
    for (i = 0; i < objc; i++) {
	if (!strcmp(Tcl_GetString(objv[i]), "-s"))
	    scriptfile = strdup(Tcl_GetString(objv[++i]));
	else
	    argv[argc++] = strdup(Tcl_GetString(objv[i]));
    }

    result = runqrouter(argc, argv);
    if (result == 0) GUI_init(interp);

    for (i = 0; i < argc; i++)
        free(argv[i]);
    free(argv);

    if (scriptfile != NULL) {
	result = Tcl_EvalFile(interp, scriptfile);
	free(scriptfile);
	if (result != TCL_OK) return result;
    }

    if ((DEFfilename[0] != '\0') && (Nlgates == NULL)) {
	read_def(NULL);
	draw_layout();
    }

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "map"					*/
/*							*/
/* Specify what to draw in the graphics window		*/
/*							*/
/*	map obstructions    draw routes (normal)	*/
/*	map congestion	    draw actual congestion	*/
/*	map estimate	    draw estimated congestion	*/
/*	map none	    route background is plain	*/
/*	map routes	    draw routes over map	*/
/*	map noroutes	    don't draw routes over map	*/
/*------------------------------------------------------*/

int qrouter_map(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int i, idx, result;

    static char *subCmds[] = {
	"obstructions", "congestion", "estimate", "none",
	"routes", "noroutes", NULL
    };
    enum SubIdx {
	ObsIdx, CongIdx, EstIdx, NoneIdx, RouteIdx, NoRouteIdx
    };
   
    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    else if ((result = Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)subCmds, "option", 0, &idx)) != TCL_OK)
	return result;

    switch (idx) {
	case ObsIdx:
	    if ((mapType & MAP_MASK) != MAP_OBSTRUCT) {
		mapType &= ~MAP_MASK;
		mapType |= MAP_OBSTRUCT;
		draw_layout();
	    }
	    break;
	case CongIdx:
	    if ((mapType & MAP_MASK) != MAP_CONGEST) {
		mapType &= ~MAP_MASK;
		mapType |= MAP_CONGEST;
		draw_layout();
	    }
	    break;
	case EstIdx:
	    if ((mapType & MAP_MASK) != MAP_ESTIMATE) {
		mapType &= ~MAP_MASK;
		mapType |= MAP_ESTIMATE;
		draw_layout();
	    }
	    break;
	case NoneIdx:
	    if ((mapType & MAP_MASK) != MAP_NONE) {
		mapType &= ~MAP_MASK;
		mapType |= MAP_NONE;
		draw_layout();
	    }
	    break;
	case RouteIdx:
	    if ((mapType & DRAW_MASK) != DRAW_ROUTES) {
		mapType &= ~DRAW_MASK;
		mapType |= DRAW_ROUTES;
		draw_layout();
	    }
	    break;
	case NoRouteIdx:
	    if ((mapType & DRAW_MASK) != DRAW_NONE) {
		mapType &= ~DRAW_MASK;
		mapType |= DRAW_NONE;
		draw_layout();
	    }
	    break;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Find the net named "netname" in the list of nets	*/
/* and return a pointer to it.				*/
/*							*/
/* NOTE:  Really need a hash table lookup here!		*/
/*------------------------------------------------------*/

NET LookupNet(char *netname)
{
    NET net;
    int i;

    for (i = 0; i < Numnets; i++) {
       net = Nlnets[i];
       if (!strcmp(net->netname, netname))
	  return net;
    }
    return NULL;
}

/*------------------------------------------------------*/
/* Command "stage1"					*/
/*							*/
/* Execute stage1 routing.  This works through the	*/
/* entire netlist, routing as much as possible but not	*/
/* doing any rip-up and re-route.  Nets that fail to	*/
/* route are put in the "FailedNets" list.		*/
/*							*/
/* The interpreter result is set to the number of	*/
/* failed routes at the end of the first stage.		*/
/*							*/
/* Options:						*/
/*							*/
/*  stage1 debug	Draw the area being searched in	*/
/*			real-time.  This slows down the	*/
/*			algorithm and is intended only	*/
/*			for diagnostic use.		*/
/*  stage1 mask none	Don't limit the search area	*/
/*  stage1 mask auto	Select the mask automatically	*/
/*  stage1 mask bbox	Use the net bbox as a mask	*/
/*  stage1 mask <value> Set the mask size to <value>,	*/
/*			an integer typ. 0 and up.	*/
/*  stage1 route <net>	Route net named <net> only.	*/
/*							*/
/*  stage1 force	Force a terminal to be routable	*/
/*------------------------------------------------------*/

int qrouter_stage1(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    u_char dodebug = FALSE;
    int i, idx, idx2, val, result, failcount;
    NET net = NULL;

    static char *subCmds[] = {
	"debug", "mask", "route", "force", NULL
    };
    enum SubIdx {
	DebugIdx, MaskIdx, RouteIdx, ForceIdx
    };
   
    static char *maskSubCmds[] = {
	"none", "auto", "bbox", NULL
    };
    enum maskSubIdx {
	NoneIdx, AutoIdx, BboxIdx
    };

    forceRoutable = FALSE;	// Don't force unless specified
    if (objc >= 2) {
	for (i = 1; i < objc; i++) {

	    if ((result = Tcl_GetIndexFromObj(interp, objv[i],
			(CONST84 char **)subCmds, "option", 0, &idx))
			!= TCL_OK)
		return result;

	    switch (idx) {
		case DebugIdx:
		    dodebug = TRUE;
		    break;

		case ForceIdx:
		    forceRoutable = TRUE;
		    break;
	
		case RouteIdx:
		    if (i >= objc - 1) {
			Tcl_WrongNumArgs(interp, 0, objv, "route ?net?");
			return TCL_ERROR;
		    }
		    i++;
		    net = LookupNet(Tcl_GetString(objv[i]));
		    if (net == NULL) {
			Tcl_SetResult(interp, "No such net", NULL);
			return TCL_ERROR;
		    }
		    break;

		case MaskIdx:
		    if (i >= objc - 1) {
			Tcl_WrongNumArgs(interp, 0, objv, "mask ?type?");
			return TCL_ERROR;
		    }
		    i++;
		    if ((result = Tcl_GetIndexFromObj(interp, objv[i],
				(CONST84 char **)maskSubCmds, "type", 0,
				&idx2)) != TCL_OK) {
			Tcl_ResetResult(interp);
			result = Tcl_GetIntFromObj(interp, objv[i], &val);
			if (result != TCL_OK) return result;
			else if (val < 0 || val > 200) {
			    Tcl_SetResult(interp, "Bad mask value", NULL);
			    return TCL_ERROR;
			}
			maskMode = (u_char)val;
		    }
		    else {
			switch(idx2) {
			    case NoneIdx:
				maskMode = MASK_NONE;
				break;
			    case AutoIdx:
				maskMode = MASK_AUTO;
				break;
			    case BboxIdx:
				maskMode = MASK_BBOX;
				break;
			}
		    }
		    break;
	    }
	}
    }

    if (net == NULL)
	failcount = dofirststage(dodebug);
    else {
	if ((net != NULL) && (net->netnodes != NULL)) {
	    result = doroute(net, (u_char)0, dodebug);
	    failcount = (result == 0) ? 0 : 1;
	}
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(failcount));

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "stage2"					*/
/*							*/
/* Execute stage2 routing.  This stage works through	*/
/* the "FailedNets" list, routing with collisions, and	*/
/* then ripping up the colliding nets and appending	*/
/* them to the "FailedNets" list.			*/
/*							*/
/* The interpreter result is set to the number of	*/
/* failed routes at the end of the second stage.	*/
/*							*/
/* Options:						*/
/*							*/
/*  stage2 debug	Draw the area being searched in	*/
/*			real-time.  This slows down the	*/
/*			algorithm and is intended only	*/
/*			for diagnostic use.		*/
/*  stage2 mask none	Don't limit the search area	*/
/*  stage2 mask auto	Select the mask automatically	*/
/*  stage2 mask bbox	Use the net bbox as a mask	*/
/*  stage2 mask <value> Set the mask size to <value>,	*/
/*			an integer typ. 0 and up.	*/
/*  stage2 route <net>	Route net named <net> only.	*/
/*							*/
/*  stage2 force	Force a terminal to be routable	*/
/*------------------------------------------------------*/

int qrouter_stage2(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    u_char dodebug = FALSE;
    int i, idx, idx2, val, result, failcount;
    NET net = NULL;

    static char *subCmds[] = {
	"debug", "mask", "route", "force", NULL
    };
    enum SubIdx {
	DebugIdx, MaskIdx, RouteIdx, ForceIdx
    };
   
    static char *maskSubCmds[] = {
	"none", "auto", "bbox", NULL
    };
    enum maskSubIdx {
	NoneIdx, AutoIdx, BboxIdx
    };

    forceRoutable = FALSE;	// Don't force unless specified
    if (objc >= 2) {
	for (i = 1; i < objc; i++) {

	    if ((result = Tcl_GetIndexFromObj(interp, objv[i],
			(CONST84 char **)subCmds, "option", 0, &idx))
			!= TCL_OK)
		return result;

	    switch (idx) {
		case DebugIdx:
		    dodebug = TRUE;
		    break;
	
		case ForceIdx:
		    forceRoutable = TRUE;
		    break;
	
		case RouteIdx:
		    if (i >= objc - 1) {
			Tcl_WrongNumArgs(interp, 0, objv, "route ?net?");
			return TCL_ERROR;
		    }
		    i++;
		    net = LookupNet(Tcl_GetString(objv[i]));
		    if (net == NULL) {
			Tcl_SetResult(interp, "No such net", NULL);
			return TCL_ERROR;
		    }
		    break;

		case MaskIdx:
		    if (i >= objc - 1) {
			Tcl_WrongNumArgs(interp, 0, objv, "mask ?type?");
			return TCL_ERROR;
		    }
		    i++;
		    if ((result = Tcl_GetIndexFromObj(interp, objv[i],
				(CONST84 char **)maskSubCmds, "type", 0,
				&idx2)) != TCL_OK) {
			Tcl_ResetResult(interp);
			result = Tcl_GetIntFromObj(interp, objv[i], &val);
			if (result != TCL_OK) return result;
			else if (val < 0 || val > 200) {
			    Tcl_SetResult(interp, "Bad mask value", NULL);
			    return TCL_ERROR;
			}
			maskMode = (u_char)val;
		    }
		    else {
			switch(idx2) {
			    case NoneIdx:
				maskMode = MASK_NONE;
				break;
			    case AutoIdx:
				maskMode = MASK_AUTO;
				break;
			    case BboxIdx:
				maskMode = MASK_BBOX;
				break;
			}
		    }
		    break;
	    }
	}
    }

    if (net == NULL)
	failcount = dosecondstage(dodebug);
    else
	failcount = route_net_ripup(net, dodebug);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(failcount));

    draw_layout();
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "remove"					*/
/*							*/
/*   Remove a net or nets, or all nets, from the	*/
/*   design.						*/
/*							*/
/* Options:						*/
/*							*/
/*   remove all		Remove all nets from the design	*/
/*   remove net <name> [...]				*/
/*			Remove the named net(s) from	*/
/*			the design.			*/
/*------------------------------------------------------*/

int qrouter_remove(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int result, idx, i;
    NET net;

    static char *subCmds[] = {
	"all", "net", NULL
    };
    enum SubIdx {
	AllIdx, NetIdx
    };

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 0, objv, "?option?");
	return TCL_ERROR;
    }
    else {
	if ((result = Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)subCmds, "option", 0, &idx))
		!= TCL_OK)
	    return result;

	switch (idx) {
	    case AllIdx:
		for (i = 0; i < Numnets; i++) {
		   net = Nlnets[i];
		   ripup_net(net, (u_char)1);
		}
		draw_layout();
		break;
	    case NetIdx:
		for (i = 2; i < objc; i++) {
		    net = LookupNet(Tcl_GetString(objv[i]));
		    if (net != NULL)
			ripup_net(net, (u_char)1);
		}
		draw_layout();
		break;
	}
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "failing"					*/
/*							*/
/*   List the nets that have failed routing.		*/
/*							*/
/* Options:						*/
/*							*/
/*   failing all	Move all nets to FailedNets	*/
/*			ordered by the standard metric	*/
/*   failing unordered	Move all nets to FailedNets,	*/
/*			as originally ordered		*/
/*------------------------------------------------------*/

int qrouter_failing(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Obj *lobj;
    NETLIST nl, nlast;
    NET net;
    int i, failcount;

    if (objc == 2) {
	if (!strncmp(Tcl_GetString(objv[1]), "unorder", 7)) {
	    // Free up FailedNets list and then move all
	    // nets to FailedNets

	    while (FailedNets != NULL) {
		nl = FailedNets->next;
		FailedNets = FailedNets->next;
		free(nl);
	    }
	    nlast = NULL;
	    for (i = 0; i < Numnets; i++) {
		net = Nlnets[i];
		nl = (NETLIST)malloc(sizeof(struct netlist_));
		nl->net = net;
		nl->next = NULL;
		if (nlast == NULL)
		    FailedNets = nl;
		else
		    nlast->next = nl;
		nlast = nl;
	    }
	}
	else if (!strncmp(Tcl_GetString(objv[1]), "all", 3)) {
	    while (FailedNets != NULL) {
		nl = FailedNets->next;
		FailedNets = FailedNets->next;
		free(nl);
	    }
	    create_netorder(0);
	    nlast = NULL;
	    for (i = 0; i < Numnets; i++) {
		net = Nlnets[i];
		nl = (NETLIST)malloc(sizeof(struct netlist_));
		nl->net = net;
		nl->next = NULL;
		if (nlast == NULL)
		    FailedNets = nl;
		else
		    nlast->next = nl;
		nlast = nl;
	    }
	}
	else if (!strncmp(Tcl_GetString(objv[1]), "summary", 7)) {
	    failcount = countlist(FailedNets);
	    lobj = Tcl_NewListObj(0, NULL);
	    Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(failcount));
	    Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(Numnets));
	    Tcl_SetObjResult(interp, lobj);
	}
	else {
	    Tcl_WrongNumArgs(interp, 0, objv, "all or unordered");
	    return TCL_ERROR;
	}
    }
    else {

	lobj = Tcl_NewListObj(0, NULL);

	for (nl = FailedNets; nl; nl = nl->next) {
	    Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(nl->net->netname, -1));
	}
	Tcl_SetObjResult(interp, lobj);
    }

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "read_lef"					*/
/*------------------------------------------------------*/

int qrouter_readlef(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    char *LEFfile;

    if (objc != 2) {
	Tcl_SetResult(interp, "No LEF filename specified!", NULL);
	return TCL_ERROR;
    }
    LEFfile = Tcl_GetString(objv[1]);

    LefRead(LEFfile);
    post_config();

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "read_def"					*/
/*------------------------------------------------------*/

int qrouter_readdef(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    if ((DEFfilename[0] == '\0') && (objc != 2)) {
	Tcl_SetResult(interp, "No DEF filename specified!", NULL);
	return TCL_ERROR;
    }
    if (objc == 2)
	read_def(Tcl_GetString(objv[1]));
    else
	read_def(NULL);

    // Redisplay
    draw_layout();

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "write_def"					*/
/*------------------------------------------------------*/

int qrouter_writedef(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    char *DEFoutfile = NULL;

    if (objc == 2)
	DEFoutfile = Tcl_GetString(objv[1]);
    else if (DEFfilename[0] == '\0') {
	Tcl_SetResult(interp, "No DEF filename specified!", NULL);
	return TCL_ERROR;
    }

    write_def(DEFoutfile);
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "read_config"				*/
/*------------------------------------------------------*/

int qrouter_readconfig(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    FILE *configFILE;
    char *configname = NULL;

    if (objc == 2)
	configname = Tcl_GetString(objv[1]);
    else {
	Tcl_SetResult(interp, "No configuration filename specified!",
		NULL);
	return TCL_ERROR;
    }
    configFILE = fopen(configname, "r");
    if (configFILE == NULL) {
	Tcl_SetResult(interp, "Failed to open configuration file.",
		NULL);
	return TCL_ERROR;
    }
    read_config(configFILE);
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "obstruction"				*/
/*							*/
/* Create an obstruction manually by specifying a	*/
/* rectangular bounding box and layer of the obstructed	*/
/* area.  Without options, returns a list of the	*/
/* defined user obstruction areas.			*/
/*							*/
/* Options:						*/
/*							*/
/*	obstruction					*/
/*	obstruction <xmin> <ymin> <xmax> <ymax> <layer>	*/
/*------------------------------------------------------*/

int qrouter_obs(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Obj *lobj;
    Tcl_Obj *oobj;
    LefList lefl;
    char *layername;
    DSEG obs;
    int layer, result;
    double x1, x2, y1, y2;

    if (objc == 1) {
	lobj = Tcl_NewListObj(0, NULL);
	for (obs = UserObs; obs; obs = obs->next) {
	    lefl = LefFindLayerByNum(obs->layer);
	    if (lefl == NULL) continue;
	    oobj = Tcl_NewListObj(0, NULL);
	    Tcl_ListObjAppendElement(interp, oobj, Tcl_NewDoubleObj(obs->x1));
	    Tcl_ListObjAppendElement(interp, oobj, Tcl_NewDoubleObj(obs->x2));
	    Tcl_ListObjAppendElement(interp, oobj, Tcl_NewDoubleObj(obs->y1));
	    Tcl_ListObjAppendElement(interp, oobj, Tcl_NewDoubleObj(obs->y2));
	    Tcl_ListObjAppendElement(interp, oobj,
			Tcl_NewStringObj(lefl->lefName, -1));
	    Tcl_ListObjAppendElement(interp, lobj, oobj);
	}
	Tcl_SetObjResult(interp, lobj);
    }
    else if (objc != 6) {
	Tcl_WrongNumArgs(interp, 1, objv, "x1 x2 y1 y2 layer");
	return TCL_ERROR;
    }
    else {
	layername = Tcl_GetString(objv[5]);
	layer = LefFindLayerNum(layername);
	if (layer < 0) {
	    Tcl_SetResult(interp, "No such layer name", NULL);
	    return TCL_ERROR;
	}
	else {
	    result = Tcl_GetDoubleFromObj(interp, objv[1], &x1);
	    if (result != TCL_OK) return result;
	    result = Tcl_GetDoubleFromObj(interp, objv[2], &y1);
	    if (result != TCL_OK) return result;
	    result = Tcl_GetDoubleFromObj(interp, objv[3], &x2);
	    if (result != TCL_OK) return result;
	    result = Tcl_GetDoubleFromObj(interp, objv[4], &y2);
	    if (result != TCL_OK) return result;

	    obs = (DSEG)malloc(sizeof(struct dseg_));
	    obs->x1 = x1;
	    obs->x2 = x2;
	    obs->y1 = y1;
	    obs->y2 = y2;
	    obs->layer = layer;
	    obs->next = UserObs;
	    UserObs = obs;
	}
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "ignore"					*/
/*							*/
/* Specify one or more net names to be ignored by the	*/
/* router.  With no options, returns a list of nets	*/
/* being ignored by the router.				*/
/*							*/
/* Options:						*/
/*							*/
/*	ignore [<net> ...]				*/
/*------------------------------------------------------*/

int qrouter_ignore(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int i;
    NET net;
    Tcl_Obj *lobj;

    if (objc == 1) {
	lobj = Tcl_NewListObj(0, NULL);
	for (i = 0; i < Numnets; i++) {
	    net = Nlnets[i];
	    if (net->flags & NET_IGNORED) {
		Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(net->netname, -1));
	    }
	}
	Tcl_SetObjResult(interp, lobj);
    }
    else {
	for (i = 1; i < objc; i++) {
	    net = LookupNet(Tcl_GetString(objv[i]));
	    if (net == NULL) {
		Tcl_SetResult(interp, "No such net", NULL);
		return TCL_ERROR;
	    }
	    net->flags |= NET_IGNORED;
	}
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "priority"					*/
/*							*/
/* Specify one or more net names to be routed first by	*/
/* the router. 	With no options, returns a list of	*/
/* nets prioritized by the router.			*/
/*							*/
/* Options:						*/
/*							*/
/*	priority [<net> ...]				*/
/*------------------------------------------------------*/

int qrouter_priority(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int i;
    char *netname;
    NET net;
    STRING cn, ctest;
    Tcl_Obj *lobj;

    if (objc == 1) {
	lobj = Tcl_NewListObj(0, NULL);
	for (i = 0; i < Numnets; i++) {
	    net = Nlnets[i];
	    if (net->flags & NET_CRITICAL) {
		Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(net->netname, -1));
	    }
	}
	Tcl_SetObjResult(interp, lobj);
    }
    else {
	for (i = objc - 1; i > 0; i++) {
	    netname = Tcl_GetString(objv[i]);
	    net = LookupNet(netname);
	    if (net == NULL) {
		Tcl_SetResult(interp, "No such net", NULL);
	    }
	    else {
		net->flags |= NET_CRITICAL;
		for (cn = CriticalNet; cn && cn->next; cn = cn->next) {
		    if (!cn->next) break;
		    if (!strcmp(cn->next->name, netname)) {
			ctest = cn->next;
			cn->next = cn->next->next;
			ctest->next = CriticalNet;
			CriticalNet = ctest;
		    }
		}
	    }
	}
	create_netorder(0, NULL);
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "layer_info"					*/
/*							*/
/* Provide information gleaned from the LEF technology	*/
/* file or files					*/
/*							*/
/* Options:						*/
/*							*/
/*	layer_info [all]				*/
/*	layer_info <layername>|<layernum>		*/
/*	layer_info <layername>|<layernum> width		*/
/*	layer_info <layername>|<layernum> pitch		*/
/*	layer_info <layername>|<layernum> orientation	*/
/*	layer_info <layername>|<layernum> offset	*/
/*	layer_info <layername>|<layernum> spacing	*/
/*	layer_info maxlayer				*/
/*							*/
/* all: generate a list summary of route information	*/
/*	in the format of the info file generated with 	*/
/*	the "-i" command-line switch.			*/
/* <layer>: generate a list summary of information for	*/
/*	the specified route layer <layer>, format the	*/
/*	same as for option "all".			*/
/* <layer> pitch:  Return the layer pitch value.	*/
/* <layer> orientation:  Return the layer orientation.	*/
/* <layer> offset:  Return the layer offset value.	*/
/* <layer> spacing:  Return the layer minimum spacing	*/
/*	value.						*/
/* maxlayer:  Return the maximum number of route layers	*/
/* 	defined by the technology.			*/
/*							*/
/* No option is the same as option "layer_info all"	*/
/*------------------------------------------------------*/

int qrouter_layerinfo(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Obj *lobj, *oobj;
    int i, idx, idx2, val, result, layer;
    char *layername;

    static char *subCmds[] = {
	"all", "maxlayer", NULL
    };
    enum SubIdx {
	AllIdx, MaxLayerIdx
    };
   
    static char *layerSubCmds[] = {
	"width", "pitch", "orientation", "offset", "spacing", NULL
    };
    enum layerSubIdx {
	WidthIdx, PitchIdx, OrientIdx, OffsetIdx, SpacingIdx
    };

    idx = idx2 = -1;

    if (objc < 2) {
	idx = AllIdx;
    }
    else {
	layername = Tcl_GetString(objv[1]);
	layer = LefFindLayerNum(layername);
	if (layer == -1) {
	    if ((result = Tcl_GetIntFromObj(interp, objv[1], &val)) == TCL_OK) {
		layer = val;
	    }
	    else {
		Tcl_ResetResult(interp);
	
		if ((result = Tcl_GetIndexFromObj(interp, objv[1],
			(CONST84 char **)subCmds, "option", 0, &idx))
			!= TCL_OK) {
		    return result;
		}
	    }
	}
	else if (objc >= 3) { 
	    if ((result = Tcl_GetIndexFromObj(interp, objv[2],
			(CONST84 char **)layerSubCmds, "option", 0, &idx2))
			!= TCL_OK) {
		return result;
	    }
	    layer = LefFindLayerNum(layername);
	}
	else {
	    layer = LefFindLayerNum(layername);
	}
    }

    if (idx == -1 && layer == -1) {
	Tcl_SetResult(interp, "Bad layer", NULL);
	return TCL_ERROR;
    }
    if (layer < 0 || layer >= Num_layers) {
	Tcl_SetResult(interp, "Bad layer", NULL);
	return TCL_ERROR;
    }

    switch (idx) {
	case AllIdx:
	    oobj = Tcl_NewListObj(0, NULL);
	    for (i = 0; i < Num_layers; i++) {
		lobj = Tcl_NewListObj(0, NULL);
		Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(LefGetRouteName(i), -1));
		Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewDoubleObj(LefGetRoutePitch(i)));
		Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewDoubleObj(LefGetRouteWidth(i)));
		if (LefGetRouteOrientation(i) == 1)
		    Tcl_ListObjAppendElement(interp, lobj,
				Tcl_NewStringObj("horizontal", -1));
		else
		    Tcl_ListObjAppendElement(interp, lobj,
				Tcl_NewStringObj("vertical", -1));
		Tcl_ListObjAppendElement(interp, oobj, lobj);
	    }
	    Tcl_SetObjResult(interp, oobj);
	    break;
	case MaxLayerIdx:
	    Tcl_SetObjResult(interp, Tcl_NewIntObj(Num_layers));
	    break;
    }

    switch (idx2) {
	case WidthIdx:
	    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(LefGetRouteWidth(layer)));
	    break;
	case PitchIdx:
	    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(LefGetRoutePitch(layer)));
	    break;
	case OrientIdx:
	    if (LefGetRouteOrientation(layer) == (u_char)0)
		Tcl_SetObjResult(interp, Tcl_NewStringObj("vertical", -1));
	    else
		Tcl_SetObjResult(interp, Tcl_NewStringObj("horizontal", -1));
	    break;
	case OffsetIdx:
	    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(LefGetRouteOffset(layer)));
	    break;
	case SpacingIdx:
	    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(LefGetRouteSpacing(layer)));
	    break;
	default:
	    if (idx != -1) break;
	    lobj = Tcl_NewListObj(0, NULL);
	    Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewStringObj(LefGetRouteName(layer), -1));
	    Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewDoubleObj(LefGetRoutePitch(layer)));
	    Tcl_ListObjAppendElement(interp, lobj,
			Tcl_NewDoubleObj(LefGetRouteWidth(layer)));
	    if (LefGetRouteOrientation(layer) == 1)
		Tcl_ListObjAppendElement(interp, lobj,
				Tcl_NewStringObj("horizontal", -1));
	    else
		Tcl_ListObjAppendElement(interp, lobj,
				Tcl_NewStringObj("vertical", -1));
	    Tcl_SetObjResult(interp, lobj);
	    break;
    }
}

/*------------------------------------------------------*/
/* Command "via"					*/
/*							*/
/* Various via configuration options for qrouter.	*/
/*							*/
/* stack: Value is the maximum number of vias that may	*/
/*	  be stacked directly on top of each other at	*/
/*	  a single point.  Value "none", "0", and "1"	*/
/*	  all mean the same thing.			*/
/*							*/
/* via_pattern:  If vias are non-square, then they are	*/
/*	  placed in a checkerboard pattern, with every	*/
/*	  other via rotated 90 degrees.  If inverted,	*/
/*	  the rotation is swapped relative to the grid	*/
/*	  positions used in the non-inverted case.	*/
/*							*/
/* Options:						*/
/*							*/
/*	via stack [none|all|<value>]			*/
/*	via pattern [normal|inverted]			*/
/*------------------------------------------------------*/

int qrouter_via(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int idx, idx2, result, value;

    static char *subCmds[] = {
	"stack", "pattern", NULL
    };
    enum SubIdx {
	StackIdx, PatternIdx
    };
   
    static char *stackSubCmds[] = {
	"none", "all", NULL
    };
    enum stackSubIdx {
	NoneIdx, AllIdx
    };

    static char *patternSubCmds[] = {
	"none", "normal", "invert", NULL
    };
    enum patternSubIdx {
	PatNoneIdx, PatNormalIdx, PatInvertIdx
    };

    if (objc >= 2) {
	if ((result = Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)subCmds, "option", 0, &idx))
		!= TCL_OK)
	    return result;

	if (objc == 2) {
	    switch (idx) {
		case StackIdx:
		    Tcl_SetObjResult(interp, Tcl_NewIntObj(StackedContacts));
		    break;
		case PatternIdx:
		    Tcl_SetObjResult(interp,
				Tcl_NewStringObj(
				patternSubCmds[ViaPattern + 1], -1));
		    break;
	    }
	}
	else {
	    switch (idx) {
		case StackIdx:
		    result = Tcl_GetIntFromObj(interp, objv[2], &value);
		    if (result == TCL_OK) {
			if (value <= 0) value = 1;
			else if (value >= Num_layers) value = Num_layers - 1;
			StackedContacts = value;
			break;
		    }
		    Tcl_ResetResult(interp);
		    if ((result = Tcl_GetIndexFromObj(interp, objv[2],
				(CONST84 char **)stackSubCmds, "option",
				0, &idx2)) != TCL_OK)
			return result;
		    switch (idx2) {
			case NoneIdx:
			    StackedContacts = 1;
			    break;
			case AllIdx:
			    StackedContacts = Num_layers - 1;
			    break;
		    }
		    break;
		case PatternIdx:
		    if ((result = Tcl_GetIndexFromObj(interp, objv[2],
				(CONST84 char **)patternSubCmds, "option",
				0, &idx2)) != TCL_OK)
			return result;
		    ViaPattern = idx2 - 1;
		    break;
	    }
	}
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "verbose"					*/
/*							*/
/* Set the level of how much output to print to the	*/
/* console.  0 is minimal output, 4 is maximum output	*/
/* With no argument, return the level of verbose. 	*/
/*							*/
/* Options:						*/
/*							*/
/*	resolution [<value>]				*/
/*------------------------------------------------------*/

int qrouter_verbose(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int result, value;

    if (objc == 1) {
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Verbose));
    }
    else if (objc == 2) {

	result = Tcl_GetIntFromObj(interp, objv[1], &value);
	if (result != TCL_OK) return result;
	if (value < 0 || value > 255) {
	    Tcl_SetResult(interp, "Verbose level out of range", NULL);
	    return TCL_ERROR;
	}
	Verbose = (u_char)value;
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}


/*------------------------------------------------------*/
/* Command "resolution"					*/
/*							*/
/* Set the level of resolution of the output relative	*/
/* to the base units of centimicrons.  The default	*/
/* value of 1 rounds all output values to the nearest	*/
/* centimicron;  value 10 is to the nearest nanometer,	*/
/* and so forth.					*/
/* With no argument, return the value of the output	*/
/* resolution.						*/
/*							*/
/* Options:						*/
/*							*/
/*	resolution [<value>]				*/
/*------------------------------------------------------*/

int qrouter_resolution(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int result, value;

    if (objc == 1) {
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Scales.iscale));
    }
    else if (objc == 2) {

	result = Tcl_GetIntFromObj(interp, objv[1], &value);
	if (result != TCL_OK) return result;
	if (value < 1) {
	    Tcl_SetResult(interp, "Resolution out of range", NULL);
	    return TCL_ERROR;
	}
	Scales.iscale = value;
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}


/*------------------------------------------------------*/
/* Command "layers"					*/
/*							*/
/* Set the number of layers used for routing		*/
/* independently of the number defined in the		*/
/* technology LEF file.	 Without an argument, return	*/
/* the number of layers being used (this is not the 	*/
/* same as "layer_info maxlayer")			*/
/*							*/
/* Options:						*/
/*							*/
/*	layers [<number>]				*/
/*------------------------------------------------------*/

int qrouter_layers(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int result, value;

    if (objc == 1) {
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Num_layers));
    }
    else if (objc == 2) {
	result = Tcl_GetIntFromObj(interp, objv[1], &value);
	if (result != TCL_OK) return result;
	if (value <= 0 || value > LefGetMaxLayer()) {
	    Tcl_SetResult(interp, "Number of layers out of range", NULL);
	    return TCL_ERROR;
	}
	Num_layers = value;
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "passes"					*/
/*							*/
/* Set the maximum number of attempted passes of the	*/
/* route search algorithm, where the routing		*/
/* constraints (maximum cost and route area mask) are	*/
/* relaxed on each pass.				*/
/* With no argument, return the value for the maximum	*/
/* number of passes.					*/
/* The default number of passes is 10 and normally	*/
/* there is no reason for the user to change it.	*/
/*							*/
/* Options:						*/
/*							*/
/*	passes [<number>]				*/
/*------------------------------------------------------*/

int qrouter_passes(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int result, value;

    if (objc == 1) {
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Numpasses));
    }
    else if (objc == 2) {
	result = Tcl_GetIntFromObj(interp, objv[1], &value);
	if (result != TCL_OK) return result;
	if (value <= 0) {
	    Tcl_SetResult(interp, "Number of passes out of range", NULL);
	    return TCL_ERROR;
	}
	Numpasses = value;
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "vdd"					*/
/*							*/
/* Set the name of the net used for VDD power routing	*/
/* With no argument, return the name of the power	*/
/* net.							*/
/*							*/
/* Options:						*/
/*							*/
/*	vdd [<name>]					*/
/*------------------------------------------------------*/

int qrouter_vdd(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    if (objc == 1) {
	if (vddnet == NULL)
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("(none)", -1));
	else
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(vddnet, -1));
    }
    else if (objc == 2) {
	if (vddnet != NULL) free(vddnet);
	vddnet = strdup(Tcl_GetString(objv[1]));
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "gnd"					*/
/*							*/
/* Set the name of the net used for ground routing.	*/
/* With no argument, return the name of the ground	*/
/* net.							*/
/*							*/
/* Options:						*/
/*							*/
/*	gnd [<name>]					*/
/*------------------------------------------------------*/

int qrouter_gnd(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    if (objc == 1) {
	if (gndnet == NULL)
	    Tcl_SetObjResult(interp, Tcl_NewStringObj("(none)", -1));
	else
	    Tcl_SetObjResult(interp, Tcl_NewStringObj(gndnet, -1));
    }
    else if (objc == 2) {
	if (gndnet != NULL) free(gndnet);
	gndnet = strdup(Tcl_GetString(objv[1]));
    }
    else {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
/* Command "cost"					*/
/*							*/
/* Query or set the value of a cost function		*/
/*							*/
/* Options:						*/
/*							*/
/*	cost segment					*/
/*	cost via					*/
/*	cost jog					*/
/*	cost crossover					*/
/*	cost block					*/
/*	cost conflict					*/
/*------------------------------------------------------*/

int qrouter_cost(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    int idx, result, value;

    static char *subCmds[] = {
	"segment", "via", "jog", "crossover",
	"block", "conflict", NULL
    };
    enum SubIdx {
	SegIdx, ViaIdx, JogIdx, XOverIdx, BlockIdx, ConflictIdx
    };
   
    value = 0;
    if (objc == 3) {
	result = Tcl_GetIntFromObj(interp, objv[2], &value);
	if (result != TCL_OK) return result;

	if (value <= 0) {
	    Tcl_SetResult(interp, "Bad cost value", NULL);
	    return TCL_ERROR;
	}
    }
    else if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }

    if ((result = Tcl_GetIndexFromObj(interp, objv[1],
		(CONST84 char **)subCmds, "option", 0, &idx)) != TCL_OK)
	return result;

    switch (idx) {
	case SegIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(SegCost));
	    else
		SegCost = value;
	    break;

	case ViaIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(ViaCost));
	    else
		ViaCost = value;
	    break;

	case JogIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(JogCost));
	    else
		JogCost = value;
	    break;

	case XOverIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(XverCost));
	    else
		XverCost = value;
	    break;

	case BlockIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(BlockCost));
	    else
		BlockCost = value;
	    break;

	case ConflictIdx:
	    if (objc == 2)
		Tcl_SetObjResult(interp, Tcl_NewIntObj(ConflictCost));
	    else
		ConflictCost = value;
	    break;
    }

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/

typedef struct clist_ *CLIST;

typedef struct clist_ {
   GATE gate;
   double congestion;
} Clist;

/*------------------------------------------------------*/
/* Compare function for qsort()				*/
/*------------------------------------------------------*/

int compcong(CLIST *a, CLIST *b)
{
   CLIST p = *a;
   CLIST q = *b;

   if (p->congestion < q->congestion)
      return (1);
   else if (p->congestion > q->congestion)
      return (-1);
   else
      return (0);
}

/*------------------------------------------------------*/
/* Command "congested"					*/
/*							*/
/* Return a list of instances, ranked by congestion.	*/
/* This list can be passed back to a placement tool to	*/
/* direct it to add padding around these cells to ease	*/
/* congestion and make the layout more routable.	*/
/*							*/
/* Options:						*/
/*							*/
/* congested <n>	List the top <n> congested	*/
/*			gates in the design.		*/
/*------------------------------------------------------*/

int qrouter_congested(ClientData clientData, Tcl_Interp *interp,
	int objc, Tcl_Obj *CONST objv[])
{
    NET net;
    int i, x, y, nwidth, nheight, area, length;
    int value, entries, numgates, result;
    float density, *Congestion;
    CLIST *cgates, csrch;
    GATE gsrch;
    struct seg_ bbox;
    double dx, dy, cavg;
    Tcl_Obj *lobj, *dobj;

    if (objc == 2) {
	result = Tcl_GetIntFromObj(interp, objv[1], &entries);
	if (result != TCL_OK) return result;

	if (value <= 0) {
	    Tcl_SetResult(interp, "List size must be > 0", NULL);
	    return TCL_ERROR;
	}
    }
    else
	entries = 0;

    Congestion = (float *)calloc(NumChannelsX[0] * NumChannelsY[0],
			sizeof(float));

    // Use net bounding boxes to estimate congestion

    for (i = 0; i < Numnets; i++) {
	net = Nlnets[i];
	nwidth = (net->xmax - net->xmin + 1);
	nheight = (net->ymax - net->ymin + 1);
	area = nwidth * nheight;
	if (nwidth > nheight) {
	    length = nwidth + (nheight >> 1) * net->numnodes;
	}
	else {
	    length = nheight + (nwidth >> 1) * net->numnodes;
	}
	density = (float)length / (float)area;

	for (x = net->xmin; x < net->xmax; x++)
	    for (y = net->ymin; y < net->ymax; y++)
		Congestion[OGRID(x, y, 0)] += density;
    }

    // Use instance bounding boxes to estimate average congestion
    // in the area of an instance.

    numgates = 0;
    for (gsrch = Nlgates; gsrch; gsrch = gsrch->next) numgates++;

    cgates = (CLIST *)malloc(numgates * sizeof(CLIST));
    i = 0;

    for (gsrch = Nlgates; gsrch; gsrch = gsrch->next) {

	// Ignore pins, as the point of congestion planning
	// is to add padding to the core instances;  including
	// pins just makes it harder for the placement script
	// to process the information.

	if (gsrch->gatetype == PinMacro) continue;

	cgates[i] = (CLIST)malloc(sizeof(Clist));
	dx = gsrch->placedX;
	dy = gsrch->placedY;
	bbox.x1 = (int)((dx - Xlowerbound) / PitchX[0]) - 1;
	bbox.y1 = (int)((dy - Ylowerbound) / PitchY[0]) - 1;
	dx = gsrch->placedX + gsrch->width;
	dy = gsrch->placedY + gsrch->height;
	bbox.x2 = (int)((dx - Xlowerbound) / PitchX[0]) - 1;
	bbox.y2 = (int)((dy - Ylowerbound) / PitchY[0]) - 1;

	cavg = 0.0;
	for (x = bbox.x1; x <= bbox.x2; x++) {
	    for (y = bbox.y1; y <= bbox.y2; y++) {
		cavg += Congestion[OGRID(x, y, 0)];
	    }
	}
	cavg /= (bbox.x2 - bbox.x1 + 1);
	cavg /= (bbox.y2 - bbox.y1 + 1);

	cgates[i]->gate = gsrch;
	cgates[i++]->congestion = cavg / Num_layers;
    }

    // Re-set "numgates", because we have rejected pins from the
    // original count.
    numgates = i;

    // Order Clist and copy contents back to the interpreter as a list

    qsort((char *)cgates, numgates, (int)sizeof(CLIST), (__compar_fn_t)compcong);

    lobj = Tcl_NewListObj(0, NULL);

    if (entries == 0) entries = numgates;
    else if (entries > numgates) entries = numgates;
    for (i = 0; i < entries; i++) {
	csrch = cgates[i];
	gsrch = csrch->gate;
	dobj = Tcl_NewListObj(0, NULL);
	Tcl_ListObjAppendElement(interp, dobj,
			Tcl_NewStringObj(gsrch->gatename, -1));
	Tcl_ListObjAppendElement(interp, dobj,
			Tcl_NewDoubleObj(csrch->congestion));
	Tcl_ListObjAppendElement(interp, lobj, dobj);
    }
    Tcl_SetObjResult(interp, lobj);

    // Cleanup
    free(Congestion);
    for (i = 0; i < numgates; i++) free(cgates[i]);
    free(cgates);

    return QrouterTagCallback(interp, objc, objv);
}

/*------------------------------------------------------*/
