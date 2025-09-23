# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(http-client-h2-rxflow-warmcat "/tmp/libwebsockets/build/bin/lws-minimal-http-client-h2-rxflow")
set_tests_properties(http-client-h2-rxflow-warmcat PROPERTIES  TIMEOUT "30" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow/CMakeLists.txt;20;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow/CMakeLists.txt;0;")
add_test(http-client-h2-rxflow-warmcat-h1 "/tmp/libwebsockets/build/bin/lws-minimal-http-client-h2-rxflow" "--h1")
set_tests_properties(http-client-h2-rxflow-warmcat-h1 PROPERTIES  TIMEOUT "30" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow/CMakeLists.txt;21;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-h2-rxflow/CMakeLists.txt;0;")
