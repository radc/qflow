#!/usr/bin/python
import re
import sys
from sys import argv

maxFreq = "NAO DEFINIDO\n"
maxFreqUnid = "Digite: \"qflow sta <nome_do_projeto>\" para obter resultados de tempo"

if len(sys.argv) <> 2:
	sys.exit ("Passe o arquivo qflow_vars.sh como parametro")

qflow_vars = open(sys.argv[1])
for line in qflow_vars:
	m = re.search('set synthlog=(.*)', line)
	if (m):
		synthDir = m.group(1)
		print "Diretorio do synth.log: "+synthDir
	
arquivo = open(synthDir)
#print "Portas utilizadas no projeto:"
arraySyn = {}
for line in arquivo:
	m = re.search('ABC RESULTS.*:\s*([A-Z]+.*) cells.*:\s*(\d+)', line)
	if (m):
		#print '\t'+m.group(1)+ ": "+  m.group(2)
		arraySyn[m.group(1)] = m.group(2)

	m = re.search('Library.*\"(.*)\"', line)
	if (m):
		techFile = m.group(1)
		print "Biblioteca de celulas: "+techFile

	m = re.search('.*maximum clock frequency.* (\d+\.?\d*) (.*)', line)
	if (m):
		maxFreq = m.group(1)
		maxFreqUnid = m.group(2)
	
#print arraySyn
print
print "Portas utilizadas no projeto:"
for elemento in arraySyn:
	print '\t'+elemento+ ": "+  arraySyn[elemento]


stdcellsFile = open(techFile)
arrayLib = {}
a = 0
for line in stdcellsFile:
	k = re.search('cell \((.*)\) { area : (\d*)',line)
	if (k):
		#print k.group(1), k.group(2)
		arrayLib[k.group(1)] = k.group(2)
	else:		
		if (a == 1):
			n = re.search('(\d+)',line)
			if (n):
				a = 0
				#print cell,n.group(1)
				arrayLib[cell] = n.group(1)	
		else:
			m = re.search('cell \((.*)\)',line)
			cell = "";
			if (m):
				a = 1
				cell = m.group(1)

gates = 0.0
for elemento in arraySyn:
	gates = gates + (float(arrayLib[elemento])/float(arrayLib['NAND2X1'])) * float(arraySyn[elemento])

print
print "NUMERO DE GATES EQUIVALENTES UTILIZADOS:", gates
print "FREQUENCIA MAXIMA DE OPERACAO:", maxFreq+" "+maxFreqUnid	



