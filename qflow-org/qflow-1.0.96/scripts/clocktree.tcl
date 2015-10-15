#!/bin/tclsh
#---------------------------------------------------------------------------
# clocktree.tcl ---
#
# Read a GrayWolf .pin cell placement output file.  Find nets exceeding
# a given fanout (default 16).  Break these nets up into regions such
# that no part exceeds the maximum fanout.  Use a clustering algorithm to
# assign each gate attached to the net to a region.  Rewrite the netlist.
#
#---------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

if {$argc < 5 || $argc > 7} {
   puts stdout "Usage:  clocktree.tcl <module_name>"
   puts stdout "	<synth_dir> <layout_dir> <lef_file>"
   puts stdout "	<buffer_name> \[<max_fanout>\]"
   exit 0
}

set maxfanout 16

puts stdout "Running clocktree.tcl"

set modulename  [lindex $argv 0]
set synthdir	[lindex $argv 1]
set layoutdir	[lindex $argv 2]
set leffile	[lindex $argv 3]
set bufname	[lindex $argv 4]

set pinfile ${layoutdir}/${modulename}.pin
set parfile ${layoutdir}/${modulename}.par
set bliffile ${synthdir}/${modulename}.blif
set outfile ${synthdir}/${modulename}_tmp.blif
set ignorefile ${synthdir}/${modulename}_nofanout

if {$argc == 6} {set maxfanout [lindex $argv 5]}

#---------------------------------------------------------------------------

if [catch {open ${pinfile} r} fpin] {
   puts stderr "Error: can't open file $pinfile for input"
   exit 2
}

if [catch {open ${bliffile} r} fnet] {
   puts stderr "Error: can't open file $bliffile for input"
   exit 2
}

if [catch {open ${leffile} r} flef] {
   puts stderr "Error: can't open file $leffile for input"
   exit 2
}

set ignorenets {}
if ![catch {open ${ignorefile} r} fign] {
   while {[gets $fign line] >= 0} {
      lappend ignorenets $line
   }
   close $fign
   set ignorenets [lsort -uniq $ignorenets]
   puts stdout "nets to be ignored: $ignorenets"
}

if ![catch {open ${parfile} r} fpar] {
   # Not an error to not have a .par file, but if we do, read
   # it to see if there is a random.seed value, and use it to
   # see the Tcl random number generator. 
   while {[gets $fpar line] >= 0} {
      if [regexp {random\.seed[ \t]*:[ \t]*([0-9]+)} $line lmatch seedval] {
	 srand $seedval
	 break
      }
   }
   close $fpar
}

#------------------------------------------------------------------
# Pick up list of nets not to break up into trees (e.g., vdd, gnd)
#------------------------------------------------------------------

#------------------------------------------------------------------------------------
# LEF file reading routines
#------------------------------------------------------------------------------------

#----------------------------------------------------------------
# Skip port information for a macro pin from the LEF MACRO block
#----------------------------------------------------------------

proc parse_port {pinname macroname leffile} {
   global $macroname units

   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*END[ \t]*$} $line lmatch] { break }
   }
}

#----------------------------------------------------------------
# Parse pin information from the LEF MACRO block
#----------------------------------------------------------------

proc parse_pin {pinname macroname leffile} {
   global $macroname

   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*PORT} $line lmatch] {
         parse_port $pinname $macroname $leffile
      } elseif [regexp {[ \t]*DIRECTION[ \t]+([^ \t]+)[ \t]*;} $line lmatch porttype] {
         # puts stdout "Port $pinname direction \"$porttype\""
	 if {"$porttype" == "OUTPUT"} {
	    if [catch {set ${macroname}(output)}] {set ${macroname}(output) {}}
	    lappend ${macroname}(output) ${pinname}
	 } elseif {"$porttype" == "INPUT"} {
	    if [catch {set ${macroname}(input)}] {set ${macroname}(input) {}}
	    lappend ${macroname}(input) ${pinname}
	 }
      } elseif [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch pintest] {
         if {"$pintest" == "$pinname"} {
            break
         } else {
            puts stdout "Unexpected END statement $line while parsing pin $pinname"
         }
      }
   }
}

#----------------------------------------------------------------
# Read through a section that we don't care about.
#----------------------------------------------------------------

proc skip_section {leffile sectionname} {
   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch sectiontest] {
         if {"$sectiontest" != "$sectionname"} {
            puts -nonewline stderr "Unexpected END statement $line "
            puts stderr "while reading section $sectionname"
         }
         break
      }
   }
}

#----------------------------------------------------------------
# Parse the macro contents of the LEF file and retain the information
# about cell names and their input and output pins
#----------------------------------------------------------------

proc parse_macro {leffile macroname} {
   global $macroname units

   # puts stderr "Parsing macro $macroname:  Ports are:"
   while {[gets $leffile line] >= 0} {
      if [regexp {[ \t]*PIN[ \t]+(.+)[ \t]*$} $line lmatch pinname] {
         parse_pin $pinname $macroname $leffile
      } elseif [regexp {[ \t]*END[ \t]+(.+)[ \t]*$} $line lmatch macrotest] {
         if {"$macrotest" == "$macroname"} {
            break
         } else {
            puts stderr "Unexpected END statement $line while reading macro $macroname"
         }
      }
   }
}

#------------------------------------------------------------------------------------
# Parse the .lef file, collect a list of macros and the output pins for each.
# For the macro that matches the given buffer name, get the input pin, too.
#------------------------------------------------------------------------------------

while {[gets $flef line] >= 0} {
   if [regexp {[ \t]*MACRO[ \t]+(.+)[ \t]*$} $line lmatch macroname] {
      # Parse the "macro" statement
      parse_macro $flef $macroname
   } elseif [regexp {[ \t]*LAYER[ \t]+(.+)[ \t]*$} $line lmatch layername] {
      skip_section $flef $layername
   } elseif [regexp {[ \t]*VIA[ \t]+(.+)[ \t]*$} $line lmatch layername] {
      if [regexp {(.+)[ \t]+DEFAULT} $layername lmatch viadefname] {
         skip_section $flef $viadefname
      } else {
         skip_section $flef $layername
      }
   } elseif [regexp {[ \t]*VIARULE[ \t]+([^ \t]+)} $line lmatch viarulename] {
      skip_section $flef $viarulename
   } elseif [regexp {[ \t]*SITE[ \t]+(.+)[ \t]*$} $line lmatch sitename] {
      skip_section $flef $sitename
   } elseif [regexp {[ \t]*UNITS[ \t]*$} $line lmatch] {
      skip_section $flef UNITS
   } elseif [regexp {[ \t]*SPACING[ \t]*$} $line lmatch] {
      skip_section $flef SPACING
   } elseif [regexp {[ \t]*END[ \t]+LIBRARY[ \t]*$} $line lmatch] {
      break
   } elseif [regexp {^[ \t]*#} $line lmatch] {
      # Comment line, ignore.
   } elseif ![regexp {^[ \t]*$} $line lmatch] {
      # Other things we don't care about
      set matches 0
      if [regexp {[ \t]*NAMESCASESENSITIVE} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*VERSION} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*BUSBITCHARS} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*DIVIDERCHAR} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*USEMINSPACING} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*CLEARANCEMEASURE} $line lmatch] {
         incr matches
      } elseif [regexp {[ \t]*MANUFACTURINGGRID} $line lmatch] {
         incr matches
      } else {
	 # Just ignore additional lines. . .
         # puts stderr "Unexpected input in LEF file:  Only macro defs were expected!"
         # puts stdout "Line is: $line"
      }
   }
}

# Check that we have an input and output pin for the buffer cell

if [catch {set ${bufname}(input)}] {
   puts stdout "Buffer cell ${bufname} not in LEF file or has no input pin."
   exit 2
}
if [catch {set ${bufname}(output)}] {
   puts stdout "Buffer cell ${bufname} has no output pin."
   exit 2
}
if {[llength ${bufname}(input)] != 1} {
   puts stdout "Buffer cell ${bufname} has more than one input pin!"
   exit 2
} else {
   set bufinpin [eval [subst {lindex \$${bufname}(input) 0}]]
}
if {[llength ${bufname}(output)] != 1} {
   puts stdout "Buffer cell ${bufname} has more than one output pin!"
   exit 2
} else {
   set bufoutpin [eval [subst {lindex \$${bufname}(output) 0}]]
}

#------------------------------------------------------------------------------------
# Parse the .pin file, and count the total number of connections on each net
#------------------------------------------------------------------------------------

set nets [dict create]
set splitnets [dict create]
set hiernets [dict create]
set outputs [dict create]

while {[gets $fpin line] >= 0} {
   # Each line in the file is:
   #     <netname> <subnet> <macro> <pinname> <x> <y> <row> <orient> <layer>
   regexp {^([^ ]+)[ \t]+(\d+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+[^ ]+[ \t]+[^ ]+[ \t]+([^ ]+)} \
		$line lmatch netname subnet instance pinname px py layer

   if {([string first twfeed ${instance}] == -1) &&
		([string first twfeed ${pinname}] == -1)} { 
      if {"$instance" != "PSEUDO_CELL"} {
	 # If fill cells were merged into the instance to reduce
	 # density, then strip the fill cell information
	 if {[regexp {.+\.[0-9]+X?[0-9]?\.(.+)_[0-9]+} $instance lmatch cellname] == 1} {
	    regexp {.+\.[0-9]+X?[0-9]?\.(.+_[0-9]+)} $instance lmatch instance
	 } else {
            # Get the cell name from the (plain) instance name
            regexp {(.*)_[0-9]+} $instance lmatch cellname
	 }

	 if {[string first twpin_ $instance] == 0} {
	    dict set outputs $netname [list $instance $pinname]
	 } elseif {![catch {set ${cellname}(output)}]} {
	    if {[eval [subst {lsearch \$${cellname}(output) $pinname}]] >= 0} {
	       dict set outputs $netname [list $instance $pinname]
	    } elseif {![catch {set ${cellname}(input)}]} {
	       if {[eval [subst {lsearch \$${cellname}(input) $pinname}]] >= 0} {
	          set ilist [list $pinname $px $py -1 {}]
	          if [catch {set inst [dict get $nets $netname]}] {
	             set inst [dict create]
	             dict set nets $netname $inst
	          }
	          dict set nets $netname $instance $ilist
	       }
	    }
	 }
      }
   }
}

close $fpin

puts stdout "Number of nets:  [dict size $nets]"

dict for {netname inst} $nets {
   set netsize [dict size $inst]
   if {$netsize > $maxfanout} {
      if {[lsearch $ignorenets $netname] >= 0} {continue}
   }

   set hierlevel 0
   while {$netsize > $maxfanout} {
      puts stdout "Net $netname fanout $netsize exceeds maximum of $maxfanout"

      set subnets [/ $netsize $maxfanout]
      set remain [% $netsize $maxfanout]
      if {$remain > 0} {incr subnets}
      puts stdout "Breaking net into $subnets subnets" 

      # Clustering algorithm (Voronoi tessalation)
      # Modified 8/19/2013 to ensure a maximum number of entries in a
      # single cluster.

      set centers {}
      set idxused {}
      for {set i 0} {$i < $subnets} {incr i} {
	 set idx [int [* [rand] $netsize]]
	 while {[lsearch $idxused $idx] >= 0} {
	    set idx [int [* [rand] $netsize]]
	 }
	 set j 0
         dict for {instname ilist} $inst {
	    if {$j == $idx} {
	       lappend centers [lrange $ilist 1 2]
	       lappend idxused $idx
	       break
	    }
	    incr j
	 }
      }

      # 10 iterations is sort of arbitrary. . . algorithm usually
      # doesn't change after 7 or 8 cycles.

      for {set i 0} {$i < 10} {incr i} {
	 # For each terminal in the net, find the closest centroid
	 # but keep the list of calculated distances to all centroids
	 # in case the one at minimum distance is overcrowded.

	 set members [lrepeat $subnets 0]
         dict for {instname ilist} $inst {
	    set px [lindex $ilist 1]
	    set py [lindex $ilist 2]
	    set dists {}
	    foreach cpair $centers {
	       set difx [- $px [lindex $cpair 0]]
	       set dify [- $py [lindex $cpair 1]]
	       set sqdist [+ [* $difx $difx] [* $dify $dify]]
	       lappend dists $sqdist
	    }
	    set cidx [lindex [lsort -indices $dists] 0]
	    set members [lreplace $members $cidx $cidx [+ 1 [lindex $members $cidx]]]
	        
	    dict set inst $instname [lreplace $ilist 3 4 $cidx $dists]
	 }

	 # Restrict membership reduction to the last 5 passes, so that the
	 # grouping is already mostly correct

	 if {$i >= 5} {

	    # For any groups whose membership exceeds maxfanout, find the members
	    # that are closest to another group with membership < maxfanout
	    # and reassign them to that group.

	    for {set j 0} {$j < $subnets} {incr j} {
	       if {[lindex $members $j] > $maxfanout} {

	          set mintest {}	 ;# Create a new list
	          dict for {instname ilist} $inst {
		     # Find which member has the smallest next-to-minimum distance
		     if {[lindex $ilist 3] == $j} {
		        set dists [lindex $ilist 4]
		        for {set k 0} {$k < $subnets} {incr k} {
			   if {$k != $j} {
			      lappend mintest [list [lindex $dists $k] $k $instname]
			   }
		        }
		     }
	          }
	       }
	       while {[lindex $members $j] > $maxfanout} {
	          set tidx [lindex [lsort -indices -index 0 $mintest] 0]
	          set mentry [lindex $mintest $tidx]

	          # puts -nonewline stdout "Reducing membership in group $j"
	          # puts stdout " from [lindex $members $j] to 16"
	          # puts stdout "tidx = $tidx"
	          # puts stdout "mentry = $mentry"
	          # flush stdout

	          set cidx [lindex $mentry 1]
	          set mininst [lindex $mentry 2]
	          if {[lindex $members $cidx] < $maxfanout} {
		     # puts -nonewline stdout "Group $cidx has [lindex $members $cidx]"
		     # puts stdout " members;  room to grow!"
		     set ilist [dict get $inst $mininst]
		     if {[lindex $ilist 3] != $j} {
		        puts -nonewline stdout "ERROR:  Entry has group "
		        puts stdout "[lindex $ilist 3], NOT $j!"
	 	     }
	             dict set inst $mininst [lreplace $ilist 3 3 $cidx]
		     set members [lreplace $members $j $j [- [lindex $members $j] 1]]
		     set members [lreplace $members $cidx $cidx \
				[+ [lindex $members $cidx] 1]]

		     # Make sure that other entries in $mintest with $mininst are
		     # disabled from the search.
		     foreach entry [lsearch -all -exact -index 2 $mintest $mininst] {
		        set mintest [lreplace $mintest $entry $entry [list [** 2 63] 0 0]]
		     }
	          }
	          set mintest [lreplace $mintest $tidx $tidx [list [** 2 63] 0 0]]
	       }
	       # puts stdout "Group $j done."

	       # puts stdout "Checking:"
	       # puts stdout "Member count says: [lindex $members $j]"
	    }

 	    # puts stdout "Full cross-check:"
	    # for {set j 0} {$j < $subnets} {incr j} {
	    #    puts stdout "Member count 1 for group $j is: [lindex $members $j]"
	    #    set mcount 0
            #    dict for {instname ilist} $inst {
	    #       if {[lindex $ilist 3] == $j} {incr mcount}
	    #    }
	    #    puts stdout "Member count 2 for group $j is: $mcount"
	    # }

	 }	;# i >= 5

	 # Recompute the centroids
	 set temp {}
         for {set j 0} {$j < $subnets} {incr j} {
	    lappend temp [list 0 0 0]
	 }
         dict for {instname ilist} $inst {
	    set cidx [lindex $ilist 3]
	    set cvals [lindex $temp $cidx]
	    set ctot [lindex $cvals 2]
	    incr ctot

	    set px [+ [lindex $cvals 0] [lindex $ilist 1]]
	    set py [+ [lindex $cvals 1] [lindex $ilist 2]]

	    set temp [lreplace $temp $cidx $cidx [list $px $py $ctot]]
	 }
         for {set j 0} {$j < $subnets} {incr j} {
	    set cvals [lindex $temp $j]
	    if {[lindex $cvals 2] == 0} {set cvals [lreplace $cvals 2 2 1]}
	    set cx [/ [lindex $cvals 0] [lindex $cvals 2]]
	    set cy [/ [lindex $cvals 1] [lindex $cvals 2]]
	    set centers [lreplace $centers $j $j [list $cx $cy]]
	 }
      }

      # (for now) print out the clusters

      if {[catch {set outputnet [dict get $outputs $netname]}]} {
	 puts stdout "Net is a module input (not connected to any output)"
      } else {
         puts stdout "Net output [lindex $outputnet 0]/[lindex $outputnet 1]"
      }

      set clusters [dict create]
      for {set i 0} {$i < $subnets} {incr i} {
	 puts stdout "Cluster $i:"
	 set check 0
         dict for {instname ilist} $inst {
	    set cidx [lindex $ilist 3]
	    if {$cidx == $i} {
	       set pinname [lindex $ilist 0]
	       puts stdout "${instname}/${pinname}"
	       dict set clusters "${instname}/${pinname}" $i
	       incr check
	       if {$check > $maxfanout} {
		  puts stdout "ERROR: Fanout exceeds maximum!  Cannot happen!"
	       }
	    }
	 }
      }
      puts stdout ""

      if {$hierlevel == 0} {

	 # Create a record in the "splitnets" dictionary

         set clustinfo [list $clusters $subnets]
         dict set splitnets $netname $clustinfo
      } else {

	 # Create a record in the "hiernets" dictionary

         set clustinfo [list $clusters $subnets]
         dict set hiernets ${netname}_hier${hierlevel} $clustinfo
      }

      # The "tree" is only 1 deep, so if the number of clusters exceeds
      # maxfanout, then set a return flag so that the calling script
      # will know that it needs to run clocktree again.  Clocktree can
      # be run as many times as needed, and will keep making the trees
      # nest one level deeper on each run, as needed.

      set netsize $subnets
      if {$netsize > $maxfanout} {

	 # build up a new "inst" dictionary to include one buffer cell per
	 # cluster, positioned at the cluster center.  Buffers are local
	 # to this dictionary and don't have to have unique instance names

	 set inst [dict create]
         for {set i 0} {$i < $netsize} {incr i} {
	    set cctr [lindex $centers $i]
	    set px [lindex $cctr 0]
	    set py [lindex $cctr 1]
            set ilist [list $bufinpin $px $py -1 {}]
	    dict set inst ${bufname}_${i} $ilist
	 }
	 incr hierlevel
      }
   }
}

#-----------------------------------------------------------------
# Read the blif file and write the revised version
#-----------------------------------------------------------------

if [catch {open ${outfile} w} fout] {
   puts stderr "Error: can't open file $topdir/$synthdir/$outfile for output"
   exit 2
}

# We use the same method as blif2cel.tcl to get the instance numbers

set mode none
set counts [dict create]
while {[gets $fnet line] >= 0} {
   if [regexp {^[ \t]*\.gate[ \t]+([^ \t]+)[ \t]+(.*)$} $line lmatch macroname rest] {
      if {"$mode" == "pins"} {

	 # Dump the buffers in the tree first
	 dict for {netname clusters} $splitnets {

	    # Check if there is a hierarchy to this buffer tree
	    set i 1
	    while {![catch {dict get $hiernets ${netname}_hier${i}}]} {
	       incr i
	    }
	    incr i -1
	    if {$i == 0} {

	       # Simple case:  one level of hierarchy, all the buffers have their
	       # inputs connected to the original net "netname".  Output net name
	       # is encoded as netname + "_buf" + cluster number (0 to maxfanout-1)

	       set subnets [lindex $clusters 1]
	       for {set i 0} {$i < $subnets} {incr i} {

	          puts -nonewline $fout " .gate $bufname ${bufinpin}=${netname}"

	          set outname ${netname}_buf${i}
	          if {[string first \[ $outname] >= 0} {
		     # recast "[X]_bufN" as "[X_bufN]" so as not to hose Bdnet2Verilog
		     set outname "[string map {\] ""} $outname]\]"
	          }
	          if {[string first < $outname] >= 0} {
		     # recast "sig<X>_bufN" as "sig_X_bufN" so as not to
		     # create problems downstream.
		     set outname "[string map {< _ > ""} $outname]"
	          }
	          puts $fout " ${bufoutpin}=${outname}"
	       }
	    } else {
	       # Hierarchy top is "netname", every other net gets modified to
	       # "netname" + "_hier" + hierarchy level + "_buf" + cluster number

	       set last -1
	       while {$i > 0} {

		  if {$last == -1} {
		     set hierlastname ${netname}
		  } else {
	             set hierlist [dict get $hiernets ${netname}_hier${last}]
	             set hiersubnets [lindex $hierlist 1]
	             set lastclust   [lindex $hierlist 0]
		  }

	          set hiernetname ${netname}_hier${i}
	          set hierclust [dict get $hiernets ${netname}_hier${i}]
	          set subnets [lindex $hierclust 1]
	          for {set j 0} {$j < $subnets} {incr j} {

		     if {$last != -1} {
		        set hierlevel [dict get $lastclust ${bufname}_${j}/${bufinpin}]
		        set hierlastname ${netname}_hier${last}_buf${hierlevel}
		     }
	             puts -nonewline $fout " .gate $bufname ${bufinpin}=${hierlastname}"

	             set outname ${hiernetname}_buf${j}
	             if {[string first \[ $outname] >= 0} {
		        # recast "[X]_bufN" as "[X_bufN]" so as not to hose Bdnet2Verilog
		        set outname "[string map {\] ""} $outname]\]"
	             }
	             if {[string first < $outname] >= 0} {
		        # recast "sig<X>_bufN" as "sig_X_bufN" so as not to
		        # create problems downstream.
		        set outname "[string map {< _ > ""} $outname]"
	             }
	             puts $fout " ${bufoutpin}=${outname}"
	          }
		  set last $i
		  incr i -1
	       }

	       # Last hierarchy level

	       set cluster [dict get $splitnets ${netname}]
	       set subnets [lindex $cluster 1]
	       for {set j 0} {$j < $subnets} {incr j} {
		  # Pick up which net is the input to this gate from hiernets
	          set hierlist [dict get $hiernets ${netname}_hier1]

	          set hiersubnets [lindex $hierlist 1]
	          set hierclust   [lindex $hierlist 0]
		  set hierlevel [dict get $hierclust ${bufname}_${j}/${bufinpin}]
	          puts -nonewline $fout " .gate $bufname ${bufinpin}=${netname}_hier1_buf${hierlevel}"

	          set outname ${netname}_buf${j}
	          if {[string first \[ $outname] >= 0} {
		     # recast "[X]_bufN" as "[X_bufN]" so as not to hose Bdnet2Verilog
		     set outname "[string map {\] ""} $outname]\]"
	          }
	          if {[string first < $outname] >= 0} {
		     # recast "sig<X>_bufN" as "sig_X_bufN" so as not to
		     # create problems downstream.
		     set outname "[string map {< _ > ""} $outname]"
	          }
	          puts $fout " ${bufoutpin}=${outname}"
	       }
	    }
	 }
      }
      set mode $macroname
      
      if {[catch {set j [dict get $counts $mode]}]} {set j 1}
      set cellinst "${mode}_$j"
      dict set counts $mode [+ 1 $j]

      puts -nonewline $fout " .gate $mode"
      foreach pinsig $rest {
	 if [regexp {([^ \t=]+)=([^ \t]+)} $pinsig lmatch pinname netname] {
	    if {![catch {set clusters [dict get $splitnets $netname]}]} {
	       # puts stdout "Found net $netname on instance $cellinst"
	       set cluster [lindex $clusters 0]
	       if {![catch {set subidx [dict get $cluster ${cellinst}/${pinname}]}]} {
	          # puts stdout "${cellinst}/${pinname} = ${netname}_buf${subidx}"
	          set netname "${netname}_buf${subidx}"
	          if {[string first \[ $netname] >= 0} {
		     # recast "[X]_bufN" as "[X_bufN]" so as not to hose Bdnet2Verilog
		     set netname "[string map {\] ""} $netname]\]"
	          }
	          if {[string first < $netname] >= 0} {
		     # recast "sig<X>_bufN" as "sig_X_bufN" so as not to
		     # create problems downstream.
		     set netname "[string map {< _ > ""} $netname]"
	          }
	          puts -nonewline $fout " ${pinname}=${netname}"
	       } else {
	          # puts stdout "${cellinst}/${pinname} != ${netname}_buf?"
	          puts -nonewline $fout " $pinsig"
	       }
	    } else {
	       puts -nonewline $fout " $pinsig"
	    }
	 } else {
	    puts -nonewline $fout " $pinsig"
	 }
      }
      puts $fout ""

   } elseif [regexp {^[ \t]*\.inputs} $line lmatch] {
      puts $fout $line
      set mode "pins"
   } elseif [regexp {^[ \t]*\.outputs} $line lmatch] {
      puts $fout $line
      set mode "pins"
   } elseif [regexp {^[ \t]*\.model[ \t]+([^ \t]+)} $line lmatch cellverify] {
      puts $fout $line
      if {"$modulename" != "$cellverify"} {
         puts -nonewline stderr "WARNING:  model name ${cellverify} does not"
         puts stderr " match filename ${modulename}!"
      }
   } elseif [regexp {^[ \t]*\.end} $line lmatch] {
      puts $fout $line
      break
   } elseif {"$mode" == "pins"} {
      puts $fout $line
   }
}

close $fnet
close $fout

#-----------------------------------------------------------------
puts stdout "Done with clocktree.tcl"
#-----------------------------------------------------------------

