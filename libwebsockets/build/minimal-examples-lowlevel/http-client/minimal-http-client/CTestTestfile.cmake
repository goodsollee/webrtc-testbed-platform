# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/http-client/minimal-http-client
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(http-client-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-http-client")
set_tests_properties(http-client-warmcat PROPERTIES  TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client/CMakeLists.txt;53;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client/CMakeLists.txt;0;")
add_test(http-client-warmcat-h1 "/tmp/libwebsockets/build/bin/lws-minimal-http-client" "--h1")
set_tests_properties(http-client-warmcat-h1 PROPERTIES  TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client/CMakeLists.txt;58;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client/CMakeLists.txt;0;")
