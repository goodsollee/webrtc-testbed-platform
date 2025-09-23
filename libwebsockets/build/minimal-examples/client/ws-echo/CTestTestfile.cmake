# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples/client/ws-echo
# Build directory: /tmp/libwebsockets/build/minimal-examples/client/ws-echo
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(mssws_echo-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-ss-ws-echo")
set_tests_properties(mssws_echo-warmcat PROPERTIES  TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples/client/ws-echo" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples/client/ws-echo/CMakeLists.txt;91;add_test;/tmp/libwebsockets/minimal-examples/client/ws-echo/CMakeLists.txt;0;")
