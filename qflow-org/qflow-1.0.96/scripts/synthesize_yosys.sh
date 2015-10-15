#!/bin/tcsh -f
#
# synthesize_yosys.sh:
#-------------------------------------------------------------------------
#
# This script synthesizes verilog files for qflow using yosys
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
   echo Usage:  synthesize_yosys.sh <project_path> <source_name>
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
   echo			$yosys_options	for yosys
   echo			$yosys_script	for yosys
   echo			$nobuffers	to bypass ybuffer
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
# Quick sanity check
#---------------------------------------------------------------------

if ("x$synthtool" == "xodin") then
   echo "Error:  synthesize_yosys called with synthtool set to odin"
   exit 1
endif

#---------------------------------------------------------------------
# Determine hierarchy by running yosys with a simple script to check
# hierarchy.  Add files until yosys no longer reports an error.
# Any error not related to a missing source file causes the script
# to rerun yosys and dump error information into the log file, and
# exit.
#---------------------------------------------------------------------

cd ${sourcedir}

set uniquedeplist = ""
set yerrcnt = 2

while ($yerrcnt > 1)

# Note:  While the use of read_liberty to allow structural verilog only
# works in yosys 0.3.1 and newer, the following line works for the
# purpose of querying the hierarchy in all versions.

cat > ${rootname}.ys << EOF
# Synthesis script for yosys created by qflow
read_liberty -lib -ignore_miss_dir -setattr blackbox ${techdir}/${libertyfile}
read_verilog ${rootname}.v
EOF

foreach subname ( $uniquedeplist )
   echo "read_verilog ${subname}.v" >> ${rootname}.ys
end

cat >> ${rootname}.ys << EOF
# Hierarchy check
hierarchy -check
EOF

set yerrors = `eval ${bindir}/yosys -s ${rootname}.ys |& sed -e "/\\/s#\\#/#g" \
		| grep ERROR`
set yerrcnt = `echo $yerrors | wc -c`

if ($yerrcnt > 1) then
   set yvalid = `echo $yerrors | grep "referenced in module" | wc -c`
   if ($yvalid > 1) then
      set newdep = `echo $yerrors | cut -d " " -f 3 | cut -c3- | cut -d "'" -f 1`
      set uniquedeplist = "${uniquedeplist} ${newdep}"
   else
      ${bindir}/yosys -s ${rootname}.ys >& ${synthlog}
      echo "Errors detected in verilog source, need to be corrected." \
		|& tee -a ${synthlog}
      echo "See file ${synthlog} for error output."
      echo "Synthesis flow stopped due to error condition." >> ${synthlog}
      exit 1
   endif
endif

# end while ($yerrcnt > 1)
end

#---------------------------------------------------------------------
# Generate the main yosys script
#---------------------------------------------------------------------

set blif_opts = ""

# Set option for generating buffers
set blif_opts = "${blif_opts} -buf ${bufcell} ${bufpin_in} ${bufpin_out}"

# Set option for generating only the flattened top-level cell
# set blif_opts = "${blif_opts} ${rootname}"

# Determine version of yosys
set versionstring = `${bindir}/yosys -V | cut -d' ' -f2`
set major = `echo $versionstring | cut -d. -f1`
set minor = `echo $versionstring | cut -d. -f2`

# Sigh. . .  versioning doesn't follow any fixed standard
set minortest = `echo $minor | cut -d+ -f2`
if ( ${minortest} == "" ) then

   set revisionstring = `echo $versionstring | cut -d. -f3`
   if ( ${revisionstring} == "" ) set revisionstring = 0
   set revision = `echo $revisionstring | cut -d+ -f1`
   set subrevision = `echo $revisionstring | cut -d+ -f2`
   if ( ${subrevision} == "" ) set subrevision = 0

else
   set revision = 0
   set minor = `echo $minor | cut -d+ -f1`
   set subrevision = ${minortest}

endif
      
cat > ${rootname}.ys << EOF
# Synthesis script for yosys created by qflow
EOF

# From yosys version 3.0.0+514, structural verilog using cells from the
# the same standard cell set that is mapped by abc is supported.
if (( ${major} == 0 && ${minor} == 3 && ${revision} == 0 && ${subrevision} >= 514) || \
    ( ${major} == 0 && ${minor} == 3 && ${revision} > 0 ) || \
    ( ${major} == 0 && ${minor} > 3 ) || \
    ( ${major} > 0) ) then
cat > ${rootname}.ys << EOF
read_liberty -lib -ignore_miss_dir -setattr blackbox ${techdir}/${libertyfile}
EOF
endif

cat > ${rootname}.ys << EOF
read_verilog ${rootname}.v
EOF

foreach subname ( $uniquedeplist )
   echo "read_verilog ${subname}.v" >> ${rootname}.ys
end

# Will not support yosys 0.0.x syntax; flag a warning instead

if ( ${major} == 0 && ${minor} == 0 ) then
   echo "Warning: yosys 0.0.x unsupported.  Please update!"
   echo "Output is likely to be incompatible with qflow."
endif

cat >> ${rootname}.ys << EOF
# High-level synthesis
hierarchy -top ${rootname}
EOF

if ( ${?yosys_script} ) then
   if ( -f ${yosys_script} ) then
      cat ${yosys_script} >> ${rootname}.ys
   else
      echo "Error: yosys script ${yosys_script} specified but not found"
   endif
else

   cat >> ${rootname}.ys << EOF

# High-level synthesis
proc; memory; opt; fsm; opt
EOF

endif

cat >> ${rootname}.ys << EOF
# Map to internal cell library
techmap; opt

# Map register flops
dfflibmap -liberty ${techdir}/${libertyfile}
opt

# Map combinatorial cells
abc -liberty ${techdir}/${libertyfile}
flatten
EOF

# Map tiehi and tielo, if they are defined

if ( ${?tiehi} && ${?tiehipin_out} ) then
   if ( "${tiehi}" != "" ) then
      echo "hilomap -hicell $tiehi $tiehipin_out" >> ${rootname}.ys  
   endif
endif

if ( ${?tielo} && ${?tielopin_out} ) then
   if ( "${tielo}" != "" ) then
      echo "hilomap -locell $tielo $tielopin_out" >> ${rootname}.ys  
   endif
endif

# Output buffering, if not specifically prevented
if ( ${major} > 0 || ${minor} > 1 ) then
   if (!($?nobuffers)) then
       cat >> ${rootname}.ys << EOF
# Output buffering
iopadmap -outpad ${bufcell} ${bufpin_in}:${bufpin_out} -bits
EOF
   endif
endif

cat >> ${rootname}.ys << EOF
# Cleanup
opt
clean
write_blif ${blif_opts} ${rootname}_mapped.blif
EOF

#---------------------------------------------------------------------
# Yosys synthesis
#---------------------------------------------------------------------

if ( ! ${?yosys_options} ) then
   set yosys_options = ""
endif

# Check if "yosys_options" specifies a script to use for yosys.
# If not, call yosys with the default script.
set usescript = `echo ${yosys_options} | grep -- -s | wc -l`

# If there is a file ${rootname}_mapped.blif, move it to a temporary
# place so we can see if yosys generates a new one or not.

if ( -f ${rootname}_mapped.blif ) then
   mv ${rootname}_mapped.blif ${rootname}_mapped_orig.blif
endif

echo "Running yosys for verilog parsing and synthesis" |& tee -a ${synthlog}
if ( ${usescript} == 1 ) then
   eval ${bindir}/yosys ${yosys_options} |& tee -a ${synthlog}
else
   eval ${bindir}/yosys ${yosys_options} -s ${rootname}.ys |& tee -a ${synthlog}
endif

#---------------------------------------------------------------------
# Spot check:  Did yosys produce file ${rootname}_mapped.blif?
#---------------------------------------------------------------------

if ( !( -f ${rootname}_mapped.blif )) then
   echo "outputprep failure:  No file ${rootname}_mapped.blif." \
	|& tee -a ${synthlog}
   echo "Premature exit." |& tee -a ${synthlog}
   echo "Synthesis flow stopped due to error condition." >> ${synthlog}
   # Replace the old blif file, if we had moved it
   if ( -f ${rootname}_mapped_orig.blif ) then
      mv ${rootname}_mapped_orig.blif ${rootname}_mapped.blif
   endif
   exit 1
else
   # Remove the old blif file, if we had moved it
   if ( -f ${rootname}_mapped_orig.blif ) then
      rm ${rootname}_mapped_orig.blif
   endif
endif

echo "Cleaning up output syntax" |& tee -a ${synthlog}
${scriptdir}/ypostproc.tcl ${rootname}_mapped.blif ${rootname} \
	${techdir}/${techname}.sh

#----------------------------------------------------------------------
# Add buffers in front of all outputs (for yosys versions before 0.2.0)
#----------------------------------------------------------------------

if ( ${major} == 0 && ${minor} < 2 ) then
   if ($?nobuffers) then
      set final_blif = "${rootname}_mapped_tmp.blif"
   else
      echo "Adding output buffers"
      ${scriptdir}/ybuffer.tcl ${rootname}_mapped_tmp.blif \
		${rootname}_mapped_buf.blif ${techdir}/${techname}.sh
      set final_blif = "${rootname}_mapped_buf.blif"
   endif
else
   # Buffers already handled within yosys
   set final_blif = "${rootname}_mapped_tmp.blif"
endif

#---------------------------------------------------------------------
# The following definitions will replace "LOGIC0" and "LOGIC1"
# with buffers from gnd and vdd, respectively.  This takes care
# of technologies where tie-low and tie-high cells are not
# defined.
#---------------------------------------------------------------------

echo "Cleaning Up blif file syntax" |& tee -a ${synthlog}

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
# Remove backslashes, references to "$techmap", and
# make local input nodes of the form $0node<a:b><c> into the
# form node<c>_FF_INPUT
#---------------------------------------------------------------------

cat ${final_blif} | sed \
	-e "$subs0a" -e "$subs0b" -e "$subs1a" -e "$subs1b" \
	-e 's/\\\([^$]\)/\1/g' \
	-e 's/$techmap//g' \
	-e 's/$0\([^ \t<]*\)<[0-9]*:[0-9]*>\([^ \t]*\)/\1\2_FF_INPUT/g' \
	> ${synthdir}/${rootname}.blif

# Switch to synthdir for processing of the BDNET netlist
cd ${synthdir}

#---------------------------------------------------------------------
# Make a copy of the original blif file, as this will be overwritten
# by the fanout handling process
#---------------------------------------------------------------------

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

${bindir}/blif2Verilog -c -p -v ${vddnet} -g ${gndnet} ${rootname}.blif \
	> ${rootname}.rtlnopwr.v

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

#-------------------------------------------------------------------------
# Notice about editing should be replaced with automatic handling of
# user directions about what to put in the .par file, like number of
# rows or cell aspect ratio, etc., etc.
#-------------------------------------------------------------------------
# echo "Edit ${layoutdir}/${rootname}.par, then run placement and route" \
#		|& tee -a ${synthlog}

set endtime = `date`
echo "Synthesis script ended on $endtime" >> $synthlog
