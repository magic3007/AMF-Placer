cd build
shopt -s extglob
rm * -rf
rm .* -rf
cmake XXX/src  # Please specify the source code path
make -j8
# load MKL for parallelism
# execute the line below before you run the placer in the terminal
# source /opt/intel/compilers_and_libraries_2020.4.304/linux/mkl/bin/mklvars.sh intel64
