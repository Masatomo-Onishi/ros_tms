cmake_minimum_required(VERSION 2.8.3)
project(tms_ss_pot)

find_package(catkin REQUIRED COMPONENTS 
  roscpp 
  tms_msg_ss
  tms_msg_db
  message_generation
  )

###################################
## catkin specific configuration ##
###################################
catkin_package(
#  INCLUDE_DIRS include
   LIBRARIES tms_ss_pot
   CATKIN_DEPENDS message_runtime roscpp std_msgs
   DEPENDS system_lib
)

###########
## Build ##
###########
include_directories(
  ${catkin_INCLUDE_DIRS}
)

add_executable(pot_manager src/pot.cpp)
add_dependencies(pot_manager tms_ss_ps_gencpp 
tms_msg_ss_gencpp 
tms_msg_db_gencpp
tms_msg_rp_gencpp)
target_link_libraries(pot_manager   ${catkin_LIBRARIES})
