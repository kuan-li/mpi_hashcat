
# do not place extra / in the end of PREFIX
PREFIX                  = /vol6/home/guo_anju/mm-rel-3.6
MPI                     = 1
CC											= /usr/local/mpi3-gcc/bin/mpicc
CC_LINUX_32             = /usr/local/mpi3-gcc/bin/mpicc
CC_LINUX_64             = /usr/local/mpi3-gcc/bin/mpicc
MM_CFLAGS								= -I /vol6/cuda7.5/cuda-7.5/include
MM_LFLAGS								= -L /vol6/cuda7.5/cuda-7.5/lib64 
DISABLE_LKW             = 1
include src/Makefile
