# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-secure-streams
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/api-tests/api-test-secure-streams
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(api-test-secure-streams "/tmp/libwebsockets/build/bin/lws-api-test-secure-streams")
set_tests_properties(api-test-secure-streams PROPERTIES  TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-secure-streams" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-secure-streams/CMakeLists.txt;18;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-secure-streams/CMakeLists.txt;0;")
