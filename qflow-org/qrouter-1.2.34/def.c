/*
 * def.c --      
 *
 * This module incorporates the LEF/DEF format for standard-cell place and
 * route.
 *
 * Version 0.1 (September 26, 2003):  DEF input of designs.
 *
 * Written by Tim Edwards, Open Circuit Design
 * Modified April 2013 for use with qrouter
 *
 * It is assumed that the LEF files have been read in prior to this, and
 * layer information is already known.  The DEF file should have information
 * primarily on die are, track placement, pins, components, and nets.
 *
 * To-do: Routed nets should have their routes dropped into track obstructions,
 * and the nets should be ignored.  Currently, routed nets are parsed and the
 * routes are ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <math.h>		/* for roundf() function, if std=c99 */

#include "qrouter.h"
#include "node.h"
#include "qconfig.h"
#include "maze.h"
#include "lef.h"


#ifndef TCL_QROUTER

/* Find an instance in the instance list.  If qrouter	*/
/* is compiled with Tcl support, then this routine is	*/
/* found in tclqrouter.c and uses hash tables, greatly	*/
/* speeding up the read-in of large DEF files.		*/

GATE
DefFindInstance(char *name)
{
    GATE ginst;

    for (ginst = Nlgates; ginst; ginst = ginst->next) {
	if (!strcasecmp(ginst->gatename, name))
	    return ginst;
    }
    return NULL;
}

/* For the non-Tcl version, this is an empty placeholder */

void
DefHashInstance(GATE gateginfo)
{
}

#endif	/* TCL_QROUTER */

/*
 *------------------------------------------------------------
 *
 * DefAddRoutes --
 *
 *	Parse a network route statement from the DEF file.
 *	If "special" is 1, then, add the geometry to the
 *	list of obstructions.  If "special" is 0, then read
 *	the geometry into a route structure for the net. 
 *
 * Results:
 *	Returns the last token encountered.
 *
 * Side Effects:
 *	Reads from input stream;
 *	Adds information to the layout database.
 *
 *------------------------------------------------------------
 */

char *
DefAddRoutes(FILE *f, float oscale, NET net, char special)
{
    char *token;
    SEG newRoute = NULL;
    DSEG lr, drect;
    struct point_ refp;
    char valid = FALSE;		/* is there a valid reference point? */
    char initial = TRUE;
    struct dseg_ locarea;
    double x, y, lx, ly, w;
    int routeLayer, paintLayer;
    LefList lefl;
    NODE node;
    ROUTE routednet = NULL;

    node = net->netnodes;

    /* Set pitches and allocate memory for Obs[] if we haven't yet. */
    set_num_channels();

    while (initial || (token = LefNextToken(f, TRUE)) != NULL)
    {
	/* Get next point, token "NEW", or via name */
	if (initial || !strcmp(token, "NEW") || !strcmp(token, "new"))
	{
	    /* initial pass is like a NEW record, but has no NEW keyword */
	    initial = FALSE;

	    /* invalidate reference point */
	    valid = FALSE;

	    token = LefNextToken(f, TRUE);
	    routeLayer = LefFindLayerNum(token);

	    if (routeLayer < 0)
	    {
		LefError("Unknown layer type \"%s\" for NEW route\n", token); 
		continue;
	    }
	    paintLayer = routeLayer;

	    if (special)
	    {
		/* SPECIALNETS has the additional width */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%lg", &w) != 1)
		{
		    LefError("Bad width in special net\n");
		    continue;
		}
		if (w != 0)
		    w /= oscale;
		else
		    w = LefGetRouteWidth(paintLayer); 
	    }
	    else
		w = LefGetRouteWidth(paintLayer); 

	    // Create a new route record, add to the 1st node

	    if (special == (char)0) {
	       routednet = (ROUTE)malloc(sizeof(struct route_));
	       routednet->next = net->routes;
	       net->routes = routednet;

	       routednet->netnum = net->netnum;
	       routednet->segments = NULL;
	       routednet->flags = (u_char)0;
	    }
	}
	else if (*token != '(')	/* via name */
	{
	    /* A '+' or ';' record ends the route */
	    if (*token == ';' || *token == '+')
		break;

	    else if (valid == FALSE)
	    {
		LefError("Route has via name \"%s\" but no points!\n", token);
		continue;
	    }
	    lefl = LefFindLayer(token);
	    if (lefl != NULL)
	    {
		/* The area to paint is derived from the via definitions. */

		if (lefl != NULL)
		{
		    if (lefl->lefClass == CLASS_VIA) {
			paintLayer = Num_layers - 1;
			routeLayer = -1;
			lr = lefl->info.via.lr;
			while (lr != NULL) {
			   routeLayer = lr->layer;
			   if (routeLayer < paintLayer) paintLayer = routeLayer;
			   if ((routeLayer >= 0) && (special == (char)1) &&
					(valid == TRUE)) {
			      drect = (DSEG)malloc(sizeof(struct dseg_));
			      drect->x1 = x + lr->x1;
			      drect->x2 = x + lr->x2;
			      drect->y1 = y + lr->y1;
			      drect->y2 = y + lr->y2;
			      drect->layer = lr->layer;
			      drect->next = UserObs;
			      UserObs = drect;
			   }
			   lr = lr->next;
			}
			if (routeLayer == -1) paintLayer = lefl->type;
		    }
		    else
		    	paintLayer = lefl->type;
		}
		else
		{
		    LefError("Error: Via \"%s\" named but undefined.\n", token);
		    paintLayer = routeLayer;
		}
		if ((special == (char)0) && (paintLayer >= 0)) {

		    newRoute = (SEG)malloc(sizeof(struct seg_));
		    newRoute->segtype = ST_VIA;
		    newRoute->x1 = refp.x1;
		    newRoute->x2 = refp.x1;
		    newRoute->y1 = refp.y1;
		    newRoute->y2 = refp.y1;
		    newRoute->layer = paintLayer;

		    if (routednet == NULL) {
			routednet = (ROUTE)malloc(sizeof(struct route_));
			routednet->next = net->routes;
			net->routes = routednet;

			routednet->netnum = net->netnum;
			routednet->segments = NULL;
			routednet->flags = (u_char)0;
		    }
		    newRoute->next = routednet->segments;
		    routednet->segments = newRoute;
		}
		else
		    LefError("Via \"%s\" does not define a metal layer!\n", token);
	    }
	    else
		LefError("Via name \"%s\" unknown in route.\n", token);
	}
	else
	{
	    /* Revert to the routing layer type, in case we painted a via */
	    paintLayer = routeLayer;

	    /* Record current reference point */
	    locarea.x1 = refp.x1;
	    locarea.y1 = refp.y1;
	    lx = x;
	    ly = y;

	    /* Read an (X Y) point */
	    token = LefNextToken(f, TRUE);	/* read X */
	    if (*token == '*')
	    {
		if (valid == FALSE)
		{
		    LefError("No reference point for \"*\" wildcard\n"); 
		    goto endCoord;
		}
	    }
	    else if (sscanf(token, "%lg", &x) == 1)
	    {
		x /= oscale;		// In microns
		refp.x1 = (int)((x - Xlowerbound + EPS) / PitchX[paintLayer]);
	    }
	    else
	    {
		LefError("Cannot parse X coordinate.\n"); 
		goto endCoord;
	    }
	    token = LefNextToken(f, TRUE);	/* read Y */
	    if (*token == '*')
	    {
		if (valid == FALSE)
		{
		    LefError("No reference point for \"*\" wildcard\n"); 
		    if (newRoute != NULL) {
			free(newRoute);
			newRoute = NULL;
		    }
		    goto endCoord;
		}
	    }
	    else if (sscanf(token, "%lg", &y) == 1)
	    {
		y /= oscale;		// In microns
		refp.y1 = (int)((y - Ylowerbound + EPS) / PitchY[paintLayer]);
	    }
	    else
	    {
		LefError("Cannot parse Y coordinate.\n"); 
		goto endCoord;
	    }

	    /* Indicate that we have a valid reference point */

	    if (valid == FALSE)
	    {
		valid = TRUE;
	    }
	    else if ((locarea.x1 != refp.x1) && (locarea.y1 != refp.y1))
	    {
		/* Skip over nonmanhattan segments, reset the reference	*/
		/* point, and output a warning.				*/

		LefError("Can't deal with nonmanhattan geometry in route.\n");
		locarea.x1 = refp.x1;
		locarea.y1 = refp.y1;
		lx = x;
		ly = y;
	    }
	    else
	    {
		locarea.x2 = refp.x1;
		locarea.y2 = refp.y1;
		lx = x;
		ly = y;

		if (special == (char)1) {
		   if (valid == TRUE) {
		      drect = (DSEG)malloc(sizeof(struct dseg_));
		      if (lx > x) {
		         drect->x1 = x - w;
		         drect->x2 = lx + w;
		      }
		      else {
		         drect->x1 = x + w;
		         drect->x2 = lx - w;
		      }
		      if (ly > y) {
		         drect->y1 = y - w;
		         drect->y2 = ly + w;
		      }
		      else {
		         drect->y1 = y + w;
		         drect->y2 = ly - w;
		      }
		      drect->layer = routeLayer;
		      drect->next = UserObs;
		      UserObs = drect;
		   }
		}
		else if (paintLayer >= 0) {
		   newRoute = (SEG)malloc(sizeof(struct seg_));
		   newRoute->segtype = ST_WIRE;
		   newRoute->x1 = locarea.x1;
		   newRoute->x2 = locarea.x2;
		   newRoute->y1 = locarea.y1;
		   newRoute->y2 = locarea.y2;
		   newRoute->layer = paintLayer;

		   if (routednet == NULL) {
			routednet = (ROUTE)malloc(sizeof(struct route_));
			routednet->next = net->routes;
			net->routes = routednet;

			routednet->netnum = net->netnum;
			routednet->segments = NULL;
			routednet->flags = (u_char)0;
		   }
		   newRoute->next = routednet->segments;
		   routednet->segments = newRoute;
		}
	    }

endCoord:
	    /* Find the closing parenthesis for the coordinate pair */
	    while (*token != ')')
		token = LefNextToken(f, TRUE);
	}

    }

    /* Make sure we have allocated memory for nets */
    allocate_obs_array();

    /* Write the route(s) back into Obs[] */
    writeback_all_routes(net);

    return token;	/* Pass back the last token found */
}

/*
 *------------------------------------------------------------
 *
 * DefReadGatePin ---
 *
 *	Given a gate name and a pin name in a net from the
 *	DEF file NETS section, find the position of the
 *	gate, then the position of the pin within the gate,
 *	and add pin and obstruction information to the grid
 *	network.
 *
 *------------------------------------------------------------
 */

void
DefReadGatePin(NET net, NODE node, char *instname, char *pinname, double *home)
{
    NODE node2;
    int i, j;
    GATE gateginfo;
    DSEG drect;
    GATE g;
    double dx, dy;
    int gridx, gridy;
    DPOINT dp;

    g = DefFindInstance(instname);
    if (g) {

	gateginfo = g->gatetype;

	if (!gateginfo) {
	    LefError("Endpoint %s/%s of net %s not found\n",
				instname, pinname, net->netname);
	    return;
	}
	for (i = 0; i < gateginfo->nodes; i++) {
	    if (!strcasecmp(gateginfo->node[i], pinname)) {
		node->taps = (DPOINT)NULL;
		node->extend = (DPOINT)NULL;

		for (drect = g->taps[i]; drect; drect = drect->next) {

		    // Add all routing gridpoints that fall inside
		    // the rectangle.  Much to do here:
		    // (1) routable area should extend 1/2 route width
		    // to each side, as spacing to obstructions allows.
		    // (2) terminals that are wide enough to route to
		    // but not centered on gridpoints should be marked
		    // in some way, and handled appropriately.

		    gridx = (int)((drect->x1 - Xlowerbound) /
				PitchX[drect->layer]) - 1;
		    if (gridx < 0) gridx = 0;
		    while (1) {
			dx = (gridx * PitchX[drect->layer]) + Xlowerbound;
			if (dx > drect->x2 + home[drect->layer]) break;
			if (dx < drect->x1 - home[drect->layer]) {
			    gridx++;
			    continue;
			}
			gridy = (int)((drect->y1 - Ylowerbound) /
					PitchY[drect->layer]) - 1;
			if (gridy < 0) gridy = 0;
			while (1) {
			    dy = (gridy * PitchY[drect->layer])
					+ Ylowerbound;
			    if (dy > drect->y2 + home[drect->layer]) break;
			    if (dy < drect->y1 - home[drect->layer]) {
				gridy++;
				continue;
			    }

			    // Routing grid point is an interior point
			    // of a gate port.  Record the position

			    dp = (DPOINT)malloc(sizeof(struct dpoint_));
			    dp->layer = drect->layer;
			    dp->x = dx;
			    dp->y = dy;
			    dp->gridx = gridx;
			    dp->gridy = gridy;

			    if (dy >= drect->y1 && dx >= drect->x1 &&
					dy <= drect->y2 && dx <= drect->x2) {
				dp->next = node->taps;
				node->taps = dp;
			    }
			    else {
				dp->next = node->extend;
				node->extend = dp;
			    }
			    gridy++;
			}
			gridx++;
		    }
		}
		node->netnum = net->netnum;
		g->netnum[i] = net->netnum;
		g->noderec[i] = node;
		node->netname = net->netname;
		node->next = net->netnodes;
		net->netnodes = node;
		break;
	    }
	}
	if (i < gateginfo->nodes) return;
    }
}

/*
 *------------------------------------------------------------
 *
 * DefReadNets --
 *
 *	Read a NETS or SPECIALNETS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Networks are created, and geometry may be
 *	painted into the database top-level cell.
 *
 *------------------------------------------------------------
 */

enum def_net_keys {DEF_NET_START = 0, DEF_NET_END};
enum def_netprop_keys {
	DEF_NETPROP_USE = 0, DEF_NETPROP_ROUTED, DEF_NETPROP_FIXED,
	DEF_NETPROP_COVER, DEF_NETPROP_SHAPE, DEF_NETPROP_SOURCE,
	DEF_NETPROP_WEIGHT, DEF_NETPROP_PROPERTY};

void
DefReadNets(FILE *f, char *sname, float oscale, char special, int total)
{
    char *token;
    int keyword, subkey;
    int i, processed = 0;
    int nodeidx;
    char instname[MAX_NAME_LEN], pinname[MAX_NAME_LEN];

    NET net;
    int netidx;
    NODE node, node2;
    GATE gateginfo;
    DSEG drect;
    GATE g;
    double dx, dy;
    int gridx, gridy;
    DPOINT dp;
    double home[MAX_LAYERS];

    static char *net_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *net_property_keys[] = {
	"USE",
	"ROUTED",
	"FIXED",
	"COVER",
	"SHAPE",
	"SOURCE",
	"WEIGHT",
	"PROPERTY",
	NULL
    };

    if (Numnets == 0)
    {
	// Initialize net and node records
	netidx = MIN_NET_NUMBER;
	Nlnets = (NET *)malloc(total * sizeof(NET));
	for (i = 0; i < total; i++) Nlnets[i] = NULL;

	// Compute distance for keepout halo for each route layer
	// NOTE:  This must match the definition for the keepout halo
	// used in nodes.c!
	for (i = 0; i < Num_layers; i++) {
	    home[i] = LefGetViaWidth(i, i, 0) / 2.0 + LefGetRouteSpacing(i);
	}
    }
    else {
	Nlnets = (NET *)realloc(Nlnets, (Numnets + total) * sizeof(NET));
	for (i = Numnets; i < (Numnets + total); i++) Nlnets[i] = NULL;
    }

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, net_keys);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in NET "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}

	switch (keyword)
	{
	    case DEF_NET_START:

		/* Get net name */
		token = LefNextToken(f, TRUE);

		net = (NET)malloc(sizeof(struct net_));
		Nlnets[Numnets++] = net;
		net->netorder = 0;
		net->numnodes = 0;
		net->flags = 0;
		net->netname = strdup(token);
		net->netnodes = (NODE)NULL;
		net->noripup = (NETLIST)NULL;
		net->routes = (ROUTE)NULL;
		net->xmin = net->ymin = 0;
		net->xmax = net->ymax = 0;

		// Net numbers start at MIN_NET_NUMBER for regular nets,
		// use VDD_NET and GND_NET for power and ground, and 0
		// is not a valid net number.

		if (vddnet && !strcmp(token, vddnet))
		   net->netnum = VDD_NET;
		else if (gndnet && !strcmp(token, gndnet))
		   net->netnum = GND_NET;
		else
		   net->netnum = netidx++;

		nodeidx = 0;

		/* Update the record of the number of nets processed	*/
		/* and spit out a message for every 5% finished.	*/

		processed++;

		/* Get next token;  will be '(' if this is a netlist	*/
		token = LefNextToken(f, TRUE);

		/* Process all properties */
		while (token && (*token != ';'))
		{
		    /* Find connections for the net */
		    if (*token == '(')
		    {
			token = LefNextToken(f, TRUE);  /* get pin or gate */
			strcpy(instname, token);
			token = LefNextToken(f, TRUE);	/* get node name */

			if (!strcasecmp(instname, "pin")) {
			    strcpy(instname, token);
			    strcpy(pinname, "pin");
			}
			else
			    strcpy(pinname, token);

			node = (NODE)calloc(1, sizeof(struct node_));
			node->nodenum = nodeidx++;
			DefReadGatePin(net, node, instname, pinname, home);

			token = LefNextToken(f, TRUE);	/* should be ')' */

			continue;
		    }
		    else if (*token != '+')
		    {
			token = LefNextToken(f, TRUE);	/* Not a property */
			continue;	/* Ignore it, whatever it is */
		    }
		    else
			token = LefNextToken(f, TRUE);

		    subkey = Lookup(token, net_property_keys);
		    if (subkey < 0)
		    {
			LefError("Unknown net property \"%s\" in "
				"NET definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_NETPROP_USE:
			    /* Presently, we ignore this */
			    break;
			case DEF_NETPROP_SHAPE:
			    /* Ignore this too, along with the next keyword */
			    token = LefNextToken(f, TRUE);
			    break;
			case DEF_NETPROP_ROUTED:
			    // Read in the route;  qrouter now takes
			    // responsibility for this route.
			    while (token && (*token != ';'))
			        token = DefAddRoutes(f, oscale, net, special);
			    break;
			case DEF_NETPROP_FIXED:
			case DEF_NETPROP_COVER:
			    // Treat fixed nets like specialnets:  read them
			    // in as obstructions, and write them out as-is.
			    
			    while (token && (*token != ';'))
			        token = DefAddRoutes(f, oscale, net, (u_char)1);
			    break;
		    }
		}
		break;

	    case DEF_NET_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError("Net END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_NET_END) break;
    }

    // Set the number of nodes per net for each node on the net

    if (special == FALSE) {

	// Fill in the netnodes list for each net, needed for checking
	// for isolated routed groups within a net.

	for (i = 0; i < Numnets; i++) {
	    net = Nlnets[i];
	    for (node = net->netnodes; node; node = node->next)
		net->numnodes++;
	    for (node = net->netnodes; node; node = node->next)
		node->numnodes = net->numnodes;
	}
    }

    if (processed == total) {
	if (Verbose > 0)
	    Fprintf(stdout, "  Processed %d%s nets total.\n", processed,
			(special) ? " special" : "");
    }
    else
	LefError("Warning:  Number of nets read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefReadUseLocation --
 *
 *	Read location and orientation of a cell use
 *	Syntax: ( X Y ) O
 *
 * Results:
 *	0 on success, -1 on failure
 *
 * Side Effects:
 *	GATE definition for the use has the placedX, placedY,
 *	and orient values filled.
 *------------------------------------------------------------
 */
enum def_orient {DEF_NORTH, DEF_SOUTH, DEF_EAST, DEF_WEST,
	DEF_FLIPPED_NORTH, DEF_FLIPPED_SOUTH, DEF_FLIPPED_EAST,
	DEF_FLIPPED_WEST};

int
DefReadLocation(gate, f, oscale)
    GATE gate;
    FILE *f;
    float oscale;
{
    DSEG r;
    struct dseg_ tr;
    int keyword;
    char *token;
    float x, y;
    char mxflag, myflag;

    static char *orientations[] = {
	"N", "S", "E", "W", "FN", "FS", "FE", "FW"
    };

    token = LefNextToken(f, TRUE);
    if (*token != '(') goto parse_error;
    token = LefNextToken(f, TRUE);
    if (sscanf(token, "%f", &x) != 1) goto parse_error;
    token = LefNextToken(f, TRUE);
    if (sscanf(token, "%f", &y) != 1) goto parse_error;
    token = LefNextToken(f, TRUE);
    if (*token != ')') goto parse_error;
    token = LefNextToken(f, TRUE);

    keyword = Lookup(token, orientations);
    if (keyword < 0)
    {
	LefError("Unknown macro orientation \"%s\".\n", token);
	return -1;
    }

    mxflag = myflag = (char)0;

    switch (keyword)
    {
	case DEF_NORTH:
	    break;
	case DEF_SOUTH:
	    mxflag = 1;
	    myflag = 1;
	    break;
	case DEF_FLIPPED_NORTH:
	    mxflag = 1;
	    break;
	case DEF_FLIPPED_SOUTH:
	    myflag = 1;
	    break;
	case DEF_EAST:
	case DEF_WEST:
	case DEF_FLIPPED_EAST:
	case DEF_FLIPPED_WEST:
	    LefError("Error:  Cannot handle 90-degree rotated components!\n");
	    break;
    }

    if (gate) {
	gate->placedX = x / oscale;
	gate->placedY = y / oscale;
	gate->orient = MNONE;
	if (mxflag) gate->orient |= MX;
	if (myflag) gate->orient |= MY;
    }
    return 0;

parse_error:
    LefError("Cannot parse location: must be ( X Y ) orient\n");
    return -1;
}

/*
 *------------------------------------------------------------
 *
 * DefReadPins --
 *
 *	Read a PINS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Generates paint and labels in the layout.
 *
 *------------------------------------------------------------
 */

enum def_pins_keys {DEF_PINS_START = 0, DEF_PINS_END};
enum def_pins_prop_keys {
	DEF_PINS_PROP_NET = 0, DEF_PINS_PROP_DIR,
	DEF_PINS_PROP_LAYER, DEF_PINS_PROP_PLACED,
	DEF_PINS_PROP_USE, DEF_PINS_PROP_FIXED,
	DEF_PINS_PROP_COVER};

void
DefReadPins(FILE *f, char *sname, float oscale, int total)
{
    char *token;
    char pinname[MAX_NAME_LEN];
    int keyword, subkey, values;
    int processed = 0;
    int pinDir = PORT_CLASS_DEFAULT;
    DSEG currect, drect;
    GATE gate;
    int curlayer;
    double hwidth;

    static char *pin_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *pin_property_keys[] = {
	"NET",
	"DIRECTION",
	"LAYER",
	"PLACED",
	"USE",
	"FIXED",
	"COVER",
	NULL
    };

    static char *pin_classes[] = {
	"DEFAULT",
	"INPUT",
	"OUTPUT TRISTATE",
	"OUTPUT",
	"INOUT",
	"FEEDTHRU",
	NULL
    };

    static int lef_class_to_bitmask[] = {
	PORT_CLASS_DEFAULT,
	PORT_CLASS_INPUT,
	PORT_CLASS_TRISTATE,
	PORT_CLASS_OUTPUT,
	PORT_CLASS_BIDIRECTIONAL,
	PORT_CLASS_FEEDTHROUGH
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, pin_keys);

	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in PINS "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_PINS_START:		/* "-" keyword */

		/* Update the record of the number of pins		*/
		/* processed and spit out a message for every 5% done.	*/
 
		processed++;

		/* Get pin name */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%2047s", pinname) != 1)
		{
		    LefError("Bad pin statement:  Need pin name\n");
		    LefEndStatement(f);
		    break;
		}

		/* Create the pin record */
		gate = (GATE)malloc(sizeof(struct gate_));
		gate->gatetype = PinMacro;
		gate->gatename = NULL;	/* Use NET, but if none, use	*/
					/* the pin name, set at end.	*/
		gate->width = gate->height = 0;
		curlayer = -1;

		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') break;
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, pin_property_keys);
		    if (subkey < 0)
		    {
			LefError("Unknown pin property \"%s\" in "
				"PINS definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_PINS_PROP_NET:
			    /* Get the net name */
			    token = LefNextToken(f, TRUE);
			    gate->gatename = strdup(token);
			    gate->node[0] = strdup(token);
			    break;
			case DEF_PINS_PROP_DIR:
			    token = LefNextToken(f, TRUE);
			    subkey = Lookup(token, pin_classes);
			    if (subkey < 0)
				LefError("Unknown pin class\n");
			    else
				pinDir = lef_class_to_bitmask[subkey];
			    break;
			case DEF_PINS_PROP_LAYER:
			    curlayer = LefReadLayer(f, FALSE);
			    currect = LefReadRect(f, curlayer, oscale);
			    gate->width = currect->x2 - currect->x1;
			    gate->height = currect->y2 - currect->y1;
			    break;
			case DEF_PINS_PROP_USE:
			    /* Ignore this, for now */
			    break;
			case DEF_PINS_PROP_PLACED:
			case DEF_PINS_PROP_FIXED:
			case DEF_PINS_PROP_COVER:
			    DefReadLocation(gate, f, oscale);
			    break;
		    }
		}

		if (curlayer >= 0 && curlayer < Num_layers) {

		    /* If no NET was declared for pin, use pinname */
		    if (gate->gatename == NULL)
			gate->gatename = strdup(pinname);

		    /* Make sure pin is at least the size of the route layer */
		    drect = (DSEG)malloc(sizeof(struct dseg_));
		    gate->taps[0] = drect;
		    drect->next = (DSEG)NULL;

		    hwidth = LefGetRouteWidth(curlayer);
		    if (gate->width < hwidth) gate->width = hwidth;
		    if (gate->height < hwidth) gate->height = hwidth;
		    hwidth /= 2.0;
		    drect->x1 = gate->placedX - hwidth;
		    drect->y1 = gate->placedY - hwidth;
		    drect->x2 = gate->placedX + hwidth;
		    drect->y2 = gate->placedY + hwidth;
		    drect->layer = curlayer;
		    gate->obs = (DSEG)NULL;
		    gate->nodes = 1;
		    gate->next = Nlgates;
		    Nlgates = gate;

		    // Used by Tcl version of qrouter
		    DefHashInstance(gate);
		}
		else {
		    LefError("Pin %s is defined outside of route layer area!\n",
				pinname);
		    free(gate);
		}

		break;

	    case DEF_PINS_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError("Pins END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_PINS_END) break;
    }

    if (processed == total) {
	if (Verbose > 0)
	    Fprintf(stdout, "  Processed %d pins total.\n", processed);
    }
    else
	LefError("Warning:  Number of pins read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}
 
/*
 *------------------------------------------------------------
 *
 * DefReadVias --
 *
 *	Read a VIAS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Technically, this routine should be creating a cell for
 *	each defined via.  For now, it just computes the bounding
 *	rectangle and layer.
 *
 *------------------------------------------------------------
 */

enum def_vias_keys {DEF_VIAS_START = 0, DEF_VIAS_END};
enum def_vias_prop_keys {
	DEF_VIAS_PROP_RECT = 0};

void
DefReadVias(f, sname, oscale, total)
    FILE *f;
    char *sname;
    float oscale;
    int total;
{
    char *token;
    char vianame[LEF_LINE_MAX];
    int keyword, subkey, values;
    int processed = 0;
    int curlayer;
    DSEG currect;
    LefList lefl;

    static char *via_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *via_property_keys[] = {
	"RECT",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, via_keys);

	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in VIAS "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_VIAS_START:		/* "-" keyword */

		/* Update the record of the number of vias		*/
		/* processed and spit out a message for every 5% done.	*/
 
		processed++;

		/* Get via name */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%2047s", vianame) != 1)
		{
		    LefError("Bad via statement:  Need via name\n");
		    LefEndStatement(f);
		    break;
		}
		lefl = LefFindLayer(token);
                if (lefl == NULL)
                {
                    lefl = (LefList)calloc(1, sizeof(lefLayer));
                    lefl->type = -1;
                    lefl->obsType = -1;
                    lefl->lefClass = CLASS_VIA;
                    lefl->info.via.area.x1 = 0.0;
                    lefl->info.via.area.y1 = 0.0;
                    lefl->info.via.area.x2 = 0.0;
                    lefl->info.via.area.y2 = 0.0;
                    lefl->info.via.area.layer = -1;
                    lefl->info.via.cell = (GATE)NULL;
                    lefl->info.via.lr = (DSEG)NULL;
                    lefl->lefName = strdup(token);

                    lefl->next = LefInfo;
                    LefInfo = lefl;
		}
		else
		{
		    LefError("Warning:  Composite via \"%s\" redefined.\n", vianame);
		    lefl = LefRedefined(lefl, vianame);
		}

		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') break;
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, via_property_keys);
		    if (subkey < 0)
		    {
			LefError("Unknown via property \"%s\" in "
				"VIAS definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_VIAS_PROP_RECT:
			    curlayer = LefReadLayer(f, FALSE);
			    LefAddViaGeometry(f, lefl, curlayer, oscale);
			    break;
		    }
		}
		break;

	    case DEF_VIAS_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError("Vias END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_VIAS_END) break;
    }

    if (processed == total) {
	if (Verbose > 0)
	    Fprintf(stdout, "  Processed %d vias total.\n", processed);
    }
    else
	LefError("Warning:  Number of vias read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}
 
/*
 *------------------------------------------------------------
 *
 * DefReadBlockages --
 *
 *	Read a BLOCKAGES section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	UserObs list is updated with the additional
 *	obstructions.
 *
 *------------------------------------------------------------
 */

enum def_block_keys {DEF_BLOCK_START = 0, DEF_BLOCK_END};

void
DefReadBlockages(FILE *f, char *sname, float oscale, int total)
{
    char *token;
    int keyword, subkey, values, i;
    int processed = 0;
    DSEG drect, rsrch;
    LefList lefl;

    static char *blockage_keys[] = {
	"-",
	"END",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, blockage_keys);

	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in BLOCKAGE "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_BLOCK_START:		/* "-" keyword */

		/* Update the record of the number of components	*/
		/* processed and spit out a message for every 5% done.	*/
 
		processed++;

		/* Get layer name */
		token = LefNextToken(f, TRUE);
		lefl = LefFindLayer(token);
		if (lefl != NULL)
		{
		    drect = LefReadGeometry(NULL, f, oscale);
		    if (UserObs == NULL)
			UserObs = drect;
		    else {
			for (rsrch = UserObs; rsrch->next; rsrch = rsrch->next);
			rsrch->next = drect;
		    }
		}
		else
		{
		    LefError("Bad blockage statement:  Need layer name\n");
		    LefEndStatement(f);
		    break;
		}
		break;

	    case DEF_BLOCK_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError("Blockage END statement missing.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_BLOCK_END) break;
    }

    if (processed == total) {
	if (Verbose > 0)
	    Fprintf(stdout, "  Processed %d blockages total.\n", processed);
    }
    else
	LefError("Warning:  Number of blockages read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefReadComponents --
 *
 *	Read a COMPONENTS section from a DEF file.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Many.  Cell instances are created and added to
 *	the database.
 *
 *------------------------------------------------------------
 */

enum def_comp_keys {DEF_COMP_START = 0, DEF_COMP_END};
enum def_prop_keys {
	DEF_PROP_FIXED = 0, DEF_PROP_COVER,
	DEF_PROP_PLACED, DEF_PROP_UNPLACED,
	DEF_PROP_SOURCE, DEF_PROP_WEIGHT, DEF_PROP_FOREIGN,
	DEF_PROP_REGION, DEF_PROP_GENERATE, DEF_PROP_PROPERTY,
	DEF_PROP_EEQMASTER};

void
DefReadComponents(FILE *f, char *sname, float oscale, int total)
{
    GATE gateginfo;
    GATE gate;
    char *token;
    char usename[512];
    int keyword, subkey, values, i;
    int processed = 0;
    char OK;
    DSEG drect, newrect;
    double tmp, maxx, minx, maxy, miny;

    static char *component_keys[] = {
	"-",
	"END",
	NULL
    };

    static char *property_keys[] = {
	"FIXED",
	"COVER",
	"PLACED",
	"UNPLACED",
	"SOURCE",
	"WEIGHT",
	"FOREIGN",
	"REGION",
	"GENERATE",
	"PROPERTY",
	"EEQMASTER",
	NULL
    };

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, component_keys);

	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in COMPONENT "
			"definition; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}
	switch (keyword)
	{
	    case DEF_COMP_START:		/* "-" keyword */

		/* Update the record of the number of components	*/
		/* processed and spit out a message for every 5% done.	*/
 
		processed++;

		/* Get use and macro names */
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%511s", usename) != 1)
		{
		    LefError("Bad component statement:  Need use and macro names\n");
		    LefEndStatement(f);
		    break;
		}
		token = LefNextToken(f, TRUE);

		/* Find the corresponding macro */
		OK = 0;
		for (gateginfo = GateInfo; gateginfo; gateginfo = gateginfo->next) {
		    if (!strcasecmp(gateginfo->gatename, token)) {
			OK = 1;
			break;
		    }
		}
		if (!OK) {
		    LefError("Could not find a macro definition for \"%s\"\n",
				token);
		    gate = NULL;
		}
		else {
		    gate = (GATE)malloc(sizeof(struct gate_));
		    gate->gatename = strdup(usename);
		    gate->gatetype = gateginfo;
		}

		
		/* Now do a search through the line for "+" entries	*/
		/* And process each.					*/

		while ((token = LefNextToken(f, TRUE)) != NULL)
		{
		    if (*token == ';') break;
		    if (*token != '+') continue;

		    token = LefNextToken(f, TRUE);
		    subkey = Lookup(token, property_keys);
		    if (subkey < 0)
		    {
			LefError("Unknown component property \"%s\" in "
				"COMPONENT definition; ignoring.\n", token);
			continue;
		    }
		    switch (subkey)
		    {
			case DEF_PROP_PLACED:
			case DEF_PROP_UNPLACED:
			case DEF_PROP_FIXED:
			case DEF_PROP_COVER:
			    DefReadLocation(gate, f, oscale);
			    break;
			case DEF_PROP_SOURCE:
			case DEF_PROP_WEIGHT:
			case DEF_PROP_FOREIGN:
			case DEF_PROP_REGION:
			case DEF_PROP_GENERATE:
			case DEF_PROP_PROPERTY:
			case DEF_PROP_EEQMASTER:
			    token = LefNextToken(f, TRUE);
			    break;
		    }
		}

		if (gate != NULL)
		{
		    /* Process the gate */
		    gate->width = gateginfo->width;   
		    gate->height = gateginfo->height;   
		    gate->nodes = gateginfo->nodes;   
		    gate->obs = (DSEG)NULL;

		    for (i = 0; i < gate->nodes; i++) {
			/* Let the node names point to the master cell;	*/
			/* this is just diagnostic;  allows us, for	*/
			/* instance, to identify vdd and gnd nodes, so	*/
			/* we don't complain about them being		*/
			/* disconnected.				*/

			gate->node[i] = gateginfo->node[i];  /* copy pointer */
			gate->taps[i] = (DSEG)NULL;

			/* Global power/ground bus check */
			if (vddnet && !strcmp(gate->node[i], vddnet)) {
			   /* Create a placeholder node with no taps */
			   gate->netnum[i] = VDD_NET;
			   gate->noderec[i] = (NODE)calloc(1, sizeof(struct node_));
			   gate->noderec[i]->netnum = VDD_NET;
			}
			else if (gndnet && !strcmp(gate->node[i], gndnet)) {
			   /* Create a placeholder node with no taps */
			   gate->netnum[i] = GND_NET;
			   gate->noderec[i] = (NODE)calloc(1, sizeof(struct node_));
			   gate->noderec[i]->netnum = GND_NET;
			}
			else
			   gate->netnum[i] = 0;		/* Until we read NETS */

			/* Make a copy of the gate nodes and adjust for	*/
			/* instance position				*/

			for (drect = gateginfo->taps[i]; drect; drect = drect->next) {
			    newrect = (DSEG)malloc(sizeof(struct dseg_));
			    *newrect = *drect;
			    newrect->next = gate->taps[i];
			    gate->taps[i] = newrect;
			}

			for (drect = gate->taps[i]; drect; drect = drect->next) {
			    // handle offset from gate origin
			    drect->x1 -= gateginfo->placedX;
			    drect->x2 -= gateginfo->placedX;
			    drect->y1 -= gateginfo->placedY;
			    drect->y2 -= gateginfo->placedY;

			    // handle rotations and orientations here
			    if (gate->orient & MX) {
				tmp = drect->x1;
				drect->x1 = -drect->x2;
				drect->x1 += gate->placedX + gateginfo->width;
				drect->x2 = -tmp;
				drect->x2 += gate->placedX + gateginfo->width;
			    }
			    else {
				drect->x1 += gate->placedX;
				drect->x2 += gate->placedX;
			    }
			    if (gate->orient & MY) {
				tmp = drect->y1;
				drect->y1 = -drect->y2;
				drect->y1 += gate->placedY + gateginfo->height;
				drect->y2 = -tmp;
				drect->y2 += gate->placedY + gateginfo->height;
			    }
			    else {
				drect->y1 += gate->placedY;
				drect->y2 += gate->placedY;
			    }
		
			    if (drect->x1 > maxx) maxx = drect->x1;
			    else if (drect->x1 < minx) minx = drect->x1;
			    if (drect->x2 > maxx) maxx = drect->x2;
			    else if (drect->x2 < minx) minx = drect->x2;

			    if (drect->y1 > maxy) maxy = drect->y1;
			    else if (drect->y1 < miny) miny = drect->y1;
			    if (drect->y2 > maxy) maxy = drect->y2;
			    else if (drect->y2 < miny) miny = drect->y2;
			}
		    }

		    /* Make a copy of the gate obstructions and adjust	*/
		    /* for instance position				*/
		    for (drect = gateginfo->obs; drect; drect = drect->next) {
			newrect = (DSEG)malloc(sizeof(struct dseg_));
			*newrect = *drect;
			newrect->next = gate->obs;
			gate->obs = newrect;
		    }

		    for (drect = gate->obs; drect; drect = drect->next) {
			drect->x1 -= gateginfo->placedX;
			drect->x2 -= gateginfo->placedX;
			drect->y1 -= gateginfo->placedY;
			drect->y2 -= gateginfo->placedY;

			// handle rotations and orientations here
			if (gate->orient & MX) {
			    tmp = drect->x1;
			    drect->x1 = -drect->x2;
			    drect->x1 += gate->placedX + gateginfo->width;
			    drect->x2 = -tmp;
			    drect->x2 += gate->placedX + gateginfo->width;
			}
			else {
			    drect->x1 += gate->placedX;
			    drect->x2 += gate->placedX;
			}
			if (gate->orient & MY) {
			    tmp = drect->y1;
			    drect->y1 = -drect->y2;
			    drect->y1 += gate->placedY + gateginfo->height;
			    drect->y2 = -tmp;
			    drect->y2 += gate->placedY + gateginfo->height;
			}
			else {
			    drect->y1 += gate->placedY;
			    drect->y2 += gate->placedY;
			}
		    }
		    gate->next = Nlgates;
		    Nlgates = gate;

		    // Used by Tcl version of qrouter
		    DefHashInstance(gate);
		}
		break;

	    case DEF_COMP_END:
		if (!LefParseEndStatement(f, sname))
		{
		    LefError("Component END statement missing.\n");
		    keyword = -1;
		}

		/* Finish final call by placing the cell use */
		if ((total > 0) && (gate != NULL))
		{
		    // Nothing to do. . . gate has already been placed in list.
		    gate = NULL;
		}
		break;
	}
	if (keyword == DEF_COMP_END) break;
    }

    if (processed == total) {
	if (Verbose > 0)
	    Fprintf(stdout, "  Processed %d subcell instances total.\n", processed);
    }
    else
	LefError("Warning:  Number of subcells read (%d) does not match "
		"the number declared (%d).\n", processed, total);
}

/*
 *------------------------------------------------------------
 *
 * DefRead --
 *
 *	Read a .def file and parse die area, track positions,
 *	components, pins, and nets.
 *
 * Results:
 *	Returns the units scale, so the routed output can be
 *	scaled to match the DEF file header.
 *
 * Side Effects:
 *	Many.
 *
 *------------------------------------------------------------
 */

/* Enumeration of sections defined in DEF files */

enum def_sections {DEF_VERSION = 0, DEF_NAMESCASESENSITIVE,
	DEF_UNITS, DEF_DESIGN, DEF_REGIONS, DEF_ROW, DEF_TRACKS,
	DEF_GCELLGRID, DEF_DIVIDERCHAR, DEF_BUSBITCHARS,
	DEF_PROPERTYDEFINITIONS, DEF_DEFAULTCAP, DEF_TECHNOLOGY,
	DEF_HISTORY, DEF_DIEAREA, DEF_COMPONENTS, DEF_VIAS,
	DEF_PINS, DEF_PINPROPERTIES, DEF_SPECIALNETS,
	DEF_NETS, DEF_IOTIMINGS, DEF_SCANCHAINS, DEF_BLOCKAGES,
	DEF_CONSTRAINTS, DEF_GROUPS, DEF_EXTENSION,
	DEF_END};

float
DefRead(char *inName)
{
    FILE *f;
    char filename[256];
    char *token;
    int keyword, dscale, total;
    int curlayer, channels;
    int v, h, i;
    float oscale;
    double start, step;
    double llx, lly, urx, ury;
    char corient = '.';
    DSEG diearea;

    static char *sections[] = {
	"VERSION",
	"NAMESCASESENSITIVE",
	"UNITS",
	"DESIGN",
	"REGIONS",
	"ROW",
	"TRACKS",
	"GCELLGRID",
	"DIVIDERCHAR",
	"BUSBITCHARS",
	"PROPERTYDEFINITIONS",
	"DEFAULTCAP",
	"TECHNOLOGY",
	"HISTORY",
	"DIEAREA",
	"COMPONENTS",
	"VIAS",
	"PINS",
	"PINPROPERTIES",
	"SPECIALNETS",
	"NETS",
	"IOTIMINGS",
	"SCANCHAINS",
	"BLOCKAGES",
	"CONSTRAINTS",
	"GROUPS",
	"BEGINEXT",
	"END",
	NULL
    };

    if (!strrchr(inName, '.'))
	sprintf(filename, "%s.def", inName);
    else
	strcpy(filename, inName);
   
    f = fopen(filename, "r");

    if (f == NULL)
    {
	Fprintf(stderr, "Cannot open input file: ");
	perror(filename);
	return (float)0.0;
    }

    /* Initialize */

    if (Verbose > 0) {
	Fprintf(stdout, "Reading DEF data from file %s.\n", filename);
	Flush(stdout);
    }

    oscale = 1;
    lefCurrentLine = 0;
    v = h = -1;

    /* Read file contents */

    while ((token = LefNextToken(f, TRUE)) != NULL)
    {
	keyword = Lookup(token, sections);
	if (keyword < 0)
	{
	    LefError("Unknown keyword \"%s\" in DEF file; ignoring.\n", token);
	    LefEndStatement(f);
	    continue;
	}

	/* After the TRACKS have been read in, corient is 'x' or 'y'.	*/
	/* On the next keyword, finish filling in track information.	*/

	if (keyword != DEF_TRACKS && corient != '.')
	{
	    /* Because the TRACKS statement only covers the pitch of	*/
	    /* a single direction, we need to fill in with the pitch	*/
	    /* of opposing layers.  For now, we expect all horizontal	*/
	    /* routes to be at the same pitch, and all vertical routes	*/
	    /* to be at the same pitch.					*/

	    if (h == -1) h = v;
	    if (v == -1) v = h;

	    /* This code copied from qconfig.c.  Preferably, all	*/
	    /* information available in the DEF file should be taken	*/
	    /* from the DEF file.					*/

	    for (i = 0; i < Num_layers; i++)
	    {
		if (PitchX[i] != 0.0 && PitchX[i] != PitchX[v] && Verbose > 0)
		    Fprintf(stderr, "Multiple vertical route layers at different"
				" pitches.  Using pitch %g and routing on 1-of-N"
				" tracks for larger pitches.\n",
				PitchX[v]);
		PitchX[i] = PitchX[v];
		if (PitchY[i] != 0.0 && PitchY[i] != PitchY[h] && Verbose > 0)
		    Fprintf(stderr, "Multiple horizontal route layers at different"
				" pitches.  Using pitch %g and routing on 1-of-N"
				" tracks for larger pitches.\n",
				PitchY[h]);
		PitchY[i] = PitchY[h];

		corient = '.';	// So we don't run this code again.
	    }
	}

	switch (keyword)
	{
	    case DEF_VERSION:
		LefEndStatement(f);
		break;
	    case DEF_NAMESCASESENSITIVE:
		LefEndStatement(f);
		break;
	    case DEF_TECHNOLOGY:
		token = LefNextToken(f, TRUE);
		if (Verbose > 0)
		    Fprintf(stdout, "Diagnostic: DEF file technology: \"%s\"\n",
				token);
		LefEndStatement(f);
	 	break;
	    case DEF_REGIONS:
		LefSkipSection(f, sections[DEF_REGIONS]);
		break;
	    case DEF_DESIGN:
		token = LefNextToken(f, TRUE);
		if (Verbose > 0)
		    Fprintf(stdout, "Diagnostic: Design name: \"%s\"\n", token);
		LefEndStatement(f);
		break;
	    case DEF_UNITS:
		token = LefNextToken(f, TRUE);
		token = LefNextToken(f, TRUE);
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &dscale) != 1)
		{
		    LefError("Invalid syntax for UNITS statement.\n");
		    LefError("Assuming default value of 100\n");
		    dscale = 100;
		}
		/* We don't care if the scale is 100, 200, 1000, or 2000. */
		/* Do we need to deal with numeric roundoff issues?	  */
		oscale *= (float)dscale;
		LefEndStatement(f);
		break;
	    case DEF_ROW:
		LefEndStatement(f);
		break;
	    case DEF_TRACKS:
		token = LefNextToken(f, TRUE);
		if (strlen(token) != 1) {
		    LefError("Problem parsing track orientation (X or Y).\n");
		}
		corient = tolower(token[0]);	// X or Y
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%lg", &start) != 1) {
		    LefError("Problem parsing track start position.\n");
		}
		token = LefNextToken(f, TRUE);
		if (strcmp(token, "DO")) {
		    LefError("TRACKS missing DO loop.\n");
		}
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &channels) != 1) {
		    LefError("Problem parsing number of track channels.\n");
		}
		token = LefNextToken(f, TRUE);
		if (strcmp(token, "STEP")) {
		    LefError("TRACKS missing STEP size.\n");
		}
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%lg", &step) != 1) {
		    LefError("Problem parsing track step size.\n");
		}
		token = LefNextToken(f, TRUE);
		if (!strcmp(token, "LAYER")) {
		    curlayer = LefReadLayer(f, FALSE);
		}
		if (corient == 'x') {
		    Vert[curlayer] = 1;
		    PitchX[curlayer] = step / oscale;
		    if ((v == -1) || (PitchX[curlayer] < PitchX[v])) v = curlayer;
		    if ((curlayer < Num_layers - 1) && PitchX[curlayer + 1] == 0.0)
			PitchX[curlayer + 1] = PitchX[curlayer];
		    llx = start;
		    urx = start + step * channels;
		    // Fix, 5/24/2013:  Set bounds according to the tracks,
		    // since we really don't care about the die area.  But,
		    // we should make sure this is consistent across layers. . .
		    // if (llx < Xlowerbound)
			Xlowerbound = llx / oscale;
		    // if (urx > Xupperbound)
			Xupperbound = urx / oscale;
		}
		else {
		    Vert[curlayer] = 0;
		    PitchY[curlayer] = step / oscale;
		    if ((h == -1) || (PitchY[curlayer] < PitchX[h])) h = curlayer;
		    if ((curlayer < Num_layers - 1) && PitchY[curlayer + 1] == 0.0)
			PitchY[curlayer + 1] = PitchY[curlayer];
		    lly = start;
		    ury = start + step * channels;
		    // if (lly < Ylowerbound)
			Ylowerbound = lly / oscale;
		    // if (ury > Yupperbound)
			Yupperbound = ury / oscale;
		}
		LefEndStatement(f);
		break;
	    case DEF_GCELLGRID:
		LefEndStatement(f);
		break;
	    case DEF_DIVIDERCHAR:
		LefEndStatement(f);
		break;
	    case DEF_BUSBITCHARS:
		LefEndStatement(f);
		break;
	    case DEF_HISTORY:
		LefEndStatement(f);
		break;
	    case DEF_DIEAREA:
		diearea = LefReadRect(f, 0, oscale); // no current layer, use 0
		Xlowerbound = diearea->x1;
		Ylowerbound = diearea->y1;
		Xupperbound = diearea->x2;
		Yupperbound = diearea->y2;
		LefEndStatement(f);
		break;
	    case DEF_PROPERTYDEFINITIONS:
		LefSkipSection(f, sections[DEF_PROPERTYDEFINITIONS]);
		break;
	    case DEF_DEFAULTCAP:
		LefSkipSection(f, sections[DEF_DEFAULTCAP]);
		break;
	    case DEF_COMPONENTS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadComponents(f, sections[DEF_COMPONENTS], oscale, total);
		break;
	    case DEF_BLOCKAGES:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadBlockages(f, sections[DEF_BLOCKAGES], oscale, total);
		break;
	    case DEF_VIAS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadVias(f, sections[DEF_VIAS], oscale, total);
		break;
	    case DEF_PINS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadPins(f, sections[DEF_PINS], oscale, total);
		break;
	    case DEF_PINPROPERTIES:
		LefSkipSection(f, sections[DEF_PINPROPERTIES]);
		break;
	    case DEF_SPECIALNETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		DefReadNets(f, sections[DEF_SPECIALNETS], oscale, TRUE, total);
		break;
	    case DEF_NETS:
		token = LefNextToken(f, TRUE);
		if (sscanf(token, "%d", &total) != 1) total = 0;
		LefEndStatement(f);
		if (total > MAX_NETNUMS) {
		   LefError("Number of nets in design (%d) exceeds maximum (%d)\n",
				total, MAX_NETNUMS);
		}
		DefReadNets(f, sections[DEF_NETS], oscale, FALSE, total);
		break;
	    case DEF_IOTIMINGS:
		LefSkipSection(f, sections[DEF_IOTIMINGS]);
		break;
	    case DEF_SCANCHAINS:
		LefSkipSection(f, sections[DEF_SCANCHAINS]);
		break;
	    case DEF_CONSTRAINTS:
		LefSkipSection(f, sections[DEF_CONSTRAINTS]);
		break;
	    case DEF_GROUPS:
		LefSkipSection(f, sections[DEF_GROUPS]);
		break;
	    case DEF_EXTENSION:
		LefSkipSection(f, sections[DEF_EXTENSION]);
		break;
	    case DEF_END:
		if (!LefParseEndStatement(f, "DESIGN"))
		{
		    LefError("END statement out of context.\n");
		    keyword = -1;
		}
		break;
	}
	if (keyword == DEF_END) break;
    }
    if (Verbose > 0)
	Fprintf(stdout, "DEF read: Processed %d lines.\n", lefCurrentLine);
    LefError(NULL);	/* print statement of errors, if any, and reset */

    /* Cleanup */

    if (f != NULL) fclose(f);
    return oscale;
}
