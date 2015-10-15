/*--------------------------------------------------------------*/
/* qconfig.c -- .cfg file read/write for route                 	*/
/*--------------------------------------------------------------*/
/* Written by Steve Beccue 2003					*/
/*--------------------------------------------------------------*/
/* Modified by Tim Edwards, June 2011.  The route.cfg file is	*/
/* no longer the main configuration file but is supplementary	*/
/* to the LEF and DEF files.  Various configuration items that	*/
/* do not appear in the LEF and DEF formats, such as route	*/
/* costing, appear in this file, as well as a filename pointer	*/
/* to the LEF file with information on standard cell macros.	*/
/*--------------------------------------------------------------*/

#define _GNU_SOURCE	// for strcasestr(), see man page

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qrouter.h"
#include "qconfig.h"
#include "lef.h"

int    CurrentPin = 0;
int    Firstcall = TRUE;
int    PinNumber = 0;

int     Num_layers   = MAX_LAYERS;	// layers to use to route

double  PathWidth[MAX_LAYERS];		// width of the paths
int     GDSLayer[MAX_LAYERS];		// GDS layer number 
int     GDSCommentLayer = 1;		// for dummy wires, etc.
char    CIFLayer[MAX_LAYERS][50];	// CIF layer name
double  PitchX[MAX_LAYERS];		// Horizontal wire pitch of layer
double  PitchY[MAX_LAYERS];		// Vertical wire pitch of layer
int     NumChannelsX[MAX_LAYERS];	// number of wire channels in X on layer
int     NumChannelsY[MAX_LAYERS];	// number of wire channels in Y on layer
int     Vert[MAX_LAYERS];		// 1 if vertical, 0 if horizontal
int     Numpasses = 10;			// number of times to iterate in route_segs
char	StackedContacts = MAX_LAYERS;	// Value is number of contacts that may
					// be stacked on top of each other.
char	ViaPattern = VIA_PATTERN_NONE;	// Patterning to be used for vias based
					// on grid position (i.e., checkerboarding)

double  Xlowerbound=0.0;		// Bounding Box of routes, in microns
double  Xupperbound=0.0;      
double  Ylowerbound=10.0;
double  Yupperbound=10.0;      

int     SegCost = 1;               // Route cost of a segment
int     ViaCost = 5;               // Cost of via between adjacent layers
int     JogCost = 10;              // Cost of 1 grid off-direction jog
int     XverCost = 4;              // Cost of a crossover
int     BlockCost = 25;            // Cost of a crossover when node has
				   // only one tap point
int 	ConflictCost = 50;	   // Cost of shorting another route
				   // during the rip-up and reroute stage

char    *ViaX[MAX_LAYERS];
char    *ViaY[MAX_LAYERS];

/*--------------------------------------------------------------*/
/* post_config ---						*/
/*								*/
/* The following code ensures that the layer grids align.	*/
/* For now, all PitchX[i] and PitchY[i] should be the same	*/
/* for all layers.  Hopefully this restriction can be lifted	*/
/* sometime, but it will necessarily be a royal pain.		*/
/*--------------------------------------------------------------*/

void
post_config()
{
    int i, h, v;

    // Make sure that Num_layers does not exceed the number of
    // routing layers defined by the LEF file (or the config
    // file).

    i = LefGetMaxLayer();
    if (i < Num_layers) Num_layers = i;

    h = v = -1;
    for (i = 0; i < Num_layers; i++) {
       if (!Vert[i]) {
	  h = i;
	  PitchY[i] = PitchX[i];
	  PitchX[i] = 0.0;
       }
       else
	  v = i;
    }

    // In case all layers are listed as horizontal or all
    // as vertical, we should still handle it gracefully

    if (h == -1) h = v;
    else if (v == -1) v = h;

    for (i = 0; i < Num_layers; i++) {
       if (PitchX[i] != 0.0 && PitchX[i] != PitchX[v])
	  Fprintf(stderr, "Multiple vertical route layers at different"
		" pitches.  I cannot handle this!  Using pitch %g\n",
		PitchX[v]);
       PitchX[i] = PitchX[v];
       if (PitchY[i] != 0.0 && PitchY[i] != PitchY[h])
	  Fprintf(stderr, "Multiple horizontal route layers at different"
		" pitches.  I cannot handle this!  Using pitch %g\n",
		PitchY[h]);
       PitchY[i] = PitchY[h];
    }

} /* post_config() */

/*--------------------------------------------------------------*/
/* read_config - read in the config file        		*/
/*								*/
/*         ARGS: the filename (normally route.cfg)		*/
/*      RETURNS: number of lines successfully read		*/
/* SIDE EFFECTS: loads Global configuration variables		*/
/*--------------------------------------------------------------*/

int read_config(FILE *fconfig)
{
    int count, lines, i, OK;
    int iarg, iarg2;
    char carg;
    double darg, darg2, darg3, darg4;
    char sarg[MAX_LINE_LEN];
    size_t len = 0;
    char   line[MAX_LINE_LEN];
    char  *lineptr;
    STRING dnr;               // Do Not Route nets
    STRING cn;                // critical nets
    STRING strl;
    GATE   gateinfo;          // gate information, pin location, etc
    DSEG   grect, drect;

    if (Firstcall) {
	for (i = 0; i < MAX_LAYERS; i++) {
	    sprintf(line, "via%d%d", i + 1, i + 2);
	    ViaX[i] = strdup(line);
	    ViaY[i] = NULL;
	}

	DontRoute = (STRING)NULL;
	CriticalNet = (STRING)NULL;
        GateInfo = (GATE)NULL;
	Nlgates = (GATE)NULL;
	UserObs = (DSEG)NULL;

	for (i = 0; i < MAX_LAYERS; i++)
	   PitchX[i] = PitchY[i] = 0.0;

	Firstcall = 0;
    }

    if (!fconfig) return -1;

    count = 0;
    lines = 0;

    while (!feof(fconfig)) {
	fgets(line, MAX_LINE_LEN, fconfig);
	lines++;
	lineptr = line;
	while (isspace(*lineptr)) lineptr++;

	if (!strncasecmp(lineptr, "lef", 3) || !strncmp(lineptr, "read_lef", 8)) {
	    if ((i = sscanf(lineptr, "%*s %s\n", sarg)) == 1) {
	       // Argument is a filename of a LEF file from which we
	       // should get the information about gate pins & obstructions
	       OK = 1;
	       LefRead(sarg);
	    }
	}

	// The remainder of the statements is not case sensitive.

	for (i = 0; line[i] && i < MAX_LINE_LEN - 1; i++) {
	    line[i] = (char)tolower(line[i]);
	}
	
	if ((i = sscanf(lineptr, "num_layers %d", &iarg)) == 1) {
	    OK = 1; Num_layers = iarg;
	}
	else if ((i = sscanf(lineptr, "layers %d", &iarg)) == 1) {
	    OK = 1; Num_layers = iarg;
	}

	if ((i = sscanf(lineptr, "layer_%d_name %s", &iarg2, sarg)) == 2) {
	    if (iarg2 > 0 && iarg2 < 10) {
	       OK = 1; strcpy(CIFLayer[iarg2 - 1], sarg);
	    }
	}

	if ((i = sscanf(lineptr, "gds_layer_%d %d", &iarg2, &iarg)) == 2) {
	    if (iarg2 > 0 && iarg2 < 10) {
	        OK = 1; GDSLayer[iarg2 - 1] = iarg;
	    }
	}
	if ((i = sscanf(lineptr, "gds_comment_layer %d", &iarg)) == 1) {
	    OK = 1; GDSCommentLayer = iarg;
	}

	if ((i = sscanf(lineptr, "layer_1_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[0] = darg;
	}
	if ((i = sscanf(lineptr, "layer_2_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[1] = darg;
	}
	if ((i = sscanf(lineptr, "layer_3_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[2] = darg;
	}
	if ((i = sscanf(lineptr, "layer_4_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[3] = darg;
	}
	if ((i = sscanf(lineptr, "layer_5_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[4] = darg;
	}
	if ((i = sscanf(lineptr, "layer_6_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[5] = darg;
	}
	if ((i = sscanf(lineptr, "layer_7_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[6] = darg;
	}
	if ((i = sscanf(lineptr, "layer_8_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[7] = darg;
	}
	if ((i = sscanf(lineptr, "layer_9_width %lf", &darg)) == 1) {
	    OK = 1; PathWidth[8] = darg;
	}

	if ((i = sscanf(lineptr, "x lower bound %lf", &darg)) == 1) {
	    OK = 1; Xlowerbound = darg;
	}
	if ((i = sscanf(lineptr, "x upper bound %lf", &darg)) == 1) {
	    OK = 1; Xupperbound = darg;
	}
	if ((i = sscanf(lineptr, "y lower bound %lf", &darg)) == 1) {
	    OK = 1; Ylowerbound = darg;
	}
	if ((i = sscanf(lineptr, "y upper bound %lf", &darg)) == 1) {
	    OK = 1; Yupperbound = darg;
	}
	
	if ((i = sscanf(lineptr, "layer %d wire pitch %lf\n", &iarg, &darg)) == 2) {
	    OK = 1; PitchX[iarg-1] = darg;
	}
	else if (i == 1) {
	   if ((i = sscanf(lineptr, "layer %*d vertical %d\n", &iarg2)) == 1) {
	      OK = 1; Vert[iarg - 1] = iarg2;
	   }
	   else if ((i = sscanf(lineptr, "layer %*d %c\n", &carg)) == 1) {
	      if (tolower(carg) == 'v') {
		 OK = 1; Vert[iarg - 1] = 1;
	      }
	      else if (tolower(carg) == 'h') {
		 OK = 1; Vert[iarg - 1] = 0;
	      }
	   }
	}

	if ((i = sscanf(lineptr, "num passes %d\n", &iarg)) == 1) {
	    OK = 1;
	    Numpasses = iarg;
	}
	else if ((i = sscanf(lineptr, "passes %d\n", &iarg)) == 1) {
	    OK = 1;
	    Numpasses = iarg;
	}
	
	if ((i = sscanf(lineptr, "route segment cost %d", &iarg)) == 1) {
	    OK = 1; SegCost = iarg;
	}
	
	if ((i = sscanf(lineptr, "route via cost %d", &iarg)) == 1) {
	    OK = 1; ViaCost = iarg;
	}
	
	if ((i = sscanf(lineptr, "route jog cost %d", &iarg)) == 1) {
	    OK = 1; JogCost = iarg;
	}
	
	if ((i = sscanf(lineptr, "route crossover cost %d", &iarg)) == 1) {
	    OK = 1; XverCost = iarg;
	}

	if ((i = sscanf(lineptr, "route block cost %d", &iarg)) == 1) {
	    OK = 1; BlockCost = iarg;
	}

	if ((i = sscanf(lineptr, "do not route node %s\n", sarg)) == 1) {
	    OK = 1; 
	    dnr = (STRING)malloc(sizeof(struct string_));
	    dnr->name = strdup(sarg);
	    if (DontRoute != NULL) {
	       for (strl = DontRoute; strl->next; strl = strl->next);
	       strl->next = dnr;
	    }
	    else {
	       dnr->next = NULL;
	       DontRoute = dnr;
	    }
	}
	
	if ((i = sscanf(lineptr, "route priority %s\n", sarg)) == 1) {
	    OK = 1; 
	    cn = (STRING)malloc(sizeof(struct string_));
	    cn->name = strdup(sarg);
	    if (CriticalNet != NULL) {
	       for (strl = CriticalNet; strl->next; strl = strl->next);
	       strl->next = cn;
	    }
	    else {
	       cn->next = NULL;
	       CriticalNet = cn;
	    }
	}
	
	if ((i = sscanf(lineptr, "critical net %s\n", sarg)) == 1) {
	    OK = 1; 
	    cn = (STRING)malloc(sizeof(struct string_));
	    cn->name = strdup(sarg);
	    if (CriticalNet != NULL) {
	       for (strl = CriticalNet; strl->next; strl = strl->next);
	       strl->next = cn;
	    }
	    else {
	       cn->next = NULL;
	       CriticalNet = cn;
	    }
	}

	// Search for "no stack".  This allows variants like "no stacked
	// contacts", "no stacked vias", or just "no stacking", "no stacks",
	// etc.

	if (strcasestr(lineptr, "no stack") != NULL) {
	    OK = 1; StackedContacts = 1;
	}

	// Search for "stack N", where "N" is the largest number of vias
	// that can be stacked upon each other.  Values 0 and 1 are both
	// equivalent to specifying "no stack".

	if ((i = sscanf(lineptr, "stack %d", &iarg)) == 1) {
	    OK = 1; StackedContacts = iarg;
	    // Can't let StackedContacts be zero because qrouter would
	    // believe that all contacts are disallowed, leading to a
	    // lot of wasted processing time while it determines that's
	    // not possible. . .
	    if (StackedContacts == 0) StackedContacts = 1;
	}
	else if ((i = sscanf(lineptr, "via stack %d", &iarg)) == 1) {
	    OK = 1; StackedContacts = iarg;
	    if (StackedContacts == 0) StackedContacts = 1;
	}

	// Look for via patterning specifications
	if (strcasestr(lineptr, "via pattern") != NULL) {
	    if (strcasestr(lineptr + 12, "normal") != NULL)
		ViaPattern = VIA_PATTERN_NORMAL;
	    else if (strcasestr(lineptr + 12, "invert") != NULL)
		ViaPattern = VIA_PATTERN_INVERT;
 	}

	if ((i = sscanf(lineptr, "obstruction %lf %lf %lf %lf %s\n",
			&darg, &darg2, &darg3, &darg4, sarg)) == 5) {
	    OK = 1;
	    drect = (DSEG)malloc(sizeof(struct dseg_));
	    drect->x1 = darg;
	    drect->y1 = darg2;
	    drect->x2 = darg3;
	    drect->y2 = darg4;
	    drect->layer = LefFindLayerNum(sarg);
	    if (drect->layer < 0) {
		if ((i = sscanf(sarg, "%lf", &darg)) == 1) {
		    i = (int)(darg + EPS);
		    if (i >= 0 && i < Num_layers) {
		        drect->layer = i;
		    }
		}
	    }
	    if (drect->layer >= 0) {
		drect->next = UserObs;
		UserObs = drect;
	    }
	    else {
		free(drect);
	    }
	}

	if ((i = sscanf(lineptr, "gate %s %lf %lf\n", sarg, &darg, &darg2)) == 3) {
	    OK = 1; 
	    CurrentPin = 0;
	    gateinfo = (GATE)malloc(sizeof(struct gate_));
	    gateinfo->gatename = strdup(sarg);
	    gateinfo->gatetype = NULL;
	    gateinfo->width = darg;
	    gateinfo->height = darg2;
	    gateinfo->placedX = 0.0;	// implicit cell origin
	    gateinfo->placedY = 0.0;
	    gateinfo->next = GateInfo;	// prepend to linked gate list
	    GateInfo = gateinfo;
	}
	
        if ((i = sscanf(lineptr, "endgate %s\n", sarg)) == 1) {
	    OK = 1; 
	    gateinfo->nodes = CurrentPin;

	    // This syntax does not include declaration of obstructions
	    gateinfo->obs = (DSEG)NULL;
	    CurrentPin = 0;
        }

	if ((i = sscanf(lineptr, "pin %s %lf %lf\n", sarg, &darg, &darg2)) == 3) {
	    OK = 1; 
	    gateinfo->node[CurrentPin] = strdup(sarg);

	    // These style gates have only one tap per gate;  LEF file reader
	    // allows multiple taps per gate node.

	    drect = (DSEG)malloc(sizeof(struct dseg_));
	    gateinfo->taps[CurrentPin] = drect;
	    drect->x1 = drect->x2 = darg;
	    drect->y1 = drect->y2 = darg2;

	    // This syntax always defines pins on layer 0;  LEF file reader
	    // allows pins on all layers.

	    drect->layer = 0;
	    drect->next = (DSEG)NULL;
	    CurrentPin++;
	}

	if (OK == 0) {
	    if (!(lineptr[0] == '\n' || lineptr[0] == '#' || lineptr[0] == 0)) {
		Fprintf(stderr, "line not understood: %s\n", line);
	    }
	}
	OK = 0;
	line[0] = line[1] = '\0';

    }
    post_config();
    return count;

} /* read_config() */
