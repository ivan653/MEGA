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
add_test([=[GameWorldTest.RaycastAndEnemySpawningWork]=]  [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==] [==[--gtest_filter=GameWorldTest.RaycastAndEnemySpawningWork]==] --gtest_also_run_disabled_tests)
set_tests_properties([=[GameWorldTest.RaycastAndEnemySpawningWork]=]
  PROPERTIES
    
    _CMAKE_TEST_BUILD_DEPENDS [==[/home/ivan/ImportantFiles/My Games/MEGA/build/mega_tests]==]
    DEF_SOURCE_LINE [==[/home/ivan/ImportantFiles/My Games/MEGA/tests/CoreTests.cpp:75]==]
    WORKING_DIRECTORY [==[/home/ivan/ImportantFiles/My Games/MEGA/build]==]
    SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==]
    
)
set(mega_tests_TESTS [==[WorldGridTest.BoundsAndBlockPlacement]==] [==[PlayerPhysicsTest.GravityLandsOnFloorAndStepUpWorks]==] [==[MegaGeneratorTest.GeneratesConnectedMultiBiomeMegastructureQuickly]==] [==[GameWorldTest.RaycastAndEnemySpawningWork]==])
