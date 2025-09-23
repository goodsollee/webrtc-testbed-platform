# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-blob
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-blob
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ssblob-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-secure-streams")
set_tests_properties(ssblob-warmcat PROPERTIES  TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-blob/CMakeLists.txt;51;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-blob/CMakeLists.txt;0;")
