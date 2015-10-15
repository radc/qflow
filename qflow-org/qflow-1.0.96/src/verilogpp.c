/*------------------------------------------------------*/
/* verilogpp.c						*/
/*							*/
/* Tokenize a verilog source file and parse it for	*/
/* structures that the Odin-II/abc flow cannot handle.	*/
/* Generally, this means handling things that are	*/
/* relevant to an ASIC flow but not to an FPGA flow.	*/
/*							*/
/* 1) Reset signals are pulled out of the code, and	*/
/*    the reset conditions are saved to a file named	*/
/*    "<rootname>.init".  These are picked up later	*/
/*    and used to replace standard flops with set,	*/
/*    reset, or both flops.				*/
/*							*/
/* 2) Parameter and `define substitutions are done by	*/
/*    vpreproc.						*/
/*							*/
/* 3) Inverted clocks are made non-inverting, with a	*/
/*    file called "<rootname>.clk" made to track which	*/
/*    clocks are inverted.  Inverters will be put in	*/
/*    front of the clock networks in the final netlist	*/
/*    (This has not been done yet, need to check the	*/
/*    capability of Odin-II and abc to handle negedge	*/
/*    clocks).						*/
/*							*/
/* 4) (Temporary) Record all clocks signal that are 	*/
/*    not in the input list of the module in a file	*/
/*    called "<rootname>.clk".  This is needed to work	*/
/*    around a bug in Odin-II.				*/
/*							*/
/* September 4, 2013					*/
/* verilogpp.c copied from vpreproc.c, changed to use	*/
/* the same tokenizer developed for liberty2tech.	*/
/*							*/
/* October 8, 2013					*/
/* Changed .init file format to be compatible with the	*/
/* notation used by Odin-II for .blif file output.	*/
/*							*/
/* October 9, 2013					*/
/* Extended the .dep format to retain all connectivity	*/
/* information for all subcircuits, so that the reset	*/
/* values can be substituted correctly.  This relies on	*/
/* Odin-II's method of always naming flop outputs at	*/
/* the lowest level, with a hierarchical name that	*/
/* contains all of the subcircuit module and instance	*/
/* names.						*/
/*							*/
/* October 16, 2013					*/
/* Added a switch for changing between syntax used by	*/
/* Odin-II, which replaces brackets with a tilde, and	*/
/* that used by Yosys, which preserves the verilog	*/
/* bracket notation.					*/
/*------------------------------------------------------*/

#define DEBUG 1

#include <stdio.h>
#include <string.h>
#include <ctype.h>		// For isalnum()
#include <malloc.h>
#include <stdlib.h>		// For exit(n)
#include <unistd.h>		// For getopt()

#define VPP_LINE_MAX 16384

// Signal (single-bit) data structure

typedef struct _sigact *sigactptr;

typedef struct _sigact {
   char *name;			// Name of clock, reset, or enable signal
   char edgetype;		// POSEDGE or NEGEDGE
   sigactptr next;
} sigact;

typedef struct _signal *sigptr;

typedef struct _signal {
   char *name;
   char value;			// reset value: 0, 1; or -1 if signal is not registered
   sigptr link;			// signal representing reset value if not constant, NULL
				// if signal is constant, and reset value is "value".
   sigactptr reset;		// reset signal that resets this bit
} signal;

// Vector data structure
typedef struct _vector *vecptr;

typedef struct _vector {
   char *name;
   int vector_size;		// zero means signal is not a vector
   int vector_start;
   int vector_end;
   vecptr next;
} vector;

// Module data structure
typedef struct _module *modptr;

typedef struct _module {
   modptr next;
   char *name;
   vector *iolist;		// Vectors/signals that are part of the I/O list
   vector *reglist;		// Vectors/signals that are registered
   vector *wirelist;		// Non-registered vectors/signals
   sigact *clocklist;		// List of clock signals
   sigact *resetlist;		// List of reset signals
} module;

// Parameter data structure

typedef struct _parameter *parameterp;

typedef struct _parameter {
   parameterp next;
   char *name;
   char *value;
} parameter;

// Nesting block handling

typedef struct _bstack *bstackp;

typedef struct _bstack {
   bstackp next;
   int state;				// What kind of block this is
   int suspend;				// 1 if output of this block is suspended
} bstack;

// Nesting include file handling

typedef struct _fstack *fstackp;

typedef struct _fstack {
   fstackp next;
   char *filename;
   FILE *file;
   int currentLine;
} fstack;

// Nesting ifdef handling

typedef struct _istack *istackp;

typedef struct _istack {
   istackp next;
   int state;
} istack;

// Block handling types
#define	 NOTHING	 0		// Not inside anything
#define	 MODULE		 1		// Inside a module
#define	 IOLIST		 2		// Inside a module's I/O list
#define	 INPUTOUTPUT	 3		// An input or output definition
#define	 MBODY		 4		// Main body of a module
#define	 WIRE		 5		// A wire definition
#define	 REGISTER	 6		// A register defintion
#define  ALWAYS		 7		// Inside an always block
#define	 SENSELIST	 8		// Inside a sensitivity list
#define  ABODY_PEND	 9		// Main body of an always block (pending)
#define  ABODY		10		// Main body of an always block
#define	 BEGIN_END	11		// Inside a begin ... end block
#define  CASE		12		// Inside a case ... endcase block
#define  IF_ELSE	13		// Inside an if ... else block
#define  ELSE		14		// else, may or may not be followed by "if"
#define  CONDITION	15		// If condition
#define  ASSIGNMENT	16		// Inside an assignment (end with ";")
#define  BLOCKING	17		// Inside a blocking assignment (end with ";")
#define  SUBCIRCUIT	18		// Inside a subcircuit call
#define  SUBPIN		19		// Inside a subcircuit pin connection
#define	 FUNCTION	20		// Inside a function definition

// Edge types
#define  NEGEDGE	1
#define  POSEDGE	2

// Condition types
#define  UNKNOWN	-1
#define  EQUAL		1
#define  NOT_EQUAL	2
#define  NOT		3

// Output syntax styles
#define  ODIN		0
#define  YOSYS		1

// For getopt. . .

extern int	optind;
extern char	*optarg;

/*----------------------------------------------------------------------*/
/* Tokenizer routine							*/
/*									*/
/* Grab a token from the input file "flib".				*/
/* Return the token, or NULL if we have reached end-of-file.		*/
/*									*/
/* If "delimiter" is specified, then parse input up to the specified	*/
/* delimiter.  Do not return the delimiter character as part of the	*/
/* token, if "delimiter" is specified.  If the delimiter is a brace	*/
/* or parenthesis, then nesting will be tracked, and all input parsed	*/
/* up to the matching parenthesis or brace.				*/
/*									*/
/* If "delimiter" is 0, then the next token is returned, using standard	*/
/* separators for verilog files.  This includes characters like braces,	*/
/* brackets, parentheses, colons, commas, etc.  The separator character	*/
/* is returned as a token if it is not whitespace (whitespace is	*/
/* ignored).								*/
/*									*/
/* "fout" is only supplied so that we can write newlines when found.	*/
/* This keeps the line numbering of the input and output the same.	*/
/*									*/
/* Generally, newlines are ignored, except when "//" is used as a	*/
/* comment-to-EOL.  Comment blocks using slash-star are ignored by	*/
/* the tokenizer.							*/
/*----------------------------------------------------------------------*/

char *
advancetoken(fstack **filestack, FILE *fout, char *delimiter)
{
    static char token[VPP_LINE_MAX];
    static char line[VPP_LINE_MAX];
    static char *linepos = NULL;

    fstack *savestack;

    char *lineptr = linepos;
    char *lptr, *tptr;
    char *result;
    int commentblock, concat, nest;

    commentblock = 0;
    concat = 0;
    nest = 0;

    // This is a bit of a hack;  it's the only sure way to catch a
    // line when read-to-eol is requested and the input is already
    // at the end of the line.

    if (lineptr != NULL && *lineptr == '\n' && (delimiter != NULL) &&
		strchr(delimiter, '\n')) {
	*token = '\n';
	*(token + 1) = '\0';
	return token;
    }

    while (1) {		/* Keep processing until we get a token or hit EOF */

	if (lineptr != NULL && *lineptr == '/' && *(lineptr + 1) == '*') {
	    commentblock = 1;
	}

	if (commentblock == 1) {
	    if ((lptr = strstr(lineptr, "*/")) != NULL) {
		lineptr = lptr + 2;
		commentblock = 0;
	    }
	    else lineptr = NULL;
	}
	if (lineptr == NULL || *lineptr == '\0' || *lineptr == '\n') {
	    if (fout && ((lineptr != NULL) || (commentblock == 1)))
		fputs("\n", fout);
	    result = fgets(line, VPP_LINE_MAX + 1, (*filestack)->file);
	    // fprintf(stderr, "File %s Line %d\n", (*filestack)->filename,
	    //		(*filestack)->currentLine);
	    while (result == NULL) {
		if ((*filestack)->next != NULL) {
		    savestack = *filestack;
		    *filestack = (*filestack)->next;
		    fclose(savestack->file);
		    free(savestack->filename);
		    free(savestack);
		    result = fgets(line, VPP_LINE_MAX + 1, (*filestack)->file);
		}
		else
		    return NULL;
	    }
	    (*filestack)->currentLine++;
	    lineptr = line;
	}

	if (commentblock == 1) continue;

	if (lineptr == line) {
	    while ((*lineptr == ' ') || (*lineptr == '\t')) lineptr++;
	    if (fout && (lineptr > line) && (*lineptr != '\n'))
		fwrite(line, 1, lineptr - line, fout);
	}
	else {
	    while (isspace(*lineptr)) lineptr++;
	}

	if (concat == 0) tptr = token;

	// Find the next token and return just the token.  Update linepos
	// to the position just beyond the token.  All delimiters like
	// parentheses, quotes, etc., are returned as single tokens

	// If delimiter is declared, then we stop when we reach the
	// delimiter character, and return all the text preceding it
	// as the token.  If delimiter is 0, then we look for standard
	// delimiters, and separate them out and return them as tokens
	// if found.

	while (1) {
	    if (*lineptr == '\n' || *lineptr == '\0')
		break;
	    if (*lineptr == '/' && *(lineptr + 1) == '*')
		break;
	    if ((delimiter != NULL) && strchr(delimiter, *lineptr)) {
		if (nest > 0)
		    nest--;
		else
		    break;
	    }

	    // Watch for nested delimiters!
	    if ((delimiter != NULL) && strchr(delimiter, '}') && *lineptr == '{') nest++;
	    if ((delimiter != NULL) && strchr(delimiter, ')') && *lineptr == '(') nest++;
	    if ((delimiter != NULL) && strchr(delimiter, ']') && *lineptr == '[') nest++;

	    if (delimiter == NULL)
		if (*lineptr == ' ' || *lineptr == '\t')
		    break;

	    // Note: '#' must be followed directly by a value, so it
	    // is treated like part of a numerical value

	    if (delimiter == NULL) {
		if (*lineptr == '(' || *lineptr == ')') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '{' || *lineptr == '}') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '[' || *lineptr == ']') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '\"' || *lineptr == ';' || *lineptr == ',') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '~' || *lineptr == '^') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '?' || *lineptr == ':' || *lineptr == '@') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}
		if (*lineptr == '+' || *lineptr == '-' || *lineptr == '*') {
		    if (tptr == token) *tptr++ = *lineptr++;
		    break;
		}

		// Process non-blocking assignment "<=" and inequalities like
		// "==" and ">=" as a single token.  Otherwise, characters
		// ">", "<", and "=" are single tokens by themselves.

		if (*lineptr == '<' || *lineptr == '>' || *lineptr == '=') {
		    if (*(lineptr + 1) == '=') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else if (*(lineptr + 1) == '<' && *(lineptr) == '<') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else if (*(lineptr + 1) == '>' && *(lineptr) == '>') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else {
			if (tptr == token) *tptr++ = *lineptr++;
			break;
		    }
		}

		// Likewise, "!=" is a single token;  otherwise "!" is

		if (*lineptr == '!') {
		    if (*(lineptr + 1) == '=') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else {
			if (tptr == token) *tptr++ = *lineptr++;
			break;
		    }
		}

		// Likewise, "&&", "||", and "//" are a single token
		// but so are '&', "|", and "/"

		if (*lineptr == '&') {
		    if (*(lineptr + 1) == '&') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else {
			if (tptr == token) *tptr++ = *lineptr++;
			break;
		    }
		}
		if (*lineptr == '|') {
		    if (*(lineptr + 1) == '|') {
		        if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else {
			if (tptr == token) *tptr++ = *lineptr++;
			break;
		    }
		}
		if (*lineptr == '/') {
		    if (*(lineptr + 1) == '/') {
			if (tptr == token) {
			    *tptr++ = *lineptr++;
			    *tptr++ = *lineptr++;
			}
			break;
		    }
		    else {
			if (tptr == token) *tptr++ = *lineptr++;
			break;
		    }
		}
	    }
	    *tptr++ = *lineptr++;
	}
	*tptr = '\0';
	if ((delimiter != NULL) && !strchr(delimiter, *lineptr))
	    concat = 1;
	else if ((delimiter != 0) && strchr(delimiter, *lineptr))
	    break;
	else if (tptr > token)
	    break;
    }
    if (delimiter != NULL) lineptr++;

    while (*lineptr == ' ' || *lineptr == '\t') lineptr++;

    linepos = lineptr;

    // Final:  Remove trailing whitespace
    tptr = token + strlen(token) - 1;
    while (isspace(*tptr)) {
	*tptr = '\0';
	tptr--;
    }
    return token;
}

/*----------------------------------------------------------------------*/
/* Read a verilog bit value, either a 1 or 0, optionally preceeded by	*/
/* "1'b".  Return the bit value, if found, or -1, if no bit value could	*/
/* be parsed.								*/
/*----------------------------------------------------------------------*/

int
get_bitval(char *token)
{
   int bval;

   if (!strncmp(token, "1'b", 3))
      token += 3;
   if (sscanf(token, "%d", &bval) == 1) {
      if (bval == 0 || bval == 1)
	 return bval;
      else
	 return -1;
   }
   return -1;
}

/*----------------------------------------------------------------------*/
/* Read a bit value out of a vector.  Note that the vector may be	*/
/* constructed from parts using {...} notation, and the bit may be	*/
/* numeric, or it may be a signal name.					*/
/*----------------------------------------------------------------------*/

char *
parse_bit(fstack *filestack, module *topmod, char *vstr, int idx, int style)
{
   int vsize, vval, realsize;
   static char bitval[2] = "0";
   char *bptr, typechar, *vptr, *fullvec = NULL, *vloc;
   char *cptr = NULL, *newcptr;
   vector *testvec;
   int locidx;
   int currentLine = filestack->currentLine;
   static char *fullname = NULL;
   
   locidx = (idx < 0) ? 0 : idx;

   vloc = vstr;

   if (vloc[0] == '{') {
      cptr = strrchr(vloc, ',');
      if (cptr != NULL) vloc = cptr + 1;
   }	

   while (1) {
      if (sscanf(vloc, "%d'%c", &vsize, &typechar) == 2) {
	 bptr = strchr(vloc, typechar);
	 vptr = bptr + 1;
	 while (isalnum(*vptr)) vptr++;

	 // Handle the case where the bit vector needs zero padding, e.g.,
	 // "9'b0".  For decimal, octal, hex we pad more than necessary,
	 // but who cares?

	 realsize = vptr - bptr - 1;
	 if (realsize < vsize) {
	    fullvec = (char *)malloc((vsize + 1) * sizeof(char));
	    for (vptr = fullvec; vptr < fullvec + vsize; vptr++) *vptr = '0';
	    *vptr = '\0';
	    vptr = bptr + 1;
	    while (*vptr != '\0' && realsize >= 0) {
	       *(fullvec + vsize - realsize) = *vptr;
	       vptr++;
	       realsize--;
	    }
	    vptr = fullvec + vsize - 1;		// Put vptr on lsb
	 }
	 else
	    vptr--;	// Put vptr on lsb

	 if (vsize > locidx) {
	    switch (typechar) {
	       case 'b':
		  *bitval = *(vptr - locidx);
	 	  bptr = bitval;
		  if (fullvec) free(fullvec);
	 	  return bptr;
	       case 'd':		// Interpret decimal
		  vloc = bptr + 1;	// Move to start of decimal value and continue
		  break;	  
	       case 'h':		// To-do:  Interpret hex
		  *bitval = *(vptr - (locidx / 4));
		  sscanf(bitval, "%x", &vval);
		  vval >>= (locidx % 4);
		  vval &= 0x1;
	 	  bptr = bitval;
		  *bptr = (vval == 0) ? '0' : '1';
		  if (fullvec) free(fullvec);
	 	  return bptr;
	       case 'o':		// To-do:  Interpret octal
		  *bitval = *(vptr - (locidx / 3));
		  sscanf(bitval, "%o", &vval);
		  vval >>= (locidx % 3);
		  vval &= 0x1;
	 	  bptr = bitval;
		  *bptr = (vval == 0) ? '0' : '1';
		  if (fullvec) free(fullvec);
	 	  return bptr;
	    }
	 }
	 else {
	    if (cptr != NULL) {
	       // Bundle; move forward and try again. . .
	       *cptr = '\0';
  	       newcptr = strrchr(vloc, ',');
	       if (newcptr != NULL)
		  vloc = newcptr + 1;
	       else
		  vloc = vstr + 1;
	       *cptr = ',';
	       cptr = newcptr;
	       locidx -= vsize;
	       continue;
	    }
	    fprintf(stderr, "File %s, Line %d:  Not enough bits for vector.\n",
			filestack->filename, filestack->currentLine);
	    return NULL;
	 }
      }
      if (sscanf(vloc, "%d", &vval) == 1) {
	 vval >>= locidx;
	 vval &= 0x1;
	 bptr = bitval;
	 *bptr = (vval == 0) ? '0' : '1';
	 return bptr;
      }	
      else {
	 // Could be a signal name.  If so, check that it has a size
	 // compatible with locidx.

	 char *is_indexed = strchr(vloc, '[');
	 if (is_indexed) *is_indexed = '\0';

	 for (testvec = topmod->wirelist; testvec != NULL; testvec = testvec->next)
	    if (!strcmp(testvec->name, vloc))
	       break;
	 if (testvec == NULL)
	    for (testvec = topmod->iolist; testvec != NULL; testvec = testvec->next)
	       if (!strcmp(testvec->name, vloc))
	          break;
	 if (testvec == NULL)
	    for (testvec = topmod->reglist; testvec != NULL; testvec = testvec->next)
	       if (!strcmp(testvec->name, vloc))
	          break;
	 
	 if (is_indexed) *is_indexed = '[';	/* Restore index delimiter */
	 if (testvec == NULL) {
	    fprintf(stderr, "File %s, Line %d: Cannot parse signal name \"%s\" for reset\n",
			filestack->filename, filestack->currentLine, vloc);
	    return NULL;
	 }
	 else {
	    /* To-do:  Need to make sure all vector indices are aligned. . . */
	    if (locidx == 0 && testvec->vector_size == 0) {
	       return testvec->name;
	    }
	    else if (locidx >= testvec->vector_size) {
	       if (cptr != NULL) {
		  // Bundle; move backward and try next part

		  *cptr = '\0';
		  newcptr = strrchr(vstr, ',');
		  if (newcptr != NULL)
		     vloc = newcptr + 1;
		  else
		     vloc = vstr + 1;
		  *cptr = ',';
		  cptr = newcptr;
		  locidx -= testvec->vector_size;
		  continue;
	       }
	       fprintf(stderr, "File %s, Line %d:  Vector LHS exceeds dimensions of RHS.\n",
			filestack->filename, filestack->currentLine);
	       return NULL;
	    }
	    else {
	       int j, jstart, jend;
	       char *is_range = NULL;

	       if (is_indexed) {
		  sscanf(is_indexed + 1, "%d", &jstart);
		  if ((is_range = strchr(is_indexed + 1, ':')) != NULL) {
		     sscanf(is_range + 1, "%d", &jend);
	             if (jstart > jend)
			j = jend + locidx;
		     else
			j = jstart + locidx;

		     if (testvec->vector_start > testvec->vector_end) {
			if (j < testvec->vector_end) {
			   fprintf(stderr, "File %s, Line %d:  Vector RHS is outside of"
					" range %d to %d.\n",
					filestack->filename, filestack->currentLine,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_end;
			}
			else if (j > testvec->vector_start) {
			   if (cptr != NULL) {
		  	      *cptr = '\0';
		  	      newcptr = strrchr(vstr, ',');
		  	      if (newcptr != NULL)
		  	         vloc = newcptr + 1;
		  	      else
		   	         vloc = vstr + 1;
		  	      *cptr = ',';
		  	      cptr = newcptr;
		  	      locidx -= (jstart - jend + 1);
		  	      continue;
			   }
			   fprintf(stderr, "File %s, Line %d:  Vector RHS is outside of "
					" range %d to %d.\n",
					filestack->filename, filestack->currentLine,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_start;
			}
		     }
		     else {
			if (j > testvec->vector_end) {
			   fprintf(stderr, "File %s, Line %d:  Vector RHS is outside of "
					" range %d to %d.\n",
					filestack->filename, filestack->currentLine,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_end;
			}
			else if (j < testvec->vector_start) {
			   fprintf(stderr, "File %s, Line %d:  Vector RHS is outside of "
					"range %d to %d.\n",
					filestack->filename, filestack->currentLine,
					testvec->vector_start, testvec->vector_end);
			   j = testvec->vector_start;
			}
		     }
		  }
		  else {
		     // RHS is a single bit
		     j = jstart;

		     if (locidx != 0) {
			fprintf(stderr, "File %s, Line %d:  Vector LHS is set by single"
				" bit on RHS.  Padding by repetition.\n",
				filestack->filename, filestack->currentLine);
		     }
		  }
	       }
	       else {
	          if (testvec->vector_start > testvec->vector_end)
		     j = testvec->vector_end + locidx;
	          else
		     j = testvec->vector_start + locidx;
	       }

	       if (fullname != NULL) free(fullname);
	       fullname = (char *)malloc(strlen(testvec->name) + 10);
	       
	       if (style == YOSYS)
		  sprintf(fullname, "%s<%d>", testvec->name, j);
	       else
		  sprintf(fullname, "%s~%d", testvec->name, j);

	       return fullname;
	    }
	 }
      }
      break;
   }
}

/*------------------------------------------------------*/
/* Copy a line of code with parameter substitutions 	*/
/*------------------------------------------------------*/

void
paramcpy(char *dest, char *source, parameter *params)
{
   char temp[1024];
   char *sptr, *dptr, *vptr;
   int plen;
   parameter *pptr;

   strcpy(dest, source);

   for (pptr = params; pptr; pptr = pptr->next) {
      strcpy(temp, dest);
      plen = strlen(pptr->name);
      sptr = temp;
      dptr = dest;
      while (*sptr != '\0') {
	 if (!strncmp(sptr, pptr->name, plen)) {
	    vptr = pptr->value;
	    while (*vptr != '\0') {
	       *dptr++ = *vptr++;
	    }
	    sptr += plen;
	 }
	 else
	    *dptr++ = *sptr++;
      }
      *dptr = '\0';
   }
}

/*------------------------------------------------------*/
/* write_signal --					*/
/*							*/
/*	Generate an output line for the .clk or .init	*/
/*	file for the indicated signal, which is either	*/
/*	a reset or a clock.  The output includes the	*/
/*	reset or clock signal name, and then "input	*/
/*	wire", "internal wire", or "internal register",	*/
/*	depending on how the signal is defined.		*/
/*------------------------------------------------------*/

void
write_signal(module *topmod, sigact *outsig, FILE *fout, char edgetype)
{
    vector *testvec;

    for (testvec = topmod->iolist; testvec; testvec = testvec->next)
	if (!strcmp(testvec->name, outsig->name))
	     break;

    if (testvec != NULL)
	fprintf(fout, "%s%s input wire\n",
			(edgetype == NEGEDGE) ? "~" : "",
			outsig->name);
    else {
	for (testvec = topmod->wirelist; testvec; testvec = testvec->next)
	    if (!strcmp(testvec->name, outsig->name))
	        break;

	if (testvec != NULL)
	    fprintf(fout, "%s%s internal wire\n",
			(edgetype == NEGEDGE) ? "~" : "",
			outsig->name);
	else
	    fprintf(fout, "%s%s internal register\n",
			(edgetype == NEGEDGE) ? "~" : "",
			outsig->name);
    }
    if (DEBUG)
	if (edgetype == 0)
	    printf("Adding clock signal \"%s\"\n", outsig->name);
}

/*------------------------------------------------------*/
/* Stack pushing/popping routines.			*/
/*------------------------------------------------------*/

void
pushstack(bstack **stack, int state, int suspend) {
    bstack *newstack;

    newstack = (bstack *)malloc(sizeof(bstack));
    newstack->state = state;
    newstack->suspend = suspend;
    newstack->next = *stack;
    *stack = newstack;
}

int
popstack(bstack **stack) {
    bstack *stacktop;

    if (*stack == NULL) return -1;
    stacktop = *stack;
    *stack = (*stack)->next;
    free(stacktop);
    return 0;
}

/*------------------------------------------------------*/
/* Main verilog preprocessing code			*/
/*------------------------------------------------------*/

int
main(int argc, char *argv[])
{
    FILE *finit = NULL;
    FILE *fclk = NULL;
    FILE *ftmp = NULL;
    FILE *fdep = NULL;

    char *newtok, token[VPP_LINE_MAX];
    char *xp, *bptr, *filename, *cptr;
    char locfname[2048];

    bstack *stack, *tstack;
    fstack *newfile, *filestack;
    istack *ifdefstack;

    module *topmod, *newmod;
    sigact *clocksig;
    sigact *testreset, *testsig;
    vector *initvec, *testvec, *newvec;
    parameter *params = NULL;
    parameter *newparam, *testparam;

    char *subname = NULL, *instname = NULL;

    int style = YOSYS;
    int tempsuspend;
    int start, end, ival, i, j, k;
    int parm, pcont, ifdeflevel;
    int condition;
    int resetdone;
    char edgetype;
 
    /* One required argument, which is the source filename		*/
    /* One optional argument, which is -s <style>, with	<style> being	*/
    /* either "odin" or "yosys".					*/

    while ((i = getopt(argc, argv, "s:")) != EOF) {
	switch (i) {
	    case 's':
		if (!strncmp(optarg, "odin", 4))
		    style = ODIN;
		else if (!strncmp(optarg, "yosys", 5))
		    style = YOSYS;
		else {
		    fprintf(stderr, "Usage: vpreproc [-s <style>] <source_file.v>\n");
		    fprintf(stderr, "Where <style> is one of \"odin\" or \"yosys\".\n");
		    exit(1);
		}
		break;
	    default:
		fprintf(stderr, "Usage: vpreproc [-s <style>] <source_file.v>\n");
		fprintf(stderr, "Where <style> is one of \"odin\" or \"yosys\".\n");
		break;
	}
    }

    if (optind < argc) {
	filename = strdup(argv[optind]);
    }
    else {
	fprintf(stderr, "Usage: vpreproc [-s <style>] <source_file.v>\n");
	fprintf(stderr, "Where <style> is one of \"odin\" or \"yosys\".\n");
	exit(1);
    }

    /* Remove any filename extension */

    xp = strrchr(filename, '.');

    /* "filename" will be the 1st argument without any file extension.	*/
    /*		It will be used to generate all output filenames.	*/
    /* "locfname" will be the full 1st argument if it has an extension	*/
    /* 		or else ".v" will be added if it doesn't.		*/

    if (xp != NULL) {
	strcpy(locfname, filename);
	*xp = '\0';
    }
    else
        snprintf(locfname, 2045, "%s.v", filename);

    filestack = (fstack *)malloc(sizeof(fstack));
    filestack->currentLine = 0;
    filestack->filename = strdup(locfname);
    filestack->next = NULL;

    filestack->file = fopen(locfname, "r");
    if (filestack->file == NULL) {
	fprintf(stderr, "Error:  No such file or cannot open file \"%s\"\n", locfname);
	exit(1);
    }

    /* Create the files we want to write to for this file 		*/
    /* There will only be one of each file for this verilog source,	*/
    /* not one of each file per module.					*/

    sprintf(locfname, "%s.init", filename);
    finit = fopen(locfname, "w");
    if (finit == NULL) {
       fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    sprintf(locfname, "%s.clk", filename);
    fclk = fopen(locfname, "w");
    if (fclk == NULL) {
       fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    sprintf(locfname, "%s_tmp.v", filename);
    ftmp = fopen(locfname, "w");
    if (ftmp == NULL) {
	fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    sprintf(locfname, "%s.dep", filename);
    fdep = fopen(locfname, "w");
    if (fdep == NULL) {
	fprintf(stderr, "Error:  Cannot open \"%s\" for writing.\n", locfname);
    }

    topmod = NULL;
    condition = UNKNOWN;
    ifdefstack = NULL;

    /* Start the stack with a NOTHING entry */
    stack = NULL;
    pushstack(&stack, NOTHING, 0);

    /* Read continuously and break loop when input is exhausted */

    while ((newtok = advancetoken(&filestack, ftmp, NULL)) != NULL) {

	/* State-independent processing */
	/* First set is values for which we should NOT substitute parameters */
	/* and which must be recognized in any `ifdef block	*/

	if (!strcmp(newtok, "//")) {
	    fputs(" ", ftmp);
	    fputs(newtok, ftmp);
	    fputs(" ", ftmp);
	    newtok = advancetoken(&filestack, ftmp, "\n"); // Read to EOL
	    fputs(newtok, ftmp);	// Write out this line
	    continue;
	}

	if (!strcmp(newtok, "`endif")) {
	    istack *istacktest;
	    if (ifdefstack == NULL) {
		fprintf(stderr, "Error: File %s Line %d, `else with no `ifdef\n",
				filestack->filename, filestack->currentLine);
	    }
	    else {
		istacktest = ifdefstack;
		ifdefstack = ifdefstack->next;
		free(istacktest);
		continue;
	    }
	}
	else if (!strcmp(newtok, "`else")) {
	    if (ifdefstack == NULL) {
		fprintf(stderr, "Error: File %s Line %d, `else with no `ifdef\n",
				filestack->filename, filestack->currentLine);
	    }
	    else {
		// A state of -1 does not change.  Only change 1->0 and 0->1
		if (ifdefstack->state == 0)
		    ifdefstack->state = 1;
		else if (ifdefstack->state == 1)
		    ifdefstack->state = 0;
		continue;
	    }
	}

	if (!strcmp(newtok, "`ifdef")) {
	    istack *newifstack;
	    newifstack = (istack *)malloc(sizeof(istack));
	    newifstack->next = ifdefstack;
	    ifdefstack = newifstack;

	    /* Get parameter name */
	    newtok = advancetoken(&filestack, ftmp, NULL);

	    /* If we are already in an if block state 0, then	*/
	    /* set state to -1.  This prevents any changing of	*/
	    /* the value for an `else corresponding to this	*/
	    /* `ifdef block or anything nested inside it.	*/

	    if (newifstack->next && newifstack->next->state <= 0) {
		newifstack->state = -1;
	    }
	    else {
		for (newparam = params; newparam; newparam = newparam->next) {
		    // Note that defined value has "`" in front everywhere
		    // except in "`define" and "`ifdef"
		    if (*newparam->name == '`' &&
				!strcmp(newparam->name + 1, newtok)) {
			newifstack->state = 1;
			break;
		    }
		}
		if (newparam == NULL) newifstack->state = 0;
	    }
	    continue;
	}

	/* Having processed all `ifdef ... `else ... `endif statements,	*/ 
	/* we check the status and ignore all code that is blocked by	*/
	/* a not-defined condition.					*/

	if (ifdefstack && ifdefstack->state <= 0) continue;

	/* Second set is values for which we should NOT substitute parameters	*/
	/* and which must not be recognized under a rejected ifdef condition	*/

	if (!strcmp(newtok, "`undef")) {
	    /* Get parameter name */
	    newtok = advancetoken(&filestack, ftmp, NULL);
	    for (newparam = params; newparam; newparam = newparam->next) {
		if (!strcmp(newparam->name, newtok)) {
		    testparam = newparam;
		    newparam = newparam->next;
		    free(testparam->name);
		    free(testparam);
		} 
	    }
	    continue;
	}

	parm = 1;
	if (!strcmp(newtok, "parameter") || !(parm = strcmp(newtok, "`define"))) {

	    while (1) {

		pcont = 0;
		/* Get parameter name */
		newtok = advancetoken(&filestack, ftmp, NULL);
		if (!strcmp(newtok, "[")) {
		    /* Ignore array bounds on parameter definitions */
	            newtok = advancetoken(&filestack, ftmp, "]");
	            newtok = advancetoken(&filestack, ftmp, NULL);
		}

		newparam = (parameter *)malloc(sizeof(parameter));
		if (!parm) {
		    newparam->name = (char *)malloc(2 + strlen(newtok));
		    sprintf(newparam->name, "`%s", newtok);
		}
		else {
		    newparam->name = strdup(newtok);
		}
		newparam->next = NULL;

		/* Get parameter value---only accept semicolon or a	*/
		/* newline as a delimiter.  This will pick up the '='	*/
		/* so we need to skip over it, and other whitespace.	*/

		if (parm) {
		    newtok = advancetoken(&filestack, ftmp, NULL);
		    if (strcmp(newtok, "="))
			fprintf(stderr, "Error File %s Line %d: \"parameter\" without \"=\"\n",
				filestack->filename, filestack->currentLine);
		    newtok = advancetoken(&filestack, ftmp, ";\n");
		    if (strchr(newtok, '\n') != NULL) {
			fprintf(stderr, "Error File %s Line %d: \"parameter\" without "
				"ending \";\"\n",
				filestack->filename, filestack->currentLine);
		    }
		    else if (*(newtok + strlen(newtok) - 1) == ',') {
		       *(newtok + strlen(newtok) - 1) = '\0';
		       pcont = 1;
		    }
		}
		else {
		    newtok = advancetoken(&filestack, ftmp, "\n");
		}

		/* Run "paramcpy" to make any parameter substitutions	*/
		/* in the parameter itself.				*/

		paramcpy(token, newtok, params);
		newparam->value = strdup(token);

		/* Sort tokens by size to avoid substituting	*/
		/* part of a parameter (e.g., "START" and	*/
		/* "START1" should not be ambiguous!)		*/

		if (params == NULL)
		    params = newparam;
		else {
		    parameter *pptr, *lptr;
		    lptr = NULL;
		    for (pptr = params; pptr; pptr = pptr->next) {
			if (strlen(pptr->name) < strlen(newparam->name)) {
			    if (lptr == NULL) {
				newparam->next = params;
				params = newparam;
			    }
			    else {
				newparam->next = lptr->next;
				lptr->next = newparam;
			    }
			    break;
			}
			lptr = pptr;
		    }
		    if (pptr == NULL) lptr->next = newparam;
		}
		if ((parm == 0) || (pcont == 0)) break;
	    }
	    continue;
	}

	paramcpy(token, newtok, params);		// Substitute parameters

	if (!strcmp(token, "`include")) {
	    /* Get parameter name */
	    newtok = advancetoken(&filestack, ftmp, NULL);	/* Get 1st quote */
	    if (!strcmp(newtok, "\""))
		newtok = advancetoken(&filestack, ftmp, "\"");

	    // Create a new file record and push it
	    newfile = (fstack *)malloc(sizeof(fstack));
	    newfile->file = fopen(newtok, "r");
	    if (newfile->file == NULL) {
		free(newfile);
		fprintf(stderr, "Cannot read include file \"%s\"\n", newtok);
	    }
	    else {
		newfile->next = filestack;
		filestack = newfile;
		newfile->filename = strdup(newtok);
		newfile->currentLine = 0;
	    }
	    continue;
	}

	/* State-dependent processing */

	if (stack == NULL) {
	    fprintf(stderr, "Internal error:  NULL stack; should not happen!\n");
	    break;
	}

	switch (stack->state) {
	    case NOTHING:
		if (!strcmp(token, "module")) {
		    pushstack(&stack, MODULE, stack->suspend);
		    newmod = (module *)malloc(sizeof(module));
		    newmod->name = NULL;
		    newmod->next = topmod;
		    topmod = newmod;
		}
		else if (!strcmp(token, "function")) {
		    pushstack(&stack, FUNCTION, stack->suspend);
		}
		else if (!strcmp(token, "`timescale")) {
		    fputs("// ", ftmp);		// Comment this out!
		}
		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case MODULE:
		/* Get name of module and fill module structure*/
		if (topmod->name == NULL) {
		    if (!strcmp(token, "(")) {
			fprintf(stderr, "Error File %s Line %d:  No module name!\n",
				filestack->filename, filestack->currentLine);
			break;
		    }
		    if (DEBUG) printf("Found module \"%s\" in source\n", token);

		    topmod->name = strdup(token);
		    topmod->iolist = NULL;
		    topmod->reglist = NULL;
		    topmod->wirelist = NULL;
		    topmod->clocklist = NULL;
		    topmod->resetlist = NULL;
		}
		else if (!strcmp(token, "(")) {
		    pushstack(&stack, IOLIST, stack->suspend);
		}
		else if (!strcmp(token, ";")) {
		    pushstack(&stack, MBODY, stack->suspend);
		}
		else {
		    fprintf(stderr, "Error File %s Line %d: Expecting input/output list\n",
				filestack->filename, filestack->currentLine);
		}
		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case IOLIST:

		if (!strcmp(token, "input")) {
		    pushstack(&stack, INPUTOUTPUT, stack->suspend);
		    fputs("input ", ftmp);
		}
		else if (!strcmp(token, "output")) {
		    pushstack(&stack, INPUTOUTPUT, stack->suspend);
		    fputs("output ", ftmp);
		}
		else if (!strcmp(token, ")")) {
		    popstack(&stack);
		    fputs(")", ftmp);
		}
		else if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case MBODY:
		start = end = 0;
		if (!strcmp(token, "input")) {
		    start = end = -1;
		    pushstack(&stack, INPUTOUTPUT, stack->suspend);
		}
		else if (!strcmp(token, "output")) {
		    start = end = -1;
		    pushstack(&stack, INPUTOUTPUT, stack->suspend);
		}
		else if (!strcmp(token, "wire")) {
		    // Add to list of known wires
		    start = end = -1;
		    pushstack(&stack, WIRE, stack->suspend);
		}
		else if (!strcmp(token, "reg")) {
		    // Add to list of known registers
		    start = end = -1;
		    pushstack(&stack, REGISTER, stack->suspend);
		}
		else if (!strcmp(token, "assign")) {
		    // Track assignments in case they belong to clocks
		    // or resets.
		    pushstack(&stack, ASSIGNMENT, stack->suspend);
		}
		else if (!strcmp(token, "always")) {
		    pushstack(&stack, ALWAYS, 2);
		    edgetype = 0;
		    clocksig = NULL;
		    condition = UNKNOWN;
		    initvec = NULL;
		    // Clear any subcircuit references
		    if (subname != NULL) {
			free(subname);
			subname = NULL;
		    }
		    if (instname != NULL) {
			free(instname);
			instname = NULL;
		    }
		}
		else if (!strcmp(token, "function")) {
		    pushstack(&stack, FUNCTION, stack->suspend);
		}
		else if (!strcmp(token, "endmodule")) {
		    if (DEBUG) printf("End of module \"%s\" found.\n", topmod->name);
		    popstack(&stack);
		    if (stack->state == MODULE) popstack(&stack);
		}
		else {
		    // Check for any subcircuit call, as evidenced by the
		    // syntax "<name> <name> ( ...".  Append the name to the
		    // .dep file
		    if (subname == NULL) {
			subname = strdup(token);
		    }
		    else if (instname == NULL) {
			instname = strdup(token);
		    }
		    // NOTE:  Need to handle parameter passing notation here!
		    else if (!strcmp(token, "(")) {
			pushstack(&stack, SUBCIRCUIT, stack->suspend);
		        if (DEBUG) printf("Found module dependency \"%s\""
				" in source\n", subname);

			// Dependencies list is for files other than this one.
			// So check module list to see if this module is known
			// to this source file.
			for (newmod = topmod; newmod; newmod = newmod->next) {
			    if (!strcmp(newmod->name, subname))
				break;
			}
			if (newmod == NULL) {
	 		    fputs(subname, fdep);
			    fputs("\ninstance ==> ", fdep);
			    fputs(instname, fdep);
	 		    fputs("\n", fdep);
			}
			free(instname);
			free(subname);
			instname = NULL;
			subname = NULL;
		    }
		}
		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case INPUTOUTPUT:
	    case WIRE:
	    case REGISTER:

		if (!strcmp(token, ";")) {
		    fputs(token, ftmp);
		    popstack(&stack);
		}
		else if (!strcmp(token, ",")) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		    // Retain both state and vector bounds
		}
		else if (!strcmp(token, "[")) {
		    char *aptr, *cptr;
		    int aval;

		    fputs(token, ftmp);
		    newtok = advancetoken(&filestack, ftmp, "]");
		    paramcpy(token, newtok, params);	// Substitute parameters
		    fputs(token, ftmp);
		    fputs("] ", ftmp);

		    sscanf(token, "%d", &start);
		    cptr = strchr(token, ':');
		    *cptr = '\0';
		    cptr++;
		    sscanf(cptr, "%d", &end);

		    // Quick check for simple arithmetic (+/-) in
		    // vector size specification.  Need to
		    // expand this to include processing (+/-) as
		    // individual tokens. . .

		    if ((aptr = strchr(token, '-')) != NULL) {
			if (sscanf(aptr + 1, "%d", &aval) == 1)
			    start -= aval;
		    }
		    else if ((aptr = strchr(token, '+')) != NULL) {
			if (sscanf(aptr + 1, "%d", &aval) == 1)
			    start += aval;
		    }
		    if ((aptr = strchr(cptr, '-')) != NULL) {
			if (sscanf(cptr + 1, "%d", &aval) == 1)
			    end -= aval;
		    }
		    else if ((aptr = strchr(cptr, '+')) != NULL) {
			if (sscanf(cptr + 1, "%d", &aval) == 1)
			    end += aval;
		    }
		}
		else {
		    newvec = (vector *)malloc(sizeof(vector));

		    if (stack->state == INPUTOUTPUT) {
		        fputs(token, ftmp);
		        newvec->next = topmod->iolist;
		        topmod->iolist = newvec;
		        if (DEBUG) printf("Adding new I/O signal \"%s\"\n", token);
		    }
		    else if (stack->state == WIRE) {
			if (!strcmp(token, "=")) {
			    // This is a statement "wire <name> = <assignment>"
			    pushstack(&stack, ASSIGNMENT, stack->suspend);
			    // Odin doesn't handle this syntax.  Break it into
			    // two lines, "wire <name>;" and "assign <name> =
			    // <assignment>".

			    fputs(";\n   assign ", ftmp);
			    fputs(topmod->wirelist->name, ftmp);
			    fputs(" = ", ftmp);
			    break;
			}
			else {
			    fputs(token, ftmp);
		            newvec->next = topmod->wirelist;
		            topmod->wirelist = newvec;
		            if (DEBUG) printf("Adding new wire \"%s\"\n", token);
			}
		    }
		    else if (stack->state == REGISTER) {
		        fputs(token, ftmp);
		        newvec->next = topmod->reglist;
		        topmod->reglist = newvec;
		        if (DEBUG) printf("Adding new register \"%s\"\n", token);
		    }

		    newvec->name = strdup(token);
		    if (start != end) {
		        newvec->vector_size = end - start;
			if (newvec->vector_size < 0)
			    newvec->vector_size = -newvec->vector_size;
			newvec->vector_size++;
		    }
		    else
		        newvec->vector_size = -1;
		    newvec->vector_start = start;
		    newvec->vector_end = end;
		}
		break;

	    case ASSIGNMENT:
		if (!strcmp(token, ";")) {
		    popstack(&stack);
		    if (stack->state == WIRE)
			popstack(&stack);	// double-pop
		    fputs(";", ftmp);
		}
		else if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case ALWAYS:
		if (!strcmp(token, "@")) {
		    if (stack->suspend <= 1) fputs("@ ", ftmp);
		}
		else if (!strcmp(token, "*")) {
		    // Change "always @*" to "always @(*)"
		    // And change output type to be not suspended
		    fputs("always @ (*) ", ftmp);
		    stack->suspend = 0;
		    pushstack(&stack, ABODY_PEND, stack->suspend);
		}
		else if (!strcmp(token, "(")) {
		    pushstack(&stack, SENSELIST, stack->suspend);
		}
		else if (!strcmp(token, ")")) {
		    pushstack(&stack, ABODY_PEND, stack->suspend);
		}
		else {
		    fprintf(stderr, "Error File %s Line %d:  Expected sensitivity list.\n",
				filestack->filename, filestack->currentLine);
		    popstack(&stack);
		}
		break;

	    case SENSELIST:
		tempsuspend = 0;
		if (!strcmp(token, "posedge")) {
		    edgetype = POSEDGE;
		}
		else if (!strcmp(token, "negedge")) {
		    edgetype = NEGEDGE;
		}
		else if (!strcmp(token, "or")) {
		    // ignore this
		}
		else if (!strcmp(token, "*")) {
		    fputs("always @ (*)", ftmp);
		    tempsuspend = 1;
		    popstack(&stack);
		    stack->suspend = 0;
		}
		else if (!strcmp(token, ")")) {
		    resetdone = 0;
		    testreset = NULL;
		    condition = UNKNOWN;
		    stack->state = ABODY_PEND;

		    // Regenerate always @() line with posedge clock only,
		    // no reset, and no termination (yet)
		    if (clocksig != NULL)
			fprintf(ftmp, "always @( posedge %s ) ", clocksig->name);

		    if (topmod->resetlist == NULL) {
			if (clocksig != NULL) tempsuspend = 1;
			stack->suspend = 0;		// No resets, no processing
			clocksig = NULL;
		    }
		}
		else {
		    if (edgetype == POSEDGE || edgetype == NEGEDGE) {
		        /* Parse this signal */
		        if (clocksig == NULL) {
			    clocksig = (sigact *)malloc(sizeof(sigact));
			    clocksig->next = topmod->clocklist;
			    topmod->clocklist = clocksig;
			    clocksig->name = strdup(token);
			    clocksig->edgetype = edgetype;

			    // Check if this clock is in the module's input
			    // list.  Add it to the <module_name>.clk
			    // file, and indicate if it is in the module's
			    // input list, and if it is a wire or a register.
			    //
			    // Note that if the clock is in the module's
			    // output list it will be called an input;  this
			    // is okay for our purpose (see vmunge.c).

			    write_signal(topmod, clocksig, fclk, (char)0);
		        }
		        else {
			    sigact *resetsig = (sigact *)malloc(sizeof(sigact));
			    resetsig->next = topmod->resetlist;
			    topmod->resetlist = resetsig;
			    resetsig->name = strdup(token);
			    resetsig->edgetype = edgetype;

		            if (DEBUG) printf("Adding reset signal \"%s\"\n", token);
		        }
		    }
		    else {
			// This is one of those blocks where a sensitivity
			// list is used to make a wire assignment look like
			// a register, in defiance of all logic.

			if (stack->suspend != 0) {
			   fputs("always @( ", ftmp);
			   stack->suspend = 0;
			}
		    }
		}
		if (stack->suspend <= 1 && tempsuspend == 0) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;
		
	    case ABODY_PEND:
		if (!strcmp(token, "begin"))
		    if (stack->suspend > 1)
			fputs(token, ftmp);

		stack->state = ABODY;
		/* Fall through */

	    case ABODY:
		if (!strcmp(token, "begin")) {
		    pushstack(&stack, BEGIN_END, stack->suspend);

		    // NOTE:  To-do:  Generate inverted clock signal for
		    // use with negedge statements!
		}
		else if (!strcmp(token, "if")) {
		    pushstack(&stack, IF_ELSE, stack->suspend);
		}
		else if (!strcmp(token, "else")) {
			
		    if ((testreset != NULL) && (stack->suspend == 2))
			pushstack(&stack, ELSE, 3);
		    else
			pushstack(&stack, ELSE, stack->suspend);
		}
		else if (!strcmp(token, ";")) {
		    // End of one-liner always block
		    popstack(&stack);	// Return to ALWAYS
		    popstack(&stack);	// Return to MBODY
		}
		else if (!strcmp(token, "case")) {
		    pushstack(&stack, CASE, 0);
		}

		// The following are for cases caused by sloppiness
		// in "if" statement syntax in verilog syntax.
		// An "always" not followed by a "begin" ... "end"
		// block, but instead followed by "if" can then
		// be followed by arbitrarily many "else if" statements,
		// or none at all, so that one must watch for following
		// statements that do not belong in an "always" block,
		// like "wire", or "assign" or another "always", before
		// one can know whether or not the end of the "always"
		// block has been reached.

		// We look for "always", "wire", "reg", and "assign".
		// Anything else is assumed to be a subcircuit call.

		else if (!strcmp(token, "always")) {
		    while(stack->state != MBODY) popstack(&stack);
		    pushstack(&stack, ALWAYS, 2);
		    edgetype = 0;
		    clocksig = NULL;
		    condition = UNKNOWN;
		    initvec = NULL;
		    if (subname != NULL) {
			free(subname);
			subname = NULL;
		    }
		    if (instname != NULL) {
			free(instname);
			instname = NULL;
		    }
		}
		else if (!strcmp(token, "wire")) {
		    while(stack->state != MBODY) popstack(&stack);
		    pushstack(&stack, WIRE, stack->suspend);
		}
		else if (!strcmp(token, "reg")) {
		    while(stack->state != MBODY) popstack(&stack);
		    pushstack(&stack, REGISTER, stack->suspend);
		}
		else if (!strcmp(token, "assign")) {
		    while(stack->state != MBODY) popstack(&stack);
		    pushstack(&stack, ASSIGNMENT, stack->suspend);
		}
		else {
		    while(stack->state != MBODY) popstack(&stack);
		    if (subname == NULL) {
			subname = strdup(token);
		    }
		}

		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case ELSE:
		stack->state = IF_ELSE;

		if ((resetdone == 1) && (stack->suspend == 3)) {
		    testreset = NULL;
		    if (strcmp(token, "begin")) {

			// "else" followed by something other than "begin",
			// after a reset block has been processed, means
			// that we need to remove all output suspends.

			for (tstack = stack; tstack; tstack = tstack->next)
			    tstack->suspend = 0;
		    }
		}

		if (!strcmp(token, "if")) {
		    if (stack->suspend <= 1) fputs("if ", ftmp);
		    break;
		}
		// Otherwise, fall through

	    case BEGIN_END:
	    case IF_ELSE:

		tempsuspend = 0;
		if ((stack->state == BEGIN_END) && (!strcmp(token, "end"))) {
		    popstack(&stack);

		    if (stack->suspend == 3) {
			tempsuspend = 1;	// Don't output this "end"
			// But output everything after this.
			for (tstack = stack; tstack; tstack = tstack->next)
			    tstack->suspend = 0;
		    }

		    // An if statement followed by a begin-end block
		    // must have the if-else marker popped off the
		    // stack as well.

		    if (stack->state == IF_ELSE) popstack(&stack);

		    // If we're down to an ABODY, then remove it, too
		    // if (stack->state == ABODY) popstack(&stack);

		    // If we're down to an ALWAYS, then remove it, too
		    // if (stack->state == ALWAYS) popstack(&stack);
		}
		else if ((stack->state == IF_ELSE) && (!strcmp(token, ";"))) {
		    popstack(&stack);
		}
		else if ((stack->state == IF_ELSE) && (!strcmp(token, "("))) {
		    pushstack(&stack, CONDITION, stack->suspend);
		}
		else if (!strcmp(token, "begin")) {
		    // If we ended a reset block and marked suspend as 3,
		    // then the next "begin" and its associated "end"
		    // need to be absorbed.
		    if (stack->suspend == 3) tempsuspend = 1;
		    pushstack(&stack, BEGIN_END, stack->suspend == 3 ?
				1 : stack->suspend);
		}
		else if (!strcmp(token, "if")) {
		    pushstack(&stack, IF_ELSE, stack->suspend);
		}
		else if (!strcmp(token, "else")) {
		    // Mark "suspend" as 3 so that we know how to mark
		    // the stack to get the right output
		    pushstack(&stack, ELSE, (testreset == NULL) ?
				stack->suspend : 3);
		}

		// All other (normal) processing.  If testreset is set, then
		// we catch assignments and drop them into the init file until
		// the end of the if_else statement or begin_end block

		else if ((testreset != NULL) && strcmp(token, ";")) {
		    if (initvec == NULL) {
		        // This is a signal to add to init list.  Parse LHS, RHS

			for (testvec = topmod->reglist; testvec != NULL;
					testvec = testvec->next) {
			    if (!strcmp(testvec->name, token))
				break;
			}
			if (testvec == NULL) {
			    fprintf(stderr, "Error File %s Line %d:  Reset condition "
					"is not an assignment to a known registered "
					"signal.\n", filestack->filename,
					filestack->currentLine);
			}
			else {
			    initvec = testvec;
			}
		    }
		    else {
			if (!strcmp(token, "<=")) break;
			else if (!strcmp(token, "=")) break;
			else if (!strncmp(token, "#", 1)) break; // Ignore this

			if (strlen(token) > 0) {
			    // If token is a vector bundle, get all of it
			    if (*token == '{') {
				fputs(token, ftmp);
				newtok = advancetoken(&filestack, ftmp, "}");
				paramcpy(token, newtok, params);
				fputs(token, ftmp);
				fputs("}", ftmp);
			    }
				
			    if (DEBUG) printf("Reset \"%s\" to \"%s\"\n",
					initvec->name, token);
			    j = initvec->vector_start;

			    // Now that we're doing resets, we go ahead and mark
			    // it done.  This will not affect the rest of the
			    // reset processing in this block.
			    resetdone = 1;
			    while (topmod->resetlist != NULL) {
				testsig = topmod->resetlist;
				topmod->resetlist = topmod->resetlist->next;
				free(testsig);
			    }

			    if ((bptr = parse_bit(filestack, topmod, token, j, style))
					!= NULL) {

				// NOTE:  The finit file uses signal<idx> notation,
				// not the signal[idx] notation compatible with verilog,
				// mostly for reasons of variable naming and handling in
				// Tcl.  For Odin-II, it uses the ~idx notation that Odin-II
				// creates.

				if (initvec->vector_size > 0) {
				    if (style == YOSYS)
				       fprintf(finit, "%s<%d> %s\n", initvec->name, j, bptr);
				    else
				       fprintf(finit, "%s~%d %s\n", initvec->name, j, bptr);
				}
				else
				    fprintf(finit, "%s %s\n", initvec->name, bptr);

				for (i = 1; i < initvec->vector_size; i++) {
				    if (initvec->vector_start > initvec->vector_end)
					j--;
				    else
					j++;
			   	    bptr = parse_bit(filestack, topmod, token, j, style);
				    if (bptr != NULL) {
				        if (style == YOSYS)
				            fprintf(finit, "%s<%d> %s\n", initvec->name, j, bptr);
					else
				            fprintf(finit, "%s~%d %s\n", initvec->name, j, bptr);
				    }
			        }
			    }
			    initvec = NULL;
		        }
		    }
	        }
		// Everything else that is not structural (begin, end, else,
		// etc.) and is not part of reset signal processing, goes here.

		else if (!strcmp(token, "case")) {
		    pushstack(&stack, CASE, stack->suspend);
		}

		if (stack->suspend <= 1 && tempsuspend == 0) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case CONDITION:

		if (!strcmp(token, ")")) {
		    if (testreset != NULL) {
			if (condition == UNKNOWN) {
			    if (edgetype == NEGEDGE) {
				fprintf(stderr, "Error File %s Line %d: Reset edgetype"
					" is negative but first "
					"condition checked is positive.\n",
					filestack->filename, filestack->currentLine);
			    }
			    write_signal(topmod, testreset, finit, (char)POSEDGE);
			}
			else if (condition == NOT) {
			    if (edgetype == POSEDGE) {
				fprintf(stderr, "Error File %s Line %d: Reset edgetype"
					" is positive but first "
					"condition checked is negative.\n",
					filestack->filename, filestack->currentLine);
			    }
			    write_signal(topmod, testreset, finit, (char)NEGEDGE);
			}
		    }
		    popstack(&stack);
		}

		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		    break;
		}

		if (condition == -1) {
		    // We are looking for == or !=
		    if (!strcmp(token, "==")) {
			condition = EQUAL; 
			break;
		    }
		    else if (!strcmp(token, "!=")) {
			condition = NOT_EQUAL; 
			break;
		    }
		    else if (!strcmp(token, "!")) {
			condition = NOT;
			break;
		    }
		    else if (!strcmp(token, "~")) {
			condition = NOT;
			break;
		    }
		}
		else if (condition == NOT) {
		    if (!strcmp(token, "==")) {
			condition = NOT_EQUAL; 
			break;
		    }
		    else if (!strcmp(token, "!=")) {
			condition = EQUAL; 
			break;
		    }
		}

		if (testreset != NULL) {
		    // pick up RHS 
		    if ((ival = get_bitval(token)) != -1) {
			// Generate reset name in init file

			if (edgetype == POSEDGE) {
			    if ((condition == EQUAL && ival == 1) ||
					(condition == NOT_EQUAL && ival == 0)) {
			        write_signal(topmod, testreset, finit, (char)POSEDGE);
			    }
		 	}
			else {
			    if ((condition == EQUAL && ival == 0) ||
					(condition == NOT_EQUAL && ival == 1)) {
			        write_signal(topmod, testreset, finit, (char)NEGEDGE);
			    }
			}
		    }
		}
		else if (resetdone == 0) {
		    /* Look for signal in list of reset signals */
		    for (testsig = topmod->resetlist; testsig != NULL;
					testsig = testsig->next) {
			if (!strcmp(testsig->name, token))
			    break;
		    }
		    if (testsig != NULL) {
		        testreset = testsig;
			if (DEBUG) printf("Parsing reset conditions for \"%s\"\n",
					testreset->name);
		    }
		    else {
			// This is not a reset signal assignment, so we
			// should cancel the suspend state.
			stack->suspend = 0;
			fputs(token, ftmp);
			fputs(" ", ftmp);
		    }
		}
		break;

	    case SUBCIRCUIT:
		if (!strcmp(token, "("))
		    pushstack(&stack, SUBPIN, stack->suspend);
		else if (!strcmp(token, ";")) {
		    popstack(&stack);
		    fputs("\n", fdep);
		}
		else if (*token == '.') {
		    fputs(token + 1, fdep);
		    fputs("=", fdep);
		}

		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case SUBPIN:
		if (!strcmp(token, ")")) {
		    popstack(&stack);
		    fputs("\n", fdep);
		}
		else {
		    // Need to handle vector notation. . . [..]
		    fputs(token, fdep);
		}

		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case CASE:
		if (!strcmp(token, "endcase")) {
		    popstack(&stack);
		    if (stack->state == IF_ELSE) popstack(&stack);
		}
		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    case FUNCTION:
		if (!strcmp(token, "endfunction")) {
		    popstack(&stack);
		}
		if (stack->suspend <= 1) {
		    fputs(token, ftmp);
		    fputs(" ", ftmp);
		}
		break;

	    default:
		break;
	}
    }

    /* Done! */

    fclose(filestack->file);
    if (finit != NULL) fclose(finit);
    if (fclk != NULL) fclose(fclk);
    if (ftmp != NULL) fclose(ftmp);
    exit(0);
}
