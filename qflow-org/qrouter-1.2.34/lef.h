/*
 * lef.h --
 *
 * This file defines things that are used by internal LEF routines in
 * various files.
 *
 */

#ifndef _LEFINT_H
#define _LEFINT_H

/* Some constants for LEF and DEF files */

#define LEF_LINE_MAX 2048  /* Maximum length fixed by LEF/DEF specifications */
#define LEF_MAX_ERRORS 100 /* Max # errors to report; limits output if */
                           /* something is really wrong about the file */

#define DEFAULT_WIDTH 3	   /* Default metal width for routes if undefined */
#define DEFAULT_SPACING 4  /* Default spacing between metal if undefined  */

/* Structure holding the counts of regular and special nets */

typedef struct
{
    int regular;
    int special;
    u_char has_nets;
} NetCount;

/* Various modes for writing nets. */
#define DO_REGULAR  0
#define DO_SPECIAL  1
#define ALL_SPECIAL 2	/* treat all nets as SPECIALNETS */

/* Port classes */
enum port_classes {PORT_CLASS_DEFAULT = 0, PORT_CLASS_INPUT,
        PORT_CLASS_TRISTATE, PORT_CLASS_OUTPUT, PORT_CLASS_BIDIRECTIONAL,
        PORT_CLASS_FEEDTHROUGH};

/* Port uses */
enum port_uses {PORT_USE_DEFAULT = 0, PORT_USE_SIGNAL,
        PORT_USE_ANALOG, PORT_USE_POWER, PORT_USE_GROUND,
        PORT_USE_CLOCK};

/* Structure to hold information about spacing rules */
typedef struct _lefSpacingRule *lefSpacingPtr;
typedef struct _lefSpacingRule {
    lefSpacingPtr next;
    double width;	/* width, in microns */
    double spacing;	/* minimum spacing rule, in microns */
} lefSpacingRule;

/* Structure used to maintain default routing information for each	*/
/* routable layer type.							*/

typedef struct {
    lefSpacingRule *spacing;	/* spacing rules, ordered by width */
    double  width;	/* nominal route width, in microns */
    double  pitch;	/* route pitch, in microns */
    double  offset;	/* route track offset from origin, in microns */
    u_char hdirection;	/* horizontal direction preferred */
} lefRoute;

/* Structure used to maintain default generation information for each	*/
/* via or viarule (contact) type.  If "cell" is non-NULL, then the via	*/
/* is saved in a cell (pointed to by "cell"), and "area" describes the	*/
/* bounding box.  Otherwise, the via is formed by magic type "type"	*/
/* with a minimum area "area" for a single contact.			*/

typedef struct {
    struct dseg_ area;		/* Area of single contact, or cell bbox	*/
				/* in units of microns			*/
    GATE	cell;		/* Cell for fixed via def, or NULL	*/
    DSEG	lr;		/* Extra information for vias with	*/
				/* more complicated geometry.		*/
    int		obsType;	/* Secondary obstruction type		*/
} lefVia;

/* Defined types for "lefClass" in the lefLayer structure */

#define CLASS_ROUTE	0	/* routing layer */
#define CLASS_VIA	1	/* via or cut layer */
#define CLASS_MASTER	2	/* masterslice layer */
#define CLASS_OVERLAP	3	/* overlap layer */
#define CLASS_IGNORE	4	/* inactive layer */

/* Structure defining a route or via layer and matching it to a magic	*/
/* layer type.  This structure is saved in the LefInfo list.		*/

typedef struct _lefLayer *LefList;

typedef struct _lefLayer {
    LefList next;	/* Next layer in linked list */
    char *lefName;	/* CIF name of this layer */
    int	  type;		/* GDS layer type, or -1 for none */
    int	  obsType;	/* GDS type to use if this is an obstruction */
    u_char lefClass;	/* is this a via, route, or masterslice layer */
    union {
	lefRoute  route;	/* for route layers */
	lefVia	  via;		/* for contacts */
    } info;
} lefLayer;

/* External declaration of global variables */
extern int lefCurrentLine;
extern LefList LefInfo;

/* Forward declarations */

u_char LefParseEndStatement(FILE *f, char *match);
void  LefSkipSection(FILE *f, char *match);
void  LefEndStatement(FILE *f);
GATE  lefFindCell(char *name);
char *LefNextToken(FILE *f, u_char ignore_eol);
char *LefLower(char *token);
DSEG  LefReadGeometry(GATE lefMacro, FILE *f, float oscale);
LefList LefRedefined(LefList lefl, char *redefname);
void LefAddViaGeometry(FILE *f, LefList lefl, int curlayer, float oscale);
DSEG LefReadRect(FILE *f, int curlayer, float oscale);
int  LefReadLayer(FILE *f, u_char obstruct);
LefList LefFindLayer(char *token);
LefList LefFindLayerByNum(int layer);
int    LefFindLayerNum(char *token);
double LefGetRouteKeepout(int layer);
double LefGetRouteWidth(int layer);
double LefGetViaWidth(int base, int layer, int dir);
double LefGetRouteSpacing(int layer);
double LefGetRouteWideSpacing(int layer, double width);
double LefGetRoutePitch(int layer);
double LefGetRouteOffset(int layer);
char  *LefGetRouteName(int layer);
int    LefGetRouteOrientation(int layer);
int    LefGetMaxLayer();

GATE   LefFindInstance(char *name);
void   LefHashCell(GATE gateginfo);

void   LefRead(char *inName);
float  DefRead(char *inName);

void LefError(char *fmt, ...);	/* Variable argument procedure requires */
				/* parameter list.			*/

#endif /* _LEFINT_H */
