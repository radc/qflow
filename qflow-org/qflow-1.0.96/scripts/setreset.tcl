#!/bin/tclsh
#-------------------------------------------------------------------------
# setreset --- post-process a bdnet file with the contents of the
# .blif file produced by odin-ii.  For each .latch line from the .blif
# file, if the last value is 0 then the flop/latch is replaced by a
# reset flop/latch;  if 1, it is set by a set flop/latch;  otherwise
# it is left as-is.
#-------------------------------------------------------------------------
# Written by Tim Edwards January 20, 2012
# Open Circuit Design
#-------------------------------------------------------------------------

if {$argc < 8} {
   puts stderr \
	"Usage:  postproc.tcl bdnet_file blif_file flop setflop setpin resetflop resetpin srflop inverter orgate andgate"
   exit 1
}

set bdnetfile [lindex $argv 0]
set cellname [file rootname $bdnetfile]
if {"$cellname" == "$bdnetfile"} {
   set bdnetfile ${cellname}.bdnet
}

set outfile ${cellname}_tmp.bdnet

set initfile [lindex $argv 1]
set initname [file rootname $initfile]
if {"$initname" == "$initfile"} {
   set initfile ${initname}.init
}

set flop [lindex $argv 2]
set setflop [lindex $argv 3]
set setpin [lindex $argv 4]
set resetflop [lindex $argv 5]
set resetpin [lindex $argv 6]

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $bdnetfile r} bnet] {
   puts stderr "Error: can't open file $bdnetfile for reading!"
   exit 1
}

if [catch {open $initfile r} inet] {
   puts stderr "Error: can't open file $initfile for reading!"
   exit 1
}

if [catch {open $outfile w} onet] {
   puts stderr "Error: can't open file $outfile for writing!"
   exit 1
}

#-------------------------------------------------------------
# Read the file of initialization signals and their values
#-------------------------------------------------------------

set resetlist {}

if {[gets $inet line] >= 0} {
   set resetnet $line
   set resetnet [string map {! not_ ~ not_} $resetnet]
   lappend resetlist $resetnet
} else {
   # Empty init file---therefore, no initialization to do.
   file copy $bdnetfile $outfile
   exit 0
}

set flopsigs {}
set floptypes {}
set flopreset {}

while {[gets $inet line] >= 0} {
   if [regexp {^([^ \t]+)[ \t]+([^ \t]+)} $line lmatch signal initcond] {
      lappend flopsigs $signal
      lappend floptypes $initcond
      lappend flopreset $resetnet
   } else {
      set resetnet $line
      set resetnet [string map {! not_ ~ not_} $resetnet]
      lappend resetlist $resetnet
   }
}

close $inet

#-------------------------------------------------------------
# Now post-process the bdnet file
# The main thing to remember is that internal signals will be
# outputs of flops, but external pin names have to be translated
# to their internal names by looking at the OUTPUT section.
#-------------------------------------------------------------

set cycle 0
while {[gets $bnet line] >= 0} {
   if [regexp {^INPUT} $line lmatch] {
      set cycle 1
   } elseif [regexp {^OUTPUT} $line lmatch] {
      set cycle 2
   } elseif [regexp {^INSTANCE} $line lmatch] {
      break
   }
   if {$cycle == 2} {
      if [regexp {"(.+)"[ \t]*:[ \t]*"(.+)"} $line lmatch sigin sigout] {
	 set idx [lsearch $flopsigs $sigin]
	 if {$idx >= 0} {
	    set sigout [string map {\[ << \] >>} $sigout]
	    set flopsigs [lreplace $flopsigs $idx $idx $sigout]
	 }
      }
   }
   puts $onet $line
}

# Add a reset inverter/buffer to the netlist
# Add two inverters if the reset signal was inverted

foreach resetnet $resetlist {
   if {[string first "not_" $resetnet] == 0} {
      set rstorig [string range $resetnet 4 end]
      puts $onet ""
      puts $onet "INSTANCE \"${inverter}\":\"physical\""
      puts $onet "\t\"A\" : \"${rstorig}\";"
      puts $onet "\t\"Y\" : \"${resetnet}\";"
      puts $onet ""
   }
   puts $onet ""
   puts $onet "INSTANCE \"${inverter}\":\"physical\""
   puts $onet "\t\"A\" : \"${resetnet}\";"
   puts $onet "\t\"Y\" : \"pp_${resetnet}bar\";"
   puts $onet ""
}

set sridx 0
while {1} {
   if [regexp [subst {^INSTANCE "${flop}":"physical"}] $line lmatch] {
       gets $bnet dline
       gets $bnet cpline
       gets $bnet qline
       set srline ""
       
       if [regexp {"Q"[ \t]+:[ \t]+"(.+)"} $qline lmatch signame] {
	  set sigtest [string map {\[ << \] >>} $signame]
	  set idx [lsearch $flopsigs $sigtest]
          if {$idx < 0} {
	     # signal names with '0' appended are an artifact of VIS/SIS
	     if {[string index $signame end] == 0} {
	        set idx [lsearch $flopsigs [string range $sigtest 0 end-1]]
	     }
	  }
          if {$idx >= 0} {
	     set flopt [lindex $floptypes $idx]
	     set resetnet [lindex $flopreset $idx]
	     if {$flopt == 1} {
		set line "INSTANCE \"${setflop}\":\"physical\""
		set srline "\t\"${setpin}\" : \"${resetnet}\""
	     } elseif {$flopt == 0} {
		set line "INSTANCE \"${resetflop}\":\"physical\""
		set srline "\t\"${resetpin}\" : \"pp_${resetnet}bar\""
	     } else {
		# Set signal to another signal.  We remove all pretense of
		# being independent of the technology here. . .
		set net1 sr_net_${sridx}
		incr sridx
		set net2 sr_net_${sridx}
		incr sridx
		set line "INSTANCE \"${andgate}\":\"physical\""
		puts $onet $line
		set line "\t\"A\" : \"${resetnet}\";"
		puts $onet $line
		set line "\t\"B\" : \"${flopt}\";"
		puts $onet $line
		set line "\t\"Y\" : \"${net1}\";\n"
		puts $onet $line

		set line "INSTANCE \"${orgate}\":\"physical\""
		puts $onet $line
		set line "\t\"A\" : \"pp_${resetnet}bar\";"
		puts $onet $line
		set line "\t\"B\" : \"${flopt}\";"
		puts $onet $line
		set line "\t\"Y\" : \"${net2}\";\n"
		puts $onet $line

		set line "INSTANCE \"${srflop}\":\"physical\""
		set srline \
		   "\t\"${setpin}\" : \"${net1}\";\n\t\"${resetpin}\" : \"${net2}\";"
	     }
	  }
       }

       puts $onet $line
       puts $onet $dline
       puts $onet $cpline
       if {[string length $srline] > 0} {
          puts $onet $srline
       }
       puts $onet $qline
   } else {
       puts $onet $line
   }
   if {[gets $bnet line] < 0} break
}

# Overwrite the original file
file rename -force $outfile $bdnetfile
