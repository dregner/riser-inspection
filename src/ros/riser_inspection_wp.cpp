#include <riser_inspection_wp.hh>


RiserInspection::RiserInspection() {
    initServices(nh_);
    initSubscribers(nh_);

}

RiserInspection::~RiserInspection() = default;

void RiserInspection::initSubscribers(ros::NodeHandle &nh) {
    try {
        ros::NodeHandle nh_private("~");

        std::string gps_topic, rtk_topic, attitude_topic, root_directory;
        // Topic parameters
        nh_private.param("gps_topic", gps_topic, std::string("/dji_sdk/gps_position"));
        nh_private.param("rtk_topic", rtk_topic, std::string("/dji_sdk/rtk_position"));
        nh_private.param("attitude_topic", attitude_topic, std::string("/dji_sdk/attitude"));
        nh_private.param("root_directory", root_directory, std::string("/home/nvidia/Documents"));

        pathGenerator.setFolderName(root_directory);
        gps_sub_ = nh.subscribe<sensor_msgs::NavSatFix>(gps_topic, 1, &RiserInspection::gps_callback, this);
        rtk_sub_ = nh.subscribe<sensor_msgs::NavSatFix>(rtk_topic, 1, &RiserInspection::rtk_callback, this);
        attitude_sub_ = nh.subscribe<geometry_msgs::QuaternionStamped>(attitude_topic, 1,
                                                                       &RiserInspection::atti_callback, this);

        ROS_INFO("Subscribe complet");
    } catch (ros::Exception &e) {
        ROS_ERROR("Subscribe topics exception: %s", e.what());
    }
}

void RiserInspection::initServices(ros::NodeHandle &nh) {
    try {
        ask_control_service = nh.advertiseService("riser_inspection/ask_control",
                                                  &RiserInspection::ask_control, this);
        ROS_INFO("Service riser_inspection/waypoint_generator initialize");

        wp_folders_service = nh.advertiseService("riser_inspection/folder_and_file",
                                                 &RiserInspection::folders_serviceCB,
                                                 this);
        ROS_INFO("Service riser_inspection/Folder initialized. Waypoint file: %s/%s",
                 pathGenerator.getFolderName().c_str(),
                 pathGenerator.getFileName().c_str());

        start_mission_service = nh.advertiseService("riser_inspection/start_mission",
                                                    &RiserInspection::startMission_serviceCB, this);
        ROS_INFO("service riser_inspection/start_mission initialized");

        // DJI Mission Service Clients
        waypoint_upload_service = nh.serviceClient<dji_sdk::MissionWpUpload>("dji_sdk/mission_waypoint_upload");
        waypoint_action_service = nh.serviceClient<dji_sdk::MissionWpAction>("dji_sdk/mission_waypoint_action");
        drone_activation_service = nh.serviceClient<dji_sdk::Activation>("dji_sdk/activation");
        sdk_ctrl_authority_service = nh.serviceClient<dji_sdk::SDKControlAuthority>("dji_sdk/sdk_control_authority");
        camera_action_service = nh.serviceClient<dji_sdk::CameraAction>("dji_sdk/camera_action");


    } catch (ros::Exception &e) {
        ROS_ERROR("Subscribe topics exception: %s", e.what());
    }
}

bool
RiserInspection::ask_control(riser_inspection::askControl::Request &req, riser_inspection::askControl::Response &res) {
    ROS_INFO("Received Points");
    dji_sdk::Activation activation;
    drone_activation_service.call(activation);
    if (!activation.response.result) {
        ROS_WARN("ack.info: set = %i id = %i", activation.response.cmd_set,
                 activation.response.cmd_id);
        ROS_WARN("ack.data: %i", activation.response.ack_data);
    } else {
        ROS_WARN("Activated");
        ROS_INFO("ack.info: set = %i id = %i", activation.response.cmd_set,
                 activation.response.cmd_id);
        ROS_INFO("ack.data: %i", activation.response.ack_data);
        dji_sdk::SDKControlAuthority sdkAuthority;
        sdkAuthority.request.control_enable = 1;
        sdk_ctrl_authority_service.call(sdkAuthority);
        if (!sdkAuthority.response.result) {
            ROS_WARN("Ask authority for second time");
            ROS_WARN("ack.info: set = %i id = %i", sdkAuthority.response.cmd_set,
                     sdkAuthority.response.cmd_id);
            ROS_WARN("ack.data: %i", sdkAuthority.response.ack_data);
        } else {
            ROS_WARN("Got Authority");
            ROS_INFO("ack.info: set = %i id = %i", sdkAuthority.response.cmd_set,
                     sdkAuthority.response.cmd_id);
            ROS_INFO("ack.data: %i", sdkAuthority.response.ack_data);
            return true;
        }
    }
}

bool RiserInspection::folders_serviceCB(riser_inspection::wpFolders::Request &req,
                                        riser_inspection::wpFolders::Response &res) {

    if (req.file_name.c_str() != nullptr) {
        ROS_INFO("Changing waypoint archive name %s", req.file_name.c_str());
        pathGenerator.setFileName(req.file_name);
    }
    if (pathGenerator.exists(req.file_path)) {
        ROS_INFO("Waypoint folder changed %s/%s", req.file_path.c_str(), pathGenerator.getFileName().c_str());
        pathGenerator.setFolderName(req.file_path);
        res.result = true;
        return res.result;
    } else {
        ROS_ERROR("Folder does not exist, file will be written in %s/%s", pathGenerator.getFolderName().c_str(),
                  pathGenerator.getFileName().c_str());
        res.result = false;
        return res.result;
    }
}

bool RiserInspection::startMission_serviceCB(riser_inspection::wpStartMission::Request &req,
                                             riser_inspection::wpStartMission::Response &res) {
    doing_mission = false;
    ROS_INFO("Received Points");
    pathGenerator.reset();

    // Path generate parameters
    int riser_distance, riser_diameter, h_points, v_points, delta_h, delta_v;

    ros::param::get("/riser_inspection_wp/riser_distance", riser_distance);
    ros::param::get("/riser_inspection_wp/riser_diameter", riser_diameter);
    ros::param::get("/riser_inspection_wp/horizontal_points", h_points);
    ros::param::get("/riser_inspection_wp/vertical_points", v_points);
    ros::param::get("/riser_inspection_wp/delta_H", delta_h);
    ros::param::get("/riser_inspection_wp/delta_V", delta_v);

    // Setting intial parameters to create waypoints
    pathGenerator.setInspectionParam(riser_distance, (float) riser_diameter, h_points, v_points, delta_h,
                                     (float) delta_v);


    // Define where comes the initial value
    if (!req.use_rtk) { start_gnss_ = current_gps_; }
    else { start_gnss_ = current_rtk_; }

    start_atti_ = current_atti_;
    start_atti_eul.Set(start_atti_.w, start_atti_.x, start_atti_.y, start_atti_.z);
    // Define start positions to create waypoints
    pathGenerator.setInitCoord(start_gnss_.latitude,
                               start_gnss_.longitude, (float) 10, (float) start_atti_eul.Yaw());
    ROS_INFO("Set initial values");

    try {
        pathGenerator.createInspectionPoints(3);
        ROS_WARN("Waypoints created at %s/%s", pathGenerator.getFolderName().c_str(),
                 pathGenerator.getFileName().c_str());
    } catch (ros::Exception &e) {
        ROS_WARN("ROS error %s", e.what());
        return res.result = false;
    }
    ROS_INFO("Mission will be started using file from %s/%s", pathGenerator.getFolderName().c_str(),
             pathGenerator.getFileName().c_str());
    if (!askControlAuthority()) {
        ROS_WARN("Cannot get Authority Control");
        return res.result = false;
    } else {
        ROS_INFO("Starting Waypoint Mission");
        if (runWaypointMission(100)) {
            ROS_INFO("Finished");
            doing_mission = true;
            old_gps_ = current_gps_;
            return res.result = true;


        } else {
            ROS_WARN("Error");
            return res.result = false;
        }
    }
}


void RiserInspection::gps_callback(const sensor_msgs::NavSatFix::ConstPtr &msg) {
    current_gps_ = *msg;
    if (doing_mission) {
        if (std::abs(current_gps_.altitude - old_gps_.altitude) > 0.2) {
            if (takePicture()) {
                ROS_INFO("Took picture");
                old_gps_ = current_gps_;
            } else { ROS_WARN("Unable to take picture"); }
        }
    }
}

void RiserInspection::rtk_callback(const sensor_msgs::NavSatFix::ConstPtr &msg) {
    current_rtk_ = *msg;
}

void RiserInspection::atti_callback(const geometry_msgs::QuaternionStamped::ConstPtr &msg) {
    current_atti_ = msg->quaternion;
    current_atti_euler_.Set(current_atti_.w, current_atti_.x, current_atti_.y, current_atti_.z);
}

bool RiserInspection::takePicture() {
    dji_sdk::CameraAction cameraAction;
    cameraAction.request.camera_action = 0;
    camera_action_service.call(cameraAction);
    return cameraAction.response.result;
}

ServiceAck
RiserInspection::missionAction(DJI::OSDK::DJI_MISSION_TYPE type,
                               DJI::OSDK::MISSION_ACTION action) {
    dji_sdk::MissionWpAction missionWpAction;
    switch (type) {
        case DJI::OSDK::WAYPOINT:
            missionWpAction.request.action = action;
            waypoint_action_service.call(missionWpAction);
            if (!missionWpAction.response.result) {
                ROS_WARN("ack.info: set = %i id = %i", missionWpAction.response.cmd_set,
                         missionWpAction.response.cmd_id);
                ROS_WARN("ack.data: %i", missionWpAction.response.ack_data);
            }
            return {static_cast<bool>(missionWpAction.response.result),
                    missionWpAction.response.cmd_set,
                    missionWpAction.response.cmd_id,
                    missionWpAction.response.ack_data};
        case HOTPOINT:
            break;
    }
}

ServiceAck
RiserInspection::initWaypointMission(dji_sdk::MissionWaypointTask &waypointTask) {
    dji_sdk::MissionWpUpload missionWpUpload;
    missionWpUpload.request.waypoint_task = waypointTask;
    waypoint_upload_service.call(missionWpUpload);
    if (!missionWpUpload.response.result) {
        ROS_WARN("ack.info: set = %i id = %i", missionWpUpload.response.cmd_set,
                 missionWpUpload.response.cmd_id);
        ROS_WARN("ack.data: %i", missionWpUpload.response.ack_data);
    }
    return ServiceAck(
            missionWpUpload.response.result, missionWpUpload.response.cmd_set,
            missionWpUpload.response.cmd_id, missionWpUpload.response.ack_data);
}


bool RiserInspection::askControlAuthority() {
    // Activate
    dji_sdk::Activation activation;
    drone_activation_service.call(activation);
    if (!activation.response.result) {
        ROS_WARN("ack.info: set = %i id = %i", activation.response.cmd_set,
                 activation.response.cmd_id);
        ROS_WARN("ack.data: %i", activation.response.ack_data);
        return false;
    } else {
        ROS_INFO("Activated successfully");
        dji_sdk::SDKControlAuthority sdkAuthority;
        sdkAuthority.request.control_enable = 1;
        sdk_ctrl_authority_service.call(sdkAuthority);
        if (sdkAuthority.response.result) {
            ROS_INFO("Obtain SDK control Authority successfully");
            return true;
        } else {
            if (sdkAuthority.response.ack_data == 3 &&
                sdkAuthority.response.cmd_set == 1 && sdkAuthority.response.cmd_id == 0) {
                ROS_INFO("Obtain SDK control Authority in progess, "
                         "send the cmd again");
                sdk_ctrl_authority_service.call(sdkAuthority);
            } else {
                ROS_WARN("Failed Obtain SDK control Authority");
                return false;
                ROS_WARN("ack.info: set = %i id = %i", sdkAuthority.response.cmd_set,
                         sdkAuthority.response.cmd_id);
                ROS_WARN("ack.data: %i", sdkAuthority.response.ack_data);
            }
        }
    }
}

std::vector<DJI::OSDK::WayPointSettings>
RiserInspection::createWayPoint(const std::vector<std::vector<std::string>> &csv_file,
                                dji_sdk::MissionWaypointTask &waypointTask) {
    std::vector<DJI::OSDK::WayPointSettings> wp_list; // create a list (vector) containing waypoint structures

    // Push first waypoint as a initial position
    DJI::OSDK::WayPointSettings start_wp;
    setWaypointDefaults(&start_wp);
    start_wp.latitude = start_gnss_.latitude;
    start_wp.longitude = start_gnss_.longitude;
    start_wp.altitude = (float) 10;
    start_wp.yaw = (int16_t) start_atti_eul.Yaw();
    ROS_INFO("Waypoint created at (LLA): %f \t%f \t%f\theading:%f\n", start_gnss_.latitude,
             start_gnss_.longitude, start_gnss_.altitude, start_atti_eul.Yaw());
    start_wp.index = 0;
    wp_list.push_back(start_wp);


    for (int k = 1; k < csv_file.size(); k++) {
        /// "WP,Latitude,Longitude,AltitudeAMSL,UavYaw,Speed,WaitTime,Picture"
        DJI::OSDK::WayPointSettings wp;
        setWaypointDefaults(&wp);
        wp.index = std::stoi(csv_file[k][0]);
        wp.latitude = std::stod(csv_file[k][1]);
        wp.longitude = std::stod(csv_file[k][2]);
        wp.altitude = std::stof(csv_file[k][3]);
        wp.yaw = std::stof(csv_file[k][4]);
        wp_list.push_back(wp);
    }
    // Come back home
    start_wp.index = csv_file[0].size() + 1;
    wp_list.push_back(start_wp);
    return wp_list;
}


bool RiserInspection::runWaypointMission(int responseTimeout) {
    ros::spinOnce();

    // Waypoint Mission : Initialization
    dji_sdk::MissionWaypointTask waypointTask;
    setWaypointInitDefaults(waypointTask);


    ROS_INFO("Creating Waypoints..\n");
    // Transform from CSV to DJI vector
    std::vector<std::vector<std::string>> filePathGen = pathGenerator.read_csv(
            pathGenerator.getFolderName() + "/" + pathGenerator.getFileName(), ",");
    std::vector<WayPointSettings> generatedWP = createWayPoint(filePathGen, waypointTask);

    // Waypoint Mission: Upload the waypoints
    ROS_INFO("Uploading Waypoints..\n");
    uploadWaypoints(generatedWP, responseTimeout, waypointTask);

    // Waypoint Mission: Init mission
    ROS_INFO("Initializing Waypoint Mission..\n");
    if (initWaypointMission(waypointTask).result) {
        ROS_INFO("Waypoint upload command sent successfully");
    } else {
        ROS_WARN("Failed sending waypoint upload command");
        return false;
    }

    // Waypoint Mission: Start
    if (missionAction(DJI_MISSION_TYPE::WAYPOINT, MISSION_ACTION::START).result) {
        ROS_INFO("Mission start command sent successfully");
    } else {
        ROS_WARN("Failed sending mission start command");
        return false;
    }
    return true;
}

void RiserInspection::uploadWaypoints(std::vector<DJI::OSDK::WayPointSettings> &wp_list,
                                      int responseTimeout, dji_sdk::MissionWaypointTask &waypointTask) {
    dji_sdk::MissionWaypoint waypoint;
    for (auto wp = wp_list.begin();
         wp != wp_list.end(); ++wp) {
        ROS_INFO("Waypoint created at (LLA): %f \t%f \t%f\n ", wp->latitude,
                 wp->longitude, wp->altitude);
        waypoint.latitude = wp->latitude;
        waypoint.longitude = wp->longitude;
        waypoint.altitude = wp->altitude;
        waypoint.damping_distance = 0;
        waypoint.target_yaw = 0;
        waypoint.target_gimbal_pitch = 0;
        waypoint.turn_mode = 0;
        waypoint.has_action = 0;
        waypointTask.mission_waypoint.push_back(waypoint);
    }
}

void RiserInspection::setWaypointDefaults(DJI::OSDK::WayPointSettings *wp) {
    wp->damping = 0;
    wp->yaw = 0;
    wp->gimbalPitch = 0;
    wp->turnMode = 0;
    wp->hasAction = 0;
    wp->actionTimeLimit = 100;
    wp->actionNumber = 2;
    wp->actionRepeat = 1;
    for (int i = 0; i < 16; ++i) {
        wp->commandList[i] = 0;
        wp->commandParameter[i] = 0;
    }
    wp->commandList[0] = 0; // commandList[0] = WP_ACTION_STAY (ms)
    wp->commandParameter[0] = 5000; // Set command to wait milliseconds
    wp->commandList[2] = 1; // commandList[2] = WP_ACTION_SIMPLE_SHOT
    wp->commandParameter[2] = 1; // Set command to take photo
//    wp->commandList[4] = 1; // commandList[4] = WP_ACTION_CRAFT_YAW
//    wp->commandParameter[4] = 0; // Set command to take photo
}

void RiserInspection::setWaypointInitDefaults(dji_sdk::MissionWaypointTask &waypointTask) {
    waypointTask.velocity_range = 2; // Maximum speed joystick input(2~15m)
    waypointTask.idle_velocity = 0.2; //Cruising Speed (without joystick input, no more than vel_cmd_range)
    waypointTask.action_on_finish = dji_sdk::MissionWaypointTask::FINISH_NO_ACTION;
    waypointTask.mission_exec_times = 1;
    waypointTask.yaw_mode = dji_sdk::MissionWaypointTask::YAW_MODE_WAYPOINT;
    waypointTask.trace_mode = dji_sdk::MissionWaypointTask::TRACE_POINT;
    waypointTask.action_on_rc_lost = dji_sdk::MissionWaypointTask::ACTION_FREE;
    waypointTask.gimbal_pitch_mode = dji_sdk::MissionWaypointTask::GIMBAL_PITCH_FREE;
}