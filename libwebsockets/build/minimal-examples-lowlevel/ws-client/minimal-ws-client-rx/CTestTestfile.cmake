# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-rx
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/ws-client/minimal-ws-client-rx
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(ws-client-rx-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-ws-client-rx" "-t")
set_tests_properties(ws-client-rx-warmcat PROPERTIES  TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-rx" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-rx/CMakeLists.txt;18;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-rx/CMakeLists.txt;0;")
