# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples/client/http-post
# Build directory: /tmp/libwebsockets/build/minimal-examples/client/http-post
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(msshttp-post-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-ss-http-post")
set_tests_properties(msshttp-post-warmcat PROPERTIES  TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples/client/http-post" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples/client/http-post/CMakeLists.txt;90;add_test;/tmp/libwebsockets/minimal-examples/client/http-post/CMakeLists.txt;0;")
