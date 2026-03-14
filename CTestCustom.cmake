# Force sequential test execution to avoid race conditions
# when multiple tests access the same test data files
set(CTEST_PARALLEL_LEVEL 1)
