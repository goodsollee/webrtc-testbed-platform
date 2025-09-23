# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-smd
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-smd
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ss-smd "/tmp/libwebsockets/build/bin/lws-minimal-secure-streams-smd")
set_tests_properties(ss-smd PROPERTIES  TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-smd" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-smd/CMakeLists.txt;32;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-smd/CMakeLists.txt;0;")
