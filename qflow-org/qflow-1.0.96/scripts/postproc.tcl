#!/bin/tclsh
#-------------------------------------------------------------------------
# postproc --- post-process a mapped .blif file with the contents of the
# "init" file produced by "vpreproc".  Each signal from the "init"
# file is tracked down as the output to a flop, and that flop is replaced
# by a set or reset flop accordingly.  Information about cells to use
# and their pins, etc., are picked up from "variables_file".
#
# This script also handles some aspects of output produced by Odin-II
# and ABC.  It transforms indexes in the form "~index" to "<index>",
# removes "top^" from top-level signal names, and for hieararchical
# names in the form "module+instance", removes the module name (after
# using it to track down any reset signals).
#
# The "init_file" name passed to the script is the top-level init file.
# By taking the root of this name, the file searches <name>.dep for
# dependencies, and proceeds to pull in additional initialization files
# for additional modules used.
#
# Note that this file handles mapped blif files ONLY.  The only statements
# allowed in the file are ".model", ".inputs", ".outputs", ".latch",
# ".gate", and ".end".  The line ".default_input_arrival", if present,
# is ignored.  In the output file, all ".latch" statements are replaced
# with ".gate" statements for the specific flop gate.  The ".subckt"
# statement is equivalent to ".gate".
#
#-------------------------------------------------------------------------
# Written by Tim Edwards October 8-9, 2013
# Open Circuit Design
#-------------------------------------------------------------------------

if {$argc < 3} {
   puts stderr \
	"Usage:  postproc.tcl mapped_blif_file root_modname variables_file"
   exit 1
}

puts stdout "Postproc register initialization handling"

set mbliffile [lindex $argv 0]
set cellname [file rootname $mbliffile]
if {"$cellname" == "$mbliffile"} {
   set mbliffile ${cellname}.blif
}

set outfile ${cellname}_tmp.blif
set rootname [lindex $argv 1]
set varsfile [lindex $argv 2]

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $mbliffile r} bnet] {
   puts stderr "Error: can't open file $mbliffile for reading!"
   exit 1
}

if [catch {open $varsfile r} vfd] {
   puts stderr "Error: can't open file $varsfile for reading!"
   exit 1
}

if [catch {open $outfile w} onet] {
   puts stderr "Error: can't open file $outfile for writing!"
   exit 1
}

#-------------------------------------------------------------
# The variables file is a UNIX tcsh script, but it can be
# processed like a Tcl script if we substitute space for '='
# in the "set" commands.  Then all the variables are in Tcl
# variable space.
#-------------------------------------------------------------

while {[gets $vfd line] >= 0} {
   set tcmd [string map {= \ } $line]
   eval $tcmd
}

#-------------------------------------------------------------
# Read the file of dependencies, and follow the dependencies
# through all transformations.  Build up a list of instances
# (not used, problably not needed), and a bunch of array
# variables, in which each instance maps the connections to
# its pins from the module that calls it.
#-------------------------------------------------------------

proc readresets {prefix modname} {
   global flopsigs
   global floptypes
   global flopresetnet
   global resetlist
   global synthtool
   global $prefix

   # puts stdout "readresets:  modname prefix is $prefix modname is $modname"

   set depfile ${modname}.dep
   if [catch {open $depfile r} dfd] {
      puts stderr "Error: can't open file $depfile for reading!"
   } else {
      set newinst ""
      while {[gets $dfd line] >= 0} {
         set eidx [string first = $line]
         if {$eidx == -1} {
	    if {$line == ""} {
	       # End of instance information --- now descend into instance
	       readresets $newprefix $newmodname
	    } else {
	       set newmodname $line
	       lappend newlist $newmodname
	    }
         } else {
	    if {[string first == $line] == -1} {
	       set pinname [string range $line 0 $eidx-1]
	       set pinvalue [string range $line $eidx+1 end]
	       if [catch {set ${newprefix}($pinname) [subst \${${prefix}($pinvalue)}]}] {
	          set ${newprefix}($pinname) ${prefix}^$pinvalue
	       }
	       set temp [subst \${${newprefix}($pinname)}]
	       # puts stdout "  record pin: ${newprefix}($pinname) $temp"

	    } else {
	       set newinst [lindex $line 2]
	       lappend instlist $newinst
	       set newprefix ${prefix}.${newmodname}+${newinst}
	       global $newprefix
	    }
         }
      }
      close $dfd
   }

   # Now that all instances have been descended into and parsed,
   # handle the reset assignments for this module

   set initfile ${modname}.init
   if [catch {open $initfile r} ifd] {
      puts stderr "Error: can't open file $depfile for reading!"
      return
   }
   while {[gets $ifd line] >= 0} {
      if [regexp {^([^ \t]+)[ \t]+([^ \t]+)[ \t]+[^ \t]+} $line lmatch resetnet resettype] {
	 if {[string first ~ $resetnet] == 0} {
	    set rinvert 1
	    set resetnet [string range $resetnet 1 end]
	 } else {
	    set rinvert 0
	 }
	 # Find the name of the reset net at the top of the hierarchy
	 if {$resettype == "input"} {
	    # Value of reset net passed from above
	    if [catch {set resetnode [subst \${${prefix}($resetnet)}]}] {
	       set resetnode ${prefix}^${resetnet}
	    }
	 } else {
	    set resetnode ${prefix}^${resetnet}
	 }
	 if {$rinvert == 1} {
	    set resetnode not_$resetnode
 	 }
         lappend resetlist $resetnode

         # puts stdout "record reset: $resetnode"

      } elseif [regexp {^([^ \t]+)[ \t]+([^ \t]+)} $line lmatch signal initcond] {
	 # Flop output is always a local node name, by Odin-II convention
         lappend flopsigs ${prefix}^${signal}_FF_NODE
	 if {$initcond == "0" || $initcond == "1"} {
            lappend floptypes $initcond
	 } else {
            lappend floptypes [subst \$${prefix}($initcond)]
	 }
         lappend flopresetnet $resetnode

         # puts stdout "record flop: Q=${prefix}^${signal}_FF_NODE"
      }

   }
   close $ifd
}

#-------------------------------------------------------------
# Read the file of initialization signals and their values
# for the top-level module
#-------------------------------------------------------------

set flopsigs {}
set floptypes {}
set flopresetnet {}
set resetlist {}

readresets top $rootname

#-------------------------------------------------------------
# Now post-process the blif file
# The main thing to remember is that internal signals will be
# outputs of flops, but external pin names have to be translated
# to their internal names by looking at the OUTPUT section.
#-------------------------------------------------------------

set cycle 0
while {[gets $bnet line] >= 0} {
   set line [string map {\[ \< \] \> \.subckt \.gate} $line]
   if [regexp {^.gate} $line lmatch] {
      break
   } elseif [regexp {^.names} $line lmatch] {
      break
   } elseif [regexp {^.latch} $line lmatch] {
      break
   } elseif [regexp {^.default_input_arrival} $line lmatch] {
      continue
   }
   puts $onet $line
}

# Add a reset inverter/buffer to the netlist
# Add two inverters if the reset signal was inverted

foreach resetnet [lsort -uniq $resetlist] {
   if {[string first "not_" $resetnet] == 0} {
      set rstorig [string range $resetnet 4 end]
      puts $onet ".gate ${inverter} ${invertpin_in}=${rstorig} ${invertpin_out}=${resetnet}"
   }
   puts $onet ".gate ${inverter} ${invertpin_in}=${resetnet} ${invertpin_out}=pp_${resetnet}bar"
}

# Replace all .latch statements with .gate, with the appropriate gate type and pins,
# and copy all .gate statements as-is.

set sridx 0
while {1} {
   if [regexp {^\.latch[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]+[^ \t]+[ \t]+([^ \t]+)} \
		$line lmatch dname qname cpname] {
       set srnames ""
       set idx [lsearch $flopsigs $qname]
       if {$idx < 0} {
	  if {![catch {set alist [subst \${${qname}(alias)}]}]} {
	     foreach alias $alist {
		set idx [lsearch $flopsigs $alias]
		if {$idx >= 0} {
		   break
		}
	     }
	  }
       }
       if {$idx >= 0} {
	  set flopt [lindex $floptypes $idx]
	  set resetnet [lindex $flopresetnet $idx]
	  if {$setpininvert == 1} {
	     set setresetnet pp_${resetnet}bar
	     set setpinstatic ${vddnet}
	  } else {
	     set setresetnet ${resetnet}
	     set setpinstatic ${gndnet}
	  }
	  if {$resetpininvert == 1} {
	     set resetresetnet pp_${resetnet}bar
	     set resetpinstatic ${vddnet}
	  } else {
	     set resetresetnet ${resetnet}
	     set resetpinstatic ${gndnet}
	  }
	  if {$flopt == 1} {
	     if {[catch {set flopset}]} {
		set gname ".gate ${flopsetreset}"
		set srnames "${setpin}=${setresetnet} ${resetpin}=${resetpinstatic}"
	     } else {
		set gname ".gate ${flopset}"
		set srnames "${setpin}=${setresetnet}"
	     }
	  } elseif {$flopt == 0} {
	     if {[catch {set flopreset}]} {
		set gname ".gate ${flopsetreset}"
		set srnames "${setpin}=${setpinstatic} ${resetpin}=${resetresetnet}"
	     } else {
		set gname ".gate ${flopreset}"
		set srnames "${resetpin}=${resetresetnet}"
	     }
	  } else {
	     # Set signal to another signal.
	     set net1 sr_net_${sridx}
	     incr sridx
	     set net2 sr_net_${sridx}
	     incr sridx

	     if {$setpininvert == 0 || $resetpininvert == 1} {
		puts $onet ".gate ${inverter} ${invertpin_in}=${flopt} ${invertpin_out}=pp_${flopt}_bar"
	     }

	     if {$setpininvert == 1} {
		puts -nonewline $onet ".gate ${nandgate} ${nandpin_in1}=${resetnet} "
		puts $onet "${nandpin_in2}=${flopt} ${nandpin_out}=${net1}"
	     } else {
		puts -nonewline $onet ".gate ${norgate} ${norpin_in1}=pp_${resetnet}bar "
		puts $onet "${norpin_in2}=pp_${flopt}bar ${norpin_out}=${net1}"
	     }

	     if {$resetpininvert == 1} {
		puts -nonewline $onet ".gate ${nandgate} ${nandpin_in1}=${resetnet} "
		puts $onet "${nandpin_in2}=pp_${flopt}bar ${nandpin_out}=${net2}"
	     } else {
		puts -nonewline $onet ".gate ${norgate} ${norpin_in1}=pp_${resetnet}bar "
		puts $onet "${norpin_in2}=${flopt} ${norpin_out}=${net2}"
	     }

	     set gname ".gate ${flopsetreset}"
	     set srnames "${setpin}=${net1} ${resetpin}=${net2}"
	  }

       } else {
	   # No recorded init state, use plain flop
	   set gname ".gate ${flopcell}"
	   set srnames ""
       }
       puts $onet "$gname ${floppinin}=$dname ${floppinclk}=$cpname $srnames ${floppinout}=$qname"

   } else {

       # All lines starting with ".names" are converted into buffers.  These should be
       # pruned. . . 

       if [regexp {^.names} $line lmatch] {
	  if {![regexp {\$false$} $line lmatch]} {
             set line [string map [subst {\\\$false $gndnet \\\$true $vddnet}] $line]
	     if [regexp {^.names[ \t]+([^ \t]+)[ \t]+([^ \t]+)[ \t]*$} $line lmatch sigin sigout] {
	        puts $onet ".gate ${bufcell} ${bufpin_in}=${sigin} ${bufpin_out}=${sigout}"
	     }
	     gets $bnet line
	  }
       } else {
          puts $onet $line
       }
       if [regexp {^.end} $line lmatch] break
   }
   if {[gets $bnet line] < 0} break
   set line [string map {\[ \< \] \> \.subckt \.gate} $line]
}

close $bnet
