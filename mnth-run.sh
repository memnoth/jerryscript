#!/bin/sh

PWD=$(pwd)

EXTRA_CFLAGS="-pg -g -Og -DMNTH -Wno-error=pedantic"

BUILD_DIR="${PWD}/example_build"
INSTALL_DIR="${PWD}/example_install"

exec_build()
{
  ./tools/build.py \
    --builddir="${BUILD_DIR}" \
    --cmake-param="-DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}/" \
    --compile-flag="${EXTRA_CFLAGS}" \
    --error-messages=ON \
    --debug \
    --profile="es5.1"
}

exec_install()
{
  make -C ${BUILD_DIR} install
}

exec_cleanup()
{
  [ -d ${BUILD_DIR} ] && { echo "Removing example_build..."; rm -rf ${BUILD_DIR}; }
  [ -d ${INSTALL_DIR} ] && { echo "Removing example_install..."; rm -rf ${INSTALL_DIR}; }
}

case $1 in
  build)
    exec_build
    ;;
  install)
    exec_install
    ;;
  all)
    exec_build
    exec_install
    ;;
  clean)
    exec_cleanup
    ;;
  *)
    echo "Usage:" $0 "<OPT>"
    echo ""
    echo " OPT: build   - Execute jerryscript build system with customized options"
    echo "      install - Execute make install command"
    echo "      all     - Execute build and install options in a row"
    echo "      clean   - Clean up all build/install directories"
    ;;
esac

