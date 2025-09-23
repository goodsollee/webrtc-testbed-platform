# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/http-client/minimal-http-client-post
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(st_hcp_srv "/tmp/libwebsockets/scripts/ctest-background.sh" "hcp_srv" "/tmp/libwebsockets/build/bin/libwebsockets-test-server" "-r" "/tmp/libwebsockets/build/share/libwebsockets-test-server/" "-s" "--port" "7040")
set_tests_properties(st_hcp_srv PROPERTIES  FIXTURES_SETUP "hcp_srv" TIMEOUT "800" WORKING_DIRECTORY "." _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;61;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
add_test(ki_hcp_srv "/tmp/libwebsockets/scripts/ctest-background-kill.sh" "hcp_srv" "libwebsockets-test-server" "--port" "7040")
set_tests_properties(ki_hcp_srv PROPERTIES  FIXTURES_CLEANUP "hcp_srv" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;67;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
add_test(http-client-post "/tmp/libwebsockets/build/bin/lws-minimal-http-client-post" "-l" "--port" "7040")
set_tests_properties(http-client-post PROPERTIES  FIXTURES_REQUIRED "hcp_srv" TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;80;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
add_test(http-client-post-m "/tmp/libwebsockets/build/bin/lws-minimal-http-client-post" "-l" "-m" "--port" "7040")
set_tests_properties(http-client-post-m PROPERTIES  FIXTURES_REQUIRED "hcp_srv" TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;82;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
add_test(http-client-post-h1 "/tmp/libwebsockets/build/bin/lws-minimal-http-client-post" "-l" "--h1" "--port" "7040")
set_tests_properties(http-client-post-h1 PROPERTIES  FIXTURES_REQUIRED "hcp_srv" TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;84;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
add_test(http-client-post-m-h1 "/tmp/libwebsockets/build/bin/lws-minimal-http-client-post" "-l" "-m" "--h1" "--port" "7040")
set_tests_properties(http-client-post-m-h1 PROPERTIES  FIXTURES_REQUIRED "hcp_srv" TIMEOUT "20" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;86;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/http-client/minimal-http-client-post/CMakeLists.txt;0;")
