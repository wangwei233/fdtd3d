#!/bin/bash

set -ex

# Home directory of project where root CMakeLists.txt is placed
HOME_DIR=$1

# Build directory of unit test
BUILD_DIR=$2

# CXX compiler
CXX_COMPILER=$3

# C compiler
C_COMPILER=$4

CXX11_ENABLED=$5

mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

function build
{
  for VALUE_TYPE in f d ld; do
    for TIME_STEPS in 1 2; do
      for COMPLEX_FIELD_VALUES in ON OFF; do
      for LARGE_COORDINATES in ON OFF; do
      for MPI_CLOCK in ON OFF; do

        if [ "${VALUE_TYPE}" == "ld" ] && [ "${COMPLEX_FIELD_VALUES}" == "ON" ]; then
          continue
        fi

        cmake ${HOME_DIR} -DCMAKE_BUILD_TYPE=Debug \
          -DVALUE_TYPE=${VALUE_TYPE} \
          -DCOMPLEX_FIELD_VALUES=${COMPLEX_FIELD_VALUES} \
          -DTIME_STEPS=${TIME_STEPS} \
          -DPARALLEL_GRID_DIMENSION=3 \
          -DPRINT_MESSAGE=ON \
          -DPARALLEL_GRID=ON \
          -DPARALLEL_BUFFER_DIMENSION=x \
          -DCXX11_ENABLED=${CXX11_ENABLED} \
          -DCUDA_ENABLED=OFF \
          -DCUDA_ARCH_SM_TYPE=sm_50 \
          -DLARGE_COORDINATES=${LARGE_COORDINATES} \
          -DCMAKE_CXX_COMPILER=${CXX_COMPILER} \
          -DCMAKE_C_COMPILER=${C_COMPILER} \
          -DDYNAMIC_GRID=OFF \
          -DCOMBINED_SENDRECV=OFF \
          -DMPI_CLOCK=${MPI_CLOCK}

        res=$(echo $?)

        if [[ res -ne 0 ]]; then
          exit 1
        fi

        make unit-test-clock

        res=$(echo $?)

        if [[ res -ne 0 ]]; then
          exit 1
        fi

        ./Tests/unit-test-clock

        res=$(echo $?)

        if [[ res -ne 0 ]]; then
          exit 1
        fi
      done
      done
      done
    done
  done
}

build

exit 0
