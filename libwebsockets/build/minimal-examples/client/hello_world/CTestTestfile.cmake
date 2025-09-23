# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples/client/hello_world
# Build directory: /tmp/libwebsockets/build/minimal-examples/client/hello_world
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(mss-lws-minimal-ss-hello_world "/tmp/libwebsockets/build/bin/lws-minimal-ss-hello_world")
set_tests_properties(mss-lws-minimal-ss-hello_world PROPERTIES  TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples/client/hello_world" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples/client/hello_world/CMakeLists.txt;95;add_test;/tmp/libwebsockets/minimal-examples/client/hello_world/CMakeLists.txt;0;")
