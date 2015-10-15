#!/bin/tclsh
#-------------------------------------------------------------------------
# outputprep.tcl --- 
#
#    Odin_II modifies all DFF output signal names by adding "_FF_NODE"
#    to the name.  This is good for module input/output, as it
#    differentiates the I/O name and the DFF output name, allowing
#    buffers to be inserted between the two.  However, other internal
#    signals don't get buffered, and so the original node name from
#    the verilog source is corrupted with respect to simulations, etc.,
#    which expect to see the original node name.  This script searches
#    for all DFF outputs that are NOT I/O and removes "_FF_NODE" from
#    the name whereever it occurs.
#
# usage:
#	  outputprep.tcl <bliffile> <outfile>
#
#-------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

set bliffile [lindex $argv 0]
set cellname [file rootname $bliffile]
if {"$cellname" == "$bliffile"} {
   set bliffile ${cellname}.blif
}

set outfile [lindex $argv 1]
set outname [file rootname $outfile]
if {"$outname" == "$outfile"} {
   set outfile ${outname}.blif
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $bliffile r} bnet] {
   puts stderr "Error: can't open file $bliffile for reading!"
   exit 0
}

if [catch {open $outfile w} bout] {
   puts stderr "Error: can't open file $outfile for writing!"
   exit 0
}

#-------------------------------------------------------------
# On the premise that flop outputs cannot be module inputs, we
# look only at the "output" list.
#-------------------------------------------------------------

set outputsigs {}
set state 0
while {[gets $bnet line] >= 0} {

   if {$state == 0} {
      if [regexp {^[ \t]*\.outputs[ \t]*(.*)$} $line lmatch rest] {
         set state 1
         foreach name_in $rest {
	    lappend outputsigs $name_in
         }
      }
      puts $bout $line
   } elseif {$state == 1} {
      if [regexp {^[ \t]*\.gate} $line lmatch] {
	 # Proceed to the net rewriting part
	 set state 2
      } else {
         foreach name_in $line {
	    lappend outputsigs $name_in
         }
         puts $bout $line
      }
   }

   if {$state == 2} {

      # For any signal name containing "_FF_NODE", if the text preceding
      # "_FF_NODE" is not in the output list, then remove the "_FF_NODE" to
      # restore the original signal name.

      set oline ""
      foreach pinsig $line {
         if {[regexp {([^ \t=]+)=([^ \t_]+)_FF_NODE} $pinsig lmatch pin_name sig_name]} {
	    if {[lsearch -exact $outputsigs $sig_name] < 0} {
	       set oline "$oline ${pin_name}=${sig_name}"
	    } else {
	       set oline "$oline $pinsig"
	    }
	 } else {
	    set oline "$oline $pinsig"
	 } 
      }
      puts $bout $oline
   }
}

close $bnet 
close $bout
