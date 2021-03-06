#include <tms_rp_rp.h>

//------------------------------------------------------------------------------
using namespace std;
using namespace boost;
using namespace cnoid;
using namespace grasp;

//------------------------------------------------------------------------------
double sp5arm_init_arg[26] = {	0.0,   0.0, 10.0, 10.0,	/*waist*/
				0.0, -10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, -5.0, 10.0, 10.0,/*right arm*/
				0.0,  10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 10.0 /*left arm */}; //degree

//------------------------------------------------------------------------------
tms_rp::TmsRpSubtask* tms_rp::TmsRpSubtask::instance()
{
  static tms_rp::TmsRpSubtask* instance = new tms_rp::TmsRpSubtask();
  return instance;
}

//------------------------------------------------------------------------------
tms_rp::TmsRpSubtask::TmsRpSubtask(): ToolBar("TmsRpSubtask"),
                os_(MessageView::mainInstance()->cout()), trc_(*TmsRpController::instance()) {
  sid_ = 100000;

  rp_subtask_server             = nh1.advertiseService("rp_cmd", &TmsRpSubtask::subtask, this);

  get_data_client_              = nh1.serviceClient<tms_msg_db::TmsdbGetData>("/tms_db_reader/dbreader");
  sp5_control_client_           = nh1.serviceClient<tms_msg_rc::rc_robot_control>("sp5_control");
  sp5_virtual_control_client    = nh1.serviceClient<tms_msg_rc::rc_robot_control>("sp5_virtual_control");
  kxp_virtual_control_client    = nh1.serviceClient<tms_msg_rc::rc_robot_control>("kxp_virtual_control");
  kxp_mbase_client              = nh1.serviceClient<tms_msg_rc::tms_rc_pmove>("pmove");
  v_kxp_mbase_client            = nh1.serviceClient<tms_msg_rc::tms_rc_pmove>("virtual_pmove");
  kxp_setpose_client            = nh1.serviceClient<std_srvs::Empty>("vicon_psetodom");
  katana_client                 = nh1.serviceClient<tms_msg_rc::katana_pos_array>("katana_move_angle_array");
  v_katana_client               = nh1.serviceClient<tms_msg_rc::katana_pos_array>("virtual_katana_move_angle_array");
  kobuki_virtual_control_client = nh1.serviceClient<tms_msg_rc::rc_robot_control>("kobuki_virtual_control");
  kobuki_actual_control_client  = nh1.serviceClient<tms_msg_rc::rc_robot_control>("kobuki_control");
  mkun_virtual_control_client   = nh1.serviceClient<tms_msg_rc::rc_robot_control>("mimamorukun_virtual_control");
  mkun_control_client           = nh1.serviceClient<tms_msg_rc::rc_robot_control>("mkun_goal_pose");
  voronoi_path_planning_client_ = nh1.serviceClient<tms_msg_rp::rps_voronoi_path_planning>("rps_voronoi_path_planning");
  give_obj_client               = nh1.serviceClient<tms_msg_rp::rps_goal_planning>("rps_give_obj_pos_planning");
  refrigerator_client           = nh1.serviceClient<tms_msg_rs::rs_home_appliances>("refrigerator_controller");
  state_client                  = nh1.serviceClient<tms_msg_ts::ts_state_control>("ts_state_control");

  db_pub                        = nh1.advertise<tms_msg_db::TmsdbStamped>("tms_db_data", 10);
  kobuki_sound                  = nh1.advertise<kobuki_msgs::Sound>("/mobile_base/commands/sound", 1);
  kobuki_motorpower             = nh1.advertise<kobuki_msgs::MotorPower>("/mobile_base/commands/motor_power", 1);

  sensing_sub                   = nh1.subscribe("/ods_realtime_persondt", 10, &TmsRpSubtask::sensingCallback, this);
}

//------------------------------------------------------------------------------
tms_rp::TmsRpSubtask::~TmsRpSubtask()
{
}

//------------------------------------------------------------------------------
double tms_rp::TmsRpSubtask::distance(double x1, double y1, double x2, double y2) {
	double dis;
	dis = sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
	return dis;
}

std::string tms_rp::TmsRpSubtask::DoubleToString(double number) {
	std::stringstream ss;
	ss << number;
	return ss.str();
}

//------------------------------------------------------------------------------
void tms_rp::TmsRpSubtask::send_rc_exception(int error_type) {
	tms_msg_ts::ts_state_control rc_s_srv;
	rc_s_srv.request.type = 2; // for subtask state update;
	rc_s_srv.request.state = -1;
	switch (error_type) {
	case 0:
		rc_s_srv.request.error_msg = "RC exception. Cannot set odometry";
		return;
	case 1:
		rc_s_srv.request.error_msg = "RC exception. Cannot move vehicle";
		break;
	case 2:
		rc_s_srv.request.error_msg = "RC exception. Cannot move right arm";
		break;
	case 3:
		rc_s_srv.request.error_msg = "RC exception. Cannot move lumba";
		break;
	case 4:
		rc_s_srv.request.error_msg = "RC exception. Cannot move right gripper";
		break;
	case 5:
		rc_s_srv.request.error_msg = "RC exception. Cannot run command sync obj";
		break;
	case 6:
		rc_s_srv.request.error_msg = "RC exception. Cannot run command calc background";
		break;
	}
	state_client.call(rc_s_srv);
}

//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::get_robot_pos(bool type, int robot_id, std::string& robot_name, tms_msg_rp::rps_voronoi_path_planning& rp_srv) {
	tms_msg_db::TmsdbGetData srv;

	// get robot's present location from TMSDB
	srv.request.tmsdb.id = robot_id;
	if (type == true) { // production version
		srv.request.tmsdb.sensor = 3001; // vicon system
	} else { // simulation
		srv.request.tmsdb.sensor = 3005; // fake odometry
	}

	int ref_i=0;
	if(get_data_client_.call(srv)) {
		if (!srv.response.tmsdb.empty()) {
			for (int j=0; j<srv.response.tmsdb.size()-1; j++) {
				if (srv.response.tmsdb[j].time < srv.response.tmsdb[j+1].time) ref_i = j+1;
			}
			robot_name = srv.response.tmsdb[0].name;
			rp_srv.request.start_pos.x = srv.response.tmsdb[0].x;
			rp_srv.request.start_pos.y = srv.response.tmsdb[0].y;
			rp_srv.request.start_pos.z = 0.0;
			rp_srv.request.start_pos.th = srv.response.tmsdb[0].ry; // =yaw
			rp_srv.request.start_pos.roll = 0.0; // srv.response.tmsdb[0].rr
			rp_srv.request.start_pos.pitch = 0.0; // srv.response.tmsdb[0].rp
			rp_srv.request.start_pos.yaw = srv.response.tmsdb[0].ry; // srv.response.tmsdb[0].ry
			// set odom for production version
			if (type == true && (robot_id == 2002 || robot_id == 2003)) {
				ROS_INFO("setOdom");
				double arg[1] = {0.0};
				if (!sp5_control(true, UNIT_ALL, SET_ODOM, 1, arg)) send_rc_exception(0);
			}
		} else
			return false;
	}
	else {
		ROS_INFO("Failed to call service tms_db_get_current_robot_data.\n");
		// use odometry when the robot cannot get data from vicon system
		if (type == true) {
			srv.request.tmsdb.sensor = 3003; // odometory
			if (get_data_client_.call(srv)) {
				ref_i=0;
				for (int j=0; j<srv.response.tmsdb.size()-1; j++) {
					if (srv.response.tmsdb[j].time < srv.response.tmsdb[j+1].time) ref_i = j+1;
				}
				robot_name = srv.response.tmsdb[ref_i].name;
				rp_srv.request.start_pos.x = srv.response.tmsdb[ref_i].x;
				rp_srv.request.start_pos.y = srv.response.tmsdb[ref_i].y;
				rp_srv.request.start_pos.z = 0.0;
				rp_srv.request.start_pos.th = srv.response.tmsdb[ref_i].ry;
				rp_srv.request.start_pos.roll = 0.0; // srv.response.tmsdb[ref_i].rr
				rp_srv.request.start_pos.pitch = 0.0; // srv.response.tmsdb[ref_i].rp
				rp_srv.request.start_pos.yaw = srv.response.tmsdb[ref_i].ry; // srv.response.tmsdb[ref_i].ry
			} else {
				ROS_INFO("Failed to get robot current data\n");
				return false;
			}
		}
		return false;
	}
	return true;
}

//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::subtask(tms_msg_rp::rp_cmd::Request &req,
                      tms_msg_rp::rp_cmd::Response &res)
{
	SubtaskData sd;
    sd.type = grasp::TmsRpBar::production_version_;
	sd.robot_id = req.robot_id;
	sd.arg_type = (int)req.arg.at(0);
	sd.v_arg.clear();
	for (int i=0; i<req.arg.size(); i++)
		sd.v_arg.push_back(req.arg.at(i));

	switch (req.command) {
		case 9001:
		{   // move
			ROS_INFO("[tms_rp]move command\n");
			boost::thread mo_th(boost::bind(&TmsRpSubtask::move, this, sd));
			break;
		}
		case 9002:
		{   // grasp
			ROS_INFO("[tms_rp]grasp command\n");
			boost::thread gr_th(boost::bind(&TmsRpSubtask::grasp, this, sd));
			break;
		}
		case 9003:
		{   // give
			ROS_INFO("[tms_rp]give command\n");
			boost::thread gi_th(boost::bind(&TmsRpSubtask::give, this, sd));
		    break;
		}
		case 9004:
		{   // open the refrigerator
			ROS_INFO("[tms_rp]open command\n");
			if (sd.robot_id != 2009) {
				ROS_ERROR("It is an illegal robot_id.\n");
				res.result = 0;
				return false;
			}
			boost::thread or_th(boost::bind(&TmsRpSubtask::open_ref, this));
			break;
		}
		case 9005:
		{   // close the refrigerator
			ROS_INFO("[tms_rp]close command\n");
			if (sd.robot_id != 2009) {
				ROS_ERROR("It is an illegal robot_id.\n");
				res.result = 0;
				return false;
			}
			boost::thread cr_th(boost::bind(&TmsRpSubtask::close_ref, this));
			break;
		}
		case 9006: // kobuki_random_walker
		{
			ROS_INFO("[tms_rp]random_move command\n");
			if (sd.robot_id != 2005) {
				ROS_ERROR("It is an illegal robot_id.\n");
				res.result = 0;
				return false;
			}
			boost::thread kw_th(boost::bind(&TmsRpSubtask::random_move, this));
			break;
		}
		case 9007: // sensing using kinect
		{
			ROS_INFO("[tms_rp]sensing command\n");
			if (sd.robot_id != 2005) {
				ROS_ERROR("It is an illegal robot_id.\n");
				res.result = 0;
				return false;
			}
			boost::thread se_th(boost::bind(&TmsRpSubtask::sensing, this));
			break;
		}
		default:
		{
			ROS_ERROR("No such subtask (ID : %d)\n", req.command);
			return false;
		}
	}
	ROS_INFO("Exit from subtask function.");
	res.result = 1;
	return true;
}

//------------------------------------------------------------------------------
// service caller to sp5_(virtual_)control
bool tms_rp::TmsRpSubtask::sp5_control(bool type, int unit, int cmd, int arg_size, double* arg) {
	tms_msg_rc::rc_robot_control sp_control_srv;

	sp_control_srv.request.unit = unit;
	sp_control_srv.request.cmd  = cmd;
	sp_control_srv.request.arg.resize(arg_size);
	for (int i=0; i<sp_control_srv.request.arg.size(); i++) {
		sp_control_srv.request.arg[i] = arg[i];
//		ROS_INFO("arg[%d]=%f", i, sp_control_srv.request.arg[i]);
	}

	if (type == true) {
		if (sp5_control_client_.call(sp_control_srv)) {
			ROS_INFO("result: %d", sp_control_srv.response.result);
			if (sp_control_srv.response.result == 1) return true;
			else return false;
		} else {
			ROS_ERROR("Failed to call service sp5_control");
			return false;
		}
	} else {
		if (sp5_virtual_control_client.call(sp_control_srv)) {
			ROS_INFO("result: %d", sp_control_srv.response.result);
			return true;
		} else {
			ROS_ERROR("Failed to call service sp5_control");
			return false;
		}
	}
}

//------------------------------------------------------------------------------
// service caller to kxp_(virtual_)control
bool tms_rp::TmsRpSubtask::kxp_control(bool type, int unit, int cmd, int arg_size, double* arg) {
	if (!type) {
		tms_msg_rc::rc_robot_control kxp_control_srv;
		kxp_control_srv.request.unit = unit;
		kxp_control_srv.request.cmd  = cmd;
		kxp_control_srv.request.arg.resize(arg_size);
		for (int i=0; i<kxp_control_srv.request.arg.size(); i++) {
			kxp_control_srv.request.arg[i] = arg[i];
		}

		if (kxp_virtual_control_client.call(kxp_control_srv)) {
			ROS_INFO("result: %d", kxp_control_srv.response.result);
			return true;
		} else {
			ROS_ERROR("Failed to call service kxp_control");
			return false;
		}
	}
}

//------------------------------------------------------------------------------
void tms_rp::TmsRpSubtask::sensingCallback(const tms_msg_ss::ods_person_dt::ConstPtr& msg) {
	double dis = distance(msg->p1_x, msg->p1_y, msg->p2_x, msg->p2_y);
	if (1.5 <= dis && dis <= 1.7) {
		tms_msg_ts::ts_state_control s_srv;
		s_srv.request.type = 1; // for subtask state update;
		s_srv.request.state = 0;

		ROS_INFO("Person Detection System returns True!!!");
		kobuki_msgs::Sound sound_msg;
		sound_msg.value = 4;
		kobuki_sound.publish(sound_msg);

		kobuki_msgs::MotorPower motor_msg;
		motor_msg.state = 0;
		kobuki_motorpower.publish(motor_msg);

		s_srv.request.error_msg = "Kobuki found someone lying!!";
		state_client.call(s_srv);
	}
}

//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::update_obj(int id, double x, double y, double z,
		double rr, double rp, double ry, int place, int sensor, int state, std::string note) {
	tms_msg_db::Tmsdb assign_data;
	ros::Time now = ros::Time::now() + ros::Duration(9*60*60); // GMT +9
	assign_data.time = boost::posix_time::to_iso_extended_string(now.toBoost());
	assign_data.id = id;
	if (x != -1.0) assign_data.x = x;
	if (y != -1.0) assign_data.y = y;
	if (z != -1.0) assign_data.z = z;
	if (rr != -1.0) assign_data.rr = rr;
	if (rp != -1.0) assign_data.rp = rp;
	if (ry != -1.0) assign_data.ry = ry;
	if (place != -1) assign_data.place = place;
	if (sensor != -1) assign_data.sensor = sensor;
	if (state != -1) assign_data.state = state;
	if (note != "") assign_data.note = note;

	tms_msg_db::TmsdbStamped db_msg;
	db_msg.header.frame_id  = "/world";
	db_msg.header.stamp = ros::Time::now() + ros::Duration(9*60*60); // GMT +9
	db_msg.tmsdb.push_back(assign_data);
	for (int i=0; i<3; i++)
		db_pub.publish(db_msg);
}

//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::kxp_set_odom(void) {
	std_srvs::Empty empty;
	return kxp_setpose_client.call(empty);
}

//------------------------------------------------------------------------------
// Subtask functions that is started in a thread
bool tms_rp::TmsRpSubtask::move(SubtaskData sd) {
	ROS_INFO("type:%u,robotID:%d,arg%f", sd.type, sd.robot_id, sd.v_arg.at(0));
	tms_msg_db::TmsdbGetData srv;
	tms_msg_rp::rps_voronoi_path_planning rp_srv;
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	rp_srv.request.robot_id = sd.robot_id;
	std::string robot_name("");
	for (int cnt=0; cnt<5; cnt++) {
		if(get_robot_pos(sd.type, sd.robot_id, robot_name, rp_srv))
			break;
	}
	srv.request.tmsdb.id = sd.arg_type;
	srv.request.tmsdb.sensor = 3001;
	if (sd.type == false)
		srv.request.tmsdb.sensor = 3005;
	if (sd.arg_type == -1) { // move (x,y,th)
		rp_srv.request.goal_pos.x = sd.v_arg.at(1);
		rp_srv.request.goal_pos.y = sd.v_arg.at(2);
		rp_srv.request.goal_pos.z = 0.0;
		rp_srv.request.goal_pos.th = sd.v_arg.at(3);
		rp_srv.request.goal_pos.roll = 0.0;
		rp_srv.request.goal_pos.pitch = 0.0;
		rp_srv.request.goal_pos.yaw = sd.v_arg.at(3);
	} else if ((sd.arg_type > 2000 && sd.arg_type < 3000) || (sd.arg_type > 6000 && sd.arg_type < 7000)) { // RobotID or FurnitureID
		srv.request.tmsdb.id = sd.arg_type + sid_;
		if(get_data_client_.call(srv)) {
			// analyze etcdata and get goal position
			std::string etcdata = srv.response.tmsdb[0].etcdata;
			std::vector<std::string> v_etcdata;
			v_etcdata.clear();
			boost::split(v_etcdata, etcdata, boost::is_any_of(";"));

			int i = 0;
			while (robot_name != v_etcdata.at(i)) {
				i += 2;
				if (i >= v_etcdata.size()) {
					s_srv.request.error_msg = "This robot cannot move to the slate point";
					state_client.call(s_srv);
					return false;
				}
			}
			std::string argdata = v_etcdata.at(i+1);
			std::vector<std::string> v_argdata;
			v_argdata.clear();
			boost::split(v_argdata, argdata, boost::is_any_of(",")); // v_argdata[0, 1, 2] = [goal_x, goal_y, goal_th]

			if (v_argdata.size() == 3) {
				// string to double
				std::stringstream ss;
				double d_x, d_y, d_th;

				// goal_x
				ss << v_argdata.at(0);
				ss >> d_x;
				rp_srv.request.goal_pos.x = d_x;
				ROS_INFO("goal_x = [%f]  ", d_x);

				// goal_y
				ss.clear();
				ss.str("");
				ss << v_argdata.at(1);
				ss >> d_y;
				rp_srv.request.goal_pos.y = d_y;
				ROS_INFO("goal_y = [%f]  ", d_y);

				// goal_th
				ss.clear();
				ss.str("");
				ss << v_argdata.at(2);
				ss >> d_th;
				rp_srv.request.goal_pos.th = d_th;
				ROS_INFO("goal_th = [%f]\n", d_th);

				rp_srv.request.goal_pos.z = 0.0;
				rp_srv.request.goal_pos.roll = 0.0;
				rp_srv.request.goal_pos.pitch = 0.0;
				rp_srv.request.goal_pos.yaw = 0.0;
			} else {
				s_srv.request.error_msg = "There are incorrect data in DB. Check furniture's etcdata";
				state_client.call(s_srv);
				return false;
			}
		} else {
			s_srv.request.error_msg = "Failed to call service tms_db_get_current_furniture_data";
			state_client.call(s_srv);
    		return false;
    	}
	} else if (sd.arg_type > 7000 && sd.arg_type < 8000) { // ObjectID
		ROS_INFO("Argument IDtype is Object%d!\n", sd.arg_type);
		if(get_data_client_.call(srv)) {
			if (srv.response.tmsdb[0].place > 6000 && srv.response.tmsdb[0].place < 7000) {
				srv.request.tmsdb.id = srv.response.tmsdb[0].place + sid_;
				if(get_data_client_.call(srv)) {
					std::string etcdata = srv.response.tmsdb[0].etcdata;
					ROS_INFO("etc_data = %s", etcdata.c_str());
					std::vector<std::string> v_etcdata;
					v_etcdata.clear();
					boost::split(v_etcdata, etcdata, boost::is_any_of(";"));
					for (int i=0; i<v_etcdata.size(); i++) {
						ROS_INFO("v_etcdata[%d]=%s", i, v_etcdata.at(i).c_str());
					}

					int i = 0;
					while (robot_name != v_etcdata.at(i)) {
						i += 2;
						if (i >= v_etcdata.size()) {
							s_srv.request.error_msg = "This robot cannot move to the slate point";
							state_client.call(s_srv);
							return false;
						}
					}
					std::string argdata = v_etcdata.at(i+1);
					std::vector<std::string> v_argdata;
					v_argdata.clear();
					boost::split(v_argdata, argdata, boost::is_any_of(",")); // v_argdata[0, 1, 2] = [goal_x, goal_y, goal_th]

					if (v_argdata.size() == 3) {
						// string to double
						std::stringstream ss;
						double d_x, d_y, d_th;

						// goal_x
						ss << v_argdata.at(0);
						ss >> d_x; // string to double
						rp_srv.request.goal_pos.x = d_x;
						ROS_INFO("goal_x = [%f]  ", d_x);

						// goal_y
						ss.clear();
						ss.str("");
						ss << v_argdata.at(1);
						ss >> d_y;
						rp_srv.request.goal_pos.y = d_y;
						ROS_INFO("goal_y = [%f]  ", d_y);

						// goal_th
						ss.clear();
						ss.str("");
						ss << v_argdata.at(2);
						ss >> d_th;
						rp_srv.request.goal_pos.th = d_th;
						ROS_INFO("goal_th = [%f]  ", d_th);

						rp_srv.request.goal_pos.z = 0.0;
						rp_srv.request.goal_pos.roll = 0.0;
						rp_srv.request.goal_pos.pitch = 0.0;
						rp_srv.request.goal_pos.yaw = 0.0;
					} else {
						s_srv.request.error_msg = "There are incorrect data in DB! Check furniture's etcdata";
						state_client.call(s_srv);
						return false;
					}
				} else {
					s_srv.request.error_msg = "Failed to call service tms_db_get_current_furniture_data";
					state_client.call(s_srv);
		    		return false;
		    	}
			} else {
				s_srv.request.error_msg = "Object is in unexpected place";
				state_client.call(s_srv);
				return false;
			}
		}
	} else {
		s_srv.request.error_msg = "An Illegal arg_type number";
		state_client.call(s_srv);
		return false;
	}

	ROS_INFO("start[%f,%f,%f], goal[%f,%f,%f]\n", rp_srv.request.start_pos.x, rp_srv.request.start_pos.y,
			rp_srv.request.start_pos.th, rp_srv.request.goal_pos.x, rp_srv.request.goal_pos.y, rp_srv.request.goal_pos.th);

	// =====call service "voronoi_path_planning"=====
	int i = 1;
	if (voronoi_path_planning_client_.call(rp_srv)) {
		if (!rp_srv.response.VoronoiPath.empty()) {
			while(1) {
				ROS_INFO("result:%d, message:%s", rp_srv.response.success, rp_srv.response.message.c_str());

				// call virtual_controller
				switch (sd.robot_id) {
				case 2002: // smartpal5_1
				case 2003: // smartpal5_2
					{
						double arg[3];
						arg[0] = rp_srv.response.VoronoiPath[i].x;
						arg[1] = rp_srv.response.VoronoiPath[i].y;
						arg[2] = rp_srv.response.VoronoiPath[i].th;
						ROS_INFO("arg0:%f,1:%f,2:%f", arg[0],arg[1],arg[2]);
						// fix over 180 deg bug
						if (sd.type) {
							if (((rp_srv.response.VoronoiPath[i-1].th>90 && rp_srv.response.VoronoiPath[i-1].th<=180) &&
									(rp_srv.response.VoronoiPath[i].th>=-180 && rp_srv.response.VoronoiPath[i].th<0)) ||
									((rp_srv.response.VoronoiPath[i].th>0 && rp_srv.response.VoronoiPath[i].th<=180) &&
											(rp_srv.response.VoronoiPath[i-1].th>=-180 && rp_srv.response.VoronoiPath[i-1].th<-90))) {
								double g_ang = rp_srv.response.VoronoiPath[i].th - rp_srv.response.VoronoiPath[i-1].th;
								if (g_ang > 180) g_ang = g_ang - 360;
								else if (g_ang < -180) g_ang = g_ang + 360;
								double tmp_arg[3] = {0.0, 0.0, g_ang};
								ROS_INFO("goal_ang=%f", g_ang);
								if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_REL, 3, tmp_arg)) {
									return false;
								}
							} else {
								if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_ABS, 3, arg)) {
									send_rc_exception(1);
									return false;
								}
							}
						} else {
							if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_ABS, 3, arg)) {
								send_rc_exception(1);
								return false;
							}
							sleep(1.0);
						}

			    		if (sd.type) {
			    			for (int i=0; i<5; i++) {
				    			if (sp5_control(sd.type, UNIT_ALL, SET_ODOM, 1, arg)) break;
				    			else if (i==4) send_rc_exception(0);
			    			}
			    		} else {
			    			if (sd.robot_id == 2002) {
			    				sleep(0.7);
			    			} else if (sd.robot_id == 2003) {
			    				ROS_INFO("Please use ID 2002 when smartpal5's simulation");
			    				return false;
			    			}
			    		}
			    		break;
			    	}
		    	case 2005: //kobuki
			    	{
			    		tms_msg_rc::rc_robot_control kobuki_srv;
			    		kobuki_srv.request.arg.resize(3);
			    		kobuki_srv.request.arg[0] = rp_srv.response.VoronoiPath[i].x;
			    		kobuki_srv.request.arg[1] = rp_srv.response.VoronoiPath[i].y;
			    		kobuki_srv.request.arg[2] = rp_srv.response.VoronoiPath[i].th;

			    		if (sd.type == true) {
				    		kobuki_srv.request.cmd = 0;
				    		if (kobuki_actual_control_client.call(kobuki_srv)) ROS_INFO("result: %d", kobuki_srv.response.result);
				    		else {
				    			ROS_ERROR("Failed to call service kobuki_move");
				    			return false;
				    		}
			    		} else {
				    		kobuki_srv.request.unit = 1;
				    		kobuki_srv.request.cmd = 15;
				    		if (kobuki_virtual_control_client.call(kobuki_srv)) ROS_INFO("result: %d", kobuki_srv.response.result);
				    		else {
				    			ROS_ERROR("Failed to call service virtual_kobuki_move");
				    			return false;
				    		}
				    		sleep(0.7);
			    		}
			    		break;
			    	}
			    case 2006: // kxp
			    	{
			    		if (sd.type) { // actual version
			    			tms_msg_rc::tms_rc_pmove move_srv;
			    			double dis = distance(rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
			    					    					rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y);
			    			double ang = rp_srv.response.VoronoiPath[i].th - rp_srv.response.VoronoiPath[i-1].th;
		    				if (ang > 180.0) ang = ang - 360.0;
		    				else if (ang < -180.0) ang = ang + 360.0;
			    			ROS_INFO("voronoi[%d]:(%f,%f,%f), voronoi[%d]:(%f,%f,%f)",i-1, rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
			    					rp_srv.response.VoronoiPath[i-1].th, i, rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y, rp_srv.response.VoronoiPath[i].th);
			    			if (dis != 0) {
			    				ROS_INFO("cmd1:%f", dis);
				    			move_srv.request.command = 1;
				    			move_srv.request.pdist = dis;
				    			if(kxp_mbase_client.call(move_srv)) {
				    				kxp_set_odom();
				    			} else {
				    				ROS_ERROR("Failed to call service kxp_pioneer_control");
				    				return false;
				    			}
			    			}
			    			if (ang != 0){
			    				ROS_INFO("cmd2:%f", ang);
				    			move_srv.request.command = 2;
				    			move_srv.request.pangle = ang;
				    			if (kxp_mbase_client.call(move_srv)) {
				    				kxp_set_odom();
				    			} else {
				    				ROS_ERROR("Failed to call service kxp_katana_control");
				    				return false;
				    			}
			    			}
			    		} else { // simulation version
			    			tms_msg_rc::tms_rc_pmove kxp_srv;
			    			ROS_INFO("voronoi[%d]=%f,%f,%f", i, rp_srv.response.VoronoiPath[i].x,rp_srv.response.VoronoiPath[i].y,rp_srv.response.VoronoiPath[i].th);
			    			kxp_srv.request.w_x = rp_srv.response.VoronoiPath[i].x;
			    			kxp_srv.request.w_y = rp_srv.response.VoronoiPath[i].y;
				    		kxp_srv.request.w_th = rp_srv.response.VoronoiPath[i].th;
				    		if (v_kxp_mbase_client.call(kxp_srv)) ROS_INFO("result: %d", kxp_srv.response.success);
				    		else {
				    			ROS_ERROR("Failed to call service kxp_mbase");
				    			return false;
				    		}
				    		sleep(1); //temp
				    	}
			    		break;
			    	}
		    	case 2007: //mimamorukun
			    	{
			    		i++;
			    		tms_msg_rc::rc_robot_control mkun_srv;
			    		mkun_srv.request.unit = 1;
			    		mkun_srv.request.cmd = 15;

		    			double dis = distance(rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
		    					rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y);

			    		while (dis <= 250) {
			    			i+=2;
			    			if (i > rp_srv.response.VoronoiPath.size()) {
			    				ROS_ERROR("Mimamorukun cannot move the next point. Exit");
//			    				ROS_ERROR("i > rp_srv.response.VoronoiPath.size()");
			    				return true;
			    			}
			    			dis = distance(rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
			    					rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y);
			    		}
			    		mkun_srv.request.arg.resize(3);
			    		mkun_srv.request.arg[0] = rp_srv.response.VoronoiPath[i].x;
			    		mkun_srv.request.arg[1] = rp_srv.response.VoronoiPath[i].y;
			    		mkun_srv.request.arg[2] = rp_srv.response.VoronoiPath[i].th;
			    		if (sd.type == true) {	//real world robot
			    			ROS_INFO("[i=%d] goal x=%f, y=%f, yaw=%f", i, mkun_srv.request.arg[0], mkun_srv.request.arg[1], mkun_srv.request.arg[2]);
				    		if(mkun_control_client.call(mkun_srv))
				    			ROS_INFO("result: %d", mkun_srv.response.result);
				    		else
				    		{
				    			ROS_ERROR("Failed to call service mimamorukun_move");
				    			return false;
				    		}
			    		}else{					//call srv for simulator
				    		if(mkun_virtual_control_client.call(mkun_srv))
				    			ROS_INFO("result: %d", mkun_srv.response.result);
				    		else
				    		{
				    			ROS_ERROR("Failed to call service mimamorukun_virtual_move");
				    			return false;
				    		}
			    			sleep(1); //temp
			    			}
			    		i = 2;
			    		break;
			    	}
			    default:
			    	{
						s_srv.request.error_msg = "Unsupported robot in move function";
						state_client.call(s_srv);
						return false;
			    	}
				}
				i++;
				if (i==2 && rp_srv.response.VoronoiPath.size()<=2) {
					i++;
				}
				// Update Robot Path Planning
				if (i == 3) {
					for (int cnt=0; cnt<5; cnt++) {
							if(get_robot_pos(sd.type, sd.robot_id, robot_name, rp_srv)) {
								break;
							}
					}

					// end determination
					double error_x, error_y, error_th, error_dis;
					if (sd.robot_id == 2005 && !sd.type) {
						rp_srv.request.start_pos.th+=90;
					}
					error_x = fabs(rp_srv.request.start_pos.x - rp_srv.request.goal_pos.x);
					error_y = fabs(rp_srv.request.start_pos.y - rp_srv.request.goal_pos.y);
					error_th = fabs(rp_srv.request.start_pos.th - rp_srv.request.goal_pos.th);
					error_dis = distance(rp_srv.request.start_pos.x, rp_srv.request.start_pos.y,
							rp_srv.request.goal_pos.x, rp_srv.request.goal_pos.y);
    				if (error_th > 180.0) error_th = error_th - 360.0;
    				else if (error_th < -180.0) error_th = error_th + 360.0;
					ROS_INFO("error_x:%f,error_y:%f,error_th:%f, error_dis:%f", error_x, error_y, error_th, error_dis);
					if (error_x<=10 && error_y<=10 && (error_th>=-3 && error_th<=3)) {
						ROS_INFO("finish");
						break; // dis_error:10mm, ang_error:3deg
					}
					rp_srv.response.VoronoiPath.clear();
					voronoi_path_planning_client_.call(rp_srv);
					if (rp_srv.response.VoronoiPath.empty()) {
						s_srv.request.error_msg = "Planned path is empty";
						state_client.call(s_srv);
						return false;
					}
					i = 1;
				}
			}
		} else {
			s_srv.request.error_msg = "Planned path is empty";
			state_client.call(s_srv);
			return false;
		}
	} else {
		s_srv.request.error_msg = "Failed to call service /rps_voronoi_path_planning";
		state_client.call(s_srv);
		return false;
	}
	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

bool tms_rp::TmsRpSubtask::grasp(SubtaskData sd) {
	tms_msg_db::TmsdbGetData srv;
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	nh1.setParam("planning_mode", 1); // stop ROS-TMS viewer
	if ((sd.robot_id == 2002 || sd.robot_id == 2003) && sd.type == true) {
		if (!sp5_control(sd.type, UNIT_ARM_R, CMD_MOVE_ABS , 8, sp5arm_init_arg+4)) {
			send_rc_exception(2);
			return false;
		}
		if (!sp5_control(sd.type, UNIT_LUMBA, CMD_MOVE_REL, 4, sp5arm_init_arg)) {
			send_rc_exception(3);
			return false;
		}
		if (!sp5_control(sd.type, UNIT_GRIPPER_R, CMD_MOVE_ABS, 3, sp5arm_init_arg+12)) {
			send_rc_exception(4);
			return false;
		}
	}

	grasp::PlanBase *pb = grasp::PlanBase::instance();
	std::vector<pathInfo> trajectory;
	int state;
	std::vector<double> begin;
	std::vector<double> end;

	// SET ROBOT
	switch(sd.robot_id) {
	case 2002:
		callSynchronously(bind(&grasp::TmsRpController::createRobotRecord,trc_,2002,"smartpal5_1"));
		break;
	case 2003:
		callSynchronously(bind(&grasp::TmsRpController::createRobotRecord,trc_,2003,"smartpal5_2"));
		break;
	case 2006:
		callSynchronously(bind(&grasp::TmsRpController::createRobotRecord,trc_,2006,"kxp"));
		break;
	default:
		ROS_ERROR("An illegal robot id");
		return false;
	}

	// SET OBJECT objectID : 7001~7999
	if(sd.arg_type > 7000 && sd.arg_type < 8000) {
		srv.request.tmsdb.id = sd.arg_type;
		if (get_data_client_.call(srv)) {
			int ref_i=0;
			for (int j=0; j<srv.response.tmsdb.size()-1; j++) {
				if (srv.response.tmsdb[j].time < srv.response.tmsdb[j+1].time) ref_i = j+1;
			}
			std::string obj_name = srv.response.tmsdb[ref_i].name;
			cnoid::BodyItemPtr item = trc_.objTag2Item()[obj_name];
			callSynchronously(bind(&grasp::PlanBase::SetGraspedObject,pb,item));
			ROS_INFO("%s is grasped_object.\n", pb->targetObject->bodyItemObject->name().c_str());

			// SET ENVIRONMENT envID : 6001~6999
			int furniture_id = srv.response.tmsdb[ref_i].place;
			cout << furniture_id << endl;
			if (furniture_id > 6000 && furniture_id < 7000) {
				srv.request.tmsdb.id = furniture_id;
				if(get_data_client_.call(srv)) {
					std::string furniture_name = srv.response.tmsdb[0].name;
					cnoid::BodyItemPtr item2 = trc_.objTag2Item()[furniture_name];
					callSynchronously(bind(&grasp::PlanBase::SetEnvironment,pb,item2));
				}
			}
		} else {
			s_srv.request.error_msg = "Cannot get object's position";
			state_client.call(s_srv);
			return false;
		}
	} else {
		ROS_INFO("Robot cannot grasp this object! Wrong id:%d\n", sd.arg_type);
	}

	if( !pb->targetObject || !pb->robTag2Arm.size()) {
		s_srv.request.error_msg = "Object and robot is not set now";
		state_client.call(s_srv);
		return false;
	}
	for(int i=0;i<pb->bodyItemRobot()->body()->numJoints();i++){ // If initial position is not collided, it is stored as
		double q = pb->bodyItemRobot()->body()->joint(i)->q(); // present position
		begin.push_back(q);
		end.push_back(q);
	}
	cout << "set begin and end posture." << endl;

	callSynchronously(bind(&grasp::TmsRpController::setTolerance,trc_,0.05));
	cout << "set tolerance." << endl;

	callSynchronously(bind(&grasp::TmsRpController::graspPathPlanStart,trc_,
			0, begin, end, pb->targetArmFinger->name, pb->targetObject->name(), 1, &trajectory, &state));

	ROS_INFO("trajectory=%zd", trajectory.size());

	double arg[13];
	cnoid::Vector3 rotation, r_rotation;

	if (!trajectory.empty()) {
		arg[0] = trajectory.back().robotPos(0)*1000; // m -> mm
		arg[1] = trajectory.back().robotPos(1)*1000;
		r_rotation = grasp::rpyFromRot(trajectory.back().robotOri);
		arg[2] = rad2deg(r_rotation(2));
		arg[3] = 2;

		pb->calcForwardKinematics();
		pb->targetObject->objVisPos = pb->object()->p();
		pb->targetObject->objVisRot = pb->object()->R();

		rotation = grasp::rpyFromRot(pb->targetObject->objVisRot);
		ROS_INFO("object_x=%fm,y=%fm, z=%f\n", pb->targetObject->objVisPos(0), pb->targetObject->objVisPos(1), pb->targetObject->objVisPos(2));
		ROS_INFO("object_rr=%f,rp=%f, ry=%f\n", rotation(0), rotation(1), rotation(2));

		arg[4] = sd.arg_type;
		arg[5] = pb->targetObject->objVisPos(0)*1000; // m -> mm;
		arg[6] = pb->targetObject->objVisPos(1)*1000;
		arg[7] = pb->targetObject->objVisPos(2)*1000;
		arg[8] = rad2deg(rotation(0));
		arg[9] = rad2deg(rotation(1));
		arg[10] = rad2deg(rotation(2));

		arg[11] = sd.robot_id;// place
		arg[12] = 2;// state

		switch (sd.robot_id) {
		case 2002: // for smartpal simulation
		{
			if (sd.type == false) {
				sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_REL, 3, arg);
				update_obj(sd.arg_type, arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], 3005, arg[12], "");
			}
		}
		case 2003:
		{
			double sp5arm_arg[26] = {0.0, 0.0, 10.0, 10.0,	/*waist*/
					0.0, -10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 10.0,/*right arm*/
					0.0,  10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 10.0, 0.0, 10.0, 10.0 /*left arm */}; //degree
			if (sd.type == true) {
				if (!sp5_control(sd.type, UNIT_ALL, SET_ODOM, 1, arg)) send_rc_exception(0);
				for (int t=0; t<trajectory.size(); t++) {
					for (int u=0; u<trajectory.at(t).joints.size(); u++) {
						ROS_INFO("joint[%d][%d]=%f", t, u, rad2deg(trajectory.at(t).joints[u]));
						if (u==0 || u==1)
							sp5arm_arg[u] = rad2deg(trajectory.at(t).joints[u]);
						else if (u>=2 && u<=8)
							sp5arm_arg[u+2] = rad2deg(trajectory.at(t).joints[u]);
						else if (u==9)
							sp5arm_arg[u+3] = rad2deg(trajectory.at(t).joints[u]);
					}
					// send command to RC
					if (!sp5_control(sd.type, UNIT_LUMBA, CMD_MOVE_REL, 4, sp5arm_arg)) {
						send_rc_exception(3);
						return false;
					}
					if (!sp5_control(sd.type, UNIT_GRIPPER_R, CMD_MOVE_ABS, 3, sp5arm_arg+12)) {
						send_rc_exception(4);
						return false;
					}
					if (!sp5_control(sd.type, UNIT_ARM_R, CMD_MOVE_ABS , 8, sp5arm_arg+4)) {
						send_rc_exception(2);
						return false;
					}
				}
				update_obj(sd.arg_type, arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], 3001, arg[12], "");
			} else {
				for (int t=0; t<trajectory.size(); t++) {
					for (int u=0; u<trajectory.at(t).joints.size(); u++) {
						ROS_INFO("joint[%d][%d]=%f", t, u, rad2deg(trajectory.at(t).joints[u]));
					}
				}
			}
			break;
		}
		case 2006:
		{
			if (sd.type == false) {
				for (int t=0; t<trajectory.size(); t++)
					for (int u=0; u<trajectory.at(t).joints.size(); u++)
						ROS_INFO("joint[%d][%d]=%f", t, u, rad2deg(trajectory.at(t).joints[u]));
				kxp_control(sd.type, UNIT_ALL, CMD_SYNC_OBJ, 13, arg);
				update_obj(sd.arg_type, arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], 3005, arg[12], "");
			} else {
				tms_msg_rc::katana_pos_array katana_srv;
				tms_msg_rc::katana_pos argument;
				katana_srv.request.pose_array.clear();

				for (int t=0; t<trajectory.size(); t++) {
					argument.pose.clear();
					for (int u=0; u<trajectory.at(t).joints.size(); u++) {
						if (u==5 || u==6) {
							ROS_INFO("joint[%d][%d]=%f", t, u, (rad2deg(trajectory.at(t).joints[u]))+25);
							argument.pose.push_back((rad2deg(trajectory.at(t).joints[u])) + 25);
						} else {
							ROS_INFO("joint[%d][%d]=%f", t, u, (rad2deg(trajectory.at(t).joints[u])));
							argument.pose.push_back((rad2deg(trajectory.at(t).joints[u])));
						}
					}
					katana_srv.request.pose_array.push_back(argument);
				}
				if (!katana_client.call(katana_srv)) {
					ROS_ERROR("Failed to call katana_srv");
					return false;
				}
				update_obj(sd.arg_type, arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], 3001, arg[12], "");
			}
			break;
		}
		default:
			s_srv.request.error_msg = "Unsupported robot in grasp function";
			state_client.call(s_srv);
			return false;
		}
	}
	nh1.setParam("planning_mode", 0);

	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

bool tms_rp::TmsRpSubtask::give(SubtaskData sd) {
	tms_msg_rp::rps_voronoi_path_planning rp_srv;
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;
	int sensor_id = 3001;
	if (sd.type == false) sensor_id = 3005;

	nh1.setParam("grasping", 1); // switching model for display

	// call service for give_obj_planning
	tms_msg_rp::rps_goal_planning gop_srv;
	gop_srv.request.robot_id = sd.robot_id;
	gop_srv.request.target_id = sd.arg_type; // person's ID
	ROS_INFO("robot_id:%d, person_id:%d\n",gop_srv.request.robot_id, gop_srv.request.target_id);

	double goal_arg[19] = {-1,-1,-1,-1,-1,-1,0,0,90,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
	double goal_joint[18];

	if (give_obj_client.call(gop_srv)) {
		if (!gop_srv.response.goal_pos.empty()) {
			goal_arg[0] = gop_srv.response.goal_pos[0].x;
			goal_arg[1] = gop_srv.response.goal_pos[0].y;
			goal_arg[2] = gop_srv.response.goal_pos[0].th;
			ROS_INFO("goal_rx:%fmm,goal_ry:%fmm,goal_rth:%fdeg", goal_arg[0], goal_arg[1], goal_arg[2]);
			goal_arg[3] = gop_srv.response.target_pos.x;
			goal_arg[4] = gop_srv.response.target_pos.y;
			goal_arg[5] = gop_srv.response.target_pos.z;
			ROS_INFO("goal_ox:%fmm,goal_oy:%fmm,goal_oth:%fdeg", goal_arg[3], goal_arg[4], goal_arg[5]);
		}
		// robot's joint angles(j0~j17)
		if (!gop_srv.response.joint_angle_array.empty()) {
			for (int j=0; j<gop_srv.response.joint_angle_array.at(0).joint_angle.size(); j++)
				goal_joint[j] = gop_srv.response.joint_angle_array.at(0).joint_angle.at(j);
			for (int j=0; j<9; j++) {
				goal_arg[j+9] = goal_joint[j];
				ROS_INFO("goal_joint[%d]=%f", j+9, goal_arg[j+9]);
			}
		}
	} else {
		s_srv.request.error_msg = "Failed to call give_object_service";
		state_client.call(s_srv);
		return false;
	}

	// move base
	rp_srv.request.robot_id = sd.robot_id;
	std::string robot_name("");
	for (int cnt=0; cnt<5; cnt++) {
			if(get_robot_pos(sd.type, sd.robot_id, robot_name, rp_srv))
				break;
		}

	rp_srv.request.goal_pos.x = goal_arg[0];
	rp_srv.request.goal_pos.y = goal_arg[1];
	rp_srv.request.goal_pos.th = goal_arg[2];

	int i = 1;
	double arg[13];
	if (voronoi_path_planning_client_.call(rp_srv)) {
		if (!rp_srv.response.VoronoiPath.empty()) {
			grasp::PlanBase *pb = grasp::PlanBase::instance();
			callSynchronously(bind(&grasp::PlanBase::setGraspingState,pb,grasp::PlanBase::GRASPING));

			cnoid::Vector3 position;
			cnoid::Vector3 rotation;
			while(1) {
				arg[0] = rp_srv.response.VoronoiPath[i].x;
				arg[1] = rp_srv.response.VoronoiPath[i].y;
				arg[2] = rp_srv.response.VoronoiPath[i].th;

				if (sd.robot_id == 2002 || sd.robot_id == 2003) {
					if (sd.type) {
						if (((rp_srv.response.VoronoiPath[i-1].th>90 && rp_srv.response.VoronoiPath[i-1].th<=180) &&
								(rp_srv.response.VoronoiPath[i].th>=-180 && rp_srv.response.VoronoiPath[i].th<0)) ||
								((rp_srv.response.VoronoiPath[i].th>0 && rp_srv.response.VoronoiPath[i].th<=180) &&
								(rp_srv.response.VoronoiPath[i-1].th>=-180 && rp_srv.response.VoronoiPath[i-1].th<-90))) {
							double g_ang = rp_srv.response.VoronoiPath[i].th - rp_srv.response.VoronoiPath[i-1].th;
							if (g_ang > 180) g_ang = g_ang - 360;
							else if (g_ang < -180) g_ang = g_ang + 360;
							double tmp_arg[3] = {0.0, 0.0, g_ang};
							ROS_INFO("goal_ang=%f", g_ang);
							if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_REL, 3, tmp_arg)) {
								return false;
							}
						} else {
							if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_ABS, 3, arg)) {
								send_rc_exception(1);
								return false;
							}
						}
					} else {
						if (!sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_ABS, 3, arg)) {
							send_rc_exception(1);
							return false;
						}
						sleep(1.0);
					}
					if (sd.type == true) {
						if (!sp5_control(sd.type, UNIT_ALL, SET_ODOM, 1, arg)) send_rc_exception(0);
					} else {
						sleep(0.7);
					}
				} else if (sd.robot_id == 2006) {
					if (sd.type == true) {
		    			tms_msg_rc::tms_rc_pmove move_srv;
		    			double dis = distance(rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
		    					    					rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y);
		    			double ang = rp_srv.response.VoronoiPath[i].th - rp_srv.response.VoronoiPath[i-1].th;
	    				if (ang > 180.0) ang = ang - 360.0;
	    				else if (ang < -180.0) ang = ang + 360.0;
		    			ROS_INFO("voronoi[%d]:(%f,%f,%f), voronoi[%d]:(%f,%f,%f)",i-1, rp_srv.response.VoronoiPath[i-1].x, rp_srv.response.VoronoiPath[i-1].y,
		    					rp_srv.response.VoronoiPath[i-1].th, i, rp_srv.response.VoronoiPath[i].x, rp_srv.response.VoronoiPath[i].y, rp_srv.response.VoronoiPath[i].th);
		    			if (dis != 0) {
		    				ROS_INFO("cmd1:%f", dis);
			    			move_srv.request.command = 1;
			    			move_srv.request.pdist = dis;
			    			if(kxp_mbase_client.call(move_srv)) {
			    				kxp_set_odom();
			    			} else {
			    				ROS_ERROR("Failed to call service kxp_pioneer_control");
			    				return false;
			    			}
		    			}
		    			if (ang != 0){
		    				ROS_INFO("cmd2:%f", ang);
			    			move_srv.request.command = 2;
			    			move_srv.request.pangle = ang;
			    			if (kxp_mbase_client.call(move_srv)) {
			    				kxp_set_odom();
			    			} else {
			    				ROS_ERROR("Failed to call service kxp_katana_control");
			    				return false;
			    			}
		    			}
					} else {
						tms_msg_rc::tms_rc_pmove kxp_srv;
						kxp_srv.request.w_x = arg[0];
						kxp_srv.request.w_y = arg[1];
						kxp_srv.request.w_th = arg[2];
						if (v_kxp_mbase_client.call(kxp_srv)) ROS_INFO("result: %d", kxp_srv.response.success);
						else                  ROS_ERROR("Failed to call service kxp_mbase");
						sleep(0.7);
					}
				}

				pb->bodyItemRobot()->body()->calcForwardKinematics();
				rotation = grasp::rpyFromRot(pb->fingers(0)->tip->R()*(pb->targetArmFinger->objectPalmRot));
				position = pb->fingers(0)->tip->p()+pb->fingers(0)->tip->R()*pb->targetArmFinger->objectPalmPos;

				ROS_INFO("robot_x=%f,y=%f,z=%f", arg[0], arg[1], arg[2]);
				ROS_INFO("object_x=%f,y=%f,z=%f,rr=%f,rp=%f,ry=%f\n", position(0), position(1), position(2), rad2deg(rotation(0)), rad2deg(rotation(1)), rad2deg(rotation(2)));

				tms_msg_db::TmsdbGetData name_srv;
				name_srv.request.tmsdb.name = pb->targetObject->bodyItemObject->name();
				get_data_client_.call(name_srv);
				arg[3] = 2; // robot_state
				arg[4] = (double)name_srv.response.tmsdb[0].id; // obj_id
				arg[5] = position(0)*1000; // m -> mm;
				arg[6] = position(1)*1000;
				arg[7] = position(2)*1000;
				arg[8] = rad2deg(rotation(0));
				arg[9] = rad2deg(rotation(1));
				arg[10] = rad2deg(rotation(2));
				arg[11] = sd.robot_id;
				arg[12] = 2; // obj_state

				if (sd.robot_id == 2002 || sd.robot_id == 2003) {
					if (sd.type == false) {
						sp5_control(sd.type, UNIT_VEHICLE, CMD_MOVE_REL, 3, arg);
					}
				} else if (sd.robot_id == 2006) {
					if (sd.type == false) {
						kxp_control(sd.type, UNIT_ALL, CMD_SYNC_OBJ, 13, arg);
		    		}
				}

				std::string s_note("");
				for (int c=0; c<3; c++) {
					s_note += DoubleToString(arg[c]);
					if (c != 2)
						s_note += ";";
				}
				ROS_INFO("note=%s", s_note.c_str());
				update_obj(arg[4], arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], sensor_id, arg[12], s_note);
				i++;
				if (i==2 && rp_srv.response.VoronoiPath.size()==2) i++;
				// Update Robot Path Planning
				if (i == 3) {
					sleep(1);
					for (int cnt=0; cnt<5; cnt++) {
						if(get_robot_pos(sd.type, sd.robot_id, robot_name, rp_srv))
							break;
					}

					// end determination
					double error_x, error_y, error_th;
					error_x = fabs(rp_srv.request.start_pos.x - rp_srv.request.goal_pos.x);
					error_y = fabs(rp_srv.request.start_pos.y - rp_srv.request.goal_pos.y);
					error_th = fabs(rp_srv.request.start_pos.th - rp_srv.request.goal_pos.th);
					if (error_th > 180.0) error_th = error_th - 360.0;
					else if (error_th < -180.0) error_th = error_th + 360.0;
					ROS_INFO("error_x:%f,error_y:%f,error_th:%f", error_x, error_y, error_th);
					if (error_x<=10 && error_y<=10 && (error_th>=-3 && error_th<=3)) {
						ROS_INFO("finish");
						break; // dis_error:10mm, ang_error:3deg
					}
					rp_srv.response.VoronoiPath.clear();
					voronoi_path_planning_client_.call(rp_srv);
					if (rp_srv.response.VoronoiPath.empty()) {
						s_srv.request.error_msg = "Planned path is empty";
						state_client.call(s_srv);
						return false;
					}
					i = 1;
				}
			}
		} else {
			s_srv.request.error_msg = "Update planned path is empty";
			state_client.call(s_srv);
			return false;
		}
	} else {
		s_srv.request.error_msg = "Failed to call service /rps_voronoi_path_planning";
		state_client.call(s_srv);
		return false;
	}
	// move joint
	if (sd.robot_id == 2002 || sd.robot_id == 2003) {
		double sp5give_arg[12] = {goal_arg[10], goal_arg[9], 10.0, 10.0,
				goal_arg[11], goal_arg[12], goal_arg[13], goal_arg[14], goal_arg[15], goal_arg[16], goal_arg[17], 10.0}; //degree
		// send command to RC
		if (!sp5_control(sd.type, UNIT_LUMBA, CMD_MOVE_REL, 4, sp5give_arg)) {
			send_rc_exception(3);
			return false;
		}
		if (!sp5_control(sd.type, UNIT_ARM_R, CMD_MOVE_ABS , 8, sp5give_arg+4)) {
			send_rc_exception(2);
			return false;
		}
		// update object pos
		update_obj(arg[4], goal_arg[3], goal_arg[4], goal_arg[5], 0, 0, 90, arg[11], sensor_id, arg[12], "");

		// give object
//		double openG_arg[3] = {-40,7,7};
//		if (!sp5_control(sd.type, UNIT_GRIPPER_R, CMD_MOVE_ABS, 3, openG_arg)) {
//			send_rc_exception(4);
//			return false;
//		}
//		// transition initial posture
//		if (!sp5_control(sd.type, UNIT_LUMBA, CMD_MOVE_REL, 4, sp5arm_init_arg)) {
//			send_rc_exception(3);
//			return false;
//		}
//		if (!sp5_control(sd.type, UNIT_GRIPPER_R, CMD_MOVE_ABS, 3, sp5arm_init_arg+12)) {
//			send_rc_exception(4);
//			return false;
//		}
//		if (!sp5_control(sd.type, UNIT_ARM_R, CMD_MOVE_ABS , 8, sp5arm_init_arg+4)) {
//			send_rc_exception(2);
//			return false;
//		}
	} else if (sd.robot_id == 2006) {
		double give_pose[7] = {0.0, 0.0, 0.0, 46.8, 0.0, 24.0, 24.0};
		tms_msg_rc::katana_pos_array katana_srv;
		tms_msg_rc::katana_pos argument;
		katana_srv.request.pose_array.clear();
		argument.pose.clear();
		for (int j=0; j<7; j++)
			argument.pose.push_back(give_pose[j]);
		katana_srv.request.pose_array.push_back(argument);

		if (sd.type == true) katana_client.call(katana_srv);
		else v_katana_client.call(katana_srv);
//		update_obj(sd.arg_type, arg[5], arg[6], arg[7], arg[8], arg[9], arg[10], arg[11], 3001, arg[12], "");
	}
	nh1.setParam("grasping", 0);

	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

bool tms_rp::TmsRpSubtask::open_ref(void) {
	tms_msg_rs::rs_home_appliances ref_srv;
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	ref_srv.request.id = 2009;
	ref_srv.request.service = 1;
	if (refrigerator_client.call(ref_srv)) {}
	else {
		s_srv.request.error_msg = "Failed to call service control_refrigerator";
		state_client.call(s_srv);
	    return false;
	}
	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

bool tms_rp::TmsRpSubtask::close_ref(void) {
	tms_msg_rs::rs_home_appliances ref_srv;
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	ref_srv.request.id = 2009;
	ref_srv.request.service = 0;
	if (refrigerator_client.call(ref_srv)) {}
	else {
		s_srv.request.error_msg = "Failed to call service control_refrigerator";
		state_client.call(s_srv);
	    return false;
	}
	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}
//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::random_move(void) {
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	int ret;
	const char buf[] = "roslaunch kobuki_random_walker safe_random_walker_app.launch\n";
	ROS_INFO("%s\n", buf);

	ret = std::system(buf);
	if(ret != 0){
		s_srv.request.error_msg = "Excute command error";
		state_client.call(s_srv);
		return false;
	  }
	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

//------------------------------------------------------------------------------
bool tms_rp::TmsRpSubtask::sensing(void) {
	tms_msg_ts::ts_state_control s_srv;
	s_srv.request.type = 1; // for subtask state update;
	s_srv.request.state = 0;

	int ret;
	const char buf[] = "rosrun tms_ss_ods_person_detection ods_realtime_persondt\n";
	ROS_INFO("%s\n", buf);

	ret = std::system(buf);
	if(ret != 0){
		s_srv.request.error_msg = "Excute command error";
		state_client.call(s_srv);
		return false;
	}
	// ======= add end determination? =======
	return true;
	//	apprise TS_control of succeeding subtask execution
	s_srv.request.state = 1;
	state_client.call(s_srv);
	return true;
}

//------------------------------------------------------------------------------
tms_rp::TmsRpView* tms_rp::TmsRpView::instance()
{
  static tms_rp::TmsRpView* instance = new tms_rp::TmsRpView();
  return instance;
}

//------------------------------------------------------------------------------
tms_rp::TmsRpView::TmsRpView(): ToolBar("TmsRpView"), trc_(*TmsRpController::instance()) {
	static ros::NodeHandle nh2;
	rp_blink_arrow_server  = nh2.advertiseService("rp_arrow", &TmsRpView::blink_arrow, this);

	mat0_       <<  1, 0, 0, 0, 1, 0, 0, 0, 1;  //   0
}

//------------------------------------------------------------------------------
tms_rp::TmsRpView::~TmsRpView()
{
}

//------------------------------------------------------------------------------
//for UR m100
bool tms_rp::TmsRpView::blink_arrow(tms_msg_rp::rp_arrow::Request &req,
                           tms_msg_rp::rp_arrow::Response &res)
{
  int32_t id   = req.id;
  int32_t mode = req.mode;
  string arrow_name;

  if (id==20001) {
    arrow_name = "blink_arrow";
  }
  else if (id==20002) {
    arrow_name = "person_marker1";
  }
  else if (id==20003) {
    arrow_name = "person_marker2";
  }
  else if (id==20004) {
    arrow_name = "person_marker3";
  }
  else if (id==20005) {
    arrow_name = "person_marker4";
  }
  else if (id==20006) {
    arrow_name = "person_marker5";
  }
  else {
    res.result = 0;
    return false;
  }

  if (mode==0)
  {
    callLater(bind(&grasp::TmsRpController::disappear,trc_,arrow_name));
    res.result = 1;
    return true;
  }
  else if (mode==1)
  {
    callLater(bind(&grasp::TmsRpController::setPos,trc_,arrow_name,cnoid::Vector3(req.x/1000,req.y/1000,req.z/1000), mat0_));
    callLater(bind(&grasp::TmsRpController::appear,trc_,arrow_name));
    res.result = 1;
    return true;
  }
  else if (mode==2)
  {
    callLater(bind(&grasp::TmsRpController::setPos,trc_,arrow_name,cnoid::Vector3(req.x/1000,req.y/1000,req.z/1000), mat0_));
    for (int32_t i=0; i < 2; i++)
    {
      callLater(bind(&grasp::TmsRpController::appear,trc_,arrow_name));
      sleep(1);
      callLater(bind(&grasp::TmsRpController::disappear,trc_,arrow_name));
      sleep(1);
    }
    callLater(bind(&grasp::TmsRpController::appear,trc_,arrow_name));
    res.result = 1;
    return true;
  }

  res.result = 0;
  return false;
}
