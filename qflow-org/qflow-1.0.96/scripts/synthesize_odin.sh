#!/bin/tcsh -f
#
# synthesize_odin.sh:
#-------------------------------------------------------------------------
#
# This script synthesizes verilog files for qflow using Odin_II and ABC,
# and various preprocessors to handle verilog syntax that is beyond
# the capabilities of the synthesis tools to handle.
#
#-------------------------------------------------------------------------
# November 2006
# Steve Beccue and Tim Edwards
# MultiGiG, Inc.
# Scotts Valley, CA
# Updated 2013 Tim Edwards
# Open Circuit Design
#-------------------------------------------------------------------------

if ($#argv == 2) then
   set projectpath=$argv[1]
   set sourcename=$argv[2]
else
   echo Usage:  synthesize_odin.sh <project_path> <source_name>
   echo
   echo   where
   echo
   echo	      <project_path> is the name of the project directory containing
   echo			a file called qflow_vars.sh.
   echo
   echo	      <source_name> is the root name of the verilog file, and
   echo
   echo	      Options are set from project_vars.sh.  Use the following
   echo	      variable names:
   echo
   echo			$vpp_options	for verilog preprocessor
   echo			$odin_options	for Odin-II
   echo			$abc_options	for ABC
   echo			$abc_script	file containing alternative abc script
   echo			$fanout_options	for blifFanout
   exit 1
endif

set rootname=${sourcename:h}

#---------------------------------------------------------------------
# This script is called with the first argument <project_path>, which should
# have file "qflow_vars.sh".  Get all of our standard variable definitions
# from the qflow_vars.sh file.
#---------------------------------------------------------------------

if (! -f ${projectpath}/qflow_vars.sh ) then
   echo "Error:  Cannot find file qflow_vars.sh in path ${projectpath}"
   exit 1
endif

source ${projectpath}/qflow_vars.sh
source ${techdir}/${techname}.sh
cd ${projectpath}

# Reset the logfile
rm -f ${synthlog} >& /dev/null
touch ${synthlog}

#---------------------------------------------------------------------
# Preprocessor removes reset value initialization blocks
# from the verilog source and saves the initial states in
# a ".init" file for post-processing
#---------------------------------------------------------------------

if ( ! ${?vpp_options} ) then
   set vpp_options = ""
endif

if ("x$synthtool" == "xyosys") then
   echo "Error:  synthesize_odin called with synthtool set to yosys"
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

set vpp_style = "-s odin"

cd ${sourcedir}
echo "Running verilog preprocessor" |& tee -a ${synthlog}
${bindir}/verilogpp ${vpp_style} ${vpp_options} ${rootname}.v >>& ${synthlog}

# Hack for Odin-II bug:  to be removed when bug is fixed
${scriptdir}/vmunge.tcl ${rootname}.v >>& ${synthlog}
mv ${rootname}_munge.v ${rootname}_tmp.v

#---------------------------------------------------------------------
# Create first part of the XML config file for Odin_II
#---------------------------------------------------------------------

cat > ${rootname}.xml << EOF
<config>
   <verilog_files>
	<verilog_file>${rootname}_tmp.v</verilog_file>
EOF

#---------------------------------------------------------------------
# Recursively run preprocessor on files in ${rootname}.dep
# NOTE:  This assumes a 1:1 correspondence between filenames and
# module names;  we need to search source files for the indicated
# module names instead!
#---------------------------------------------------------------------

set alldeps = `cat ${rootname}.dep | grep -v = | sort -u | sed -e'/^\s*$/d'`
set fulldeplist = "$alldeps"

while ( "x${alldeps}" != "x" )
    set newdeps = "${alldeps}"
    set alldeps = ""
    foreach subname ( $newdeps )
	${bindir}/verilogpp ${vpp_options} ${subname}.v >>& ${synthlog}
	set deplist = `cat ${subname}.dep | grep -v = | sort -u | sed -e'/^\s*$/d'`
	set alldeps = "${alldeps} ${deplist}"
	set fulldeplist = "${fulldeplist} ${alldeps}"
    end
end

# Get a unique list of all the subcells used
set uniquedeplist = `echo $fulldeplist | sed -e "s/ /\n/g" | sort -u`

foreach subname ( $uniquedeplist )
   echo "      <verilog_file>${subname}_tmp.v</verilog_file>" \
		>> ${rootname}.xml
end

#---------------------------------------------------------------------
# Finish the XML file
#---------------------------------------------------------------------

cat >> ${rootname}.xml << EOF
   </verilog_files>
   <output>
      <output_type>blif</output_type>
      <output_path_and_name>${rootname}.blif</output_path_and_name>
   </output>
   <optimizations>
   </optimizations>
   <debug_outputs>
   </debug_outputs>
</config>
EOF

#---------------------------------------------------------------------
# Spot check:  Did vpreproc produce file ${rootname}.init?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.init || ( -M ${rootname}.init < -M ${rootname}.v ))) then
   echo "Verilog preprocessor failure:  No file ${rootname}.init." \
		|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Run odin_ii on the verilog source to get a BLIF output file
#---------------------------------------------------------------------

if ( ! ${?odin_options} ) then
   set odin_options = ""
endif

echo "Running Odin_II for verilog parsing and synthesis" |& tee -a ${synthlog}
eval ${bindir}/odin_ii ${odin_options} -U0 -c ${rootname}.xml |& tee -a ${synthlog}

#---------------------------------------------------------------------
# Check for out-of-date Odin-II version.
# NOTE:  Should get qflow's configure script to run this test at
# compile time. . .
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "invalid option" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II version error.  Odin-II needs updating."
   echo "Try code.google.com/p/vtr-verilog-to-routing/"
   echo "-----------------------------------------------"
   echo ""
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Check for Odin-II compile-time errors
#---------------------------------------------------------------------

set errline=`cat ${synthlog} | grep "core dumped" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II core dumped:"
   echo "See file ${synthlog} for details."
   echo ""
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

set errline=`cat ${synthlog} | grep "error in parsing" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Odin-II verilog preprocessor errors occurred:"
   echo "See file ${synthlog} for details."
   echo ""
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

set errline=`cat ${synthlog} | grep "Odin has decided you have failed" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "Verilog compile errors occurred:"
   echo "See file ${synthlog} for details."
   echo "----------------------------------"
   cat ${synthlog} | grep "^line"
   echo ""
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Spot check:  Did Odin-II produce file ${rootname}.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.blif || ( -M ${rootname}.blif < -M ${rootname}.init ))) then
   echo "Odin-II synthesis failure:  No file ${rootname}.blif." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# Logic optimization with abc, using the standard "resyn2" script from
# the distribution (see file abc.rc).  Map to the standard cell set and
# write a netlist-type BLIF output file of the mapped circuit.
#
# If the project_vars.sh contains a variable "abc_script", then it
# should point to a file that contains some alternative to the "resyn2"
# script.  The standard read/write commands will be executed normally,
# and the alternative script applied in between.
#---------------------------------------------------------------------

if ( ! ${?abc_options} ) then
   set abc_options = ""
endif

echo "Running abc for logic optimization" |& tee -a ${synthlog}

if ( ${?abc_script} ) then
   cat > abc.script << EOF
read_blif ${rootname}.blif
read_library ${techdir}/${techname}.genlib
read_super ${techdir}/${techname}.super
EOF
   cat ${abc_script} >> abc.script
   cat >> abc.script << EOF
map
write_blif ${rootname}_mapped.blif
quit
EOF
   ${bindir}/abc ${abc_options} -f abc.script >>& ${synthlog}
else
   ${bindir}/abc ${abc_options} >>& ${synthlog} << EOF
read_blif ${rootname}.blif
read_library ${techdir}/${techname}.genlib
read_super ${techdir}/${techname}.super
balance; rewrite; refactor; balance; rewrite; rewrite -z
balance; refactor -z; rewrite -z; balance
map
write_blif ${rootname}_mapped.blif
quit
EOF
endif

#---------------------------------------------------------------------
# Spot check:  Did ABC produce file ${rootname}_mapped.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}_mapped.blif || ( -M ${rootname}_mapped.blif \
	< -M ${rootname}.init ))) then
   echo "ABC synthesis/mapping failure:  No file ${rootname}_mapped.blif." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

set errline=`cat ${synthlog} | grep "Assertion" | grep "failed" | wc -l`
if ( $errline == 1 ) then
   echo ""
   echo "ABC exited due to failure:"
   echo "See file ${synthlog} for details."
   echo ""
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

echo "Generating resets for register flops" |& tee -a ${synthlog}
${scriptdir}/postproc.tcl ${rootname}_mapped.blif ${rootname} \
	 ${techdir}/${techname}.sh

#---------------------------------------------------------------------
# Odin_II appends "top^" to top-level signals, we want to remove these.
# It also replaces vector indexes with "~" which we want to recast to
# <>
#---------------------------------------------------------------------

echo "Cleaning Up blif file syntax" |& tee -a ${synthlog}

# The following definitions will replace "LOGIC0" and "LOGIC1"
# with buffers from gnd and vdd, respectively.  This takes care
# of technologies where tie-low and tie-high cells are not
# defined.

if ( "$tielo" == "") then
   set subs0a="/LOGIC0/s/O=/${bufpin_in}=gnd ${bufpin_out}=/"
   set subs0b="/LOGIC0/s/LOGIC0/${bufcell}/"
else
   set subs0a=""
   set subs0b=""
endif

if ( "$tiehi" == "") then
   set subs1a="/LOGIC1/s/O=/${bufpin_in}=vdd ${bufpin_out}=/"
   set subs1b="/LOGIC1/s/LOGIC1/${bufcell}/"
else
   set subs1a=""
   set subs1b=""
endif

#---------------------------------------------------------------------
# The following substitutions make the output syntax from Odin/ABC
# more palatable.  The "~i" notation for vector indices is changed to
# "<i>".  The signal notation "top.module+instance.module+instance...
# ^signal" is changed to "instance/instance/.../signal", keeping the
# hierarchy information and ensuring the uniqueness of the signal
# name, while greatly reducing the signal name lengths.  Loopback
# signals generated by "vmunge.tcl" are removed from the I/O list,
# and everywhere else returned to their original names.
#---------------------------------------------------------------------

cat ${rootname}_mapped_tmp.blif | sed -e "s/top^//g" \
	-e "/\.outputs/s/xreset_out_[^ ]*//g" \
	-e "/\.outputs/s/xloopback_out_[^ ]*//g" \
	-e "/\.inputs/s/xloopback_in_[^ ]*//g" \
	-e "s/xreset_out_//g" \
	-e "s/xloopback_out_//g" \
	-e "s/xloopback_in_//g" \
	-e "s/~\([0-9]*\)/<\1>/g" \
	-e "s/\.[^ \+]*+/\//g" \
	-e "s/=top\//=/g" \
	-e "s/\^/\//g" \
	-e "$subs0a" -e "$subs0b" -e "$subs1a" -e "$subs1b" \
	> ${synthdir}/${rootname}_tmp.blif

#---------------------------------------------------------------------
# Spot check:  Did postproc et al. produce file ${rootname}_tmp.blif?
#---------------------------------------------------------------------


if ( !( -f ${synthdir}/${rootname}_tmp.blif || \
	( -M ${synthdir}/${rootname}_tmp.blif < -M ${rootname}.init ))) then
   echo "postproc failure:  No file ${rootname}_tmp.blif." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif


# Switch to synthdir for processing of the BDNET netlist
cd ${synthdir}

#---------------------------------------------------------------------
# Add initial conditions with set and reset flops
#---------------------------------------------------------------------

echo "Restoring original names on internal DFF outputs" |& tee -a ${synthlog}
${scriptdir}/outputprep.tcl ${rootname}_tmp.blif ${rootname}.blif

#---------------------------------------------------------------------
# Spot check:  Did outputprep produce file ${rootname}.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}.blif || ( -M ${rootname}.blif \
	< -M ${rootname}_tmp.blif ))) then
   echo "outputprep failure:  No file ${rootname}.blif." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#---------------------------------------------------------------------
# NOTE:  To be restored, want to handle specific user instructions
# to either double-buffer the outputs or to latch or double-latch
# the inputs (asynchronous ones, specifically)
#---------------------------------------------------------------------
# echo "Running AddIO2blif"
# ${bindir}/AddIO2blif -t ${techdir}/${techname}.genlib \
# 	-b ${bufcell} -f ${flopcell} \
# 	${rootname}_tmp.blif > ${rootname}_buf.blif

# Make a copy of the original blif file, as this will be overwritten
# by the fanout handling process
cp ${rootname}.blif ${rootname}_bak.blif

#---------------------------------------------------------------------
# Check all gates for fanout load, and adjust gate strengths as
# necessary.  Iterate this step until all gates satisfy drive
# requirements.
#
# Use option "-o value" to force a value for the (maximum expected)
# output load, in fF.  Currently, there is no way to do this from the
# command line (default is 18fF). . .
#---------------------------------------------------------------------

rm -f ${rootname}_nofanout
touch ${rootname}_nofanout
if ($?gndnet) then
   echo $gndnet >> ${rootname}_nofanout
endif
if ($?vddnet) then
   echo $vddnet >> ${rootname}_nofanout
endif

if (! $?fanout_options) then
   set fanout_options="-l 75 -c 25"
endif

echo "Running blifFanout (iterative)" |& tee -a ${synthlog}
echo "" >> ${synthlog}
if (-f ${techdir}/gate.cfg && -f ${bindir}/blifFanout ) then
   set nchanged=1000
   while ($nchanged > 0)
      mv ${rootname}.blif tmp.blif
      ${bindir}/blifFanout ${fanout_options} -f ${rootname}_nofanout \
		-p ${techdir}/gate.cfg -s ${separator} \
		-b ${bufcell} -i ${bufpin_in} -o ${bufpin_out} \
		tmp.blif ${rootname}.blif >>& ${synthlog}
      set nchanged=$status
      echo "gates resized: $nchanged" |& tee -a ${synthlog}
   end
else
   set nchanged=0
endif

#---------------------------------------------------------------------
# Spot check:  Did blifFanout produce an error?
#---------------------------------------------------------------------

if ( $nchanged < 0 ) then
   echo "blifFanout failure.  See file ${synthlog} for error messages." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

echo "" >> ${synthlog}
echo "Generating RTL verilog and SPICE netlist file in directory" \
		|& tee -a ${synthlog}
echo "	 ${synthdir}" |& tee -a ${synthlog}
echo "Files:" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtl.v" |& tee -a ${synthlog}
echo "   Verilog: ${synthdir}/${rootname}.rtlnopwr.v" |& tee -a ${synthlog}
echo "   Spice:   ${synthdir}/${rootname}.spc" |& tee -a ${synthlog}
echo "" >> ${synthlog}

echo "Running blif2Verilog." |& tee -a ${synthlog}
${bindir}/blif2Verilog -c -v ${vddnet} -g ${gndnet} ${rootname}.blif \
	> ${rootname}.rtl.v

${bindir}/blif2Verilog -c -p ${rootname}.blif > ${rootname}.rtlnopwr.v

echo "Running blif2BSpice." |& tee -a ${synthlog}
${bindir}/blif2BSpice -p ${vddnet} -g ${gndnet} -l ${techdir}/${spicefile} \
	${rootname}.blif > ${rootname}.spc

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
# Create the .cel file for GrayWolf
#-------------------------------------------------------------------------

cd ${projectpath}

echo "Running blif2cel.tcl" |& tee -a ${synthlog}

${scriptdir}/blif2cel.tcl ${synthdir}/${rootname}.blif \
	${techdir}/${leffile} \
	${layoutdir}/${rootname}.cel >>& ${synthlog}

#---------------------------------------------------------------------
# Spot check:  Did blif2cel produce file ${rootname}.cel?
#---------------------------------------------------------------------

if ( !( -f ${layoutdir}/${rootname}.cel || ( -M ${layoutdir}/${rootname}.cel \
	< -M ${rootname}.blif ))) then
   echo "blif2cel failure:  No file ${rootname}.cel." |& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   exit 1
endif

#-------------------------------------------------------------------------
# Notice about editing should be replaced with automatic handling of
# user directions about what to put in the .par file, like number of
# rows or cell aspect ratio, etc., etc.
#-------------------------------------------------------------------------
echo "Edit ${layoutdir}/${rootname}.par, then run placement and route" \
		|& tee -a ${synthlog}
