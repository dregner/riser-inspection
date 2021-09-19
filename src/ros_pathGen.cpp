#include <ros_pathGen.hh>
#include <experimental/filesystem>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

PathGenerate::PathGenerate() {
    saved_wp_ << "Latitude, Longitude, AltitudeAMSL, Speed, Picture, WP, CameraTilt, UavYaw" << "\n";
    initServices(nh_);
    initSubscribers(nh_);
}

PathGenerate::~PathGenerate() {

}

void PathGenerate::setInitCoord(double lon, double lat, int alt, int head) {
    lon0_ = lon;
    lat0_ = lat;
    alt0_ = alt;
    head0_ = head;
}

void PathGenerate::setDJIwaypointTask(float velocity_range, float idle_velocity, int action_on_finish,
                                      int mission_exec_times, int yaw_mode, int trace_mode,
                                      int action_on_rc_lost, int gimbal_pitch_mode) {
    waypointTaskDJI_[0] = velocity_range;
    waypointTaskDJI_[1] = idle_velocity;
    waypointTaskDJI_[2] = action_on_finish;
    waypointTaskDJI_[3] = mission_exec_times;
    waypointTaskDJI_[4] = yaw_mode;
    waypointTaskDJI_[5] = trace_mode;
    waypointTaskDJI_[6] = action_on_rc_lost;
    waypointTaskDJI_[7] = gimbal_pitch_mode;

}

void PathGenerate::rotz_cartP(int yaw) {

    rotz_[0][0] = (int) cos(DEG2RAD(yaw));
    rotz_[0][1] = (int) (sin(DEG2RAD(yaw)) * -1);
    rotz_[0][2] = 0;

    rotz_[1][0] = (int) sin(DEG2RAD(yaw));
    rotz_[1][1] = (int) cos(DEG2RAD(yaw));
    rotz_[1][2] = 0;

    rotz_[2][0] = 0;
    rotz_[2][1] = 0;
    rotz_[2][2] = 1;
}

void PathGenerate::initServices(ros::NodeHandle &nh) {
    try {
        generate_pathway_srv_ = nh.advertiseService("riser_inspection/waypoint_generator",
                                                    &PathGenerate::PathGen_serviceCB, this);
        ROS_INFO("Service riser_inspection/waypoint_generator initialize");
        wp_folders_srv = nh.advertiseService("riser_inspection/Folder", &PathGenerate::Folders_serviceCB, this);
        ROS_INFO("Service riser_inspection/Folder initialized");
    } catch (ros::Exception &e) {
        ROS_ERROR("Subscribe topics exception: %s", e.what());
    }
}


void PathGenerate::initSubscribers(ros::NodeHandle &nh) {
    try {
        ros::NodeHandle nh_private("~");

        std::string left_topic_, right_topic_, gps_topic_, rtk_topic_, imu_topic_;
        nh_private.param("gps_topic", gps_topic_, std::string("/dji_osdk_ros/gps_position"));
        nh_private.param("rtk_topic", rtk_topic_, std::string("/dji_osdk_ros/rtk_position"));

        gps_position_sub_.subscribe(nh, gps_topic_, 1);
        ROS_INFO("Subscriber in Camera GPS Topic: %s", gps_topic_.c_str());
        rtk_position_sub_.subscribe(nh, rtk_topic_, 1);
        ROS_INFO("Subscriber in Camera RTK Topic: %s", rtk_topic_.c_str());

        sync_.reset(new Sync(PathGeneratePolicy(10), gps_position_sub_, rtk_position_sub_));
        sync_->registerCallback(boost::bind(&PathGenerate::get_gps_position, this, _1, _2));

        ROS_INFO("Subscribe complet");
    } catch (ros::Exception &e) {
        ROS_ERROR("Subscribe topics exception: %s", e.what());
    }
}

void PathGenerate::get_gps_position(const sensor_msgs::NavSatFixConstPtr &msg_gps,
                                    const sensor_msgs::NavSatFixConstPtr &msg_rtk) {
    ptr_gps_position_ = msg_gps;
    ptr_rtk_position_ = msg_rtk;
    if (first_time) {
        lat0_ = ptr_rtk_position_->latitude;
        lon0_ = ptr_rtk_position_->longitude;
        alt0_ = ptr_rtk_position_->altitude;
        //TODO: which topic provides heading value?
        first_time = false;
    }
}


void PathGenerate::print_wp(double *wp_array, int size, int n) {
    char const *cartesian[6] = {"x", "y", "z", "dz", "dy", "dz"};
    char const *gns_cord[5] = {"lat", "lon", "alt", "roll", "pitch"};
    std::cout << "Waypoint - " << n << std::endl;
    for (int i = 0; i < size; i++) {
        if (size > 5) { std::cout << cartesian[i] << "\t" << wp_array[i] << std::endl; }
        else { std::cout << gns_cord[i] << "\t" << wp_array[i] << std::endl; }
    }
}


void PathGenerate::csv_save_wp(double *wp_array, int row) {
    for (int i = 0; i < row; i++) {
        if (saved_wp_.is_open()) {
            if (i != row - 1) { saved_wp_ << std::setprecision(10) << wp_array[i] << ", "; }
            else { saved_wp_ << std::setprecision(10) << wp_array[i] << "\n"; }
        }
    }
}

void PathGenerate::pointCartToCord(double cart_wp[6], int nCount) {

    //[6371 km]. the approximate radius of earth
    long R = 6371 * 1000;



    // Convert starting coordinate (lat lon alt) to cartesian (XYZ)
    double x0 = R * DEG2RAD(lon0_) * cos(DEG2RAD(lon0_));
    double y0 = R * DEG2RAD(lat0_);
    double z0 = (cart_array_[2] * -1) + alt0_;


    double firstHeading = 0;

    double x = cart_array_[0] + x0;
    double y = cart_array_[1] + y0;
    double alt = cart_array_[2] + z0;


    double lon = RAD2DEG(x / (R * cos(DEG2RAD(lon0_))));
    double lat = RAD2DEG(y / R);

    coord_array_[0] = lat;
    coord_array_[1] = lon;
    coord_array_[2] = alt;


    double roll = RAD2DEG(atan2(cart_wp[4], cart_wp[3]));

    double heading = roll - 180 - 90;

    if (nCount == 0) {
        firstHeading = heading;
    }

    double pitch = DEG2RAD(asin(cart_array_[5]));

    heading = (heading - firstHeading + head0_);

    if (heading < -180) {
        heading = heading + 360;
    } else if (heading > 180) {
        heading = heading - 360;
    }
//
    coord_array_[3] = heading;
    coord_array_[4] = pitch;
    ROS_INFO("Coord: %f %f %f %f %f", coord_array_[0], coord_array_[1], coord_array_[2], coord_array_[3],
             coord_array_[4]);
    PathGenerate::csv_save_wp(coord_array_, sizeof(coord_array_) / sizeof(coord_array_[0]));
}

void PathGenerate::createInspectionPoints(const double phi, const float d, const float da, const float nh,
                                          const float dv, const float nv) {
    // Inspection radius.
    double r = d + phi / 2;

    int nCount = 0;
    float p[3] = {0, 0, 0};

    // Height change
    float hc = ((nv - 1) * dv);

    //Path control
    bool isVertical = true;

    // Position [XY] - Horizontal levels

    for (int i = 0; i <= nh - 1; i++) {
        // Update Alpha
        float alpha = i * da;
        float hs;
        float hl;

        //Controls vertical path
        if (isVertical) {
            hl = 0; // Initial height
            hs = dv; // Height step
        } else {
            hl = hc; // Initial height
            hs = dv * (-1); // Initial step
        }

        for (int j = 0; j <= nv - 1; j++) {
            float z = j * hs + hl;

            // Lines below set position
            float x = r * cos(DEG2RAD(alpha));
            float y = r * sin(DEG2RAD(alpha));
            p[0] = x;
            p[1] = y;
            p[2] = z;

            // Orientation
            float endPoint[3] = {0, 0, z};

            double dr[3] = {endPoint[0] - x, endPoint[1] - y, endPoint[2] - z};

            // Calculation of the absolute value of the array
            float absolute = sqrt((dr[0] * dr[0]) + (dr[1] * dr[1]) + (dr[2] * dr[2]));

            dr[0] = dr[0] / absolute;
            dr[1] = dr[1] / absolute;
            dr[2] = dr[2] / absolute;
            double dr2 = dr[2] / absolute;

            PathGenerate::rotz_cartP(180);

            for (int j = 0; j < sizeof(rotz_) / sizeof(rotz_[0]); j++) { // j from 0 to 2
                double var_p = 0, var_dr = 0;
                for (int k = 0; k < sizeof(rotz_[0]) / sizeof(int); k++) { // k from 0 to 2
                    var_p += (rotz_[j][k] * p[j]);
                    var_dr += (rotz_[j][k] * dr[j]);
                    if (k == 2) {
                        p[j] = var_p;
                        dr[j] = var_dr;
                    }
                }
            }

            cart_array_[0] = p[0];
            cart_array_[1] = p[1];
            cart_array_[2] = z + alt0_;
            cart_array_[3] = dr[0];
            cart_array_[4] = dr[1];
            cart_array_[5] = dr2;
            PathGenerate::pointCartToCord(cart_array_, nCount);

//            print_wp(cart_array_, sizeof(cart_array_) / sizeof(cart_array_[0]), nCount);
//            csv_save_wp(cart_array_, sizeof(cart_array_) / sizeof(cart_array_[0]));
            nCount++;
        }
        isVertical = !isVertical;
    }
}


bool PathGenerate::PathGen_serviceCB(riser_inspection::wpGenerate::Request &req,
                                     riser_inspection::wpGenerate::Response &res) {
    std::string slash = "/";
    saved_wp_.open(file_path_ + slash + file_name_);
    try {
        PathGenerate::createInspectionPoints(req.riser_diameter, req.riser_distance, req.delta_angle,
                                             req.horizontal_number,
                                             req.delta_height, req.vertical_number);
        ROS_INFO("Waypoints created");
    } catch (ros::Exception &e) {
        ROS_INFO("ROS error %s", e.what());
        res.result = false;
        saved_wp_.close();
        return res.result;
    }
    res.result = true;
    saved_wp_.close();
    return res.result;
}


inline bool PathGenerate::exists(const std::string &name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

bool PathGenerate::Folders_serviceCB(riser_inspection::wpFolders::Request &req,
                                     riser_inspection::wpFolders::Response &res) {

    if (req.file_name.c_str() != NULL) {
        ROS_INFO("Changing waypoint archive name %s", req.file_name.c_str());
        file_name_ = req.file_name;
    }
    if (exists(req.file_path.c_str())) {
        ROS_INFO("Waypoint folder changed %s/%s", req.file_path.c_str(), file_name_.c_str());
        file_path_ = req.file_path;
        res.result = true;
        return res.result;
    } else {
        ROS_ERROR("Folder does not exist, file will be written in %s/%s", file_path_.c_str(), file_name_.c_str());
        res.result = false;
        return res.result;
    }
}

