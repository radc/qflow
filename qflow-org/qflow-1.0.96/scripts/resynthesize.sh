#!/bin/tcsh -f
#
# resynthesize.sh:
#-------------------------------------------------------------------------
#
# This script runs the clock tree insertion (which load-balances all
# high-fanout networks), then re-runs parts of the synthesis script to
# regenerate the netlist for placement and routing.
#
#-------------------------------------------------------------------------
# June 2009
# Tim Edwards
# MultiGiG, Inc.
# Scotts Valley, CA
#
# Updated April 2013 Tim Edwards
# Open Circuit Design
#-------------------------------------------------------------------------

if ($#argv < 2) then
   echo Usage:  resynthesize.sh [options] <project_path> <module_name>
   exit 1
endif

# Split out options from the main arguments
set argline=`getopt "c:b:f:nx" $argv[1-]`

# Corrected 9/9/08; quotes must be added or "-n" disappears with "echo".
set options=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $1}'`
set cmdargs=`echo "$argline" | awk 'BEGIN {FS = "-- "} END {print $2}'`
set argc=`echo $cmdargs | wc -w`

if ($argc == 2) then
   set argv1=`echo $cmdargs | cut -d' ' -f1`
   set argv2=`echo $cmdargs | cut -d' ' -f2`
else
   echo Usage:  resynthesize.sh [options] <project_path> <module_name>
   echo   where
   echo	      <project_path> is the name of the project directory containing
   echo			a file called qflow_vars.sh.
   echo	      <module_name> is the root name of the verilog file, and
   echo	      [options] are passed verbatim to the AddIOToBDNet program.
   exit 1
endif

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

touch ${synthlog}

if (! ${?clocktree_options}) then
   set clocktree_options = ""
endif

# Check if last line of log file says "error condition"
set errcond = `tail -1 ${synthlog} | grep "error condition" | wc -l`
if ( ${errcond} == 1 ) then
   echo "Synthesis flow stopped on error condition.  Resynthesis will"
   echo "not proceed until error condition is cleared."
   exit 1
endif

#---------------------------------------------------------------------
# Ensure that there is a .pin file in the layout directory.  Run the
# "clock tree insertion tool" (which actually buffers all high-fanout
# nets, not just the clock).
#---------------------------------------------------------------------

if (-f ${layoutdir}/${rootname}.pin ) then
   echo "Running clocktree"
   echo "" |& tee -a ${synthlog}
   ${scriptdir}/clocktree.tcl ${rootname} ${synthdir} \
		${layoutdir} ${techdir}/${leffile} ${bufcell} \
		${clocktree_options} >> ${synthlog}
else
   echo "Error:  No pin file ${layoutdir}/${rootname}.pin." |& tee -a ${synthlog}
   echo "Did you run initial_placement.sh on this design?" |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Spot check:  Did clocktree produce file ${rootname}_tmp.blif?
#---------------------------------------------------------------------

if ( !( -f ${synthdir}/${rootname}_tmp.blif || \
	( -M ${synthdir}/${rootname}_tmp.blif \
        < -M ${layoutdir}/${rootname}.cel ))) then
   echo "clocktree failure:  No file ${rootname}_tmp.blif." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

# Make a backup of the original .blif file, then copy the modified
# one over it.

cd ${synthdir}
cp ${rootname}.blif ${rootname}_orig.blif
cp ${rootname}_tmp.blif ${rootname}.blif
rm -f ${rootname}_tmp.blif

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

if (! ${?fanout_options} ) then
   set fanout_options = "-l 75 -c 25"
endif

echo "Running blifFanout (iterative)" |& tee -a ${synthlog}
echo "" |& tee -a ${synthlog}
if (-f ${techdir}/gate.cfg && -f ${bindir}/blifFanout ) then
   set nchanged=1000
   while ($nchanged > 0)
      mv ${rootname}.blif tmp.blif
      if ("x${separator}" == "x") then
	 set sepoption=""
      else
	 set sepoption="-s ${separator}"
      endif
      ${bindir}/blifFanout ${fanout_options} -f ${rootname}_nofanout \
		-p ${techdir}/gate.cfg ${sepoption} \
		-b ${bufcell} -i ${bufpin_in} -o ${bufpin_out} \
		tmp.blif ${rootname}.blif >>& ${synthlog}
      set nchanged=$status
      echo "gates resized: $nchanged" |& tee -a ${synthlog}
   end
else
   set nchanged=0
endif

#---------------------------------------------------------------------
# Spot check:  Did blifFanout exit with an error?
#---------------------------------------------------------------------

if ( $nchanged < 0 ) then
   echo "blifFanout failure:  See ${synthlog} for error messages"
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

echo "" |& tee -a ${synthlog}
echo "Generating RTL verilog and SPICE netlist file in directory" \
	|& tee -a ${synthlog}
echo "   ${synthdir}" |& tee -a ${synthlog}
echo "Files:" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtl.v" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtlnopwr.v" |& tee -a ${synthlog}
echo "   Spice:   ${synthdir}/${rootname}.spc" |& tee -a ${synthlog}
echo "" |& tee -a ${synthlog}

echo "Running blif2Verilog." |& tee -a ${synthlog}
${bindir}/blif2Verilog -c -v ${vddnet} -g ${gndnet} ${rootname}.blif \
	> ${rootname}.rtl.v

${bindir}/blif2Verilog -c -p -v ${vddnet} -g ${gndnet} ${rootname}.blif \
	> ${rootname}.rtlnopwr.v

echo "Running blif2BSpice." |& tee -a ${synthlog}
${bindir}/blif2BSpice ${rootname}.blif -p ${vddnet} -g ${gndnet} \
	-l ${techdir}/${spicefile} > ${rootname}.spc

#---------------------------------------------------------------------
# Spot check:  Did blif2Verilog or blif2BSpice exit with an error?
# Note that these files are not critical to the main synthesis flow,
# so if they are missing, we flag a warning but do not exit.
#---------------------------------------------------------------------

if ( !( -f ${rootname}.rtl.v || \
	( -M ${rootname}.rtl.v < -M ${rootname}.blif ))) then
   echo "blif2Verilog failure:  No file ${rootname}.rtl.v created." \
		|& tee -a ${synthlog}
endif

if ( !( -f ${rootname}.rtlnopwr.v || \
	( -M ${rootname}.rtlnopwr.v < -M ${rootname}.blif ))) then
   echo "blif2Verilog failure:  No file ${rootname}.rtlnopwr.v created." \
		|& tee -a ${synthlog}
endif

if ( !( -f ${rootname}.spc || \
	( -M ${rootname}.spc < -M ${rootname}.blif ))) then
   echo "blif2BSpice failure:  No file ${rootname}.spc created." \
		|& tee -a ${synthlog}
endif

#-------------------------------------------------------------------------
# Clean up after myself!
#-------------------------------------------------------------------------

cd ${projectpath}

rm -f ${synthdir}/${rootname}_tmp.blif >& /dev/null
rm -f ${synthdir}/${rootname}_tmp_tmp.blif >& /dev/null
rm -f ${synthdir}/tmp.blif >& /dev/null
rm -f ${sourcedir}/${rootname}.blif

#-------------------------------------------------------------------------
# Regenerate the .cel file for GrayWolf
# (Assume that the .par file was already made the first time through)
#-------------------------------------------------------------------------

echo "Running blif2cel.tcl" |& tee -a ${synthlog}
${scriptdir}/blif2cel.tcl ${synthdir}/${rootname}.blif \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}.cel >>& ${synthlog}

#---------------------------------------------------------------------
# Spot check:  Did blif2cel produce file ${rootname}.cel?
#---------------------------------------------------------------------

if ( !( -f ${layoutdir}/${rootname}.cel || \
	( -M ${layoutdir}/${rootname}.cel \
        < -M ${synthdir}/${rootname}.blif ))) then
   echo "blif2cel failure:  No file ${rootname}.cel." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#-------------------------------------------------------------------------
# If placement option "initial_density" is set, run the decongest
# script.  This will annotate the .cel file with fill cells to pad
# out the area to the specified density.
#-------------------------------------------------------------------------

cd ${layoutdir}

if ( ${?initial_density} ) then
   echo "Running decongest to set initial density of ${initial_density}"
   ${scriptdir}/decongest.tcl ${rootname} ${techdir}/${leffile} \
                ${fillcell} ${initial_density} |& tee -a ${synthlog}
   cp ${rootname}.cel ${rootname}.cel.bak
   mv ${rootname}.acel ${rootname}.cel
endif

cd ${projectpath}

#---------------------------------------------------------------------

echo "Now re-run placement, and route" |& tee -a ${synthlog}
