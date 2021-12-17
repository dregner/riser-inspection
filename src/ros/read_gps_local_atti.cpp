//
// Created by vant3d on 09/12/2021.
//

#include <opencv2/highgui/highgui.hpp>
#include <sensor_msgs/NavSatFix.h>
#include <geometry_msgs/QuaternionStamped.h>
#include <geometry_msgs/PoseStamped.h>
#include <ros/ros.h>
#include <ignition/math/Pose3.hh>
#include <message_filters/subscriber.h>
#include <message_filters/sync_policies/approximate_time.h>

#define RAD2DEG(RAD) ((RAD) * 180 / M_PI)

void callback(const sensor_msgs::NavSatFix::ConstPtr &gps_msg,
              const geometry_msgs::QuaternionStamped::ConstPtr &atti_msg,
              const geometry_msgs::PoseStamped::ConstPtr &local_msg) {

    ignition::math::Quaterniond rpy;
    rpy.Set(atti_msg->quaternion.w, atti_msg->quaternion.x, atti_msg->quaternion.y, atti_msg->quaternion.z);
    float yaw = RAD2DEG(rpy.Yaw()) - 90;
    if (yaw < -180) { yaw = RAD2DEG(rpy.Yaw()) - 90 + 360; }
    if (yaw > 180) { yaw = RAD2DEG(rpy.Yaw()) - 90 - 360; }

    std::cout << "R: " << RAD2DEG(rpy.Roll()) << "\tP: " << RAD2DEG(rpy.Pitch()) << "\tY: " << RAD2DEG(rpy.Yaw())
              << std::endl;
    std::cout << "OFFSET -> Yaw-90" << std::endl;
    std::cout << "R: " << RAD2DEG(rpy.Roll()) << "\tP: " << RAD2DEG(rpy.Pitch()) << "\tY: " << -yaw << std::endl;
    std::cout << "GPS" << std::endl;
    std::cout << "LAT: " << gps_msg->latitude << "\tLON: " << gps_msg->longitude << "\tALT: " << gps_msg->altitude
              << std::endl;
    std::cout << "LOCAL POS" << std::endl;
    std::cout << "X: " << local_msg->pose.position.x << "\tY: " << local_msg->pose.position.y << "\tZ: "
              << local_msg->pose.position.z << std::endl;
    std::cout << "\033[2J\033[1;1H";     // clear terminal

}

int main(int argc, char **argv) {

    ros::init(argc, argv, "stereo_thread");
    ros::NodeHandle nh;

    message_filters::Subscriber<sensor_msgs::NavSatFix> gps(nh, "/dji_sdk/gps_position", 1);
    message_filters::Subscriber<geometry_msgs::QuaternionStamped> atti(nh, "/dji_sdk/attitude", 1);
    message_filters::Subscriber<geometry_msgs::PoseStamped> local(nh, "/dji_sdk/local_position", 1);


    typedef message_filters::sync_policies::ApproximateTime<sensor_msgs::NavSatFix, geometry_msgs::QuaternionStamped, geometry_msgs::PoseStamped> MySyncPolicy;
    // ExactTime takes a queue size as its constructor argument, hence MySyncPolicy(10)
    message_filters::Synchronizer<MySyncPolicy> sync(MySyncPolicy(100), gps, atti, local);
    sync.registerCallback(boost::bind(&callback, _1, _2, _3));

    while (ros::ok()) {
        ros::spinOnce();
    }
    return 0;
}
