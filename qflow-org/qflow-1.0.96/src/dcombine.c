/*------------------------------------------------------*/
/* dcombine.c ---					*/
/*							*/
/*   Merge two BDNET netlists into one.  The argments	*/
/*   are:						*/
/*							*/
/*	dcombine <verilog_file> <bdnet_file1> ...	*/
/*							*/
/* The <verilog_file> is parsed to find the name, size,	*/
/* and direction of all top-level system inputs and	*/
/* outputs.  Then each <bdnet_file#> is parsed and	*/
/* added to the combined output file.  Nodes are	*/
/* tracked to make sure that no node names are repeated	*/
/* between the BDNET input files.			*/
/*							*/
/* Output is to stdout.					*/
/*------------------------------------------------------*/

#define DEBUG 1

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <malloc.h>
#include <stdlib.h>

// Define states
#define HEADER_STUFF	0x0001
#define MODULE_VALID	0x0002
#define MODULE_ARGS	0x0004
#define INPUT_OUTPUT	0x0008
#define IO_SIGNAL	0x0010
#define MAIN_BODY	0x0020
#define INSTANCE	0x0040
#define FORM		0x0080
#define PIN		0x0100
#define NET		0x0200
#define OUTPUT_NET	0x0400
#define DONE		0x0800

// Define all the structures we need to hold the complete
// netlist for a clock domain

typedef struct _pin *pinptr;

typedef struct _pin {
   char *name;		// Local name of pin
   char *net;		// Name of net to which pin is connected
   pinptr next;
} pin;

typedef struct _instance *instptr;

typedef struct _instance {
   char *name;
   pinptr pins;
   instptr next;
} instance;

typedef struct _module *modptr;

typedef struct _module {
   char *name;
   char *prefix;		// Used to disambiguate node names
   pinptr outputs;
   instptr instances;
   modptr next;
} module;

typedef struct _signal *sigptr;

typedef struct _signal {
   char *name;
   int start;	// -1 indicates that this is not a vector
   int end;
   int type;
   sigptr next;
} signal;

// Define I/O types
#define TYPE_INPUT	0x0001
#define TYPE_OUTPUT	0x0002
#define TYPE_INTERNAL	0x0004

//-------------------------------------------------------------------
// Check if the name "net" is in modsigs as an input net.  If so,
// return the name unaltered.  Otherwise, prepend "prefix".
// However, if the name is in brackets ("[...]"), then put the
// prefix inside the brackets.
//-------------------------------------------------------------------

char *
get_net_name(sigptr modsigs, char *prefix, char *net)
{
    static char netname[512];
    sigptr testsig;
    char *bptr;

    bptr = strchr(net, '<');
    if (bptr) *bptr = '\0';

    for (testsig = modsigs; testsig; testsig = testsig->next) {
	if (testsig->type == TYPE_INPUT || testsig->type == TYPE_INTERNAL)
	    if (!strcmp(testsig->name, net)) {
		if (bptr) *bptr = '<';
		return net;
	    }
    }
    if (bptr) *bptr = '<';

    if (net[0] == '[')
	sprintf(netname, "[%s%s", prefix, net + 1);
    else
	sprintf(netname, "%s%s", prefix, net);

    return (char *)netname;
}

//-------------------------------------------------------------------
// Main routine
//-------------------------------------------------------------------

int
main(int argc, char *argv[])
{
    FILE *fsource = NULL;	/* Verilog source */
    FILE *fnet = NULL;		/* BDNET source */
    char *xp, *fptr, *token, *pptr, *bptr;
    char *module_name, *testnet;
    char locfname[512];
    char signame[512];
    char linebuf[2048];
    const char *toklist;
    int state, vlimit, i;
    sigptr modsigs, newsig, testsig;
    modptr modules = NULL, testmod, testmod2;
    instptr testinst;
    pinptr testpin, testout;

    // Need at least 3 arguments, the verilog source and at least
    // two BDNET files.  If there is only one BDNET file then there
    // is nothing to combine.

    if (argc < 3) {
	fprintf(stderr, "Usage: dcombine <verilog_file> <bdnet_file1> ...\n");
	exit(1);
    }

    /* If argv[1] does not have a .v extension, make one */

    xp = strrchr(argv[1], '.');
    if (xp != NULL)
	snprintf(locfname, 511, "%s", argv[1]);
    else
	snprintf(locfname, 511, "%s.v", argv[1]);

    fsource = fopen(locfname, "r");
    if (fsource == NULL) {
	fprintf(stderr, "Error:  No such file or cannot open file \"%s\"\n",
			locfname);
	exit(1);
    }

    // Read verilog file until we get a keyword "input" or "output";  then,
    // parse all such lines.  Retain information about the I/O list, and
    // use this to sort the BDNET files.

    modsigs = NULL;
    toklist = " \t\n";
    state = HEADER_STUFF;

    fgets(linebuf, 2047, fsource);
    fptr = linebuf;

    while (1) {
	token = strtok(fptr, toklist);

	while (token != NULL) {

	    if (!strncmp(token, "//", 2)) {
		break;		// Comment, forget rest of line
	    }

	    switch (state) {
		case HEADER_STUFF:
		    if (!strcmp(token, "module")) {
			state = MODULE_VALID;
		    }
		    break;

		case MODULE_VALID:
		    module_name = strdup(token);
		    if (pptr = strchr(module_name, '(')) *pptr = '\0';
		    state = MODULE_ARGS;
		    toklist = " \t\n)";
		    break;

		case MODULE_ARGS:
		    if (!strcmp(token, ";")) {
			state = INPUT_OUTPUT;
			toklist = " \t\n";
		    }
		    break;

		case INPUT_OUTPUT:
		    if (!strcmp(token, "input")) {
			toklist = " \t\n\[:]";
			state = IO_SIGNAL;
			newsig = (signal *)malloc(sizeof(signal));
			newsig->next = modsigs;
			modsigs = newsig;
			newsig->type = TYPE_INPUT;
			newsig->start = -1;
			newsig->end = -1;
		    }
		    else if (!strcmp(token, "output")) {
			toklist = " \t\n\[:]";
			state = IO_SIGNAL;
			newsig = (signal *)malloc(sizeof(signal));
			newsig->next = modsigs;
			modsigs = newsig;
			newsig->type = TYPE_OUTPUT;
			newsig->start = -1;
			newsig->end = -1;
		    }
		    else if (token != "") {
			state = MAIN_BODY;
		    }
		    break;

		case IO_SIGNAL:
		    pptr = strchr(token, ';');
		    if (pptr != NULL) *pptr = '\0';

		    if (pptr != token) {
			if (sscanf(token, "%d", &vlimit) == 1) {
			    if (newsig->start == -1)
				newsig->start = vlimit;
			    else if (newsig->end == -1)
				newsig->end = vlimit;
			}
			else
			    newsig->name = strdup(token);
		    }

		    if (pptr != NULL) {
			*pptr = ';';
			state = INPUT_OUTPUT;
			toklist = " \t\n";
		    }
		    break;

		case MAIN_BODY:
		    // Just read to the end of input
		    break;

		default:
		    break;
	    }

	    // Proceed to next token
	    token = strtok(NULL, toklist);
	}

	if (fgets(linebuf, 2047, fsource) == NULL)
	    break;
    }
    fclose(fsource);
    fsource = NULL;

    /* Start writing the output combined BDNET file */

    fprintf(stdout, "MODEL \"%s\";\n\nINPUT", module_name);

    /* Write all of the input pins */

    for (testsig = modsigs; testsig; testsig = testsig->next) {
	if (testsig->type == TYPE_INPUT) {
	    if (testsig->start == -1)
		fprintf(stdout, "\n\t\"%s\" : \"%s\"",
				testsig->name, testsig->name);

	    else if (testsig->start < testsig->end) {
		for (i = testsig->start; i <= testsig->end; i++)
		    fprintf(stdout, "\n\t\"%s<%d>\" : \"%s<%d>\"",
					testsig->name, i, testsig->name, i);
	    }
	    else {
		for (i = testsig->start; i >= testsig->end; i--)
		    fprintf(stdout, "\n\t\"%s<%d>\" : \"%s<%d>\"",
					testsig->name, i, testsig->name, i);
	    }
	}
    }
    fprintf(stdout, ";\n\nOUTPUT");	// Finish the INPUT section

    /* Now open each BDNET netlist file and process it */

    for (i = 2; i < argc; i++) {
	char *newname;
	modptr newmod;
	instptr newinst;
	pinptr newpin;

	newmod = (modptr)malloc(sizeof(module));
	newmod->next = modules;
	modules = newmod;
	newmod->prefix = (char *)malloc(6);	// Up to 999 modules, overkill!
	newname = strdup(argv[i]);
	sprintf(newmod->prefix, "d%d_", i - 1);
	newmod->instances = NULL;
	newmod->outputs = NULL;

	xp = strrchr(newname, '.');
	if (xp != NULL) *xp = '\0';
	newmod->name = (char *)malloc(strlen(xp) + 7);
	sprintf(newmod->name, "%s.bdnet", newname);
	free(newname);

	fsource = fopen(newmod->name, "r");
	if (fsource == NULL) {
	    fprintf(stderr, "Error:  No such file or cannot open file \"%s\"\n",
				newmod->name);
	    exit(1);
	}

	toklist = " \t\n\":;";
	state = HEADER_STUFF;

	fgets(linebuf, 2047, fsource);
	fptr = linebuf;

	while (1) {
	    token = strtok(fptr, toklist);

	    while (token != NULL) {

		switch (state) {
		    case HEADER_STUFF:
			if (!strcmp(token, "OUTPUT"))
			    state = INPUT_OUTPUT;
			else if (!strcmp(token, "INSTANCE"))
			    state = INSTANCE;
			break;

		    case INPUT_OUTPUT:
			if (!strcmp(token, "INSTANCE"))
			    state = INSTANCE;
			else {
			    newpin = (pinptr)malloc(sizeof(pin));
			    newpin->next = newmod->outputs;
			    newmod->outputs = newpin;
			    newpin->name = strdup(token);
			    state = OUTPUT_NET;
			}
			break;

		    case OUTPUT_NET:
			state = INPUT_OUTPUT;

			// Special cases: "vcc", "gnd", and "unconn"
			// in the output list should be reversed;  these
			// global names should replace the signal name
			// where found.

			if (!strcmp(token, "vcc") || !strcmp(token, "gnd") ||
				!strcmp(token, "unconn")) {
			    newpin->net = newpin->name;
			    newpin->name = strdup(token);
			}
			else
			    newpin->net = strdup(token);

			// Add this pin to modsig as a signal we
			// should not prefix when referencing.
			// Note:  We could try to set the vector limits
			// here, but we don't use that information.

			if (get_net_name(modsigs, "", newpin->name) != newpin->name)
			{
			    newsig = (sigptr)malloc(sizeof(signal));
			    newsig->next = modsigs;
			    modsigs = newsig;
			    newsig->name = strdup(newpin->name);
			    newsig->type = TYPE_INTERNAL;
			    bptr = strchr(newsig->name, '<');
			    if (bptr != NULL) {
				*bptr = '\0';
				sscanf(bptr + 1, "%d", &vlimit);    
				newsig->start = newsig->end = vlimit;
			    }
			    else {
				newsig->start = newsig->end = -1;
			    }
			}

			break;

		    case INSTANCE:
			state = FORM;
			newinst = NULL;

			// Special case:  Ignore instance LOGIC0 and LOGIC1.
			// These are built-in names that are required to get
			// correct synthesis when static values occur in an
			// I/O list.  If there are real TIEHI and TIELO cells
			// in the standard cell set, those names will be
			// substituted.  Here, we will find the signal that
			// is set high or low and connect it to vcc or gnd,
			// accordingly.

			if (!strcmp(token, "LOGIC0") || !strcmp(token, "LOGIC1"))
			    break;

			newinst = (instptr)malloc(sizeof(instance));
			newinst->name = strdup(token);
			newinst->pins = NULL;
			newinst->next = newmod->instances;
			newmod->instances = newinst;
			break;

		    case FORM:
			// This should always be "physical", don't really care
			state = PIN;
			break;

		    case PIN:
			if (!strcmp(token, "INSTANCE"))
			    state = INSTANCE;
			else if (!strcmp(token, "ENDMODEL"))
			    state = DONE;
			else {
			    state = NET;
			    if (newinst == NULL)
				newpin = NULL;
			    else {
				newpin = (pinptr)malloc(sizeof(pin));
				newpin->next = newinst->pins;
				newinst->pins = newpin;
				newpin->name = strdup(token);
			    }
			}
			break;

		    case NET:
			state = PIN;
			if (newpin) newpin->net = strdup(token);
			break;

		    default:
			break;
		}

		// Proceed to next token
		token = strtok(NULL, toklist);
	    }
	    if (fgets(linebuf, 2047, fsource) == NULL)
		break;
	}
	fclose(fsource);
	fsource = NULL;
    }    

    /* For each output pin, find it in the modules, and print	*/
    /* the pin name, twice.  The net name will be replaced by	*/
    /* the pin name on all occurrences.				*/

    for (testsig = modsigs; testsig; testsig = testsig->next) {
	if (testsig->type == TYPE_OUTPUT) {
	    if (testsig->start == -1)
		fprintf(stdout, "\n\t\"%s\" : \"%s\"",
				testsig->name, testsig->name);

	    else if (testsig->start < testsig->end) {
		for (i = testsig->start; i <= testsig->end; i++)
		    fprintf(stdout, "\n\t\"%s<%d>\" : \"%s<%d>\"",
					testsig->name, i, testsig->name, i);
	    }
	    else {
		for (i = testsig->start; i >= testsig->end; i--)
		    fprintf(stdout, "\n\t\"%s<%d>\" : \"%s<%d>\"",
					testsig->name, i, testsig->name, i);
	    }
	}
    }

    fprintf(stdout, ";\n\n");	// Finish the OUTPUT section

    /* Now write all instances of all modules, making replacements for	*/
    /* net names that appear in the output list of the module.		*/

    for (testmod = modules; testmod; testmod = testmod->next) {
	for (testinst = testmod->instances; testinst; testinst = testinst->next) {
	    fprintf(stdout, "INSTANCE \"%s\":\"physical\"\n", testinst->name);
	    for (testpin = testinst->pins; testpin; testpin = testpin->next) {
		for (testmod2 = modules; testmod2; testmod2 = testmod2->next) {
		    for (testout = testmod2->outputs; testout; testout = testout->next) {
			if (!strcmp(testout->net, testpin->net)) {
			    fprintf(stdout, "\t\"%s\" : \"%s\";\n", testpin->name,
					testout->name);
			    break;
			}
		    }
		    if (testout != NULL) break;
		}
		if (testout == NULL) {
		    testnet = get_net_name(modsigs, testmod->prefix, testpin->net);
		    fprintf(stdout, "\t\"%s\" : \"%s\";\n", testpin->name, testnet);
		}
	    }
	    fprintf(stdout, "\n");
	}
    }
    fprintf(stdout, "ENDMODEL;\n");

    return 0;
}
