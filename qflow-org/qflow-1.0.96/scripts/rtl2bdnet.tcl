#!/bin/tclsh
#
# Usage:
#	rtl2bdnet.tcl <verilog_rtl_filename> [<bdnet_filename>]
#
# If bdnet_filename is not specified, then name will be the root name
# of the verilog RTL file with the .bdnet extension.
#
#------------------------------------------------------------
# Original bdnet2sim.tcl Written by Tim Edwards February 12, 2007
# Modified for Verilog RTL input, Tim Edwards, April 25, 2011
# Modified for BDNET output, Tim Edwards, November 19, 2012
#------------------------------------------------------------

set rtlfile [lindex $argv 0]
set cellname [file rootname $rtlfile]
if {"$cellname" == "$rtlfile"} {
   set rtlfile ${cellname}.rtl.v
}

set prefix ""
if {$argc > 2} {
   if {[lindex $argv [expr {$argc - 2}]] == "-prefix"} {
      set prefix [lindex $argv [expr {$argc - 1}]]/
      incr argc -2
   }
}

if {$argc == 2} {
   set bdnetfile [lindex $argv 1]
} else {
   set bdnetfile ${cellname}.bdnet
}

set scriptdir [file dirname $argv0]

#-------------------------------------------------------------
# Open files for read and write

if [catch {open $rtlfile r} fnet] {
   puts stderr "Error: can't open file $rtlfile for reading!"
   exit 0
}

if [catch {open $bdnetfile w} fbdnet] {
   puts stderr "Error: can't open file $bdnetfile for writing!"
   exit 0
}

#----------------------------------------------------------------
# Parse the contents of the RTL file and dump each cell
# instance to the .bdnet file output.

puts stdout "Reading RTL file. . ."
flush stdout

set fnet [open $rtlfile r]
set mode none
while {[gets $fnet line] >= 0} {
   if [regexp {^[ \t]*//} $line lmatch] {
      # Comment line---do nothing
   } elseif [regexp {^module[ \t]+([^ \t]+)[ \t]*\(} $line lmatch cellverify] {
      if {"$cellname" != "$cellverify"} {
	 puts -nonewline stderr "WARNING:  MODEL name ${cellverify} does not"
	 puts stderr " match filename ${cellname}!"
      }
      puts $fbdnet "MODEL \"${cellverify}\";"
   } elseif [regexp {^[ \t]*([^ \t]+)[ \t]+([^ \t]+)[ \t]+\((.*)$} $line \
		lmatch macroname instname rest] {
      # New instance found
      if {$mode != "instances"} {
         puts $fbdnet ";"
	 puts $fbdnet ""
      }
      set mode instances
      puts $fbdnet "INSTANCE \"$macroname\":\"physical\""

      # parse "rest" for any pin information on the first line
      while {[regexp {[ \t]*.([^(]+)\(([^) \t]+)[ \t]*\),*(.*)$} $rest \
		lmatch pinname netname more] > 0} {
	 puts $fbdnet "        \"$pinname\"   :   \"$netname\";"
	 set rest $more
      }
      puts $fbdnet ""
   } elseif [regexp {^endmodule} $line lmatch] {
      set mode end
      puts $fbdnet "ENDMODEL;"
   } elseif [regexp {^[ \t]*input[ \t]+(.*)$} $line lmatch rest] {
      if {$mode != "input"} {
         puts $fbdnet ""
	 puts -nonewline $fbdnet "INPUT"
      }
      set mode "input"
      while {[regexp {[ \t]*([^ \t,;]+)[ \t]*([,;])(.*)$} $rest \
		lmatch inname divider more] > 0} {
	 set vlo -1
	 set vhi -1
	 if {[regexp {[ \t]*\[([^]]+)\]} $rest lmatch vector]} {
	    if {[regexp {([0-9]+):([0-9]+)} $vector lmatch vstart vend]} {
	       if {$vstart > $vend} {
		  set vhi $vstart
		  set vlo $vend
	       } else {
		  set vlo $vstart
		  set vhi $vend
 	       }
	    }
	 }
	 if {$vlo < 0} {
	    puts $fbdnet ""
	    puts -nonewline $fbdnet "        \"$inname\"   :   \"$inname\""
	 } else {
	    incr vlo -1
	    while {$vhi > $vlo} {
	       puts $fbdnet ""
	       puts -nonewline $fbdnet "        \"$inname\[$vhi\]\"   :   \"$inname\[$vhi\]\""
	       incr vhi -1
	    }
	 }
	 if {"$divider" != ";"} {
	    set rest $more
	    regexp {^[ \t]*input[ \t]+(.*)$} $rest lmatch more
	 }
	 set rest $more
      }
   } elseif [regexp {^[ \t]*output[ \t]+(.*)$} $line lmatch rest] {
      if {$mode != "output"} {
         puts $fbdnet ";"
         puts $fbdnet ""
	 puts -nonewline $fbdnet "OUTPUT"
      }
      set mode "output"
      while {[regexp {[ \t]*([^ \t,;]+)[ \t]*([,;])(.*)$} $rest \
		lmatch outname divider more] > 0} {
	 set vlo -1
	 set vhi -1
	 if {[regexp {[ \t]*\[([^]]+)\]} $rest lmatch vector]} {
	    if {[regexp {([0-9]+):([0-9]+)} $vector lmatch vstart vend]} {
	       if {$vstart > $vend} {
		  set vhi $vstart
		  set vlo $vend
	       } else {
		  set vlo $vstart
		  set vhi $vend
 	       }
	    }
	 }
	 if {$vlo < 0} {
	    puts $fbdnet ""
	    puts -nonewline $fbdnet "        \"$outname\"   :   \"$outname\""
	 } else {
	    incr vlo -1
	    while {$vhi > $vlo} {
	       puts $fbdnet ""
	       puts -nonewline $fbdnet "        \"$outname\[$vhi\]\"   :   \"$outname\[$vhi\]\""
	       incr vhi -1
	    }
	 }
	 if {"$divider" != ";"} {
	    set rest $more
	    regexp {^[ \t]*output[ \t]+(.*)$} $rest lmatch more
	 }
	 set rest $more
      }
   } elseif [regexp {^[ \t]*wire} $line lmatch] {
      set mode "wires"
   } else {
      while {[regexp {[ \t]*.([^(]+)\(([^) \t]+)[ \t]*\),*(.*)$} $line \
		lmatch pinname netname rest] > 0} {
	 set line $rest
      }
   }
}
close $fnet

puts stdout "Done!"
