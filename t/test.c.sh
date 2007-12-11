#!/bin/bash

exec 3>t/externs.h
exec 4>t/suites.h

for F in t/[0-9][0-9]*.c; do
  TESTNAME=${F%.c}
  TESTNAME=${TESTNAME:4} # "t/nn" == 4 character

  echo "extern CU_TestInfo ${TESTNAME}_tests[];" >&3

  if grep "${TESTNAME}_init" $F >/dev/null; then
    echo "extern int ${TESTNAME}_init(void);" >&3
    INITFUNC="${TESTNAME}_init"
  else
    INITFUNC="NULL"
  fi

  if grep "${TESTNAME}_fini" $F >/dev/null; then
    echo "extern int ${TESTNAME}_fini(void);" >&3
    FINIFUNC="${TESTNAME}_fini"
  else
    FINIFUNC="NULL"
  fi

  echo "  { \"$TESTNAME\", $INITFUNC, $FINIFUNC, ${TESTNAME}_tests }," >&4
done
