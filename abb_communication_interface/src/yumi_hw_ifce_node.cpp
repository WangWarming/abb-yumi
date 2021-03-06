// SYS
#include <sys/mman.h>
#include <cmath>
#include <time.h>
#include <signal.h>
#include <stdexcept>

// ROS headers
#include <ros/ros.h>
#include <controller_manager/controller_manager.h>

// the lwr hw fri interface
#include "yumi_hw/yumi_hw_rapid.h"

bool g_quit = false;

void quitRequested(int sig)
{
  g_quit = true;
}


// Get the URDF XML from the parameter server
std::string getURDF(ros::NodeHandle &model_nh_, std::string param_name)
{
  std::string urdf_string;
  std::string robot_description = "/robot_description";

  // search and wait for robot_description on param server
  while (urdf_string.empty())
  {
    std::string search_param_name;
    if (model_nh_.searchParam(param_name, search_param_name))
    {
      ROS_INFO_ONCE_NAMED("LWRHWFRI", "LWRHWFRI node is waiting for model"
        " URDF in parameter [%s] on the ROS param server.", search_param_name.c_str());

      model_nh_.getParam(search_param_name, urdf_string);
    }
    else
    {
      ROS_INFO_ONCE_NAMED("LWRHWFRI", "LWRHWFRI node is waiting for model"
        " URDF in parameter [%s] on the ROS param server.", robot_description.c_str());

      model_nh_.getParam(param_name, urdf_string);
    }

    usleep(100000);
  }
  ROS_DEBUG_STREAM_NAMED("LWRHWFRI", "Received URDF from param server, parsing...");

  return urdf_string;
}

int main( int argc, char** argv )
{
  // initialize ROS
  ros::init(argc, argv, "yumi_hw_interface", ros::init_options::NoSigintHandler);

  // ros spinner
  ros::AsyncSpinner spinner(4);
  spinner.start();

  // custom signal handlers
  signal(SIGTERM, quitRequested);
  signal(SIGINT, quitRequested);
  signal(SIGHUP, quitRequested);

  // create a node
  ros::NodeHandle yumi_nh ("~");

  // get params or give default values
  int port;
  std::string hintToRemoteHost;
  std::string name;
  //yumi_nh.param("port", port, 49939);
  yumi_nh.param("ip", hintToRemoteHost, std::string("192.168.125.1") );
  yumi_nh.param("name", name, std::string("yumi"));

  // get the general robot description, the lwr class will take care of parsing what's useful to itself
  std::string urdf_string = getURDF(yumi_nh, "/robot_description");

  YumiHWRapid yumi_robot;
  yumi_robot.create(name, urdf_string);
  yumi_robot.setup(hintToRemoteHost);
  
  if(!yumi_robot.init())
  {
    ROS_FATAL_NAMED("yumi_hw","Could not initialize robot real interface");
    return -1;
  }

  // timer variables
  struct timespec ts = {0, 0};
  ros::Time last(ts.tv_sec, ts.tv_nsec), now(ts.tv_sec, ts.tv_nsec);
  ros::Duration period(1.0);

  float sampling_time = yumi_robot.getSampleTime();
  ROS_INFO("Sampling time on robot: %f", sampling_time);

  //the controller manager
  controller_manager::ControllerManager manager(&yumi_robot);

  // run as fast as possible
  while( !g_quit )
  {
    // get the time / period
    //if (!clock_gettime(CLOCK_MONOTONIC, &ts))
    //{
      now = ros::Time::now();	
      //now.sec = ts.tv_sec;
      //now.nsec = ts.tv_nsec;
      period = now - last;
      last = now;
    //} 
    //else
    //{
    //  ROS_FATAL("Failed to poll realtime clock!");
    //  break;
    //}

    // read the state from the lwr
    yumi_robot.read(now, period);
    
    // update the controllers
    manager.update(now, period);

    // write the command to the lwr
    yumi_robot.write(now, period);
    
    //std::cout<<"Period is "<<period.toSec()<<std::endl;
    //ros::Duration(sampling_time).sleep();
  }

  std::cerr<<"Stopping spinner..."<<std::endl;
  spinner.stop();

  std::cerr<<"Bye!"<<std::endl;

  return 0;
}
