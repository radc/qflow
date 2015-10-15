#!/bin/tclsh
#-------------------------------------------------------------------------
# outpreporig --- process a BDNET netlist file to change the internal
# signal name by prepending it with the text "raw_" while leaving the
# output signal name intact.  This step is preformed after the DFFs
# have been replaced with ones applying initial values, but before
# the output buffers are inserted.
#
# NOTE:  Since "abc" inserts buffers on its own now, this routine is
# no longer needed.  See "outputprep.tcl" instead for some additional
# node name handling.
#-------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

set bdnetfile [lindex $argv 0]
set cellname [file rootname $bdnetfile]
if {"$cellname" == "$bdnetfile"} {
   set bdnetfile ${cellname}.bdnet
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $bdnetfile r} bnet] {
   puts stderr "Error: can't open file $bdnetfile for reading!"
   exit 0
}

if [catch {open ${cellname}_tmp.bdnet w} btmp] {
   puts stderr "Error: can't open file ${cellname}_tmp.bdnet for writing!"
   exit 0
}

#-------------------------------------------------------------

set outputsigs {}
set state 0
while {[gets $bnet line] >= 0} {

   if {$state == 0} {
      if [regexp {^[ \t]*OUTPUT} $line lmatch] {
         set state 1
      }
      puts $btmp $line
   } elseif {$state == 1} {

      if [regexp {^[ \t]*"([^ \t:]+)"[ \t:]+"([^ \t:]+)"(.*)$} $line \
		lmatch name_in name_out rest] {
	 if [string match $name_in $name_out] {
	    lappend outputsigs $name_in
	 }
	 puts $btmp "\t\"$name_in\" : \"raw_$name_in\"$rest"
      } else {
	 puts $btmp $line
      }

      if [regexp {^[ \t]*INSTANCE} $line lmatch] {
	 set state 2
      }
   } elseif {$state == 2} {
      if [regexp {^[ \t]*"([^ \t:]+)"[ \t:]+"([^ \t:]+)"(.*)$} $line \
		lmatch pin_name sig_name rest] {
	 if {[lsearch -exact $outputsigs $sig_name] >= 0} {
	    puts $btmp "\t\"$pin_name\" : \"raw_$sig_name\"$rest"
	 } else {
	    puts $btmp $line
	 }
      } else {
	 puts $btmp $line
      }
   }
}

close $bnet 
close $btmp

# Rename the temporary file, overwriting the original file
file rename -force ${cellname}_tmp.bdnet $bdnetfile
