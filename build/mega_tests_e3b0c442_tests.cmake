add_test([=[WorldGridTest.BoundsAndBlockPlacement]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=WorldGridTest.BoundsAndBlockPlacement]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[WorldGridTest.BoundsAndBlockPlacement]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:11]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[PlayerPhysicsTest.GravityLandsOnFloorAndStepUpWorks]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=PlayerPhysicsTest.GravityLandsOnFloorAndStepUpWorks]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[PlayerPhysicsTest.GravityLandsOnFloorAndStepUpWorks]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:27]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[MegaGeneratorTest.GeneratesConnectedMultiBiomeMegastructureQuickly]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=MegaGeneratorTest.GeneratesConnectedMultiBiomeMegastructureQuickly]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[MegaGeneratorTest.GeneratesConnectedMultiBiomeMegastructureQuickly]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:49]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[GameWorldTest.RaycastAndStarterRoomSurvivalWork]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=GameWorldTest.RaycastAndStarterRoomSurvivalWork]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[GameWorldTest.RaycastAndStarterRoomSurvivalWork]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:75]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[RobotSimTest.ObservePatrolAndChaseFootKinematics]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=RobotSimTest.ObservePatrolAndChaseFootKinematics]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[RobotSimTest.ObservePatrolAndChaseFootKinematics]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:157]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
add_test([=[RobotSimTest.ObserveMapNavigationPatrolAndChase]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=RobotSimTest.ObserveMapNavigationPatrolAndChase]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[RobotSimTest.ObserveMapNavigationPatrolAndChase]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:231]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(mega_tests_TESTS [==[WorldGridTest.BoundsAndBlockPlacement]==] [==[PlayerPhysicsTest.GravityLandsOnFloorAndStepUpWorks]==] [==[MegaGeneratorTest.GeneratesConnectedMultiBiomeMegastructureQuickly]==] [==[GameWorldTest.RaycastAndStarterRoomSurvivalWork]==] [==[RobotSimTest.ObservePatrolAndChaseFootKinematics]==] [==[RobotSimTest.ObserveMapNavigationPatrolAndChase]==])
