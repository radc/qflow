#!/bin/tclsh
#-------------------------------------------------------------------------
# vispreproc --- preprocess a verilog file to make it compatible with
#	VIS.  Note that general preprocessing for most open-source
#	synthesis tools, such as breaking out reset initializations
#	and dividing blocks into single clock domains, is handled by
#	the "vpreproc" tool.  This is just for things ideosyncratic to
#	VIS.
#-------------------------------------------------------------------------
# Tim Edwards
# Open Circuit Design
# April 2013
#-------------------------------------------------------------------------
namespace path {::tcl::mathop ::tcl::mathfunc}

set verilogfile [lindex $argv 0]
set cellname [file rootname $verilogfile]
if {"$cellname" == "$verilogfile"} {
   set verilogfile ${cellname}.v
}

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $verilogfile r} vnet] {
   puts stderr "Error: can't open file $verilogfile for reading!"
   exit 0
}

if [catch {open ${cellname}_vis.v w} vtmp] {
   puts stderr "Error: can't open file ${cellname}_tmp.v for writing!"
   exit 0
}

#-------------------------------------------------------------

while {[gets $vnet line] >= 0} {

    # VIS does not handle non-blocking assignments.  It wants
    # to see "a = b" everywhere, NOT "a <= b", although it
    # treats them as nonblocking, anyway.

    regsub -all {<=} $line = line
    puts $vtmp $line
}

#-------------------------------------------------------------

close $vnet
close $vtmp
