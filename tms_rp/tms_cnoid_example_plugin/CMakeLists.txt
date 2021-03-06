## cmake and project setting
cmake_minimum_required(VERSION 2.8.3)
project(example_plugin)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs 
  sensor_msgs
)

## catkin specific configuration
catkin_package(
  INCLUDE_DIRS include
  LIBRARIES example_plugin
  CATKIN_DEPENDS roscpp std_msgs sensor_msgs message_runtime
  DEPENDS eigen
)

## choreonoid specific configuration 
set(ChoreonoidPath ~/choreonoid)
set(graspPluginPath ${ChoreonoidPath}/extplugin/graspPlugin)
set(prmPath ${ChoreonoidPath}/extplugin/graspPlugin/PRM)
set(ChoreonoidLibPath ${ChoreonoidPath}/lib/choreonoid-1.4)

set(libname CnoidExamplePlugin)

## Qt specific configuration 
find_package(Qt4 REQUIRED)
set(QT_USE_QTOPENGL TRUE)
include(${QT_USE_FILE})
include_directories(${QT_INCLUDE_DIR} ${QT_OPENGL_INCLUDE_DIR})
add_definitions(-DQT_NO_KEYWORDS)
#QT4_ADD_RESOURCES(RCS src/tms_cnoid_example_icons.qrc)

## Build option
include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${ChoreonoidPath}
  ${ChoreonoidPath}/include
  ${graspPluginPath}
  ${prmPath}
  #${Eigen_INCLUDE_DIRS}
  /usr/include/eigen3
)

link_directories(
  ${ChoreonoidLibPath}
)

#add_library(
#  ${libname}
#  SHARED
#  #${RCS}
#  src/example_bar.cpp
#  src/example_plugin.cpp
#)

#add_dependencies(${libname} tms_msg_db_gencpp)

#target_link_libraries(
#  ${libname}
#  CnoidBodyPlugin
#  CnoidPoseSeqPlugin
#  CnoidGraspPlugin
#  CnoidPRMPlugin
#  ${catkin_LIBRARIES}
#)

#set_target_properties(
#  ${libname}
#  PROPERTIES LIBRARY_OUTPUT_DIRECTORY
#  ${ChoreonoidLibPath}
#)

add_definitions(-DCNOID_BACKWARD_COMPATIBILITY)
