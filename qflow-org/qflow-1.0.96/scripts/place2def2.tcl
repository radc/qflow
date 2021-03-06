#!/usr/bin/tclsh
#---------------------------------------------------------------------------
# place2def2.tcl <rootname> <lefname> ---
#
# Read a GrayWolf .pl1 cell placement output file, and write a
# DEF file of placed COMPONENTS and PINS, and unrouted NETS.
# add ROW, TRACKS, and DIEAREA statements, as needed.
#
# Modified 11/15/2012
# Corrected declaration of tracks and placment of obstruction layers and
# pins for the conditions of > 3 routing layers, or for reversed routing
# layer orientations.
#
# Modified 12/06/2012
# Correction to use track offset information from the technology LEF file
# instead of assuming a 1/2 track pitch offset.  Also, corrected the pin
# positions, which were not subtracting off the offset used for the cell
# positions.
#
# Modified 3/29/2013
# Correction for pin positions when the cell boundary is not on a track
# position, and the track offset information is in the OFFSET line for
# the route layer in the technology LEF file.
#---------------------------------------------------------------------------

if {$argc < 3} {
   puts stdout "Usage:  place2def2 <project_name> <qrouter_path> <technology_lef> ..."
   exit 0
}

# NOTE:  There is no scaling.  GrayWolf values are in centimicrons,
# as are DEF values (UNITS DISTANCE MICRONS 100)

puts stdout "Running place2def2.tcl"

set topname [lindex $argv 0]

set pl1name ${topname}.pl1
set pl2name ${topname}.pl2
set parname ${topname}.par
set pinname ${topname}.pin
set defname ${topname}.def
set cfgname ${topname}.cfg
set infoname ${topname}.info

# Number of layers we want to use for routing is determined
# by counting the layers listed in the .par file.

if [catch {open $parname r} fpar] {
   puts stderr "Error: can't open file $parname for input"
   return
}

set qrouter_path [lindex $argv 1]
set leffiles [lrange $argv 2 end]

#-----------------------------------------------------------------
# Pick up the width of a feedthrough cell from the .par file
# Also find the number of routing layers to use by counting
# "layer" lines
#-----------------------------------------------------------------

set paramval 0
set numlayers 0
set rulesection 0

while {[gets $fpar line] >= 0} {
   regexp {^[ \t]*TWSC[*.]feedThruWidth[ \t]*:[ \t]*([^ ]+)[ \t]+} $line lmatch \
		paramval
   if {[regexp {^[ \t]*RULES} $line lmatch] == 1} {
      set rulesection 1
   }
   if {$rulesection == 1} {
      if {[regexp {^[ \t]*layer[ \t]*} $line lmatch] == 1} { 
	 incr numlayers
      }
      if {[regexp {^[ \t]*ENDRULES} $line lmatch] == 1} {
	 set rulesection 0
      }
   }
}
close $fpar
if {$paramval > 0} {
   puts stdout "Adjusting to feedthrough width of $paramval"
}   

if {$numlayers == 0} {
   puts stdout "No layers specified in .par file.  Using default 3"
   set numlayers 3
}

if [catch {open $pl1name r} fpl1] {
   puts stderr "Error: can't open file $pl1name for input"
   return
}

if [catch {open $pl2name r} fpl2] {
   puts stderr "Error: can't open file $pl2name for input"
   return
}

if [catch {open $defname w} fdef] {
   puts stderr "Error: can't open file $defname for output"
   return
}

if [catch {open $cfgname w} fcfg] {
   puts stderr "Error: can't open file $cfgname for output"
   return
}

#-----------------------------------------------------------------
# DEF file header
#-----------------------------------------------------------------

puts $fdef "VERSION 5.6 ;"
puts $fdef "NAMESCASESENSITIVE ON ;"
puts $fdef "DIVIDERCHAR \"/\" ;"
puts $fdef "BUSBITCHARS \"<>\" ;"
puts $fdef "DESIGN $topname ;"
puts $fdef "UNITS DISTANCE MICRONS 100 ;"
puts $fdef ""

#-----------------------------------------------------------------
# Part 1:  Area and routing tracks
#-----------------------------------------------------------------

#-----------------------------------------------------------------
# Read the .pl1 file to get pin position corrections due to
# feedthroughs (i.e., count the number of feedthroughs used per row)
#-----------------------------------------------------------------

set rowyvals {}
while {[gets $fpl1 line] >= 0} {
   regexp {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)} $line lmatch \
		instance feedx feedy
   if {[string equal -length 6 $instance twfeed]} {
      set rowidx [lsearch $rowyvals $feedy]
      if {$rowidx < 0} {
	 set rowidx [llength $rowyvals]
	 lappend rowyvals $feedy
         incr rowidx
	 set rowxoff($rowidx) $paramval
      } else {
         incr rowidx
	 incr rowxoff($rowidx) $paramval
      }
   }
}
close $fpl1
set fpl1 [open $pl1name r]	;# reopen for next pass

#-----------------------------------------------------------------
# Generate route configuration file
#-----------------------------------------------------------------

puts $fcfg "# route configuration file"
puts $fcfg "# for project ${topname}"
puts $fcfg ""
puts $fcfg "Num_layers  $numlayers"
# NOTE:  Use the following to prevent via stacks that the
# technology disallows.
puts $fcfg "stack 2"
puts $fcfg ""
foreach leffile $leffiles {puts $fcfg "lef ${leffile}"}
puts $fcfg ""
flush $fcfg

#-----------------------------------------------------------------
# Read the .pl2 file and get the full die area (components only)
#-----------------------------------------------------------------

# To avoid having to parse a LEF file from a Tcl script, I have
# made a qrouter "-i" option to print out route layer information;
# this should be found in file ${topname}.info

catch {exec $qrouter_path -i $infoname -c $cfgname}

set finf [open $infoname r]
if {$finf != {}} {
   set i 0
   while {[gets $finf line] >= 0} {

      # Older versions of qrouter assumed a track offset of 1/2 track pitch.
      # Newer versions correctly take the offset from the LEF file and dump the
      # value to the info file.  Also the newer version records the track width,
      # although this is not used.

      if {[regexp {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)} \
		$line lmatch layer pitch offset width orient] <= 0} {
         regexp {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)} $line lmatch \
		layer pitch orient 
	 set offset [expr 0.5 * $pitch]
	 set width $pitch
      }
      incr i
      set metal${i}(name) $layer
      set metal${i}(pitch) $pitch
      set metal${i}(orient) $orient
      set metal${i}(offset) $offset
      set metal${i}(width) $width
   }
   close $finf

   # NOTE:  Treating all pitches the same for all layers in the same
   # direction.  This is good for doing various calculations on cell
   # and pin positions.  The track positions themselves will be placed
   # according to the given route layer pitch.

   if {$metal1(orient) == "horizontal"} {
      set pitchx [expr 100 * $metal2(pitch)]
      set pitchy [expr 100 * $metal1(pitch)]
      set offsetx [expr 100 * $metal2(offset)]
      set offsety [expr 100 * $metal1(offset)]
      set widthx [expr 100 * $metal2(width)]
      set widthy [expr 100 * $metal1(width)]
   } else {
      set pitchx [expr 100 * $metal1(pitch)]
      set pitchy [expr 100 * $metal2(pitch)]
      set offsetx [expr 100 * $metal1(offset)]
      set offsety [expr 100 * $metal2(offset)]
      set widthx [expr 100 * $metal1(width)]
      set widthy [expr 100 * $metal2(width)]
   }
} else {
   puts stdout "Warning:  No file $infoname generated, using defaults."

   set pitchx 160
   set pitchy 200
   set offsetx 80
   set offsety 100
   set widthx 160
   set widthy 200
}

# Add numlayers to the configuration file now that we know it

set halfpitchx [expr $pitchx / 2];
set halfpitchy [expr $pitchy / 2];
set halfwidthx [expr $widthx / 2];
set halfwidthy [expr $widthy / 2];

set xbot 1000
set ybot 1000 
set cellxbot 1000
set cellybot 1000 
set xtop 0
set postxtop 0
set ytop 0
set smash 0

while {[gets $fpl2 line] >= 0} {
   regexp {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+[^ ]+[ \t]+([^ ]+)} \
		$line lmatch rowidx llx lly urx ury align
   set pline [regexp {^[ \t]*twpin} $line lmatch]
   if {$pline > 0} {

      # Re-align pins to remove feedthrough offsets
      if {$align != -1} {
	 set llx [expr $llx - $smash]
	 set urx [expr $urx - $smash]
      }

      # Re-align pins to the nearest track pitch.  Set pin size to be
      # equal to a route width.  

      set pincx [expr ($llx + $urx) / 2]
      set pincy [expr ($lly + $ury) / 2]
      set pincx [expr $pincx - $halfpitchx - $offsetx]
      set pincy [expr $pincy - $halfpitchy - $offsety]
      set xgrid [expr 1 + floor($pincx / $pitchx)]
      set ygrid [expr 1 + floor($pincy / $pitchy)]
      set pincx [expr $xgrid * $pitchx + $offsetx]
      set pincy [expr $ygrid * $pitchy + $offsety]
      set llx [expr $pincx - $halfwidthx]
      set lly [expr $pincy - $halfwidthy]
      set urx [expr $pincx + $halfwidthx]
      set ury [expr $pincy + $halfwidthy]
      set posturx $urx

      # puts stdout "At twpin:  llx=$llx pincx=$pincx urx=$urx posturx=$posturx"
      # puts stdout "smash=$smash halfpitch=($halfpitchx,$halfpitchy)"

   } else {
      # For each row, subtract the feedthrough correction
       set posturx [expr $urx]
       set yvals($rowidx) [list $lly $ury]

       if {$llx < $cellxbot} {set cellxbot $llx}
       if {$lly < $cellybot} {set cellybot $lly}
   }
   if {$llx < $xbot} {set xbot $llx}
   if {$lly < $ybot} {set ybot $lly}
   if {$urx > $xtop} {set xtop $urx}
   if {$posturx > $postxtop} {set postxtop $posturx}
   if {$ury > $ytop} {set ytop $ury}

   if {$pline <= 0} {
      if {$xtop != $postxtop} {
         set smash [expr $xtop - $postxtop - $pitchx]
      }
      set corexbot $xbot
      set corextop $postxtop
      set coreybot $ybot
      set coreytop $ytop

      # puts stdout "At row: smash=$smash corexbot=$corexbot"
      # puts stdout "xtop=$xtop postxtop=$postxtop pitch=($pitchx, $pitchy)"
   }
}
close $fpl2

puts stdout "Limits: xbot = $xbot ybot = $ybot xtop = $xtop ytop = $ytop"

# Move cells down and left by the track offset.

set cellxbot [expr $cellxbot - $offsetx]
set cellybot [expr $cellybot - $offsety]

# Also adjust core values to put lower left corner at offsetx,offsety

set corextop [expr $offsetx + $corextop - $corexbot]
set coreytop [expr $offsety + $coreytop - $coreybot]
set corexbot $offsetx
set coreybot $offsety

puts stdout "Core values: $corexbot $coreybot $corextop $coreytop"
puts stdout "Offsets: $offsetx $offsety"

# Expand die dimensions by a half pitch in all directions, then snap to
# the track grid (assumes that the origin (0, 0) is a track position)

set diexbot [expr $xbot - $cellxbot - $halfpitchx]
set xgrid [expr floor($diexbot / $pitchx)]
set diexbot [expr $xgrid * $pitchx]
set dieybot [expr $ybot - $cellybot - $halfpitchy]
set ygrid [expr floor($dieybot / $pitchy)]
set dieybot [expr $ygrid * $pitchy]

set diextop [expr $postxtop - $cellxbot + $halfpitchx]
set xgrid [expr floor($diextop / $pitchx)]
set diextop [expr $xgrid * $pitchx]
set dieytop [expr $ytop - $cellybot + $halfpitchy]
set ygrid [expr floor($dieytop / $pitchy)]
set dieytop [expr $ygrid * $pitchy]

puts $fdef "DIEAREA ( $diexbot $dieybot ) ( $diextop $dieytop ) ;"
puts $fdef ""

#------------------------------------------------------------------
# Write the tracks (we need to match the coordinate positions. . .)
#------------------------------------------------------------------

set width [expr $diextop - $diexbot]
set height [expr $dieytop - $dieybot]

for {set i 1} {$i <= $numlayers} {incr i} {
   set mname [subst \$metal${i}(name)]
   set mpitch [expr 100 * [subst \$metal${i}(pitch)]]
   if {[subst \$metal${i}(orient)] == "vertical"} {
      set xtracks [expr 1 + int($width / $mpitch)];
      puts $fdef "TRACKS X $diexbot DO $xtracks STEP $mpitch LAYER $mname ;"
   } else {
      set ytracks [expr 1 + int($height / $mpitch)];
      puts $fdef "TRACKS Y $dieybot DO $ytracks STEP $mpitch LAYER $mname ;"
   }
}
puts $fdef ""

# diagnostic
puts stdout "$numlayers routing layers"

set xtracks [expr int($width / $pitchx)];
set ytracks [expr int($height / $pitchy)];

if {$metal1(orient) == "horizontal"} {
   puts stdout \
	"$ytracks horizontal tracks from $dieybot to $dieytop step $pitchy (M1, M3, ...)"
   puts stdout \
	"$xtracks vertical tracks from $diexbot to $diextop step $pitchx (M2, M4, ...)"
} else {
   puts stdout \
	"$ytracks horizontal tracks from $dieybot to $dieytop step $pitchy (M2, M4, ...)"
   puts stdout \
	"$xtracks vertical tracks from $diexbot to $diextop step $pitchx (M1, M3, ...)"
}

# generate obstruction around pin areas, so these will not have vias
# dropped on top of the pin labels (convert values to microns)
set diexbot_um [expr ($diexbot - $pitchx) / 100.0]
set diextop_um [expr ($diextop + $pitchx) / 100.0]
set dieybot_um [expr ($dieybot - $pitchy) / 100.0]
set dieytop_um [expr ($dieytop + $pitchy) / 100.0]
set corexbot_um [expr $corexbot / 100.0]
set corextop_um [expr ($corextop + $pitchx) / 100.0]
set coreybot_um [expr $coreybot / 100.0]
set coreytop_um [expr ($coreytop + $pitchy) / 100.0]

#  Obstruct all positions in metal1, unless there are only 2 routing layers defined.
if {$numlayers > 2} {
   set mname $metal1(name)
   # 1. Top
   puts $fcfg \
	"obstruction $diexbot_um $coreytop_um $diextop_um $dieytop_um $mname"
   # 2. Bottom
   puts $fcfg \
	"obstruction $diexbot_um $dieybot_um $diextop_um $coreybot_um $mname"
   # 3. Left
   puts $fcfg \
	"obstruction $diexbot_um $dieybot_um $corexbot_um $dieytop_um $mname"
   # 4. Right
   puts $fcfg \
	"obstruction $corextop_um $dieybot_um $diextop_um $dieytop_um $mname"
}

# Place obstructions along top and bottom, or left and right, on the layers that
# are between pin layers.

if {$metal2(orient) == "vertical"} {
   for {set i 3} {$i <= $numlayers} {incr i 2} {
      set mname [subst \$metal${i}(name)]
      # 1. Top
      puts $fcfg \
	"obstruction $corexbot_um $coreytop_um $corextop_um $dieytop_um $mname"
      # 2. Bottom
      puts $fcfg \
	"obstruction $corexbot_um $dieybot_um $corextop_um $coreybot_um $mname"
   }
   for {set i 2} {$i <= $numlayers} {incr i 2} {
      set mname [subst \$metal${i}(name)]
      # 3. Left
      puts $fcfg \
	"obstruction $diexbot_um $coreybot_um $corexbot_um $coreytop_um $mname"
      # 4. Right
      puts $fcfg \
	"obstruction $corextop_um $coreybot_um $diextop_um $coreytop_um $mname"
   }
} else {
   for {set i 3} {$i <= $numlayers} {incr i 2} {
      set mname [subst \$metal${i}(name)]
      # 1. Left
      puts $fcfg \
	"obstruction $diexbot_um $coreybot_um $corexbot_um $coreytop_um $mname"
      # 2. Right
      puts $fcfg \
	"obstruction $corextop_um $coreybot_um $diextop_um $coreytop_um $mname"
   }
   for {set i 2} {$i <= $numlayers} {incr i 2} {
      set mname [subst \$metal${i}(name)]
      # 3. Top
      puts $fcfg \
	"obstruction $corexbot_um $coreytop_um $corextop_um $dieytop_um $mname"
      # 3. Bottom
      puts $fcfg \
	"obstruction $corexbot_um $dieybot_um $corextop_um $coreybot_um $mname"
   }
}

# (test) generate blockages between power buses
# This gets the router stuck.  Too hard!  Limit to a small strip in
# the middle, enough to ensure placement of a substrate/well contact row.
# (This code not relevant to the OSU standard cell set, where power buses
# overlap.)

set i 1
while {![catch {set yvals($i)}]} {
   set oddrow [expr {$i % 2}]
   set y1 [lindex $yvals($i) 0]
#   if {$oddrow} {
#      set y2 [expr {$y1 + 196}]
#   } else {
#      set y2 [expr {$y1 + 84}]
#   }
   set y2 [expr {$y1 + 40}]
   set y3 [lindex $yvals($i) 1]
#   if {$oddrow} {
#      set y4 [expr {$y3 - 84}]
#   } else {
#      set y4 [expr {$y3 - 196}]
#   }
   set y4 [expr {$y3 - 40}]
#  puts $fcfg "obstruction $corexbot $y1 $corextop $y2 $metal1(name)"
#  puts $fcfg "obstruction $corexbot $y4 $corextop $y3 $metal1(name)"
   incr i
}

close $fcfg

#-----------------------------------------------------------------
# Part 2:  Components and pins (placed)
#-----------------------------------------------------------------

#-----------------------------------------------------------------
# Pass number 1:  Read the .pl1 file and get the number of components and pins
#-----------------------------------------------------------------

set numpins 0
set numcomps 0
while {[gets $fpl1 line] >= 0} {
   # We only care about the first word on each line in this pass
   regexp {^[ \t]*([^ ]+)[ \t]+} $line lmatch instance
   if {[string equal -length 6 $instance twpin_]} {
      incr numpins
   } elseif {![string equal -length 6 $instance twfeed]} {
      incr numcomps
   }
}
close $fpl1

# The .pl1 file always has components first, then pins

puts $fdef "COMPONENTS $numcomps ;"

#-----------------------------------------------------------------
# Pass number 2:  Re-read the .pl1 file
#-----------------------------------------------------------------

set fpl1 [open $pl1name r]
set lastrow -1

# Use layers 2 and 3 for pin placement, according to the routing
# orientation.  For now, we only allow pins on those layers
# (need to relax this requirement).  If only 2 routing layers are
# defined, use the metal1 layer for pins

if {$metal2(orient) == "vertical"} {
   set vlayer $metal2(name)
   if {$numlayers < 3} {
      set hlayer $metal1(name)
   } else {
      set hlayer $metal3(name)
   }
} else {
   set hlayer $metal2(name)
   if {$numlayers < 3} {
      set vlayer $metal1(name)
   } else {
      set vlayer $metal3(name)
   }
}

while {[gets $fpl1 line] >= 0} {
   # Each line in the file is <instance> <llx> <lly> <urx> <ury> <orient> <row>
   regexp \
   {^[ \t]*([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)} \
	$line lmatch instance llx lly urx ury orient row
   switch $orient {
      0 {set ostr "N"}
      1 {set ostr "FS"}
      2 {set ostr "FN"}
      3 {set ostr "S"}
      4 {set ostr "FE"}
      5 {set ostr "FW"}
      6 {set ostr "W"}
      7 {set ostr "E"}
   }

   if {$lastrow != $row} {
      set feedoffset 0
   }
   set lastrow $row

   # Handle the "cells" named "twpin_*"

   if {[string equal -length 6 $instance twpin_]} {
      set labname [string range $instance 6 end]
      if {$row == -1 || $row == -2} {
	 set labtype $hlayer
      } else {
	 set labtype $vlayer
      }

      # Only deal with pin center position

      set pincx [expr ($llx + $urx) / 2]
      set pincy [expr ($lly + $ury) / 2]

      # Reposition the pins to account for feedthrough removal and
      # cell repositioning

      set pincx [expr $pincx - $feedoffset - $cellxbot]
      set pincy [expr $pincy - $cellybot]

      # Reposition the pins to match track positions.  Make pins point labels.
      # Do NOT offset by (offsetx, offsety) because the offset has been applied
      # to the cell positions.

      # set pincx [expr $pincx - $halfpitchx - $offsetx]
      # set pincy [expr $pincy - $halfpitchy - $offsety]
      set pincx [expr $pincx - $halfpitchx]
      set pincy [expr $pincy - $halfpitchy]
      set xgrid [expr 1 + floor($pincx / $pitchx)]
      set ygrid [expr 1 + floor($pincy / $pitchy)]
      # set llx [expr $xgrid * $pitchx + $offsetx]
      # set lly [expr $ygrid * $pitchy + $offsety]
      set llx [expr $xgrid * $pitchx]
      set lly [expr $ygrid * $pitchy]

      puts $fdef "- $labname + NET $labname"
      # puts $fdef "  + LAYER $labtype ( -14 -14 ) ( 14 14 )"
      puts $fdef "  + LAYER $labtype ( 0 0 ) ( 1 1 )"
      puts $fdef "  + PLACED ( $llx $lly ) N ;"

   } else {

      if {[string equal -length 6 $instance twfeed]} {

         # Ignore the cells named "twfeed*", except to adjust the X offset
	 # of all cells that come after it.
	 incr feedoffset $paramval

      } else {

         # Get cellname from instance name.
         regsub {([^_]+)_[\d]+} $instance {\1} cellname

         set llxoff [expr $llx - $feedoffset - $cellxbot]
	 set llyoff [expr $lly - $cellybot]
	
	 puts $fdef "- $instance $cellname + PLACED ( $llxoff $llyoff ) $ostr ;"

	 incr numcomps -1
	 if {$numcomps == 0} {
	    puts $fdef "END COMPONENTS"
	    puts $fdef ""
	    puts $fdef "PINS $numpins ;"
         }
      }
   }
}

close $fpl1

#---------------------------------------------------------------------------
# Part 3:  Nets (unrouted)
#---------------------------------------------------------------------------
# Read a GrayWolf .pin netlist file and produce a DEF netlist file for
# use with lithoroute.  The routine is the same as place2net2.tcl (for
# generating a netlist for the Magic interactive maze router), but the
# output format is DEF.
#---------------------------------------------------------------------------

set pinfile ${topname}.pin

if [catch {open $pinfile r} fpin] {
   puts stderr "Error: can't open file $pinfile for input"
   exit 0
}

#-----------------------------------------------------------------
# Pass #1: Parse the .pin file, and count the total number of nets
#-----------------------------------------------------------------

set numnets 0
set curnet {}
while {[gets $fpin line] >= 0} {
   regexp {^([^ ]+)[ \t]+} $line lmatch netname
   if {"$netname" != "$curnet"} {
      incr numnets
      set curnet $netname
   }
}

puts $fdef "END PINS"
puts $fdef ""
puts -nonewline $fdef "NETS $numnets "

#--------------------------------------------------------------
# Pass #2: Parse the .pin file, writing one line of output for
#	   each line of input.
#   While we're at it, enumerate the cells used.
#--------------------------------------------------------------

set curnet {}
set netblock {}
set newnet 0

set fpin [open $pinfile r]
set maclist {}

while {[gets $fpin line] >= 0} {
   # Each line in the file is:
   #     <netname> <subnet> <macro> <pinname> <x> <y> <row> <orient> <layer>
   regexp {^([^ ]+)[ \t]+(\d+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+([^ ]+)[ \t]+[^ ]+[ \t]+[^ ]+[ \t]+([^ ]+)} \
		$line lmatch netname subnet instance pinname px py layer
   if {"$netname" != "$curnet"} {
      set newnet 1
      set curnet $netname
      puts $fdef ";"
      puts $fdef "- $netname"
   }

   if {[string first twfeed ${instance}] == -1} { 
      if {[string first twpin_ ${instance}] == 0} { 
         if {$newnet == 0} {
	    puts $fdef ""	;# end each net component with newline
	 }
         puts -nonewline $fdef "  ( PIN ${pinname} ) "
	 set newnet 0
      } elseif {$instance != "PSEUDO_CELL"} {
         if {$newnet == 0} {
	    puts $fdef ""	;# end each net component with newline
	 }
         puts -nonewline $fdef "  ( ${instance} ${pinname} ) "
	 set newnet 0
	 regexp {(.+)_[^_]+} $instance lmatch macro
	 lappend maclist $macro
      }
   }
}

puts $fdef ";"
puts $fdef "END NETS"
puts $fdef ""
puts $fdef "END DESIGN"

close $fpin
close $fdef

puts stdout "Done with place2def2.tcl"
