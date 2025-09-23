# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-perf
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-perf
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ssperf-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-secure-streams-perf")
set_tests_properties(ssperf-warmcat PROPERTIES  TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-perf/CMakeLists.txt;50;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-perf/CMakeLists.txt;0;")
