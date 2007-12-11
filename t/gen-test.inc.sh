#!/bin/bash

F="$1"
TESTNAME=${F%.c}
TESTNAME=${TESTNAME:4} # "t/nn" == 4 character

echo "CU_TestInfo ${TESTNAME}_tests[] = {"
sed -n 's/^static void test_\(.*\)(void)$/  { "\1", test_\1 },/p' $F
echo "  CU_TEST_INFO_NULL,"
echo "};"
