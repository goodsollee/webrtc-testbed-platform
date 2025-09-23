# CMake generated Testfile for 
# Source directory: /tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam
# Build directory: /tmp/libwebsockets/build/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(st_wcs_srv "/tmp/libwebsockets/scripts/ctest-background.sh" "wcs_srv" "/tmp/libwebsockets/build/bin/libwebsockets-test-server" "-r" "/tmp/libwebsockets/build/share/libwebsockets-test-server/" "-s" "--port" "7620")
set_tests_properties(st_wcs_srv PROPERTIES  FIXTURES_SETUP "wcs_srv" TIMEOUT "800" WORKING_DIRECTORY "." _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;54;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;0;")
add_test(ki_wcs_srv "/tmp/libwebsockets/scripts/ctest-background-kill.sh" "wcs_srv" "libwebsockets-test-server" "--port" "7620")
set_tests_properties(ki_wcs_srv PROPERTIES  FIXTURES_CLEANUP "wcs_srv" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;59;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;0;")
add_test(ws-client-spam "/tmp/libwebsockets/build/bin/lws-minimal-ws-client-spam" "--server" "localhost" "--port" "7620" "-l" "32" "-c" "3")
set_tests_properties(ws-client-spam PROPERTIES  FIXTURES_REQUIRED "wcs_srv" TIMEOUT "40" WORKING_DIRECTORY "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam" _BACKTRACE_TRIPLES "/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;68;add_test;/tmp/libwebsockets/minimal-examples-lowlevel/ws-client/minimal-ws-client-spam/CMakeLists.txt;0;")
