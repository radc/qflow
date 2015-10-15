#!/bin/tcsh -f
#----------------------------------------------------------
# Qflow layout display script using magic-8.0
#----------------------------------------------------------
# Tim Edwards, April 2013
#----------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  display.sh [options] <project_path> <source_name>
   exit 1
endif

# Split out options from the main arguments (no options---placeholder only)
set argline=(`getopt "" $argv[1-]`)
set cmdargs=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $2}'`
set argc=`echo $cmdargs | wc -w`

if ($argc == 2) then
   set argv1=`echo $cmdargs | cut -d' ' -f1`
   set argv2=`echo $cmdargs | cut -d' ' -f2`
else
   echo Usage:  display.sh [options] <project_path> <source_name>
   echo   where
   echo       <project_path> is the name of the project directory containing
   echo                 a file called qflow_vars.sh.
   echo       <source_name> is the root name of the verilog file, and
   exit 1
endif

foreach option (${argline})
   switch (${option})
      case --:
	 break
   endsw
end

set projectpath=$argv1
set sourcename=$argv2
set rootname=${sourcename:h}

# This script is called with the first argument <project_path>, which should
# have file "qflow_vars.sh".  Get all of our standard variable definitions
# from the qflow_vars.sh file.

if (! -f ${projectpath}/qflow_vars.sh ) then
   echo "Error:  Cannot find file qflow_vars.sh in path ${projectpath}"
   exit 1
endif

source ${projectpath}/qflow_vars.sh
source ${techdir}/${techname}.sh
cd ${projectpath}

#----------------------------------------------------------
# Copy the .magicrc file from the tech directory to the
# layout directory, if it does not have one.  This file
# automatically loads the correct technology file.
#----------------------------------------------------------

if (! -f ${layoutdir}/.magicrc ) then
   if ( -f ${techdir}/${magicrc} ) then
      cp ${techdir}/${magicrc} ${layoutdir}/.magicrc
   endif
endif

#----------------------------------------------------------
# Done with initialization
#----------------------------------------------------------

cd ${layoutdir}

#---------------------------------------------------
# Create magic layout (.mag file) using the
# technology LEF file to determine route widths
# and other parameters.
#---------------------------------------------------

if ($techleffile == "") then
   set lefcmd="lef read ${techdir}/${leffile}"
else
   set lefcmd="lef read ${techdir}/${techleffile}\nlef read ${techdir}/${techleffile}"
endif

# Timestamp handling:  If the .mag file is more recent
# than the .def file, then print a message and do not
# overwrite.

set docreate=1
if ( -f ${rootname}.def && -f ${rootname}.mag) then
   set defstamp=`stat --format="%Y" ${rootname}.def`
   set magstamp=`stat --format="%Y" ${rootname}.mag`
   if ( $magstamp > $defstamp ) then
      echo "Magic database file ${rootname}.mag is more recent than DEF file."
      echo "If you want to recreate the .mag file, remove or rename the existing one."
      set docreate=0
   endif
endif

# The following script reads in the DEF file and modifies labels so
# that they are rotated outward from the cell, since DEF files don't
# indicate label geometry.

if ( ${docreate} == 1) then
${bindir}/magic -dnull -noconsole <<EOF
drc off
box 0 0 0 0
snap int
${lefcmd}
def read ${rootname}
select top cell
select area labels
setlabel font FreeSans
setlabel size 0.3um
box grow s -[box height]
box grow s 100
select area labels
setlabel rotate 90
setlabel just e
select top cell
box height 100
select area labels
setlabel rotate 270
setlabel just w
select top cell
box width 100
select area labels
setlabel just w
select top cell
box grow w -[box width]
box grow w 100
select area labels
setlabel just e
save ${sourcename}
quit -noprompt
EOF

endif

# Run magic and query what graphics device types are
# available.  Use OpenGL if available, fall back on
# X11, or else exit with a message

${bindir}/magic -noconsole -d <<EOF >& .magic_displays
exit
EOF

set magicogl=`cat .magic_displays | grep OGL | wc -l`
set magicx11=`cat .magic_displays | grep X11 | wc -l`

rm -f .magic_displays

# Run magic again, this time interactively.  The script
# exits when the user exits magic.

if ( ${magicogl} >= 1 ) then
   ${bindir}/magic -d OGL ${rootname}
else if ( ${magicx11} >= 1) then
   ${bindir}/magic -d X11 ${rootname}
else
   echo "Magic does not support OpenGL or X11 graphics on this host."
endif

#------------------------------------------------------------
# Done!
#------------------------------------------------------------
