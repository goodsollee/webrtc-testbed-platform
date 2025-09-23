# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-lws_smd
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/api-tests/api-test-lws_smd
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(api-test-lws_smd "/tmp/libwebsockets/build/bin/lws-api-test-lws_smd")
set_tests_properties(api-test-lws_smd PROPERTIES  RUN_SERIAL "TRUE" TIMEOUT "60" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-lws_smd" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-lws_smd/CMakeLists.txt;15;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/api-tests/api-test-lws_smd/CMakeLists.txt;0;")
