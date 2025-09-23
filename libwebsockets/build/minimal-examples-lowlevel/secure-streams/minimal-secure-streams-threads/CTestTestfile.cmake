# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-threads
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-threads
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ss-threads "/tmp/libwebsockets/build/bin/lws-minimal-secure-streams-threads")
set_tests_properties(ss-threads PROPERTIES  TIMEOUT "10" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-threads" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-threads/CMakeLists.txt;33;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-threads/CMakeLists.txt;0;")
