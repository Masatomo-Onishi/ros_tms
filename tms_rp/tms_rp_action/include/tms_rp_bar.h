#ifndef _TMS_ACTION_BAR_H_INCLUDED
#define _TMS_ACTION_BAR_H_INCLUDED

#include <tms_rp_controller.h>

#include <ros/ros.h>
#include <std_msgs/String.h>
#include <std_msgs/Int32.h>
#include <tf/transform_datatypes.h>
#include <geometry_msgs/Quaternion.h>
#include <geometry_msgs/Transform.h>
#include <geometry_msgs/TransformStamped.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/LaserScan.h>

#include <tms_msg_rc/rc_robot_control.h>
#include <tms_msg_rc/robot_control.h>

#include <tms_msg_rp/rps_path_planning.h>
#include <tms_msg_rp/rps_position.h>
#include <tms_msg_rp/rps_route.h>
#include <tms_msg_rp/rps_map_data.h>
#include <tms_msg_rp/rps_map_y.h>
#include <tms_msg_rp/rps_map_full.h>

#include <tms_msg_db/TmsdbGetData.h>
#include <tms_msg_db/Tmsdb.h>
#include <tms_msg_db/TmsdbStamped.h>

#include <tms_msg_ss/tracking_points.h>

#include <cnoid/ItemTreeView>
#include <cnoid/MessageView>
#include <cnoid/Archive>
#include <cnoid/SpinBox>
#include <cnoid/Button>
#include <cnoid/BodyItem>
#include <cnoid/ToolBar>
#include <cnoid/SignalProxy>
#include <cnoid/SceneView>
#include <cnoid/MessageView>
#include <cnoid/MainWindow>
#include <cnoid/LazyCaller>
#include <Grasp/exportdef.h>
#include <PRM/TrajectoryPlanner.h>

#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <sstream>
#include <fstream>
#include <time.h>
#include <cstdlib>
#include <string>
#include <iostream>
#include <exception>

#include <QDialog>
#include <QCheckBox>
#include <QLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QTextStream>
#include <QMessageBox>

#include <pcl/conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl_conversions/pcl_conversions.h>

#define MAX_ICS_OBJECT_NUM    25
#define MAX_FURNITURE_NUM     22

#define PERSON 1
//#define PERSON 0

using namespace std;
using namespace cnoid;

namespace grasp {
class SelectGoalPosDialog : public QDialog
{
  public:
    cnoid::SpinBox goal_pos_x_;
    cnoid::SpinBox goal_pos_y_;
    cnoid::SpinBox goal_pos_ry_;
    SelectGoalPosDialog() ;
    void okClicked();
};

class SetMapParamDialog : public QDialog
{
public:
  DoubleSpinBox x_llimit_;
  DoubleSpinBox x_ulimit_;
  DoubleSpinBox y_llimit_;
  DoubleSpinBox y_ulimit_;
  DoubleSpinBox cell_size_;
  SetMapParamDialog() ;
  void okClicked();
};

class TmsRpBar : public cnoid::ToolBar, public boost::signals::trackable {
 public:
  TmsRpBar();
  static TmsRpBar* instance();
  virtual ~TmsRpBar();

  int argc_;
  char **argv_;
  uint32_t sid_;
  Matrix3d mat0_, mat_ccw90_, mat_ccw180_, mat_cw90_;

  ros::ServiceClient get_data_client_;
  ros::ServiceClient sp5_control_client_;
  ros::ServiceClient path_planning_client_;
  ros::ServiceClient ardrone_client_;
  ros::ServiceClient request_robot_path_;
  ros::Subscriber    subscribe_umo_tracker_;

  tms_msg_ss::tracking_points    unknown_moving_object_position_;

  boost::signal<void(const cnoid::ItemList<cnoid::BodyItem>& selectedBodyItems)>& sigBodyItemSelectionChanged() {return sigBodyItemSelectionChanged_;}

  bool object_state_[MAX_ICS_OBJECT_NUM];
  static std::string object_name_[MAX_ICS_OBJECT_NUM];
  static std::string furniture_name_[MAX_FURNITURE_NUM];
  static bool is_ros_Init_;

  double goal_position_x_;
  double goal_position_y_;
  double goal_position_ry_;

  static bool production_version_;

  SgInvariantGroupPtr group_lrf_raw_data_;

  cnoid::BodyItemPtr currentBodyItem_;
  cnoid::ItemList<cnoid::BodyItem> selectedBodyItems_;
  cnoid::ItemList<cnoid::BodyItem> target_body_items_;

 private:
  MessageView& mes_;
  std::ostream& os_;
  TmsRpController& trc_;
  ros::NodeHandle nh;
  ToolButton* static_map_toggle_;
  ToolButton* dynamic_map_toggle_;
  ToolButton* local_map_toggle_;
  ToolButton* path_map_toggle_;
  ToolButton* robot_map_toggle_;
  ToolButton* point2d_toggle_;
  ToolButton* person_toggle_;

  boost::signal<void(const cnoid::ItemList<cnoid::BodyItem>& selectedBodyItems)> sigBodyItemSelectionChanged_;

  void itemSelectionChanged(const cnoid::ItemList<cnoid::BodyItem>& bodyItems);

  void simulation();
  void connectROS();
  void simulationButtonClicked();
  void connectRosButtonClicked();

  void setCollisionTargetButtonClicked();
  void makeCollisionMapButtonClicked();

  void pathPlanButtonClicked();
  void ardroneButtonClicked();
  void smartpalButtonClicked();
  void moveSmartpal();

  void receivePointCloudData(const sensor_msgs::PointCloud2::ConstPtr& msg);
  void receiveStaticMapData(const tms_msg_rp::rps_map_full::ConstPtr& msg);
  void receiveDynamicMapData(const tms_msg_rp::rps_map_full::ConstPtr& msg);
  void receivePathMapData(const tms_msg_rp::rps_route::ConstPtr& msg);
  void receiveLrfRawData1(const sensor_msgs::LaserScan::ConstPtr& msg);
  void receiveLrfRawData2(const sensor_msgs::LaserScan::ConstPtr& msg);
  void receiveUnknownMovingObjectTrackerInfo(const tms_msg_ss::tracking_points::ConstPtr& msg);

  void initPoseButtonClicked();
  void changePlanningMode();
};
}

#endif
