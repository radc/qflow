/*--------------------------------------------------------------*/
/* maze.c -- general purpose maze router routines.		*/
/*								*/
/* This file contains the main cost evaluation routine, 	*/
/* the route segment generator, and a routine to search		*/
/* the database for all parts of a routed network.		*/
/*--------------------------------------------------------------*/
/* Written by Tim Edwards, June 2011, based on code by Steve	*/
/* Beccue							*/
/*--------------------------------------------------------------*/

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#define  MAZE

#include "qrouter.h"
#include "qconfig.h"
#include "node.h"
#include "maze.h"
#include "lef.h"

extern int TotalRoutes;

/*--------------------------------------------------------------*/
/* find_unrouted_node() --					*/
/*								*/
/* On a power bus, the nodes are routed individually, using the	*/
/* entire power bus as the destination.  So to find out if a	*/
/* node is already routed or not, the only way is to check the	*/
/* routes recorded for the net and determine if any net		*/
/* endpoint is on a node.					*/
/*								*/
/* Return the first node found that is not connected to any	*/
/* route endpoint.  If all nodes are routed, then return NULL	*/
/*--------------------------------------------------------------*/

NODE find_unrouted_node(NET net)
{
    int i, numroutes;
    u_char *routednode;
    NODE node;
    ROUTE rt;
    SEG seg1, seg2;
    DPOINT tap;

    // Quick check:  If the number of routes == number of nodes,
    // then return NULL and we're done.

    numroutes = 0;
    for (rt = net->routes; rt; rt = rt->next) numroutes++;
    if (numroutes == net->numnodes) return NULL;

    routednode = (u_char *)malloc(net->numnodes * sizeof(u_char));
    for (i = 0; i < net->numnodes; i++) routednode[i] = 0;

    // Otherwise, we don't know which nodes have been routed,
    // so check each one individually.

    for (rt = net->routes; rt; rt = rt->next) {
	seg1 = rt->segments;
	seg2 = seg1;
	while (seg2->next) seg2 = seg2->next;

	for (node = net->netnodes; node; node = node->next) {
	    if (routednode[node->nodenum] == 1) continue;

	    for (tap = node->taps; tap; tap = tap->next) {
		if (seg1->x1 == tap->gridx && seg1->y1 == tap->gridy
				&& seg1->layer == tap->layer) {
		    routednode[node->nodenum] = 1;
		    break;
		}
		else if (seg1->x2 == tap->gridx && seg1->y2 == tap->gridy
				&& seg1->layer == tap->layer) {
		    routednode[node->nodenum] = 1;
		    break;
		}
		else if (seg2->x1 == tap->gridx && seg2->y1 == tap->gridy
				&& seg2->layer == tap->layer) {
		    routednode[node->nodenum] = 1;
		    break;
		}
		else if (seg2->x2 == tap->gridx && seg2->y2 == tap->gridy
				&& seg2->layer == tap->layer) {
		    routednode[node->nodenum] = 1;
		    break;
		}
	    }
	    if (tap == NULL) {
		for (tap = node->extend; tap; tap = tap->next) {

		    seg1 = rt->segments;
		    seg2 = seg1;
		    while (seg2->next) seg2 = seg2->next;

		    if (seg1->x1 == tap->gridx && seg1->y1 == tap->gridy
				&& seg1->layer == tap->layer) {
			routednode[node->nodenum] = 1;
			break;
		    }
		    else if (seg1->x2 == tap->gridx && seg1->y2 == tap->gridy
				&& seg1->layer == tap->layer) {
			routednode[node->nodenum] = 1;
			break;
		    }
		    else if (seg2->x1 == tap->gridx && seg2->y1 == tap->gridy
				&& seg2->layer == tap->layer) {
			routednode[node->nodenum] = 1;
			break;
		    }
		    else if (seg2->x2 == tap->gridx && seg2->y2 == tap->gridy
				&& seg2->layer == tap->layer) {
			routednode[node->nodenum] = 1;
			break;
		    }
		}
	    }
	}
    }

    for (node = net->netnodes; node; node = node->next) {
	if (routednode[node->nodenum] == 0) {
	    free(routednode);
	    return node;
	}
    }

    free(routednode);
    return NULL;	/* Statement should never be reached */
}

/*--------------------------------------------------------------*/
/* set_powerbus_to_net()					*/
/* If we have a power or ground net, go through the entire Obs	*/
/* array and mark all points matching the net as TARGET in Obs2	*/
/*								*/
/* We do this after the call to PR_SOURCE, before the calls	*/
/* to set PR_TARGET.						*/
/*								*/
/* If any grid position was marked as TARGET, return 1, else	*/
/* return 0 (meaning the net has been routed already).		*/
/*--------------------------------------------------------------*/

int set_powerbus_to_net(int netnum)
{
    int x, y, lay, rval;
    PROUTE *Pr;

    rval = 0;
    if ((netnum == VDD_NET) || (netnum == GND_NET)) {
       for (lay = 0; lay < Num_layers; lay++)
          for (x = 0; x < NumChannelsX[lay]; x++)
	     for (y = 0; y < NumChannelsY[lay]; y++)
		if ((Obs[lay][OGRID(x, y, lay)] & NETNUM_MASK) == netnum) {
		   Pr = &Obs2[lay][OGRID(x, y, lay)];
		   // Skip locations that have been purposefully disabled
		   if (!(Pr->flags & PR_COST) && (Pr->prdata.net == MAXNETNUM))
		      continue;
		   else if (!(Pr->flags & PR_SOURCE)) {
		      Pr->flags |= (PR_TARGET | PR_COST);
		      Pr->prdata.cost = MAXRT;
		      rval = 1;
		   }
		}
    }
    return rval;
}

/*--------------------------------------------------------------*/
/* clear_non_source_targets --					*/
/*								*/
/* Look at all target nodes of a net.  For any that are not	*/
/* marked as SOURCE, but have terminal points marked as		*/
/* PROCESSED, remove the PROCESSED flag and put the position	*/
/* back on the stack for visiting on the next round.		*/
/*--------------------------------------------------------------*/

void clear_non_source_targets(NET net, POINT *pushlist)
{
   NODE node;
   DPOINT ntap;
   PROUTE *Pr;
   POINT gpoint;
   int lay, x, y;

   for (node = net->netnodes; node; node = node->next) {
      for (ntap = node->taps; ntap; ntap = ntap->next) {
	 lay = ntap->layer;
	 x = ntap->gridx;
	 y = ntap->gridy;
	 Pr = &Obs2[lay][OGRID(x, y, lay)];
	 if (Pr->flags & PR_TARGET) {
	    if (Pr->flags & PR_PROCESSED) {
		Pr->flags &= ~PR_PROCESSED;
		gpoint = (POINT)malloc(sizeof(struct point_));
		gpoint->x1 = x;
		gpoint->y1 = y;
		gpoint->layer = lay;
		gpoint->next = *pushlist;
		*pushlist = gpoint;
	    }
	 }
      }
      if (ntap == NULL) {
	 // Try extended tap areas
         for (ntap = node->extend; ntap; ntap = ntap->next) {
	    lay = ntap->layer;
	    x = ntap->gridx;
	    y = ntap->gridy;
	    Pr = &Obs2[lay][OGRID(x, y, lay)];
	    if (Pr->flags & PR_TARGET) {
		if (Pr->flags & PR_PROCESSED) {
		    Pr->flags &= ~PR_PROCESSED;
		    gpoint = (POINT)malloc(sizeof(struct point_));
		    gpoint->x1 = x;
		    gpoint->y1 = y;
		    gpoint->layer = lay;
		    gpoint->next = *pushlist;
		    *pushlist = gpoint;
		}
	    }
         }
      }
   }
}

/*--------------------------------------------------------------*/
/* clear_target_node --						*/
/*								*/
/* Remove PR_TARGET flags from all points belonging to a node	*/
/*--------------------------------------------------------------*/

int clear_target_node(NODE node)
{
    int x, y, lay;
    DPOINT ntap;
    PROUTE *Pr;

    /* Process tap points of the node */

    for (ntap = node->taps; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;
       if (Nodeloc[lay][OGRID(x, y, lay)] == (NODE)NULL)
	  continue;
       Pr = &Obs2[lay][OGRID(x, y, lay)];
       Pr->flags = 0;
       Pr->prdata.net = node->netnum;
    }

    for (ntap = node->extend; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;

       if (Nodesav[lay][OGRID(x, y, lay)] == (NODE)NULL ||
       	   Nodesav[lay][OGRID(x, y, lay)] != node)
       continue;
	
       Pr = &Obs2[lay][OGRID(x, y, lay)];
       Pr->flags = 0;
       Pr->prdata.net = node->netnum;
    }
}

/*--------------------------------------------------------------*/
/* count_targets() ---						*/
/*								*/
/* Count the number of nodes of a net that are still marked as	*/
/* TARGET.							*/
/*--------------------------------------------------------------*/

int
count_targets(NET net)
{
   NODE node;
   PROUTE *Pr;
   DPOINT ntap;
   int lay, x, y;
   int count = 0;

   for (node = net->netnodes; node; node = node->next) {
      for (ntap = node->taps; ntap; ntap = ntap->next) {
	 lay = ntap->layer;
	 x = ntap->gridx;
	 y = ntap->gridy;
	 Pr = &Obs2[lay][OGRID(x, y, lay)];
	 if (Pr->flags & PR_TARGET) {
	    count++;
	    break;
	 }
      }
      if (ntap == NULL) {
	 // Try extended tap areas
         for (ntap = node->extend; ntap; ntap = ntap->next) {
	    lay = ntap->layer;
	    x = ntap->gridx;
	    y = ntap->gridy;
	    Pr = &Obs2[lay][OGRID(x, y, lay)];
	    if (Pr->flags & PR_TARGET) {
	       count++;
	       break;
	    }
         }
      }
   }
   return count;
}

/*--------------------------------------------------------------*/
/* set_node_to_net() ---					*/
/*								*/
/* Change the Obs2[][] flag values to "newflags" for all tap	*/
/* positions of route terminal "node".  Then follow all routes	*/
/* connected to "node", updating their positions.  Where those	*/
/* routes connect to other nodes, repeat recursively.		*/
/*								*/
/* Return value is 1 if at least one terminal of the node	*/
/* is already marked as PR_SOURCE, indicating that the node	*/
/* has already been routed.  Otherwise, the return value is	*/
/* zero of no error occured, and -1 if any point was found to	*/
/* be unoccupied by any net, which should not happen.		*/
/*								*/
/* If "bbox" is non-null, record the grid extents of the node	*/
/* in the x1, x2, y1, y2 values					*/
/*								*/
/* If "stage" is 1 (rip-up and reroute), then don't let an	*/
/* existing route prevent us from adding terminals.  However,	*/
/* the area will be first checked for any part of the terminal	*/
/* that is routable, only resorting to overwriting colliding	*/
/* nets if there are no other available taps.  Defcon stage 3	*/
/* indicates desperation due to a complete lack of routable	*/
/* taps.  This happens if, for example, a port is offset from	*/
/* the routing grid and tightly boxed in by obstructions.  In	*/
/* such case, we allow routing on an obstruction, but flag the	*/
/* point.  In the output stage, the stub route information will	*/
/* be used to reposition the contact on the port and away from	*/
/* the obstruction.						*/
/*								*/
/* If we completely fail to find a tap point under any		*/
/* condition, then return -2.  This is a fatal error;  there	*/
/* will be no way to route the net.				*/
/*--------------------------------------------------------------*/

int set_node_to_net(NODE node, int newflags, POINT *pushlist, SEG bbox, u_char stage)
{
    int x, y, lay, k, obsnet = 0;
    int result = 0;
    u_char found_one = (u_char)0;
    POINT gpoint;
    DPOINT ntap;
    PROUTE *Pr;

    /* If called from set_routes_to_net, the node has no taps, and the	*/
    /* net is a power bus, just return.					*/

    if ((node->taps == NULL) && (node->extend == NULL)) {
       if ((node->netnum == VDD_NET) || (node->netnum == GND_NET))
	   return result;
    }

    /* Process tap points of the node */

    for (ntap = node->taps; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;
       Pr = &Obs2[lay][OGRID(x, y, lay)];
       if ((Pr->flags & (newflags | PR_COST)) == PR_COST) {
	  Fprintf(stderr, "Error:  Tap position %d, %d layer %d not "
			"marked as source!\n", x, y, lay);
	  return -1;	// This should not happen.
       }

       if (Pr->flags & PR_SOURCE) {
	  result = 1;				// Node is already connected!
       }
       else if (((Pr->prdata.net == node->netnum) || (stage == (u_char)2))
			&& !(Pr->flags & newflags)) {

	  // If we got here, we're on the rip-up stage, and there
	  // is an existing route completely blocking the terminal.
	  // So we will route over it and flag it as a collision.

	  if (Pr->prdata.net != node->netnum) {
	     if ((Pr->prdata.net == (NO_NET | OBSTRUCT_MASK)) ||
			(Pr->prdata.net == NO_NET))
		continue;
	     else
	        Pr->flags |= PR_CONFLICT;
	  }

	  // Do the source and dest nodes need to be marked routable?
	  Pr->flags |= (newflags == PR_SOURCE) ? newflags : (newflags | PR_COST);

	  Pr->prdata.cost = (newflags == PR_SOURCE) ? 0 : MAXRT;

	  // push this point on the stack to process

	  if (pushlist != NULL) {
	     gpoint = (POINT)malloc(sizeof(struct point_));
	     gpoint->x1 = x;
	     gpoint->y1 = y;
	     gpoint->layer = lay;
	     gpoint->next = *pushlist;
	     *pushlist = gpoint;
	  }
	  found_one = (u_char)1;

	  // record extents
	  if (bbox) {
	     if (x < bbox->x1) bbox->x1 = x;
	     if (x > bbox->x2) bbox->x2 = x;
	     if (y < bbox->y1) bbox->y1 = y;
	     if (y > bbox->y2) bbox->y2 = y;
	  }
       }
       else if ((Pr->prdata.net < MAXNETNUM) && (Pr->prdata.net > 0)) obsnet++;
    }

    // Do the same for point in the halo around the tap, but only if
    // they have been attached to the net during a past routing run.

    for (ntap = node->extend; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;

       // Don't process extended areas if they coincide with other nodes.

       // if (Nodeloc[lay][OGRID(x, y, lay)] != (NODE)NULL &&
       //	Nodeloc[lay][OGRID(x, y, lay)] != node)
       //    continue;

       if (Nodesav[lay][OGRID(x, y, lay)] == (NODE)NULL ||
			Nodesav[lay][OGRID(x, y, lay)] != node)
	  continue;

       Pr = &Obs2[lay][OGRID(x, y, lay)];
       if (Pr->flags & PR_SOURCE) {
	  result = 1;				// Node is already connected!
       }
       else if ( !(Pr->flags & newflags) &&
		((Pr->prdata.net == node->netnum) ||
		(stage == (u_char)2 && Pr->prdata.net < MAXNETNUM) ||
		(stage == (u_char)3))) {

	  if (Pr->prdata.net != node->netnum) Pr->flags |= PR_CONFLICT;
	  Pr->flags |= (newflags == PR_SOURCE) ? newflags : (newflags | PR_COST);
	  Pr->prdata.cost = (newflags == PR_SOURCE) ? 0 : MAXRT;

	  // push this point on the stack to process

	  if (pushlist != NULL) {
	     gpoint = (POINT)malloc(sizeof(struct point_));
	     gpoint->x1 = x;
	     gpoint->y1 = y;
	     gpoint->layer = lay;
	     gpoint->next = *pushlist;
	     *pushlist = gpoint;
	  }
	  found_one = (u_char)1;

	  // record extents
	  if (bbox) {
	     if (x < bbox->x1) bbox->x1 = x;
	     if (x > bbox->x2) bbox->x2 = x;
	     if (y < bbox->y1) bbox->y1 = y;
	     if (y > bbox->y2) bbox->y2 = y;
	  }
       }
       else if ((Pr->prdata.net < MAXNETNUM) && (Pr->prdata.net > 0)) obsnet++;
    }

    // In the case that no valid tap points were found,	if we're on the
    // rip-up and reroute section, try again, ignoring existing routes that
    // are in the way of the tap point.  If that fails, then we will
    // route over obstructions and shift the contact when committing the
    // route solution.  And if that fails, we're basically hosed.
    //
    // Make sure to check for the case that the tap point is simply not
    // reachable from any grid point, in the first stage, so we don't
    // wait until the rip-up and reroute stage to route them.

    if ((result == 0) && (found_one == (u_char)0)) {
       if (stage == (u_char)1)
          return set_node_to_net(node, newflags, pushlist, bbox, (u_char)2);
       else if (stage == (u_char)2)
          return set_node_to_net(node, newflags, pushlist, bbox, (u_char)3);
       else if ((stage == (u_char)0) && (obsnet == 0))
          return set_node_to_net(node, newflags, pushlist, bbox, (u_char)3);
       else
	  return -2;
    }

    return result;
}

/*--------------------------------------------------------------*/
/* Set all taps of node "node" to MAXNETNUM, so that it will not	*/
/* be routed to. 						*/
/*--------------------------------------------------------------*/

int disable_node_nets(NODE node)
{
    int x, y, lay;
    int result = 0;
    DPOINT ntap;
    PROUTE *Pr;

    /* Process tap points of the node */

    for (ntap = node->taps; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;
       Pr = &Obs2[lay][OGRID(x, y, lay)];
       if (Pr->flags & PR_SOURCE || Pr->flags & PR_TARGET || Pr->flags & PR_COST) {
	  result = 1;
       }
       else if (Pr->prdata.net == node->netnum) {
	  Pr->prdata.net = MAXNETNUM;
       }
    }

    // Do the same for point in the halo around the tap, but only if
    // they have been attached to the net during a past routing run.

    for (ntap = node->extend; ntap; ntap = ntap->next) {
       lay = ntap->layer;
       x = ntap->gridx;
       y = ntap->gridy;
       Pr = &Obs2[lay][OGRID(x, y, lay)];
       if (Pr->flags & PR_SOURCE || Pr->flags & PR_TARGET || Pr->flags & PR_COST) {
	  result = 1;
       }
       else if (Pr->prdata.net == node->netnum) {
	  Pr->prdata.net = MAXNETNUM;
       }
    }
    return result;
}

/*--------------------------------------------------------------*/
/* That which is already routed (routes should be attached to	*/
/* source nodes) is routable by definition. . .			*/
/*--------------------------------------------------------------*/

int set_route_to_net(NET net, ROUTE rt, int newflags, POINT *pushlist,
		SEG bbox, u_char stage)
{
    int x, y, lay, k;
    int result = 0;
    POINT gpoint;
    SEG seg;
    NODE n2;
    PROUTE *Pr;

    if (rt && rt->segments) {
	for (seg = rt->segments; seg; seg = seg->next) {
	    lay = seg->layer;
	    x = seg->x1;
	    y = seg->y1;
	    while (1) {
		Pr = &Obs2[lay][OGRID(x, y, lay)];
		Pr->flags = (newflags == PR_SOURCE) ? newflags : (newflags | PR_COST);
		// Conflicts should not happen (check for this?)
		// if (Pr->prdata.net != node->netnum) Pr->flags |= PR_CONFLICT;
		Pr->prdata.cost = (newflags == PR_SOURCE) ? 0 : MAXRT;

		// push this point on the stack to process

		if (pushlist != NULL) {
	  	   gpoint = (POINT)malloc(sizeof(struct point_));
	  	   gpoint->x1 = x;
	  	   gpoint->y1 = y;
	  	   gpoint->layer = lay;
	  	   gpoint->next = *pushlist;
	 	   *pushlist = gpoint;
		}

		// record extents
		if (bbox) {
		   if (x < bbox->x1) bbox->x1 = x;
		   if (x > bbox->x2) bbox->x2 = x;
		   if (y < bbox->y1) bbox->y1 = y;
		   if (y > bbox->y2) bbox->y2 = y;
		}

		// If we found another node connected to the route,
		// then process it, too.

		n2 = Nodeloc[lay][OGRID(x, y, lay)];
		if ((n2 != (NODE)NULL) && (n2 != net->netnodes)) {
		   if (newflags == PR_SOURCE) clear_target_node(n2);
		   result = set_node_to_net(n2, newflags, pushlist, bbox, stage);
		   // On error, continue processing
		}

		// Process top part of via
		if (seg->segtype & ST_VIA) {
		   if (lay != seg->layer) break;
		   lay++;
		   continue;
		}

		// Move to next grid position in segment
		if (x == seg->x2 && y == seg->y2) break;
		if (seg->x2 > seg->x1) x++;
		else if (seg->x2 < seg->x1) x--;
		if (seg->y2 > seg->y1) y++;
		else if (seg->y2 < seg->y1) y--;
	    }
	}
    }
    return result;
}

/*--------------------------------------------------------------*/
/* Process all routes of a net, and set their routed positions	*/
/* to SOURCE in Obs2[]						*/
/*--------------------------------------------------------------*/

int set_routes_to_net(NET net, int newflags, POINT *pushlist, SEG bbox,
		u_char stage)
{
    ROUTE rt;
    int result;

    for (rt = net->routes; rt; rt = rt->next)
	result = set_route_to_net(net, rt, newflags, pushlist, bbox, stage);

    return result;
}

/*--------------------------------------------------------------*/
/* Find nets that are colliding with the given net "net", and	*/
/* create and return a list of them.				*/
/*--------------------------------------------------------------*/

NETLIST find_colliding(NET net)
{
   NETLIST nl = (NETLIST)NULL, cnl;
   NET  fnet;
   ROUTE rt;
   SEG seg;
   int lay, i, x, y, orignet;

   /* Scan the routed points for recorded collisions.	*/

   for (rt = net->routes; rt; rt = rt->next) {
      if (rt->segments) {
	 for (seg = rt->segments; seg; seg = seg->next) {
	    lay = seg->layer;
	    x = seg->x1;
	    y = seg->y1;

	    // The following skips over vias, which is okay, since
	    // those positions are covered by segments on both layers
	    // or are terminal positions that by definition can't
	    // belong to a different net.

	    while (1) {
	       orignet = Obs[lay][OGRID(x, y, lay)] & NETNUM_MASK;

	       if (orignet != net->netnum) {

	          /* Route collision.  Save this net if it is	*/
	          /* not already in the list of colliding nets.	*/

	          for (cnl = nl; cnl; cnl = cnl->next) {
		     if (cnl->net->netnum == orignet)
		        break;
	          }
	          if (cnl == NULL) {
		     for (i = 0; i < Numnets; i++) {
			fnet = Nlnets[i];
		        if (fnet->netnum == orignet) {
			   cnl = (NETLIST)malloc(sizeof(struct netlist_));
		           cnl->net = fnet;
		           cnl->next = nl;
		           nl = cnl;
			   break;
			}
		     }
		  }
	       }
	       if ((x == seg->x2) && (y == seg->y2)) break;

	       if (x < seg->x2) x++;
	       else if (x > seg->x2) x--;
	       if (y < seg->y2) y++;
	       else if (y > seg->y2) y--;
	    }
	 }
      }
   }

   /* Diagnostic */

   if ((nl != NULL) && (Verbose > 0)) {
      Fprintf(stdout, "Best route of %s collides with nets: ",
		net->netname);
      for (cnl = nl; cnl; cnl = cnl->next) {
         Fprintf(stdout, "%s ", cnl->net->netname);
      }
      Fprintf(stdout, "\n");
   }

   return nl;
}

/*--------------------------------------------------------------*/
/* ripup_net ---						*/
/*								*/
/* Rip up the entire network located at position x, y, lay.	*/
/*								*/
/* If argument "restore" is TRUE, then at each node, restore	*/
/* the crossover cost by attaching the node back to the Nodeloc	*/
/* array.							*/
/*--------------------------------------------------------------*/

u_char ripup_net(NET net, u_char restore)
{
   int thisnet, oldnet, x, y, lay, dir;
   double sreq;
   NODE node;
   ROUTE rt;
   SEG seg;
   DPOINT ntap;

   thisnet = net->netnum;

   for (rt = net->routes; rt; rt = rt->next) {
      if (rt->segments) {
	 for (seg = rt->segments; seg; seg = seg->next) {
	    lay = seg->layer;
	    x = seg->x1;
	    y = seg->y1;
	    while (1) {
	       oldnet = Obs[lay][OGRID(x, y, lay)] & NETNUM_MASK;
	       if ((oldnet > 0) && (oldnet < MAXNETNUM)) {
	          if (oldnet != thisnet) {
		     Fprintf(stderr, "Error: position %d %d layer %d has net "
				"%d not %d!\n", x, y, lay, oldnet, thisnet);
		     return FALSE;	// Something went wrong
	          }

	          // Reset the net number to zero along this route for
	          // every point that is not a node tap.  Points that
		  // were routed over obstructions to reach off-grid
		  // taps are returned to obstructions.

	          if (Nodesav[lay][OGRID(x, y, lay)] == (NODE)NULL) {
		     dir = Obs[lay][OGRID(x, y, lay)] & PINOBSTRUCTMASK;
		     if (dir == 0)
		        Obs[lay][OGRID(x, y, lay)] = 0;
		     else
		        Obs[lay][OGRID(x, y, lay)] = NO_NET | dir;
		  }

		  // Routes which had blockages added on the sides due
		  // to spacing constraints have (NO_NET | ROUTED_NET)
		  // set;  these flags should be removed.

		  if (needblock[lay] & (ROUTEBLOCKX | VIABLOCKX)) {
		     if ((x > 0) && ((Obs[lay][OGRID(x - 1, y, lay)] &
				(NO_NET | ROUTED_NET)) == (NO_NET | ROUTED_NET)))
			Obs[lay][OGRID(x - 1, y, lay)] &= ~(NO_NET | ROUTED_NET);
		     else if ((x < NumChannelsX[lay] - 1) &&
				((Obs[lay][OGRID(x + 1, y, lay)] &
				(NO_NET | ROUTED_NET)) == (NO_NET | ROUTED_NET)))
			Obs[lay][OGRID(x + 1, y, lay)] &= ~(NO_NET | ROUTED_NET);
		  }
		  if (needblock[lay] & (ROUTEBLOCKY | VIABLOCKY)) {
		     if ((y > 0) && ((Obs[lay][OGRID(x, y - 1, lay)] &
				(NO_NET | ROUTED_NET)) == (NO_NET | ROUTED_NET)))
			Obs[lay][OGRID(x, y - 1, lay)] &= ~(NO_NET | ROUTED_NET);
		     else if ((y < NumChannelsY[lay] - 1) &&
				((Obs[lay][OGRID(x, y + 1, lay)] &
				(NO_NET | ROUTED_NET)) == (NO_NET | ROUTED_NET)))
			Obs[lay][OGRID(x, y + 1, lay)] &= ~(NO_NET | ROUTED_NET);
		  }
	       }

	       // This break condition misses via ends, but those are
	       // terminals and don't get ripped out.

	       if ((x == seg->x2) && (y == seg->y2)) break;

	       if (x < seg->x2) x++;
	       else if (x > seg->x2) x--;
	       if (y < seg->y2) y++;
	       else if (y > seg->y2) y--;
	    }
	 }
      }
   }

   // For each net node tap, restore the node pointer on Nodeloc so
   // that crossover costs are again applied to routes over this
   // node tap.

   if (restore != 0) {
      for (node = net->netnodes; node; node = node->next) {
	 for (ntap = node->taps; ntap; ntap = ntap->next) {
	    lay = ntap->layer;
	    x = ntap->gridx;
	    y = ntap->gridy;
	    Nodeloc[lay][OGRID(x, y, lay)] = Nodesav[lay][OGRID(x, y, lay)];
	 }
      }
   }

   /* Remove all routing information from this net */

   while (net->routes) {
      rt = net->routes;
      net->routes = rt->next;
      while (rt->segments) {
	 seg = rt->segments->next;
	 free(rt->segments);
	 rt->segments = seg;
      }
      free(rt);
   }

   return TRUE;
}

/*--------------------------------------------------------------*/
/* eval_pt - evaluate cost to get from given point to		*/
/*	current point.  Current point is passed in "ept", and	*/
/* 	the direction from the new point to the current point	*/
/*	is indicated by "flags".				*/
/*								*/
/*	ONLY consider the cost of the single step itself.	*/
/*								*/
/*      If "stage" is nonzero, then this is a second stage	*/
/*	routing, where we should consider other nets to be a	*/
/*	high cost to short to, rather than a blockage.  This	*/
/* 	will allow us to finish the route, but with a minimum	*/
/*	number of collisions with other nets.  Then, we rip up	*/
/*	those nets, add them to the "failed" stack, and re-	*/
/*	route this one.						*/
/*								*/
/*  ARGS: none							*/
/*  RETURNS: 1 if node needs to be (re)processed, 0 if not.	*/
/*  SIDE EFFECTS: none (get this right or else)			*/
/*--------------------------------------------------------------*/

int eval_pt(GRIDP *ept, u_char flags, u_char stage)
{
    int thiscost = 0;
    NODE node;
    NETLIST nl;
    PROUTE *Pr, *Pt;
    GRIDP newpt;

    newpt = *ept;

    switch (flags) {
       case PR_PRED_N:
	  newpt.y--;
	  break;
       case PR_PRED_S:
	  newpt.y++;
	  break;
       case PR_PRED_E:
	  newpt.x--;
	  break;
       case PR_PRED_W:
	  newpt.x++;
	  break;
       case PR_PRED_U:
	  newpt.lay--;
	  break;
       case PR_PRED_D:
	  newpt.lay++;
	  break;
    }

    Pr = &Obs2[newpt.lay][OGRID(newpt.x, newpt.y, newpt.lay)];

    if (!(Pr->flags & (PR_COST | PR_SOURCE))) {
       // 2nd stage allows routes to cross existing routes
       if (stage && (Pr->prdata.net < MAXNETNUM)) {
	  // if (Nodeloc[newpt.lay][OGRID(newpt.x, newpt.y, newpt.lay)] != NULL)
	  if (Nodesav[newpt.lay][OGRID(newpt.x, newpt.y, newpt.lay)] != NULL)
	     return 0;			// But cannot route over terminals!

	  // Is net k in the "noripup" list?  If so, don't route it */

	  for (nl = CurNet->noripup; nl; nl = nl->next) {
	     if (nl->net->netnum == Pr->prdata.net)
		return 0;
	  }

	  // In case of a collision, we change the grid point to be routable
	  // but flag it as a point of collision so we can later see what
	  // were the net numbers of the interfering routes by cross-referencing
	  // the Obs[][] array.

	  Pr->flags |= (PR_CONFLICT | PR_COST);
	  Pr->prdata.cost = MAXRT;
	  thiscost = ConflictCost;
       }
       else
          return 0;		// Position is not routeable
    }

    // Compute the cost to step from the current point to the new point.
    // "BlockCost" is used if the node has only one point to connect to,
    // so that routing over it could block it entirely.

    if (newpt.lay > 0) {
	if ((node = Nodeloc[newpt.lay - 1][OGRID(newpt.x, newpt.y, newpt.lay - 1)])
			!= (NODE)NULL) {
	    Pt = &Obs2[newpt.lay - 1][OGRID(newpt.x, newpt.y, newpt.lay - 1)];
	    if (!(Pt->flags & PR_TARGET) && !(Pt->flags & PR_SOURCE)) {
		if (node->taps && (node->taps->next == NULL))
		   thiscost += BlockCost;	// Cost to block out a tap
		else if (node->taps == NULL) {
		   if (node->extend != NULL) {
			if (node->extend->next == NULL)
			   // Node has only one extended access point:  Try
			   // very hard to avoid routing over it 
			   thiscost += 10 * BlockCost;
			else
			   thiscost += BlockCost;
		   }
		   // If both node->taps and node->extend are NULL, then
		   // the node has no access and will never be routed, so
		   // don't bother costing it.
		}
		else
	           thiscost += XverCost;	// Cross-under cost
	    }
	}
    }
    if (newpt.lay < Num_layers - 1) {
	if ((node = Nodeloc[newpt.lay + 1][OGRID(newpt.x, newpt.y, newpt.lay + 1)])
			!= (NODE)NULL) {
	    Pt = &Obs2[newpt.lay + 1][OGRID(newpt.x, newpt.y, newpt.lay + 1)];
	    if (!(Pt->flags & PR_TARGET) && !(Pt->flags & PR_SOURCE)) {
		if (node->taps && (node->taps->next == NULL))
		   thiscost += BlockCost;	// Cost to block out a tap
		else
	           thiscost += XverCost;	// Cross-over cost
	    }
	}
    }
    if (ept->lay != newpt.lay) thiscost += ViaCost;
    if (ept->x != newpt.x) thiscost += (Vert[newpt.lay] * JogCost +
			(1 - Vert[newpt.lay]) * SegCost);
    if (ept->y != newpt.y) thiscost += (Vert[newpt.lay] * SegCost +
			(1 - Vert[newpt.lay]) * JogCost);

    // Add the cost to the cost of the original position
    thiscost += ept->cost;
   
    // Replace node information if cost is minimum

    if (Pr->flags & PR_CONFLICT)
       thiscost += ConflictCost;	// For 2nd stage routes

    if (thiscost < Pr->prdata.cost) {
       Pr->flags &= ~PR_PRED_DMASK;
       Pr->flags |= flags;
       Pr->prdata.cost = thiscost;
       Pr->flags &= ~PR_PROCESSED;	// Need to reprocess this node

       if (Verbose > 3) {
	  Fprintf(stdout, "New cost %d at (%d %d %d)\n", thiscost,
		newpt.x, newpt.y, newpt.lay);
       }
       return 1;
    }
    return 0;	// New position did not get a lower cost

} /* eval_pt() */

/*------------------------------------------------------*/
/* writeback_segment() ---				*/
/*							*/
/*	Copy information from a single segment back	*/
/* 	the Obs[] array.				*/
/*							*/
/* NOTE:  "needblock" is used to handle			*/
/* cases where the existence of a route prohibits any	*/
/* routing on adjacent tracks on the same plane due to	*/
/* DRC restrictions (i.e., metal is too wide for single	*/
/* track spacing).  Be sure to check the value of	*/
/* adjacent positions in Obs against the mask		*/
/* NETNUM_MASK, because NETNUM_MASK includes NO_NET.	*/
/* By replacing only empty and routable positions with	*/
/* the unique flag combination	NO_NET | ROUTED_NET, it	*/
/* is possible to detect and remove the same if that	*/
/* net is ripped up, without touching any position	*/
/* originally marked NO_NET.				*/
/*							*/
/* Another NOTE:  Tap offsets can cause the position	*/
/* in front of the offset to be unroutable.  So if the	*/
/* segment is on a tap offset, mark the position in	*/
/* front as unroutable.  If the segment neighbors an	*/
/* offset tap, then mark the tap unroutable.		*/
/*------------------------------------------------------*/

void writeback_segment(SEG seg, int netnum)
{
   double dist;
   int  i, layer;
   u_int sobs;
   NODE node;

   if (seg->segtype == ST_VIA) {
      Obs[seg->layer + 1][OGRID(seg->x1, seg->y1, seg->layer + 1)] = netnum;
      if (needblock[seg->layer + 1] & VIABLOCKX) {
	 if ((seg->x1 < (NumChannelsX[seg->layer + 1] - 1)) &&
		(Obs[seg->layer + 1][OGRID(seg->x1 + 1, seg->y1, seg->layer + 1)]
		& NETNUM_MASK) == 0)
 	    Obs[seg->layer + 1][OGRID(seg->x1 + 1, seg->y1, seg->layer + 1)] =
			(NO_NET | ROUTED_NET);
	 if ((seg->x1 > 0) &&
		(Obs[seg->layer + 1][OGRID(seg->x1 - 1, seg->y1, seg->layer + 1)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer + 1][OGRID(seg->x1 - 1, seg->y1, seg->layer + 1)] =
			(NO_NET | ROUTED_NET);
      }
      if (needblock[seg->layer + 1] & VIABLOCKY) {
	 if ((seg->y1 < (NumChannelsY[seg->layer + 1] - 1)) &&
		(Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 + 1, seg->layer + 1)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 + 1, seg->layer + 1)] =
			(NO_NET | ROUTED_NET);
	 if ((seg->y1 > 0) &&
		(Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 - 1, seg->layer + 1)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 - 1, seg->layer + 1)] =
			(NO_NET | ROUTED_NET);
      }

      // Check position on each side for an offset tap on a different net, and
      // mark the position unroutable.
      //
      // NOTE:  This is a bit conservative, as it will block positions next to
      // an offset tap without checking if the offset distance is enough to
      // cause a DRC error.  Could be refined. . .

      layer = (seg->layer == 0) ? 0 : seg->layer - 1;
      if (seg->x1 < (NumChannelsX[layer] - 1)) {
	 sobs = Obs[layer][OGRID(seg->x1 + 1, seg->y1, layer)];
	 if ((sobs & OFFSET_TAP) && !(sobs & ROUTED_NET)) {
	    if (sobs & STUBROUTE_EW) {
	       dist = Stub[layer][OGRID(seg->x1 + 1, seg->y1, layer)];
	       if (dist > 0) {
		  Obs[layer][OGRID(seg->x1 + 1, seg->y1, layer)] |=
			(NO_NET | ROUTED_NET);
	       }
	    }
	 }
      }
      if (seg->x1 > 0) {
	 sobs = Obs[layer][OGRID(seg->x1 - 1, seg->y1, layer)];
	 if ((sobs & OFFSET_TAP) && !(sobs & ROUTED_NET)) {
	    if (sobs & STUBROUTE_EW) {
	       dist = Stub[layer][OGRID(seg->x1 - 1, seg->y1, layer)];
	       if (dist < 0) {
		  Obs[layer][OGRID(seg->x1 - 1, seg->y1, layer)] |=
			(NO_NET | ROUTED_NET);
	       }
	    }
	 }
      }
      if (seg->y1 < (NumChannelsY[layer] - 1)) {
	 sobs = Obs[layer][OGRID(seg->x1, seg->y1 + 1, layer)];
	 if ((sobs & OFFSET_TAP) && !(sobs & ROUTED_NET)) {
	    if (sobs & STUBROUTE_NS) {
	       dist = Stub[layer][OGRID(seg->x1, seg->y1 + 1, layer)];
	       if (dist > 0) {
		  Obs[layer][OGRID(seg->x1, seg->y1 + 1, layer)] |=
			(NO_NET | ROUTED_NET);
	       }
	    }
	 }
      }
      if (seg->y1 > 0) {
	 sobs = Obs[layer][OGRID(seg->x1, seg->y1 - 1, layer)];
	 if ((sobs & OFFSET_TAP) && !(sobs & ROUTED_NET)) {
	    if (sobs & STUBROUTE_NS) {
	       dist = Stub[layer][OGRID(seg->x1, seg->y1 - 1, layer)];
	       if (dist < 0) {
		  Obs[layer][OGRID(seg->x1, seg->y1 - 1, layer)] |=
			(NO_NET | ROUTED_NET);
	       }
	    }
	 }
      }

      // If position itself is an offset route, then make the route position
      // on the forward side of the offset unroutable, on both via layers.
      // (Like the above code, there is no check for whether the offset
      // distance is enough to cause a DRC violation.)

      sobs = Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)];
      if (sobs & OFFSET_TAP) {
	 dist = Stub[layer][OGRID(seg->x1, seg->y1, seg->layer)];
	 if (sobs & STUBROUTE_EW) {
	    if ((dist > 0) && (seg->x1 < (NumChannelsX[seg->layer] - 1))) {
	       Obs[seg->layer][OGRID(seg->x1 + 1, seg->y1, seg->layer)] |=
			(NO_NET | ROUTED_NET);
	       Obs[seg->layer + 1][OGRID(seg->x1 + 1, seg->y1, seg->layer + 1)] |=
				(NO_NET | ROUTED_NET);
	    }
	    if ((dist < 0) && (seg->x1 > 0)) {
	       Obs[seg->layer][OGRID(seg->x1 - 1, seg->y1, seg->layer)] |=
			(NO_NET | ROUTED_NET);
	       Obs[seg->layer + 1][OGRID(seg->x1 - 1, seg->y1, seg->layer + 1)] |=
				(NO_NET | ROUTED_NET);
	    }
	 }
	 else if (sobs & STUBROUTE_NS) {
	    if ((dist > 0) && (seg->y1 < (NumChannelsY[seg->layer] - 1))) {
	       Obs[seg->layer][OGRID(seg->x1, seg->y1 + 1, seg->layer)] |=
			(NO_NET | ROUTED_NET);
	       Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 + 1, seg->layer + 1)] |=
				(NO_NET | ROUTED_NET);
	    }
	    if ((dist < 0) && (seg->y1 > 0)) {
	       Obs[seg->layer][OGRID(seg->x1, seg->y1 - 1, seg->layer)] |=
			(NO_NET | ROUTED_NET);
	       Obs[seg->layer + 1][OGRID(seg->x1, seg->y1 - 1, seg->layer + 1)] |=
				(NO_NET | ROUTED_NET);
	    }
	 }
      }
   }

   for (i = seg->x1; ; i += (seg->x2 > seg->x1) ? 1 : -1) {
      Obs[seg->layer][OGRID(i, seg->y1, seg->layer)] = netnum;
      if (needblock[seg->layer] & ROUTEBLOCKY) {
         if ((seg->y1 < (NumChannelsY[seg->layer] - 1)) &&
		(Obs[seg->layer][OGRID(i, seg->y1 + 1, seg->layer)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer][OGRID(i, seg->y1 + 1, seg->layer)] =
			(NO_NET | ROUTED_NET);
	 if ((seg->y1 > 0) &&
		(Obs[seg->layer][OGRID(i, seg->y1 - 1, seg->layer)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer][OGRID(i, seg->y1 - 1, seg->layer)] =
			(NO_NET | ROUTED_NET);
      }
      if (i == seg->x2) break;
   }
   for (i = seg->y1; ; i += (seg->y2 > seg->y1) ? 1 : -1) {
      Obs[seg->layer][OGRID(seg->x1, i, seg->layer)] = netnum;
      if (needblock[seg->layer] & ROUTEBLOCKX) {
	 if ((seg->x1 < (NumChannelsX[seg->layer] - 1)) &&
		(Obs[seg->layer][OGRID(seg->x1 + 1, i, seg->layer)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer][OGRID(seg->x1 + 1, i, seg->layer)] =
			(NO_NET | ROUTED_NET);
	 if ((seg->x1 > 0) &&
		(Obs[seg->layer][OGRID(seg->x1 - 1, i, seg->layer)]
		& NETNUM_MASK) == 0)
	    Obs[seg->layer][OGRID(seg->x1 - 1, i, seg->layer)] =
			(NO_NET | ROUTED_NET);
      }
      if (i == seg->y2) break;
   }
}

/*--------------------------------------------------------------*/
/* commit_proute - turn the potential route into an actual	*/
/*		route by generating the route segments		*/
/*								*/
/*  ARGS:   route structure to fill;  "stage" is 1 if we're on	*/
/*	    second stage routing, in which case we fill the	*/
/*	    route structure but don't modify the Obs array.	*/
/*								*/
/*  RETURNS: 1 on success, 0 on stacked via failure, and 	*/
/*	    -1 on failure to find a terminal.  On a stacked	*/
/* 	    via failure, the route is committed anyway.		*/
/*								*/
/*  SIDE EFFECTS: Obs update, RT llseg added			*/
/*--------------------------------------------------------------*/

int commit_proute(ROUTE rt, GRIDP *ept, u_char stage)
{
   SEG  seg, lseg;
   int  i, j, k, lay, lay2, rval;
   int  x, y;
   int  dx, dy, dl;
   u_int netnum, netobs1, netobs2, dir1, dir2;
   u_char first = (u_char)1;
   u_char dmask;
   u_char pflags, p2flags;
   PROUTE *Pr;
   POINT newlr, newlr2, lrtop, lrend, lrnext, lrcur, lrprev;
   double sreq;

   if (Verbose > 1) {
      Flush(stdout);
      Fprintf(stdout, "\nCommit: TotalRoutes = %d\n", TotalRoutes);
   }

   netnum = rt->netnum;

   Pr = &Obs2[ept->lay][OGRID(ept->x, ept->y, ept->lay)];
   if (!(Pr->flags & PR_COST)) {
      Fprintf(stderr, "commit_proute(): impossible - terminal is not routable!\n");
      return -1;
   }

   // Generate an indexed route, recording the series of predecessors and their
   // positions.

   lrtop = (POINT)malloc(sizeof(struct point_));
   lrtop->x1 = ept->x;
   lrtop->y1 = ept->y;
   lrtop->layer = ept->lay;
   lrtop->next = NULL;
   lrend = lrtop;

   while (1) {

      Pr = &Obs2[lrend->layer][OGRID(lrend->x1, lrend->y1, lrend->layer)];
      dmask = Pr->flags & PR_PRED_DMASK;
      if (dmask == PR_PRED_NONE) break;

      newlr = (POINT)malloc(sizeof(struct point_));
      newlr->x1 = lrend->x1;
      newlr->y1 = lrend->y1;
      newlr->layer = lrend->layer;
      lrend->next = newlr;
      newlr->next = NULL;

      switch (dmask) {
         case PR_PRED_N:
	    (newlr->y1)++;
	    break;
         case PR_PRED_S:
	    (newlr->y1)--;
	    break;
         case PR_PRED_E:
	    (newlr->x1)++;
	    break;
         case PR_PRED_W:
	    (newlr->x1)--;
	    break;
         case PR_PRED_U:
	    (newlr->layer)++;
	    break;
         case PR_PRED_D:
	    (newlr->layer)--;
	    break;
      }
      lrend = newlr;
   }
   lrend = lrtop;

   // TEST:  Walk through the solution, and look for stacked vias.  When
   // found, look for an alternative path that avoids the stack.

   rval = 1;
   if (StackedContacts < (Num_layers - 1)) {
      POINT lrppre;
      POINT a, b;
      PROUTE *pri, *pri2;
      int stacks = 1, stackheight;
      int cx, cy, cl;
      int mincost, minx, miny, ci, ci2, collide, cost;

      while (stacks != 0) {	// Keep doing until all illegal stacks are gone
	 stacks = 0;
	 lrcur = lrend;
	 lrprev = lrend->next;

	 while (lrprev != NULL) {
	    lrppre = lrprev->next;
	    if (lrppre == NULL) break;
	    stackheight = 0;
	    a = lrcur;
	    b = lrprev;
	    while (a->layer != b->layer) {
	       stackheight++;
	       a = b;
	       b = a->next;
	       if (b == NULL) break;
	    }
	    collide = FALSE;
	    while (stackheight > StackedContacts) {	// Illegal stack found
	       stacks++;

	       // Try to move the second contact in the path
	       cx = lrprev->x1;
	       cy = lrprev->y1;
	       cl = lrprev->layer;
	       mincost = MAXRT;
	       dl = lrppre->layer;

	       // Check all four positions around the contact for the
	       // lowest cost, and make sure the position below that
	       // is available.
	       dx = cx + 1;	// Check to the right
	       pri = &Obs2[cl][OGRID(dx, cy, cl)];
	       pflags = pri->flags;
	       cost = pri->prdata.cost;
	       if (collide && !(pflags & (PR_COST | PR_SOURCE)) &&
			(pri->prdata.net < MAXNETNUM)) {
		  pflags = 0;
		  cost = ConflictCost;
	       }
	       if (pflags & PR_COST) {
		  pflags &= ~PR_COST;
		  if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE && cost < mincost) {
	             pri2 = &Obs2[dl][OGRID(dx, cy, dl)];
		     p2flags = pri2->flags;
		     if (p2flags & PR_COST) {
			p2flags &= ~PR_COST;
		        if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri2->prdata.cost < MAXRT) {
		           mincost = cost;
		           minx = dx;
		           miny = cy;
			}
		     }
		     else if (collide && !(p2flags & (PR_COST | PR_SOURCE)) &&
				(pri2->prdata.net < MAXNETNUM) &&
				((cost + ConflictCost) < mincost)) {
			mincost = cost + ConflictCost;
			minx = dx;
			miny = dy;
		     }
		  }
	       }
	       dx = cx - 1;	// Check to the left
	       pri = &Obs2[cl][OGRID(dx, cy, cl)];
	       pflags = pri->flags;
	       cost = pri->prdata.cost;
	       if (collide && !(pflags & (PR_COST | PR_SOURCE)) &&
			(pri->prdata.net < MAXNETNUM)) {
		  pflags = 0;
		  cost = ConflictCost;
	       }
	       if (pflags & PR_COST) {
		  pflags &= ~PR_COST;
		  if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE && cost < mincost) {
	             pri2 = &Obs2[dl][OGRID(dx, cy, dl)];
		     p2flags = pri2->flags;
		     if (p2flags & PR_COST) {
			p2flags &= ~PR_COST;
		        if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri2->prdata.cost < MAXRT) {
		           mincost = cost;
		           minx = dx;
		           miny = cy;
			}
		     }
		     else if (collide && !(p2flags & (PR_COST | PR_SOURCE)) &&
				(pri2->prdata.net < MAXNETNUM) &&
				((cost + ConflictCost) < mincost)) {
			mincost = cost + ConflictCost;
			minx = dx;
			miny = dy;
		     }
		  }
	       }

	       dy = cy + 1;	// Check north
	       pri = &Obs2[cl][OGRID(cx, dy, cl)];
	       pflags = pri->flags;
	       cost = pri->prdata.cost;
	       if (collide && !(pflags & (PR_COST | PR_SOURCE)) &&
			(pri->prdata.net < MAXNETNUM)) {
		  pflags = 0;
		  cost = ConflictCost;
	       }
	       if (pflags & PR_COST) {
		  pflags &= ~PR_COST;
		  if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE && cost < mincost) {
	             pri2 = &Obs2[dl][OGRID(cx, dy, dl)];
		     p2flags = pri2->flags;
		     if (p2flags & PR_COST) {
			p2flags &= ~PR_COST;
		        if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri2->prdata.cost < MAXRT) {
		           mincost = cost;
		           minx = cx;
		           miny = dy;
			}
		     }
		     else if (collide && !(p2flags & (PR_COST | PR_SOURCE)) &&
				(pri2->prdata.net < MAXNETNUM) &&
				((cost + ConflictCost) < mincost)) {
			mincost = cost + ConflictCost;
			minx = dx;
			miny = dy;
		     }
		  }
	       }

	       dy = cy - 1;	// Check south
	       pri = &Obs2[cl][OGRID(cx, dy, cl)];
	       pflags = pri->flags;
	       cost = pri->prdata.cost;
	       if (collide && !(pflags & (PR_COST | PR_SOURCE)) &&
			(pri->prdata.net < MAXNETNUM)) {
		  pflags = 0;
		  cost = ConflictCost;
	       }
	       if (pflags & PR_COST) {
		  pflags &= ~PR_COST;
		  if (pflags & PR_PRED_DMASK != PR_PRED_NONE && cost < mincost) {
	             pri2 = &Obs2[dl][OGRID(cx, dy, dl)];
		     p2flags = pri2->flags;
		     if (p2flags & PR_COST) {
		        p2flags &= ~PR_COST;
		        if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri2->prdata.cost < MAXRT) {
		           mincost = cost;
		           minx = cx;
		           miny = dy;
			}
		     }
		     else if (collide && !(p2flags & (PR_COST | PR_SOURCE)) &&
				(pri2->prdata.net < MAXNETNUM) &&
				((cost + ConflictCost) < mincost)) {
			mincost = cost + ConflictCost;
			minx = dx;
			miny = dy;
		     }
		  }
	       }

	       // Was there an available route?  If so, modify
	       // records to route through this alternate path.  If not,
	       // then try to move the first contact instead.

	       if (mincost < MAXRT) {
	          pri = &Obs2[cl][OGRID(minx, miny, cl)];

		  newlr = (POINT)malloc(sizeof(struct point_));
		  newlr->x1 = minx;
		  newlr->y1 = miny;
		  newlr->layer = cl;

	          pri2 = &Obs2[dl][OGRID(minx, miny, dl)];

		  newlr2 = (POINT)malloc(sizeof(struct point_));
		  newlr2->x1 = minx;
		  newlr2->y1 = miny;
		  newlr2->layer = dl;

		  lrprev->next = newlr;
		  newlr->next = newlr2;

		  // Check if point at pri2 is equal to position of
		  // lrppre->next.  If so, bypass lrppre.

		  if (lrnext = lrppre->next) {
		     if (lrnext->x1 == minx && lrnext->y1 == miny &&
				lrnext->layer == dl) {
			newlr->next = lrnext;
			free(lrppre);
			free(newlr2);
			lrppre = lrnext;	// ?
		     }
		     else
		        newlr2->next = lrppre;
		  }
		  else
		     newlr2->next = lrppre;

		  break;	// Found a solution;  we're done.
	       }
	       else {

		  // If we couldn't offset lrprev position, then try
		  // offsetting lrcur.

	          cx = lrcur->x1;
	          cy = lrcur->y1;
	          cl = lrcur->layer;
	          mincost = MAXRT;
	          dl = lrprev->layer;

	          dx = cx + 1;	// Check to the right
	          pri = &Obs2[cl][OGRID(dx, cy, cl)];
	          pflags = pri->flags;
		  if (pflags & PR_COST) {
		     pflags &= ~PR_COST;
		     if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri->prdata.cost < mincost) {
	                pri2 = &Obs2[dl][OGRID(dx, cy, dl)];
		        p2flags = pri2->flags;
			if (p2flags & PR_COST) {
			   p2flags &= ~PR_COST;
		           if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
					pri2->prdata.cost < MAXRT) {
		              mincost = pri->prdata.cost;
		              minx = dx;
		              miny = cy;
			   }
		        }
		     }
	          }

	          dx = cx - 1;	// Check to the left
	          pri = &Obs2[cl][OGRID(dx, cy, cl)];
	          pflags = pri->flags;
		  if (pflags & PR_COST) {
		     pflags &= ~PR_COST;
		     if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri->prdata.cost < mincost) {
	                pri2 = &Obs2[dl][OGRID(dx, cy, dl)];
		        p2flags = pri2->flags;
			if (p2flags & PR_COST) {
			   p2flags &= ~PR_COST;
		           if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
					pri2->prdata.cost < MAXRT) {
		              mincost = pri->prdata.cost;
		              minx = dx;
		              miny = cy;
			   }
		        }
		     }
	          }

	          dy = cy + 1;	// Check north
	          pri = &Obs2[cl][OGRID(cx, dy, cl)];
	          pflags = pri->flags;
		  if (pflags & PR_COST) {
		     pflags &= ~PR_COST;
		     if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri->prdata.cost < mincost) {
	                pri2 = &Obs2[dl][OGRID(cx, dy, dl)];
		        p2flags = pri2->flags;
			if (p2flags & PR_COST) {
			   p2flags &= ~PR_COST;
		           if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
					pri2->prdata.cost < MAXRT) {
		              mincost = pri->prdata.cost;
		              minx = cx;
		              miny = dy;
			   }
		        }
		     }
	          }

	          dy = cy - 1;	// Check south
	          pri = &Obs2[cl][OGRID(cx, dy, cl)];
	          pflags = pri->flags;
		  if (pflags & PR_COST) {
		     pflags &= ~PR_COST;
		     if ((pflags & PR_PRED_DMASK) != PR_PRED_NONE &&
				pri->prdata.cost < mincost) {
	                pri2 = &Obs2[dl][OGRID(cx, dy, dl)];
		        p2flags = pri2->flags;
			if (p2flags & PR_COST) {
			   p2flags &= ~PR_COST;
		           if ((p2flags & PR_PRED_DMASK) != PR_PRED_NONE &&
					pri2->prdata.cost < MAXRT) {
		              mincost = pri->prdata.cost;
		              minx = cx;
		              miny = dy;
			   }
		        }
		     }
	          }

		  if (mincost < MAXRT) {
		     newlr = (POINT)malloc(sizeof(struct point_));
		     newlr->x1 = minx;
		     newlr->y1 = miny;
		     newlr->layer = cl;

		     newlr2 = (POINT)malloc(sizeof(struct point_));
		     newlr2->x1 = minx;
		     newlr2->y1 = miny;
		     newlr2->layer = dl;

		     // If newlr is a source or target, then make it
		     // the endpoint, because we have just moved the
		     // endpoint along the source or target, and the
		     // original endpoint position is not needed.

	             pri = &Obs2[cl][OGRID(minx, miny, cl)];
	             pri2 = &Obs2[lrcur->layer][OGRID(lrcur->x1,
					lrcur->y1, lrcur->layer)];
		     if (((pri->flags & PR_SOURCE) && (pri2->flags & PR_SOURCE)) ||
			 	((pri->flags & PR_TARGET) &&
				(pri2->flags & PR_TARGET)) && (lrcur == lrtop)) {
			lrtop = newlr;
			lrend = newlr;
			free(lrcur);
			lrcur = newlr;
		     }
		     else
		        lrcur->next = newlr;

		     newlr->next = newlr2;

		     // Check if point at pri2 is equal to position of
		     // lrprev->next.  If so, bypass lrprev.

		     if (lrppre->x1 == minx && lrppre->y1 == miny &&
				lrppre->layer == dl) {
			newlr->next = lrppre;
			free(lrprev);
			free(newlr2);
			lrprev = lrcur;
		     }
		     else
			newlr2->next = lrprev;
		
		     break;	// Found a solution;  we're done.
		  }
		  else if (stage == 0) {
		     // On the first stage, we call it an error and move
		     // on to the next net.  This is a bit conservative,
		     // but it works because failing to remove a stacked
		     // via is a rare occurrance.

		     if (Verbose > 0)
			Fprintf(stderr, "Failed to remove stacked via "
				"at grid point %d %d.\n",
				lrcur->x1, lrcur->y1);
		     stacks = 0;
		     rval = 0;
		     goto cleanup;
		  }
		  else {
		     if (collide == TRUE) {
		        Fprintf(stderr, "Failed to remove stacked via "
				"at grid point %d %d;  position may "
				"not be routable.\n",
				lrcur->x1, lrcur->y1);
			stacks = 0;
			rval = 0;
			goto cleanup;
		     }

		     // On the second stage, we will run through the
		     // search again, but allow overwriting other
		     // nets, which will be treated like other colliding
		     // nets in the regular route path search.

		     collide = TRUE;
		  }
	       }
	    }
	    lrcur = lrprev;
	    lrprev = lrppre;
	 }
      }
   }
 
   lrend = lrtop;
   lrcur = lrtop;
   lrprev = lrcur->next;
   lseg = (SEG)NULL;

   while (1) {
      seg = (SEG)malloc(sizeof(struct seg_));
      seg->next = NULL;

      seg->segtype = (lrcur->layer == lrprev->layer) ? ST_WIRE : ST_VIA;

      seg->x1 = lrcur->x1;
      seg->y1 = lrcur->y1;

      seg->layer = MIN(lrcur->layer, lrprev->layer);

      seg->x2 = lrprev->x1;
      seg->y2 = lrprev->y1;

      dx = seg->x2 - seg->x1;
      dy = seg->y2 - seg->y1;

      // segments are in order---place final segment at end of list
      if (rt->segments == NULL)
	 rt->segments = seg;
      else
	 lseg->next = seg;

      // Continue processing predecessors as long as the direction is the same,
      // so we get a single long wire segment.  This minimizes the number of
      // segments produced.  Vias have to be handled one at a time, as we make
      // no assumptions about stacked vias.

      if (seg->segtype == ST_WIRE) {
	 while ((lrnext = lrprev->next) != NULL) {
	    lrnext = lrprev->next;
	    if (((lrnext->x1 - lrprev->x1) == dx) &&
			((lrnext->y1 - lrprev->y1) == dy) &&
			(lrnext->layer == lrprev->layer)) {
	       lrcur = lrprev;
	       lrprev = lrnext;
	       seg->x2 = lrprev->x1;
	       seg->y2 = lrprev->y1;
	    }
	    else
	       break;
	 }
      }

      if (Verbose > 3) {
         Fprintf(stdout, "commit: index = %d, net = %d\n",
		Pr->prdata.net, netnum);

	 if (seg->segtype == ST_WIRE) {
            Fprintf(stdout, "commit: wire layer %d, (%d,%d) to (%d,%d)\n",
		seg->layer, seg->x1, seg->y1, seg->x2, seg->y2);
	 }
	 else {
            Fprintf(stdout, "commit: via %d to %d\n", seg->layer,
			seg->layer + 1);
	 }
	 Flush(stdout);
      }

      // now fill in the Obs structure with this route....

      lay2 = (seg->segtype & ST_VIA) ? seg->layer + 1 : seg->layer;

      netobs1 = Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)];
      netobs2 = Obs[lay2][OGRID(seg->x2, seg->y2, lay2)];

      dir1 = netobs1 & PINOBSTRUCTMASK;
      dir2 = netobs2 & PINOBSTRUCTMASK;

      netobs1 &= NETNUM_MASK;
      netobs2 &= NETNUM_MASK;

      netnum |= ROUTED_NET;

      // If Obs shows this position as an obstruction, then this was a port with
      // no taps in reach of a grid point.  This will be dealt with by moving
      // the via off-grid and onto the port position in emit_routes().

      if (stage == (u_char)0) {
	 writeback_segment(seg, netnum);

         if (first && dir1) {
	    first = (u_char)0;
         }
	 else if (first && dir2 && (seg->segtype & ST_VIA) && lrprev &&
			(lrprev->layer != lay2)) {
	    // This also applies to vias at the beginning of a route
	    // if the path goes down instead of up (can happen on pins,
	    // in particular)
	    Obs[lay2][OGRID(seg->x1, seg->y1, lay2)] |= dir2;
	 }
      }

      // Keep stub information on obstructions that have been routed
      // over, so that in the rip-up stage, we can return them to obstructions.

      Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)] |= dir1;
      Obs[lay2][OGRID(seg->x2, seg->y2, lay2)] |= dir2;

      // An offset route end on the previous segment, if it is a via, needs
      // to carry over to this one, if it is a wire route.

      if (lseg && ((lseg->segtype & (ST_VIA | ST_OFFSET_END)) ==
			(ST_VIA | ST_OFFSET_END)))
	 if (seg->segtype != ST_VIA)
	    seg->segtype |= ST_OFFSET_START;

      // Check if the route ends are offset.  If so, add flags.  The segment
      // entries are integer grid points, so offsets need to be made when
      // the location is output.

      if (dir1 & OFFSET_TAP) {
	 seg->segtype |= ST_OFFSET_START;

	 // An offset on a via needs to be applied to the previous route
	 // segment as well, if that route is a wire.

	 if (lseg && (seg->segtype & ST_VIA) && !(lseg->segtype & ST_VIA))
	    lseg->segtype |= ST_OFFSET_END;
      }

      if (dir2 & OFFSET_TAP) seg->segtype |= ST_OFFSET_END;

      lrend = lrcur;		// Save the last route position
      lrend->x1 = lrcur->x1;
      lrend->y1 = lrcur->y1;
      lrend->layer = lrcur->layer;

      lrcur = lrprev;		// Move to the next route position
      lrcur->x1 = seg->x2;
      lrcur->y1 = seg->y2;
      lrprev = lrcur->next;

      if (lrprev == NULL) {

         if (dir2 && (stage == (u_char)0)) {
	    Obs[lay2][OGRID(seg->x2, seg->y2, lay2)] |= dir2;
         }
	 else if (dir1 && (seg->segtype & ST_VIA)) {
	    // This also applies to vias at the end of a route
	    Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)] |= dir1;
	 }

	 // Before returning, set *ept to the endpoint
	 // position.  This is for diagnostic purposes only.
	 ept->x = lrend->x1;
	 ept->y = lrend->y1;
	 ept->lay = lrend->layer;

	 // Clean up allocated memory for the route. . .
	 while (lrtop != NULL) {
	    lrnext = lrtop->next;
	    free(lrtop);
	    lrtop = lrnext;
	 }
	 return rval;	// Success
      }
      lseg = seg;	// Move to next segment position
   }

cleanup:

   while (lrtop != NULL) {
      lrnext = lrtop->next;
      free(lrtop);
      lrtop = lrnext;
   }
   return 0;

} /* commit_proute() */

/*------------------------------------------------------*/
/* writeback_route() ---				*/
/*							*/
/*   This routine is the last part of the routine	*/
/*   above.  It copies the net defined by the segments	*/
/*   in the route structure "rt" into the Obs array.	*/
/*   This is used only for stage 2, when the writeback	*/
/*   is not done by commit_proute because we want to	*/
/*   rip up nets first, and also done prior to routing	*/
/*   for any pre-defined net routes.			*/
/*------------------------------------------------------*/

int writeback_route(ROUTE rt)
{
   SEG seg;
   int  i, lay2;
   u_int netnum, dir1, dir2;
   u_char first = (u_char)1;

   netnum = rt->netnum | ROUTED_NET;
   for (seg = rt->segments; seg; seg = seg->next) {

      /* Save stub route information at segment ends. 		*/
      /* NOTE:  Where segment end is a via, make sure we are	*/
      /* placing the segment end on the right metal layer!	*/

      lay2 = (seg->segtype & ST_VIA) ? seg->layer + 1 : seg->layer;

      dir1 = Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)] & PINOBSTRUCTMASK;
      dir2 = Obs[lay2][OGRID(seg->x2, seg->y2, lay2)] & PINOBSTRUCTMASK;

      writeback_segment(seg, netnum);

      if (first) {
	 first = (u_char)0;
	 if (dir1)
	    Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)] |= dir1;
	 else if (dir2)
	    Obs[lay2][OGRID(seg->x2, seg->y2, lay2)] |= dir2;
      }
      else if (!seg->next) {
	 if (dir1)
	    Obs[seg->layer][OGRID(seg->x1, seg->y1, seg->layer)] |= dir1;
	 else if (dir2)
	    Obs[lay2][OGRID(seg->x2, seg->y2, lay2)] |= dir2;
      }
   }
   return TRUE;
}

/*------------------------------------------------------*/
/* Writeback all routes belonging to a net		*/
/*------------------------------------------------------*/

int writeback_all_routes(NET net)
{
   ROUTE rt;
   int result = TRUE;

   for (rt = net->routes; rt; rt = rt->next) {
      if (writeback_route(rt) == FALSE)
	 result = FALSE;
   }
   return result;
}

/* end of maze.c */
