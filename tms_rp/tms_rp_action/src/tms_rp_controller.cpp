#include <tms_rp_controller.h>
#include <tms_rp_string_renderer.h>

#include <Grasp/PlanBase.h>
#include <Grasp/GraspController.h>
#include <PRM/TrajectoryPlanner.h>

#include <cnoid/JointPath>
#include <cnoid/ItemTreeView>
#include <cnoid/RootItem>
#include <cnoid/MessageView>

#include <ros/ros.h>
#include <tms_msg_db/TmsdbGetData.h>

#include <fstream>

using namespace grasp;
using namespace std;
using namespace cnoid;

#include <time.h>
#include <sys/resource.h>
#include <stdlib.h>

double getrusage_sec() {
	struct rusage t;
	struct timeval tv;
	getrusage(RUSAGE_SELF, &t);
	tv = t.ru_utime;
	return tv.tv_sec + (double)tv.tv_usec*1e-6;
}

TmsRpController* TmsRpController::instance()
{
  static TmsRpController* instance = new TmsRpController();
	return instance;
}

TmsRpController::TmsRpController() : os_ (MessageView::mainInstance()->cout() )
{
    //char excutePath[255];
    //char* excutePathResult;
    //excutePathResult = getcwd(excutePath, 255);
    //os_ << "excutePath : " << excutePath << endl;

    std::string homepath = getenv("HOME");
    objectBasePath = homepath + "/catkin_ws/src/ros_tms/tms_rp/tms_models";

    ifstream objListFile( (objectBasePath+"/model_list.txt").c_str() );
	char line[1024];
	while (objListFile){
		objListFile.getline(line,1024);
		stringstream ss;
		ss << line;
		int id;
		string gp,file;
		ss >> id;
		ss >> gp;
		ss >> file;
		objId2File.insert(pair <int, string>(id,file));
		objId2PrePlan.insert(pair <int, string>(id,gp));
	}
	if(objId2File.size()==0){
		os_ << "Error: cannot read objList.txt" << endl;
	}
	isToleranceMode=false;
	isPlanBaseMode=true;
	isPlanJointMode=true;
	isSyncJointBaseMode=false;
	
	PlanBase::instance()->useObjectSafeBoundingBox =true;
  PlanBase::instance()->doInitialCollision=true;
	
	objectPalmPos = Vector3(0,0,0);
	objectPalmRot = Matrix3::Identity();
	
}

bool TmsRpController::setTolerance(double setTolerance){
	PlanBase::instance()->tolerance = setTolerance;
	return true;
}

bool TmsRpController::graspPathPlanStart(
			int mode, std::vector<double> begin, std::vector<double> end,
			std::string robotId, std::string objectTagId, double resolution,
			std::vector<pathInfo>* trajectory, int* state)
{
	*state = 0;

	PlanBase* tc = PlanBase::instance();

	if(robTag2Arm().find(robotId) == robTag2Arm().end()){
		os_ << "Error no robotid " << robotId << endl;
		*state = 1;
		return false;
	}
	tc->targetArmFinger = robTag2Arm()[robotId];

	tc->arm()->searchBasePositionMode = isPlanBaseMode;
	if(isPlanBaseMode){
		tc->arm()->base_p = tc->body()->link(0)->p();
		tc->arm()->base_R = tc->body()->link(0)->R();
	}
	if(isSyncJointBaseMode) setTrajectoryPlanWholeDOF();
	else tc->setTrajectoryPlanDOF();

	if(objTag2Item().find(objectTagId) == objTag2Item().end()){
		os_ << "Error no objectTagId " << objectTagId << endl;
		*state = 2;
		return false;
	}
	tc->SetGraspedObject(objTag2Item()[objectTagId]);
	tc->targetObject->preplanningFileName = objTag2PrePlan[objectTagId];

	Vector3 object_p = tc->object()->p();
	Matrix3 object_R = tc->object()->R();

	if(tc->bodyItemRobot()->body()->numJoints() != (int)begin.size() ||
		tc->bodyItemRobot()->body()->numJoints() != (int)end.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< tc->bodyItemRobot()->body()->numJoints() << endl;
		*state = 3;
		return false;
	}

	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}

	tc->initial();
  //tc->initialCollisionWithMemory();
	tc->graspMotionSeq.clear();

	tc->setGraspingState(PlanBase::NOT_GRASPING);
	tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
	if(!isSyncJointBaseMode && isPlanBaseMode) tc->setTrajectoryPlanMapDOF();
	MotionState beginMotionState =  tc->getMotionState();
	beginMotionState.id = planGraspPath::APPROACH;
	beginMotionState.tolerance = tc->tolerance;

	GraspController::instance()->graspPranningReultList.clear();
	bool success = GraspController::instance()->loadAndSelectGraspPattern();
	if(!success){
		os_ << "Error: Cannot find grasping_ posure" << endl;
		*state = 4;
		return false;
	}
	if(!isSyncJointBaseMode && isPlanBaseMode)  tc->setTrajectoryPlanDOF();
	MotionState graspMotionState = tc->getMotionState();
	Vector3 Pp_(tc->palm()->p());
	Matrix3 Rp_(tc->palm()->R());

	double quality = 1.0e10;

	vector <MotionState> graspMotionSeq;
	for(unsigned int k=0;k <GraspController::instance()->graspPranningReultList.size(); k++) {
		bool ikConv;
		graspMotionSeq.clear();
		tc->object()->p() = object_p;
		tc->object()->R() = object_R;

		//==== Start Point
		graspMotionSeq.push_back( beginMotionState );
		tc->setMotionState( beginMotionState );

		tc->setMotionState (GraspController::instance()->graspPranningReultList[k]);
		if(!isSyncJointBaseMode && isPlanBaseMode)  tc->setTrajectoryPlanDOF();
		graspMotionState = tc->getMotionState();
		Vector3 Pp_(tc->palm()->p());
		Matrix3 Rp_(tc->palm()->R());

		double dist = 0.0;
		for (int i = 0; i < tc->arm()->arm_path->numJoints(); i++){
			double edist =  (tc->arm()->arm_path->joint(i)->q() - tc->arm()->armStandardPose[i]);
			dist +=  edist*edist;
		}
		cout << k << " "<< dist << " " << quality << endl;
		if(dist > quality) continue;

		if(!isSyncJointBaseMode) {
			for(int i=0;i<(int)begin.size();i++){
				tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
			}
			tc->setGraspingState(PlanBase::NOT_GRASPING);
			tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
			tc->setTrajectoryPlanDOF();
			tc->calcForwardKinematics();
			graspMotionSeq.push_back( tc->getMotionState() );
			graspMotionSeq.back().id = planGraspPath::APPROACH;
			graspMotionSeq.back().tolerance = tc->tolerance;
			if( !tc->arm()->checkArmLimit() || tc->isColliding()){
				cout << "collision at base position at approach" << endl;
				continue;
			}
		}

		if(!isSyncJointBaseMode){
			tc->arm()->searchBasePositionMode = false;
			tc->setTrajectoryPlanDOF();
		}
		//==== Approach Point
		tc->setMotionState( graspMotionState );
		ikConv = tc->arm()->IK_arm(Rp_*tc->arm()->approachOffset+Pp_+Vector3(0,0,0.05), Rp_);
		tc->setGraspingState(PlanBase::UNDER_GRASPING);
		for(int i=0;i<tc->nFing();i++){
			for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
				tc->fingers(i)->fing_path->joint(j)->q() = tc->fingers(i)->fingerOpenPose[j];
			}
		}
		graspMotionSeq.push_back( tc->getMotionState() );
		graspMotionSeq.back().id = planGraspPath::APPROACH;
		graspMotionSeq.back().tolerance = 0;
		tc->calcForwardKinematics();
		if( !ikConv  || !tc->arm()->checkArmLimit() || tc->isColliding()){
			cout << "collision at approach point" << endl;
			continue;
		}

		//==== Grasp Point and Hand open
		tc->setMotionState( graspMotionState );
		tc->setGraspingState(PlanBase::UNDER_GRASPING);
		for(int i=0;i<tc->nFing();i++){
			for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
				tc->fingers(i)->fing_path->joint(j)->q() = tc->fingers(i)->fingerOpenPose[j];
			}
		}
		graspMotionSeq.push_back( tc->getMotionState() );
		graspMotionSeq.back().id = planGraspPath::CLOSING_GRIPPER;
		graspMotionSeq.back().tolerance = 0;
		tc->calcForwardKinematics();
		if( !tc->arm()->checkArmLimit() || tc->isColliding()) continue;

		//==== hand close
		tc->setMotionState(graspMotionState) ;
		tc->setGraspingState( PlanBase::GRASPING );
		graspMotionSeq.push_back( tc->getMotionState() );
		graspMotionSeq.back().id = planGraspPath::UP_HAND;
		graspMotionSeq.back().tolerance = tc->tolerance;
		tc->calcForwardKinematics();
		if( !tc->arm()->checkArmLimit() || tc->isColliding()) continue;

		//==== lift up
		ikConv = tc->arm()->IK_arm(Vector3(Vector3(0,0,0.03)+Pp_), Rp_);
		tc->setObjectContactState(PlanBase::OFF_ENVIRONMENT);
		graspMotionSeq.push_back( tc->getMotionState() );
		graspMotionSeq.back().id = planGraspPath::BACKAWAY;
		graspMotionSeq.back().tolerance = tc->tolerance;
		tc->calcForwardKinematics();
		if(!ikConv  || !tc->arm()->checkArmLimit() || tc->isColliding()){
			cout << "collision or ik error when lift up" << endl;
			continue;
		}

		//==== end point
		vector<double>closefinger;
		for(int i=0;i<tc->nFing();i++){
			for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
				closefinger.push_back(tc->fingers(i)->fing_path->joint(j)->q());
			}
		}
		for(int i=0;i<(int)begin.size();i++){
			tc->bodyItemRobot()->body()->joint(i)->q() = end[i];
		}
		int cnt=0;
		for(int i=0;i<tc->nFing();i++){
			for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
				tc->fingers(i)->fing_path->joint(j)->q() = closefinger[cnt++];
			}
		}
		graspMotionSeq.push_back( tc->getMotionState() );
		tc->calcForwardKinematics();
		if( !tc->arm()->checkArmLimit() || tc->isColliding()){
			cout << "collision at end pose" << endl;
			continue;
		}

		tc->arm()->searchBasePositionMode = isPlanBaseMode;

		tc->graspMotionSeq = graspMotionSeq;
		quality = dist;
	}
	tc->object()->p() = object_p;
	tc->object()->R() = object_R;

	if(tc->graspMotionSeq.size() < 2){
		os_ << "Error: Cannot find grasping_ posure" << endl;
		*state = 4;
		return false;
	}


	double start1 =  getrusage_sec();


//	for(unsigned int j=0; j < tc->graspMotionSeq.size();j++){
//		for(int i=0;i< tc->bodyItemRobot()->body()->numJoints() ; i++){
//			if(tc->body()->joint(i)->llimit() > tc->graspMotionSeq[j].jointSeq[i]) cout << "joint " << i << " motion "<< j  << " under lmit" << endl;
//			if(tc->body()->joint(i)->ulimit() < tc->graspMotionSeq[j].jointSeq[i]) cout << "joint " << i << " motion "<< j  << " over limit" << endl;
//		}
//	}

	success = setOutputData(trajectory,resolution);

	double end1 =  getrusage_sec();
	cout <<"pathplan " << end1-start1 << " sec "<<endl;

	if(!success){
		os_ << "Error: Cannot find motion path" << endl;
		*state = 5;
		return false;
	}


	objectPalmPos = tc->graspMotionSeq.back().objectPalmPos;
	objectPalmRot = tc->graspMotionSeq.back().objectPalmRot;

	os_ << "Planning Finished" << endl;
	cout << "Planning Finished" << endl;
	//==== Hand open
	//tc->handJoint()->link(1)->q() = 80.0*M_PI/180.0;
	//tc->handJoint()->link(2)->q() = -59.0*M_PI/180.0;
	//tc->graspMotionSeq.push_back( tc->getMotionState() );

	return true;
}


bool TmsRpController::pathPlanStart(
			int mode, std::vector<double> begin, std::vector<double> end,
			std::string robotId, std::string objectTagId, double resolution,
			std::vector<pathInfo>* trajectory, int* state)
{
	*state = 0;

	PlanBase* tc = PlanBase::instance();
	if(robTag2Arm().find(robotId) == robTag2Arm().end()){
		os_ << "Error no robotid " << robotId << endl;
		*state = 1;
		return false;
	}
	tc->targetArmFinger = robTag2Arm()[robotId];
	tc->setTrajectoryPlanDOF();

	/*
	if(objTag2Item().find(objectTagId) == objTag2Item().end()){
		os_ << "Error no objectTagId " << objectTagId << endl;
		*state = 2;
		return false;
	}
	tc->SetGraspedObject(objTag2Item()[objectTagId]);
	tc->targetObject->preplanningFileName = objTag2PrePlan[objectTagId];
*/
	tc->graspMotionSeq.clear();

	//==== start point
	if(tc->bodyItemRobot()->body()->numJoints() != (int)begin.size() ||
		tc->bodyItemRobot()->body()->numJoints() != (int)end.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< tc->bodyItemRobot()->body()->numJoints() << endl;
		*state = 3;
		return false;
	}
	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}
	tc->setGraspingState(PlanBase::NOT_GRASPING);
	tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::APPROACH;
	tc->graspMotionSeq.back().tolerance = tc->tolerance;

	//==== end point
	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = end[i];
	}
	tc->graspMotionSeq.push_back( tc->getMotionState() );

	double start1 =  getrusage_sec();

	bool success = setOutputData(trajectory,resolution);

	double end1 =  getrusage_sec();
	cout <<"pathplan " << end1-start1 << " sec "<<endl;

	if(!success){
		os_ << "Error: Cannot find motion path" << endl;
		*state = 5;
		return false;
	}
	os_ << "Output Trajectory Size " << trajectory->size() << endl;
	os_ << "Planning Finished" << endl;

	return true;
}


bool TmsRpController::releasePathPlanStart(
			int mode, std::vector<double> begin, std::vector<double> end,
			std::string robotId, std::string objectTagId, double resolution,
			std::vector<pathInfo>* trajectory, int* state)
{
	PlanBase* tc = PlanBase::instance();

	Vector3 placePos = tc->object()->p();
	Matrix3 placeRot = tc->object()->R();
	Vector3 objectPalmPos = tc->targetArmFinger->objectPalmPos;
	Matrix3 objectPalmRot = tc->targetArmFinger->objectPalmRot;


	os_ << "obj palm "<< objectPalmPos.transpose() << endl;
	os_ << placePos.transpose() << endl;

	*state = 0;
	if(robTag2Arm().find(robotId) == robTag2Arm().end()){
		os_ << "Error no robotid " << robotId << endl;
		*state = 1;
		return false;
	}
	tc->targetArmFinger = robTag2Arm()[robotId];
	if(!isSyncJointBaseMode) tc->setTrajectoryPlanDOF();

	if(objTag2Item().find(objectTagId) == objTag2Item().end()){
		os_ << "Error no objectTagId " << objectTagId << endl;
		*state = 2;
		return false;
	}
	tc->SetGraspedObject(objTag2Item()[objectTagId]);
	tc->targetObject->preplanningFileName = objTag2PrePlan[objectTagId];
	tc->RemoveEnvironment(objTag2Item()[objectTagId]);


	if(tc->bodyItemRobot()->body()->numJoints() != (int)begin.size() ||
		tc->bodyItemRobot()->body()->numJoints() != (int)end.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< tc->bodyItemRobot()->body()->numJoints() << endl;
		*state = 3;
		return false;
	}

	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}
	//tc->flush();

	tc->graspMotionSeq.clear();

	tc->setGraspingState(PlanBase::GRASPING);
	tc->targetArmFinger->objectPalmPos = objectPalmPos;
	tc->targetArmFinger->objectPalmRot = objectPalmRot;
	tc->calcForwardKinematics();
	tc->setObjectContactState(PlanBase::OFF_ENVIRONMENT);
	if(!isSyncJointBaseMode && isPlanBaseMode) tc->setTrajectoryPlanMapDOF();
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::APPROACH;
	tc->graspMotionSeq.back().tolerance = tc->tolerance;

	tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
	bool success = selectReleasePattern(placePos, placeRot);
	if(!success){
		os_ << "Error: Cannot find grasping_ posure" << endl;
		*state = 4;
		return false;
	}

	tc->targetArmFinger->objectPalmPos = objectPalmPos;
	tc->targetArmFinger->objectPalmRot = objectPalmRot;
	tc->calcForwardKinematics();
	tc->setGraspingState(PlanBase::GRASPING);
	tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
	if(!isSyncJointBaseMode) tc->setTrajectoryPlanDOF();
	MotionState graspMotionState = tc->getMotionState();

	os_ << "place "<< placePos.transpose() << "obj " << tc->object()->p() <<endl;


	Vector3 Pp_(tc->palm()->p());
	Matrix3 Rp_(tc->palm()->R());

	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}
	tc->calcForwardKinematics();
	//tc->flush();

	tc->object()->R() = tc->palm()->R()*(tc->targetArmFinger->objectPalmRot);
	tc->object()->p() = tc->palm()->p()+tc->palm()->R()*tc->targetArmFinger->objectPalmPos;
	tc->setObjectContactState(PlanBase::OFF_ENVIRONMENT);
	tc->setGraspingState(PlanBase::GRASPING);
	if(!isSyncJointBaseMode) tc->setTrajectoryPlanDOF();
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::APPROACH;
	tc->graspMotionSeq.back().tolerance = tc->tolerance;
	//tc->flush();


	//==== lift up point
	tc->arm()->IK_arm(Vector3(Vector3(0,0,0.05)+Pp_), Rp_);
	tc->setObjectContactState(PlanBase::ON_ENVIRONMENT);
	tc->calcForwardKinematics();
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::DOWN_HAND;
	tc->graspMotionSeq.back().tolerance = 0.0;
	//tc->flush();

	//==== Grasp Point and hand keep close
	tc->setMotionState(graspMotionState) ;
	tc->calcForwardKinematics();
	tc->setGraspingState(PlanBase::UNDER_GRASPING);
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::OPENING_GRIPPER;
	tc->graspMotionSeq.back().tolerance = 0.0;
	//tc->flush();

	//==== Grasp Point and Hand open
	tc->setMotionState( graspMotionState );
	tc->setGraspingState(PlanBase::UNDER_GRASPING);
	for(int i=0;i<tc->nFing();i++){
		for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
			tc->fingers(i)->fing_path->joint(j)->q() = tc->fingers(i)->fingerOpenPose[j];
		}
	}
	tc->calcForwardKinematics();
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::BACKAWAY;
	tc->graspMotionSeq.back().tolerance = 0.0;

	tc->setMotionState( graspMotionState );
	tc->arm()->IK_arm(Rp_*tc->arm()->approachOffset+Pp_+Vector3(0,0,0.05), Rp_);
	tc->setGraspingState(PlanBase::NOT_GRASPING);
	for(int i=0;i<tc->nFing();i++){
		for(int j=0;j<tc->fingers(i)->fing_path->numJoints();j++){
			tc->fingers(i)->fing_path->joint(j)->q() = tc->fingers(i)->fingerOpenPose[j];
		}
	}
	tc->graspMotionSeq.push_back( tc->getMotionState() );
	tc->graspMotionSeq.back().id = planGraspPath::APPROACH;
	tc->graspMotionSeq.back().tolerance = tc->tolerance;


	//==== end point
	for(int i=0;i<(int)begin.size();i++){
		tc->bodyItemRobot()->body()->joint(i)->q() = end[i];
	}
	tc->calcForwardKinematics();
	tc->setGraspingState(PlanBase::NOT_GRASPING);
	tc->graspMotionSeq.push_back( tc->getMotionState() );

	tc->setMotionState(tc->graspMotionSeq[0]);
	tc->targetArmFinger->objectPalmPos = objectPalmPos;
	tc->targetArmFinger->objectPalmRot = objectPalmRot;
	tc->calcForwardKinematics();
	tc->setObjPos(tc->palm()->p()+tc->palm()->R()*objectPalmPos, tc->palm()->R()*objectPalmRot );

	double start1 =  getrusage_sec();

	success = setOutputData(trajectory,resolution);

	double end1 =  getrusage_sec();
	cout <<"pathplan " << end1-start1 << " sec "<<endl;

	if(!success){
		os_ << "Error: Cannot find motion path" << endl;
		*state = 5;
		return false;
	}
	os_ << "Output Trajectory Size " << trajectory->size() << endl;

	os_ << "Planning Finished" << endl;

	return true;
}

bool TmsRpController::setRobotPosture(int mode,  std::string robotId, std::vector<double> begin, int* state){
	PlanBase* tc = PlanBase::instance();

	*state = 0;
	if(robTag2Arm().find(robotId) == robTag2Arm().end()){ 
		os_ << "Error no robotid " << robotId << endl;
		*state = 1;
		return false;
	}
	tc->targetArmFinger = robTag2Arm()[robotId];

	if(tc->bodyItemRobot()->body()->numJoints() != (int)begin.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< tc->bodyItemRobot()->body()->numJoints() << endl;
		*state = 3;
		return false;
	}

	for(int i=0;i<(int)begin.size();i++){  
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}
	tc->calcForwardKinematics();
	//tc->flush();
	return true;
}


bool TmsRpController::setRobotState(int mode,  std::string robotId, Vector3 pos, Matrix3 ori, std::vector<double> begin, int* state, Vector3 graspPos, Matrix3 graspOri){
	PlanBase* tc = PlanBase::instance();

	*state = 0;
	if(robTag2Arm().find(robotId) == robTag2Arm().end()){ 
		os_ << "Error no robotid " << robotId << endl;
		*state = 1;
		return false;
	}
	if(tc->bodyItemRobot()->body()->numJoints() != (int)begin.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< tc->bodyItemRobot()->body()->numJoints() << endl;
		*state = 3;
		return false;
	}
	tc->targetArmFinger = robTag2Arm()[robotId];
	tc->bodyItemRobot()->body()->link(0)->p() = pos;
	tc->bodyItemRobot()->body()->link(0)->R() = ori;
	if( mode == planGraspPath::RELEASE_PLAN || mode == planGraspPath::PATH_PLAN_WITH_GRASPED_OBJECT ){
		objectPalmPos = tc->targetArmFinger->objectPalmPos = graspPos;
		objectPalmRot = tc->targetArmFinger->objectPalmRot = graspOri;
	}
	for(int i=0;i<(int)begin.size();i++){  
		tc->bodyItemRobot()->body()->joint(i)->q() = begin[i];
	}
	tc->setGraspingState(PlanBase::NOT_GRASPING);
	tc->calcForwardKinematics();
	return true;
}

bool TmsRpController::selectReleasePattern(Vector3 objVisPos, Matrix3 objVisRot) {
	PlanBase* pb = PlanBase::instance();

	Vector3 palmPos;
	Matrix3 palmRot;
	
	Matrix3 rpr ( objVisRot*pb->targetArmFinger->objectPalmRot.transpose() );
	Vector3  rpp  ( objVisPos - rpr*pb->targetArmFinger->objectPalmPos );

	double Eval = 1.e10;
	bool found = false;
	
	vector<double> fpos, fpos_temp, fpos_cand;

	for(int i=0;i<18;i++){

		Vector3 trpy (0,0,2.0*3.141592*i/18.0);
		Matrix3 tr = rotFromRpy(trpy);
		
		Matrix3 tmpRot( tr*objVisRot*pb->targetArmFinger->objectPalmRot.transpose()  );
		Vector3  tmpPos( objVisPos - tmpRot*pb->targetArmFinger->objectPalmPos);

		bool ikConv = pb->arm()->IK_arm(tmpPos, tmpRot);
		if(!ikConv) continue;
		
		double dist = 0.0;
		for (int i = 0; i < pb->arm()->arm_path->numJoints(); i++){
			double edist =  (pb->arm()->arm_path->joint(i)->q() - pb->arm()->armStandardPose[i]); 
			dist +=  edist*edist;
		}
		double quality = dist;
		
		

		if (  (Eval > quality) && ikConv && pb->arm()->checkArmLimit() && !pb->isColliding() ) {

			found = true;

			palmPos = tmpPos;
			palmRot = tmpRot;

			Eval = quality;

			cout << ikConv << " " << quality << endl;

		}
	}


	if (found) {
		pb->arm()->IK_arm(palmPos, palmRot);

		pb->calcForwardKinematics();
		pb->flush();
	} else {
		os_ << " No feasible grasping_ posture found" << endl;
	}

	
	return found;

}

bool TmsRpController::createRecord(int objId, std::string tagId) {
	string objFileName = objectBasePath + objId2File[objId];
	
  if (objTag2Item().find(tagId) != objTag2Item().end() || tagId=="ALLID") {
    os_ << "Error: the tagId is already recorded " << tagId << endl;
    return false;
	}	

  Item* parentItem = RootItem::mainInstance();	
	BodyItemPtr temp = new BodyItem();
	if( !temp->load(objFileName,parentItem,"OpenHRP-VRML-MODEL") ){
		os_ << "modelLoadError: " << objFileName << endl;
		return false;	
	}
	temp->setName(tagId);
	parentItem->addChildItem (temp);
	objTag2Item().insert( pair <string,BodyItemPtr>(tagId, temp) );
	objTag2PrePlan.insert( pair <string,string>(tagId, objId2PrePlan[objId]) );

	PlanBase::instance()->SetEnvironment(objTag2Item()[tagId]);
	return true;
}	

bool TmsRpController::createRobotRecord(int objId, std::string tagId){
  if (robTag2Arm().find(tagId) != robTag2Arm().end() || tagId=="ALLID") {
    os_ << "Error: the tagId is already recorded " << tagId << endl;
    return false;
  }

  BodyItemPtr item = objTag2Item()[tagId];
  PlanBase::instance()->RemoveEnvironment(item);

  PlanBase::instance()->SetGraspingRobot(objTag2Item()[tagId]);
  PlanBase::instance()->targetArmFinger = PlanBase::instance()->armsList[0];
  cout << PlanBase::instance()->targetArmFinger->name << " is target arm"<< endl;

  if (objId == 2002 == objId ==2003) {
	  if (robTag2Arm().find(string("SmartPalRight")) != robTag2Arm().end()) {
		  robTag2Arm()[string("SmartPalRight")]->dataFilePath = objectBasePath + "/robot/smartpal/data/";
		  cout << robTag2Arm()[string("SmartPalRight")]->dataFilePath << endl;
	  }
  } else if (objId == 2006) {
	  robTag2Arm()[string("KXPArm")]->dataFilePath = objectBasePath + "/robot/KXP/data/";
	  cout << robTag2Arm()[string("KXPArm")]->dataFilePath << endl;
  }
  return true;
}

bool TmsRpController::deleteRecord(std::string tagId){
	if(objTag2Item().find(tagId) == objTag2Item().end()){ 
		os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;	
	}
	BodyItemPtr item = objTag2Item()[tagId];
	disappear(tagId);
	objTag2Item().erase(tagId);
	item->detachFromParentItem();

	return true;
}

template <typename T> std::string tostr(const T& t)
{
    std::ostringstream os; os<<t; return os.str();
}

bool TmsRpController::appear(std::string tagId) {
  if (objTag2Item().find(tagId) == objTag2Item().end()) {
                os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;	
  }
	BodyItemPtr item = objTag2Item()[tagId];
	ItemTreeView::mainInstance()->checkItem(item,true);

	if(tagId == "wheelchair"){
        int heartrate = 0;
        int mw_med = 0;
        int mw_att = 0;
        int mw_por = 0;
        std::string heartrate_s;
        std::string mw_med_s;
        std::string mw_att_s;
        std::string mw_por_s;
        ros::NodeHandle n;
        ros::ServiceClient db_client;
        db_client = n.serviceClient<tms_msg_db::TmsdbGetData>("/tms_db_reader/dbreader");
        tms_msg_db::TmsdbGetData srv;
        srv.request.tmsdb.id = 3018;	//heartrate sensor
        srv.request.tmsdb.sensor = 3018;
        if(! db_client.call(srv)){
            os_ << "Failed to get heartrate data";
        }else if(srv.response.tmsdb.empty()){
            os_ << "DB response empty";
        }else{
        	sscanf(srv.response.tmsdb[0].note.c_str(),"{\"heartrate\": \"%d\"}",&heartrate);
        	os_ << srv.response.tmsdb[0].note.c_str();
        	os_ << heartrate;
        }
        srv.request.tmsdb.id = 3017;	//mindwave mobile
        srv.request.tmsdb.sensor = 3017;
        if(! db_client.call(srv)){
            os_ << "Failed to get mindwave data";
        }else if(srv.response.tmsdb.empty()){
            os_ << "DB response empty";
        }else{
        	sscanf(srv.response.tmsdb[0].note.c_str(),"{\"meditation\": \"%d\", \"attention\": \"%d\", \"poor_signal\": \"%d\"}",&mw_med,&mw_att,&mw_por);
        }
        heartrate_s = tostr(heartrate);
        mw_med_s = tostr(mw_med);
        mw_att_s = tostr(mw_att);
        mw_por_s = tostr(mw_por);
        setString(tagId,Vector3(0,0,1),"hr:"+heartrate_s+"  med:"+mw_med_s+"  att:"+mw_att_s);
        // mw_med_s = boost::lexical_cast<string>(mw_med);
        // mw_att_s = boost::lexical_cast<string>(mw_att);
        // mw_por_s = boost::lexical_cast<string>(mw_por);
        // setString(tagId,Vector3(0,0,2),"heartrate: "+heartrate_s);
        // setString(tagId,Vector3(0,0,1),"brainwave med: "+mw_med_s+"   att: "+mw_att_s);
	}
	return true;
}

bool TmsRpController::disappear(std::string tagId){
	if(objTag2Item().find(tagId) == objTag2Item().end()){ 
		os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;	
	}
	BodyItemPtr item = objTag2Item()[tagId];
	PlanBase::instance()->RemoveEnvironment(item);
	ItemTreeView::mainInstance()->checkItem(item,false);
	return true;
}

// class stringViewer{
// private:
// 	SgStringRenderer *cr_;
// 	sting st_;
// 	BodyItemPtr item_;
// public:
// 	stringViewer(std::string tagId, Vector3 pos, string str){
// 		item_ = objTag2Item()[tagId];
// 		SgGroupPtr node  = (SgGroup*)item->body()->link(0)->shape();
// 		cr_ = new SgStringRenderer (str, pos);
// 		node->addChild(cr_);
// 		st_ = str;
// 	}
// 	void setString(string arg){
// 		st_ = arg;
// 		cr_->setString(st_);
// 	}
// 	void update(){
// 		ItemTreeView::mainInstance()->checkItem(item,false);
// 		MessageView::mainInstance()->flush();
// 		ItemTreeView::mainInstance()->checkItem(item,true);
// 		MessageView::mainInstance()->flush();
// 	}
// }

bool TmsRpController::setString(std::string tagId, Vector3 pos, string str) {
    if (objTag2Item().find(tagId) == objTag2Item().end()) {
        // os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;	
    }
	BodyItemPtr item = objTag2Item()[tagId];

	static SgStringRenderer * cr=NULL;
	if(!cr){
		SgGroupPtr node  = (SgGroup*)item->body()->link(0)->shape();
		// cr = new SgStringRenderer ("test", Vector3(0,0,0.5));
		cr = new SgStringRenderer ("", pos);
		node->addChild(cr);
	}else{
		cr->setString(str);
	}
	// os_  << "set string" << endl;
	ItemTreeView::mainInstance()->checkItem(item,false);
	MessageView::mainInstance()->flush();
	ItemTreeView::mainInstance()->checkItem(item,true);
	MessageView::mainInstance()->flush();
	return true;
}

bool TmsRpController::setPos(string tagId, Vector3 pos, Matrix3 ori){
	BodyItemPtr item = NULL;
  if(objTag2Item().find(tagId) != objTag2Item().end()){
		item = objTag2Item()[tagId];
	}
	if( robTag2Arm().find(tagId) != robTag2Arm().end() ){
		item = robTag2Arm()[tagId]->bodyItemRobot;
	}
  if(!item){
                os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;
	}
  item->body()->link(0)->p() = pos;
	item->body()->link(0)->R() = ori;
	item->calcForwardKinematics();
	item->notifyKinematicStateChange();

  return true;
}

bool TmsRpController::set_all_Pos(string tagId, Vector3 pos, Matrix3 ori, std::vector<double> begin){
	BodyItemPtr item = NULL;
	if(objTag2Item().find(tagId) != objTag2Item().end()){
		item = objTag2Item()[tagId];
	}
	if( robTag2Arm().find(tagId) != robTag2Arm().end() ){
		item = robTag2Arm()[tagId]->bodyItemRobot;
	}
	if(!item){
		os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;
	}
	if(item->body()->numJoints() != (int)begin.size() ){
		os_ << "Error: the number of Joints of input shoud be "<< item->body()->numJoints() << endl;
		return false;
	}

	item->body()->link(0)->p() = pos;
	item->body()->link(0)->R() = ori;
	for(int i=0;i<(int)begin.size();i++){
		os_ << "begin[" << i << "]=" << begin[i] << endl;
		item->body()->joint(i)->q() = begin[i];
	}
	item->calcForwardKinematics();
	item->notifyKinematicStateChange();

  return true;
}	

bool TmsRpController::getPos(string tagId, Vector3 *pos, Matrix3 *ori){
	BodyItemPtr item = NULL;
  if(objTag2Item().find(tagId) != objTag2Item().end()){
		item = objTag2Item()[tagId];
	}
	if( robTag2Arm().find(tagId) != robTag2Arm().end() ){
		item = robTag2Arm()[tagId]->bodyItemRobot;
	}
  if(!item){
                os_ << "Error: the tagId is not recorded " << tagId << endl;
		return false;
	}
  *pos = item->body()->link(0)->p();
	*ori = item->body()->link(0)->R();
	item->calcForwardKinematics();
	item->notifyKinematicStateChange();

  return true;
}

void TmsRpController::setTrajectoryPlanWholeDOF(){
	PlanBase* tc = PlanBase::instance();
	
	tc->pathPlanDOF.clear();
	
	if(isPlanJointMode){
		for(int i=0; i<tc->arm()->nJoints; i++) tc->pathPlanDOF.push_back(tc->arm()->arm_path->joint(i)->jointId());
		for(int i=0; i<tc->nFing(); i++) for(int j=0; j<tc->fingers(i)->nJoints; j++) tc->pathPlanDOF.push_back(tc->fingers(i)->fing_path->joint(j)->jointId());
	}
	if(isPlanBaseMode){
		int top = tc->body()->numJoints();
		tc->pathPlanDOF.push_back(top); //position x
		tc->pathPlanDOF.push_back(top+1); //position y;
		tc->pathPlanDOF.push_back(top+5); //yaw;
	}
}

bool TmsRpController::setBoolParameter(int mode,bool onoff){
	switch(mode){
		case planGraspPath::TOLERANCE_MODE :
			isToleranceMode=false;
			PlanBase::instance()->useObjectSafeBoundingBox = onoff;
			break;
		case planGraspPath::PLAN_BASE_MODE:
			isPlanBaseMode = onoff;
			break;
		case planGraspPath::PLAN_JOINT_MODE :
			isPlanJointMode = onoff;
			break;
		case planGraspPath::SYNC_JOINT_BASE_MODE:
			isSyncJointBaseMode=onoff;
			break;
		default:
			return false;
	}
	return true;
	
}

bool TmsRpController::setOutputData(std::vector<pathInfo>* trajectory, const int resolution){
	grasp::PlanBase *tc = grasp::PlanBase::instance();
	

	TrajectoryPlanner tp;
	bool success = tp.doTrajectoryPlanning();

	if(!success){
		return false;
	}

	vector<VectorXd> outputMotionSeq;
	vector<int> outputMotionId;
	vector<double> maxAngleSeq;
	double sumMaxAngle=0;
	for(unsigned int i=0;i < tp.motionSeq.size()-1;i++){
		double maxAngle = ( tp.motionSeq[i].jointSeq - tp.motionSeq[i+1].jointSeq ).cwiseAbs().maxCoeff();
		double tempAngle = (tp.motionSeq[i].pos - tp.motionSeq[i+1].pos).norm();
		if(maxAngle < tempAngle ) maxAngle = tempAngle;
		tempAngle = (tp.motionSeq[i].rpy - tp.motionSeq[i+1].rpy).norm();
		if(maxAngle < tempAngle ) maxAngle = tempAngle;
		sumMaxAngle += maxAngle;
		maxAngleSeq.push_back(maxAngle);
	}
	
	for(unsigned int i=0;i < tp.motionSeq.size()-1;i++){
		if(maxAngleSeq[i]==0) continue;
		int cnt= resolution*maxAngleSeq[i]/sumMaxAngle;
		if(cnt==0) cnt=1;
		for(int j=0;j<cnt;j++){
			pathInfo temp ;
			temp.state = tp.motionSeq[i].id;
			temp.robotPos = (tp.motionSeq[i].pos*(cnt-j) + tp.motionSeq[i+1].pos*j)/cnt;
			temp.robotOri = (rotFromRpy(tp.motionSeq[i].rpy*(cnt-j) + tp.motionSeq[i+1].rpy*j)/cnt);
			temp.joints = (tp.motionSeq[i].jointSeq*(cnt-j) + tp.motionSeq[i+1].jointSeq*j)/cnt;
			for(int k=0; k < tc->bodyItemRobot()->body()->numJoints(); k++){
				temp.pos.push_back(temp.joints[k]);
			}
			temp.moveBase = false;
			temp.moveJoints= false;
			double movenorm = (tp.motionSeq[i].pos - tp.motionSeq[i+1].pos).norm();
			if(movenorm) temp.moveBase = true;
			movenorm = (tp.motionSeq[i].rpy - tp.motionSeq[i+1].rpy).norm();
			if(movenorm) temp.moveBase = true;
			movenorm = (tp.motionSeq[i].jointSeq - tp.motionSeq[i+1].jointSeq).norm();
			if(movenorm) temp.moveJoints = true;
			trajectory->push_back(temp);
		}
	}
	pathInfo temp ;
	temp.state = tp.motionSeq.back().id;
	temp.robotPos = tp.motionSeq.back().pos;
	temp.robotOri =  rotFromRpy(tp.motionSeq.back().rpy);
	temp.joints = tp.motionSeq.back().jointSeq;
	for(int k=0; k < tc->bodyItemRobot()->body()->numJoints(); k++){
		temp.pos.push_back(temp.joints[k]);
	}
	temp.moveBase = false;
	temp.moveJoints= false;
	trajectory->push_back(temp);
	
	os_ << "Output Trajectory Size " << trajectory->size() << endl;
	return true;
}


/*
bool TmsRpController::graspPlanResult(const GraspPlanResult::DblSequence3& GraspPos, const GraspPlanResult::DblSequence9& GraspOri, const GraspPlanResult::DblSequence3& ApproachPos, const GraspPlanResult::DblSequence9& ApproachOri, CORBA::Double angle){
	
	PlanBase* gc = PlanBase::instance();
	
	Vector3 graspPos_(GraspPos[0],GraspPos[1],GraspPos[2]);
	Matrix3 graspOri_;
	graspOri_ << GraspOri[0],GraspOri[1],GraspOri[2],GraspOri[3],GraspOri[4],GraspOri[5],GraspOri[6],GraspOri[7],GraspOri[8];

	Vector3 approachPos_(ApproachPos[0],ApproachPos[1],ApproachPos[2]);
	Matrix3 approachOri_;
	approachOri_ << ApproachOri[0],ApproachOri[1],ApproachOri[2],ApproachOri[3],ApproachOri[4],ApproachOri[5],ApproachOri[6],ApproachOri[7],ApproachOri[8];

	
	cout << graspPos_ << graspOri_ << basePos << baseOri <<endl;
	
	
	graspPos_ = Vector3(baseOri*graspPos_ + basePos);
	graspOri_ = Matrix3(baseOri*graspOri_);
	
	approachPos_ = Vector3(baseOri*approachPos_ + basePos);
	approachOri_ = Matrix3(baseOri*approachOri_);
	
	
	bool ikConv = gc->arm()->IK_arm(approachPos_, approachOri_);
	bool limitCheck = gc->arm()->checkArmLimit();
	gc->fingers(0)->fing_path->joint(0)->q() = angle;
	gc->bodyItemRobot()->body()->calcForwardKinematics();
	gc->flush();

	if(ikConv && limitCheck){
		
	}else{
		return false;
	}

	ikConv = gc->arm()->IK_arm(graspPos_, graspOri_);
	limitCheck = gc->arm()->checkArmLimit();
	gc->fingers(0)->fing_path->joint(0)->q() = angle;
	gc->bodyItemRobot()->body()->calcForwardKinematics();
	gc->flush();

	if(ikConv && limitCheck){
		return true;
	}else{
		return false;
	}
	
}
*/

