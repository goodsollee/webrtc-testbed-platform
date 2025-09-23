# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-stress
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-stress
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ssstress-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-secure-streams-stress" "-c" "2" "--budget" "3" "--timeout_ms" "50000")
set_tests_properties(ssstress-warmcat PROPERTIES  TIMEOUT "110" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-stress" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-stress/CMakeLists.txt;51;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/secure-streams/minimal-secure-streams-stress/CMakeLists.txt;0;")
