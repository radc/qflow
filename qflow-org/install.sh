echo "proximo: apt-get install build-essential"
read $x
clear
sudo apt-get install build-essential

echo "proximo: apt-get install clang"
read $x
clear
sudo apt-get install clang

echo "proximo: apt-get install bison"
read $x
clear
sudo apt-get install bison

echo "proximo: pt-get install flex"
read $x
clear
sudo apt-get install flex

echo "proximo: pt-get install libreadline-dev"
read $x
clear
sudo apt-get install libreadline-dev

echo "proximo: pt-get install gwak"
read $x
clear
sudo apt-get install gawk

echo "proximo: pt-get install tcl-dev"
read $x
clear
sudo apt-get install tcl-dev

echo "proximo: pt-get install libffi-dev"
read $x
clear
sudo apt-get install libffi-dev

echo "proximo: pt-get install git"
read $x
clear
sudo apt-get install git

echo "proximo: pt-get install mercurial"
read $x
clear
sudo apt-get install mercurial

echo "proximo: pt-get install graphviz"
read $x
clear
sudo apt-get install graphviz

echo "proximo: pt-get install xdot"
read $x
clear
sudo apt-get install xdot

echo "proximo: pt-get install pkg-config"
read $x
clear
sudo apt-get install pkg-config

echo "proximo: pt-get install python3"
read $x
clear
sudo apt-get install python3

echo "proximo: pt-get install iverilog"
read $x
clear
sudo apt-get install iverilog

echo "proximo: pt-get install autoconf"
read $x
clear
sudo apt-get install autoconf

echo "proximo: pt-get install gperf"
read $x
clear
sudo apt-get install gperf

cd iverilog

echo "proximo: iverilog/autoconf.sh"
read $x
clear
sh autoconf.sh

chmod +x configure
echo "proximo: iverilog/configure"
read $x
clear
./configure

echo "proximo: iverilog/make"
read $x
clear
make

echo "proximo: iverilog/make install"
read $x
clear
sudo make install

cd ..
cd yosys

echo "proximo: yosys/make config-clang"
read $x
clear
make config-clang

echo "proximo: yosys/make"
read $x
clear
make

echo "proximo: yosys/make test"
read $x
clear
make test

echo "proximo: yosys/make install"
read $x
clear
sudo make install
cd ..

cd graywolf
echo "proximo: apt-get install cmake"
read $x
clear
sudo apt-get install cmake

echo "proximo: graywolf/cmake ."
read $x
clear
cmake .

echo "proximo: graywolf/make"
read $x
clear
make

echo "proximo: graywolf/make install"
read $x
clear
sudo make install
clear
cd ..

cd qrouter-1.2.34
chmod +x configure
echo "proximo: qrouter/configure"
read $x
clear
./configure

echo "proximo: qrouter/make"
read $x
clear
make

echo "proximo: qrouter/make install"
read $x
clear
sudo make install
cd ..

echo "proximo: apt-get install m4"
read $x
clear
sudo apt-get install m4

echo "proximo: apt-get install csh"
read $x
clear
sudo apt-get install csh

echo "proximo: apt-get install libx11-dev"
read $x
clear
sudo apt-get install libx11-dev

echo "proximo: apt-get install libncurses-dev"
read $x
clear
sudo apt-gcdet install libncurses-dev

echo "proximo: apt-get install tcl-dev tk-dev"
read $x
clear
sudo apt-get install tcl-dev tk-dev

echo "proximo: apt-get install blt-dev"
read $x
clear
sudo apt-get install blt-dev

echo "proximo: apt-get install freeglut3"
read $x
clear
sudo apt-get install freeglut3 

echo "proximo: apt-get install freeglut3-dev"
read $x
clear
sudo apt-get install freeglut3-dev 

echo "proximo: apt-get install freeglut3-dbg"
read $x
clear
sudo apt-get install freeglut3-dbg


cd magic-8.0.210
chmod +x configure
echo "proximo: magic/configure"
read $x
clear
./configure

echo "proximo: magic/make"
read $x
clear
make

echo "proximo: magic/make install"
read $x
clear
sudo make install
cd ..

cd qflow-1.0.96
chmod +x configure
echo "proximo: qflow/configure"
read $x
clear
./configure

echo "proximo: qflow/make"
read $x
clear
make

echo "proximo: qflow/make install"
read $x
clear
sudo make install
cd ..

echo "proximo: apt-get install tcsh"
read $x
clear
sudo apt-get install tcsh

