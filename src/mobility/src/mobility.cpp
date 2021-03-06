#include <ros/ros.h>

// ROS libraries
#include <angles/angles.h>
#include <random_numbers/random_numbers.h>
#include <tf/transform_datatypes.h>

// ROS messages
#include <std_msgs/Int16.h>
#include <std_msgs/UInt8.h>
#include <std_msgs/String.h>
#include <sensor_msgs/Joy.h>
#include <sensor_msgs/Range.h>
#include <geometry_msgs/Pose2D.h>
#include <geometry_msgs/Twist.h>
#include <nav_msgs/Odometry.h>

#include "Pose.h"
#include "TargetState.h"

// Custom messages
#include <shared_messages/TagsImage.h>

// To handle shutdown signals so the node quits properly in response to "rosnode kill"

#include <signal.h>
#include <math.h>

using namespace std;

// Random number generator
random_numbers::RandomNumberGenerator *rng;



string rover_name;
char host[128];
bool is_published_name = false;


int simulation_mode = 0;
float mobility_loop_time_step = 0.1;
float status_publish_interval = 5;
float kill_switch_timeout = 10;

pose current_location;

int transitions_to_auto = 0;
double time_stamp_transition_to_auto = 0.0;

// number of robots
int n_o_r = 6;

// string array for robots and data
string rover_data[6][4];

// theta averages
float glob_average = 0.0;
float local_average = 0.0;
float local_average_position;
float combined_theta = 0.0;


// state machine states
#define STATE_MACHINE_TRANSLATE 0
int state_machine_state = STATE_MACHINE_TRANSLATE;

//Publishers
ros::Publisher velocityPublish;
ros::Publisher stateMachinePublish;
ros::Publisher status_publisher;
ros::Publisher target_collected_publisher;
ros::Publisher angular_publisher;
ros::Publisher messagePublish;
ros::Publisher debug_publisher;

ros::Publisher posePublish;
ros::Publisher global_average_heading;
ros::Publisher local_average_heading;

//Subscribers
ros::Subscriber joySubscriber;
ros::Subscriber modeSubscriber;
ros::Subscriber targetSubscriber;
ros::Subscriber obstacleSubscriber;
ros::Subscriber odometrySubscriber;

ros::Subscriber poseSubscriber;

ros::Subscriber messageSubscriber;

//Timers
ros::Timer stateMachineTimer;
ros::Timer publish_status_timer;
ros::Timer killSwitchTimer;

// Mobility Logic Functions
void setVelocity(double linearVel, double angularVel);

// OS Signal Handler
void sigintEventHandler(int signal);

// Callback handlers
void joyCmdHandler(const geometry_msgs::Twist::ConstPtr &message);
void modeHandler(const std_msgs::UInt8::ConstPtr &message);
void targetHandler(const shared_messages::TagsImage::ConstPtr &tagInfo);
void obstacleHandler(const std_msgs::UInt8::ConstPtr &message); // 
void odometryHandler(const nav_msgs::Odometry::ConstPtr &message);
void mobilityStateMachine(const ros::TimerEvent &);
void publishStatusTimerEventHandler(const ros::TimerEvent &event);
void killSwitchTimerEventHandler(const ros::TimerEvent &event);
void messageHandler(const std_msgs::String::ConstPtr &message);

void poseHandler(const std_msgs::String::ConstPtr &message);

int main(int argc, char **argv)
{
    gethostname(host, sizeof(host));
    string hostName(host);

    rng = new random_numbers::RandomNumberGenerator(); // instantiate random number generator

    if (argc >= 2)
    {
        rover_name = argv[1];
        cout << "Welcome to the world of tomorrow " << rover_name << "!  Mobility module started." << endl;
    } else
    {
        rover_name = hostName;
        cout << "No Name Selected. Default is: " << rover_name << endl;
    }
    // NoSignalHandler so we can catch SIGINT ourselves and shutdown the node
    ros::init(argc, argv, (rover_name + "_MOBILITY"), ros::init_options::NoSigintHandler);
    ros::NodeHandle mNH;

    signal(SIGINT, sigintEventHandler); // Register the SIGINT event handler so the node can shutdown properly

    joySubscriber = mNH.subscribe((rover_name + "/joystick"), 10, joyCmdHandler);
    modeSubscriber = mNH.subscribe((rover_name + "/mode"), 1, modeHandler);
    targetSubscriber = mNH.subscribe((rover_name + "/targets"), 10, targetHandler);
    obstacleSubscriber = mNH.subscribe((rover_name + "/obstacle"), 10, obstacleHandler);
    odometrySubscriber = mNH.subscribe((rover_name + "/odom/ekf"), 10, odometryHandler);
    messageSubscriber = mNH.subscribe(("messages"), 10, messageHandler);

    poseSubscriber = mNH.subscribe(("poses"), 10, poseHandler);

    status_publisher = mNH.advertise<std_msgs::String>((rover_name + "/status"), 1, true);
    velocityPublish = mNH.advertise<geometry_msgs::Twist>((rover_name + "/velocity"), 10);
    stateMachinePublish = mNH.advertise<std_msgs::String>((rover_name + "/state_machine"), 1, true);
    messagePublish = mNH.advertise<std_msgs::String>(("messages"), 10, true);
    target_collected_publisher = mNH.advertise<std_msgs::Int16>(("targetsCollected"), 1, true);
    angular_publisher = mNH.advertise<std_msgs::String>((rover_name + "/angular"),1,true);
    publish_status_timer = mNH.createTimer(ros::Duration(status_publish_interval), publishStatusTimerEventHandler);
    killSwitchTimer = mNH.createTimer(ros::Duration(kill_switch_timeout), killSwitchTimerEventHandler);
    stateMachineTimer = mNH.createTimer(ros::Duration(mobility_loop_time_step), mobilityStateMachine);
    debug_publisher = mNH.advertise<std_msgs::String>("/debug", 1, true);
    messagePublish = mNH.advertise<std_msgs::String>(("messages"), 10 , true);

    posePublish = mNH.advertise<std_msgs::String>(("poses"), 10 , true);
    global_average_heading = mNH.advertise<std_msgs::String>(("global_average_heading"), 10 , true);
    local_average_heading = mNH.advertise<std_msgs::String>(("local_average_heading"), 10 , true);

    if (n_o_r = 3)
    {
        rover_data[0][0] = "";
        rover_data[1][0] = "";
        rover_data[2][0] = "";
    } else if(n_o_r = 6)
    {
        rover_data[0][0] = "";
        rover_data[1][0] = "";
        rover_data[2][0] = "";
        rover_data[3][0] = "";
        rover_data[4][0] = "";
        rover_data[5][0] = "";
    }

    local_average_position = 0.0;

    ros::spin();
    return EXIT_SUCCESS;
}

void mobilityStateMachine(const ros::TimerEvent &)
{
    std_msgs::String state_machine_msg;

    std_msgs::String pose_msg;

    if ((simulation_mode == 2 || simulation_mode == 3)) // Robot is in automode
    {
        if (transitions_to_auto == 0)
        {
            // This is the first time we have clicked the Autonomous Button. Log the time and increment the counter.
            transitions_to_auto++;
            time_stamp_transition_to_auto = ros::Time::now().toSec();
        }
        switch (state_machine_state)
        {
        case STATE_MACHINE_TRANSLATE:
        {
            float k = 0.1;
            state_machine_msg.data = "TRANSLATING";//, " + converter.str();
            //float angular_velocity = k * (local_average - current_location.theta);
            //float angular_velocity = k * (glob_average - current_location.theta);
            //float angular_velocity = k * (local_average_position - current_location.theta);
            float angular_velocity = k * (combined_theta - current_location.theta);
            float linear_velocity = 0.05;
            setVelocity(linear_velocity, angular_velocity);

            break;
        }
        default:
        {
            state_machine_msg.data = "DEFAULT CASE: SOMETHING WRONG!!!!";
            break;
        }
        }

    }
    else
    { // mode is NOT auto

        // publish current state for the operator to seerotational_controller
        std::stringstream converter;
        converter <<"CURRENT MODE: " << simulation_mode;

        state_machine_msg.data = "WAITING, " + converter.str();
    }

    std::stringstream converter;
    converter << rover_name << " (" << current_location.x << ", " << current_location.y << ", " << current_location.theta << ")";
    pose_msg.data = converter.str();
    posePublish.publish(pose_msg);

    stateMachinePublish.publish(state_machine_msg);
}

void setVelocity(double linearVel, double angularVel)
{
    geometry_msgs::Twist velocity;
    // Stopping and starting the timer causes it to start counting from 0 again.
    // As long as this is called before the kill switch timer reaches kill_switch_timeout seconds
    // the rover's kill switch wont be called.
    killSwitchTimer.stop();
    killSwitchTimer.start();

    velocity.linear.x = linearVel * 1.5;
    velocity.angular.z = angularVel * 8; //scaling factor for sim; removed by aBridge node
    velocityPublish.publish(velocity);
}

/***********************
 * ROS CALLBACK HANDLERS
 ************************/
void targetHandler(const shared_messages::TagsImage::ConstPtr &message) {
    // Only used if we want to take action after seeing an April Tag.
}

void modeHandler(const std_msgs::UInt8::ConstPtr &message)
{
    simulation_mode = message->data;
    setVelocity(0.0, 0.0);
}

void obstacleHandler(const std_msgs::UInt8::ConstPtr &message)
{
    if ( message->data > 0 )
    {
        if (message->data == 1)
        {
            // obstacle on right side
        }
        else
        {
            //obstacle in front or on left side
        }
    }
}

void odometryHandler(const nav_msgs::Odometry::ConstPtr &message)
{
    //Get (x,y) location directly from pose
    current_location.x = message->pose.pose.position.x;
    current_location.y = message->pose.pose.position.y;

    //Get theta rotation by converting quaternion orientation to pitch/roll/yaw
    tf::Quaternion q(message->pose.pose.orientation.x, message->pose.pose.orientation.y,
                     message->pose.pose.orientation.z, message->pose.pose.orientation.w);
    tf::Matrix3x3 m(q);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);
    current_location.theta = yaw;
}

void joyCmdHandler(const geometry_msgs::Twist::ConstPtr &message)
{
    if (simulation_mode == 0 || simulation_mode == 1)
    {
        setVelocity(message->linear.x, message->angular.z);
    }
}

void publishStatusTimerEventHandler(const ros::TimerEvent &)
{
    if (!is_published_name)
    {
        std_msgs::String name_msg;
        name_msg.data = "I ";
        name_msg.data = name_msg.data + rover_name;
        messagePublish.publish(name_msg);
        is_published_name = true;
    }

    std_msgs::String msg;
    msg.data = "online";
    status_publisher.publish(msg);
}

// Safety precaution. No movement commands - might have lost contact with ROS. Stop the rover.
// Also might no longer be receiving manual movement commands so stop the rover.
void killSwitchTimerEventHandler(const ros::TimerEvent &t)
{
    // No movement commands for killSwitchTime seconds so stop the rover
    setVelocity(0.0, 0.0);
    double current_time = ros::Time::now().toSec();
    ROS_INFO("In mobility.cpp:: killSwitchTimerEventHander(): Movement input timeout. Stopping the rover at %6.4f.",
             current_time);
}

void sigintEventHandler(int sig)
{
    // All the default sigint handler does is call shutdown()
    ros::shutdown();
}

void messageHandler(const std_msgs::String::ConstPtr& message)
{
}

void poseHandler(const std_msgs::String::ConstPtr& message)
{
    std::string msg;
    msg = message->data.c_str();

    std_msgs::String pose_msg;

    std::string delimiter = ",";
    char remove_char[]  = "()abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ ";
    int index = 0;

    std::string name = msg.substr(0, msg.find(" "));

    for (int i = 0; i < n_o_r; i++)
    {
        if(rover_data[i][0] == "")
        {
            rover_data[i][0] = name;
            index = i;
            break;
        }
        else if (rover_data[i][0] == name)
        {
            index = i;
            break;
        }
    }

    for (unsigned int i = 0; i < strlen(remove_char); i++)
    {
       msg.erase (std::remove(msg.begin(), msg.end(), remove_char[i]), msg.end());
    }

    int pos = 0; // may need to use size_t
    std::string num;

    // x
    pos = msg.find(delimiter);

    num = msg.substr(0, pos);
    rover_data[index][1] = num;
    msg.erase(0, pos + delimiter.length());

    // y
    pos = msg.find(delimiter);

    num = msg.substr(0, pos);
    rover_data[index][2] = num;
    msg.erase(0, pos + delimiter.length());

    // theta
    rover_data[index][3] = msg;

    // calculate global average heading
    float u1[2], u2[2], u3[2], u4[2], u5[2], u6[2];
    float g_avg[2];

    float seperation[2];
    float sep_distance = 1.0;
    float sep_weight = 0.5;
    float coh_weight = 0.0;
    float align_weight = 0.0;

    if (n_o_r == 3)
    {

        u1[0] = cos (strtof((rover_data[0][3]).c_str(),0));
        u1[1] = sin (strtof((rover_data[0][3]).c_str(),0));

        u2[0] = cos (strtof((rover_data[1][3]).c_str(),0));
        u2[1] = sin (strtof((rover_data[1][3]).c_str(),0));

        u3[0] = cos (strtof((rover_data[2][3]).c_str(),0));
        u3[1] = sin (strtof((rover_data[2][3]).c_str(),0));

        float g_avg[2];
        g_avg[0] = (u1[1] + u2[1] + u3[1]) / 3;
        g_avg[1] = (u1[0] + u2[0] + u3[0]) / 3;

        glob_average = atan2(g_avg[1], g_avg[0]);

        std::stringstream converter;
        converter << "Global Average Theta = " << glob_average;
        pose_msg.data = converter.str();
        global_average_heading.publish(pose_msg);
    } else if (n_o_r == 6)
    {

        u1[0] = cos (strtof((rover_data[0][3]).c_str(),0));
        u1[1] = sin (strtof((rover_data[0][3]).c_str(),0));

        u2[0] = cos (strtof((rover_data[1][3]).c_str(),0));
        u2[1] = sin (strtof((rover_data[1][3]).c_str(),0));

        u3[0] = cos (strtof((rover_data[2][3]).c_str(),0));
        u3[1] = sin (strtof((rover_data[2][3]).c_str(),0));

        u4[0] = cos (strtof((rover_data[3][3]).c_str(),0));
        u4[1] = sin (strtof((rover_data[3][3]).c_str(),0));

        u5[0] = cos (strtof((rover_data[4][3]).c_str(),0));
        u5[1] = sin (strtof((rover_data[4][3]).c_str(),0));

        u6[0] = cos (strtof((rover_data[5][3]).c_str(),0));
        u6[1] = sin (strtof((rover_data[5][3]).c_str(),0));

        g_avg[0] = (u1[1] + u2[1] + u3[1] + u4[1] + u5[1] + u6[1]) / 6;
        g_avg[1] = (u1[0] + u2[0] + u3[0] + u4[0] + u5[0] + u6[0]) / 6;

        glob_average = atan2(g_avg[1], g_avg[0]);

        std::stringstream converter;
        converter << "Global Average Theta = " << glob_average;
        pose_msg.data = converter.str();
        global_average_heading.publish(pose_msg);
    }

    // calculate local average heading

    float d12, d13, d14, d15, d16, xdif, ydif;
    int num_neighbors = 0;
    bool contained_d12 = false;
    bool contained_d13 = false;
    bool contained_d14 = false;
    bool contained_d15 = false;
    bool contained_d16 = false;
    std::stringstream gat;

    // 3 or 6 robots
    if (n_o_r == 3)
    {
        // find distances between other rovers
        if (index == 0)
        {
            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                num_neighbors++;
            }

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                num_neighbors++;
            }

            if (contained_d12 == true && contained_d13 == true)
            {
                g_avg[0] = (u1[1] + u2[1] + u3[1]) / 3;
                g_avg[1] = (u1[0] + u2[0] + u3[0]) / 3;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d12 == true)
            {
                g_avg[0] = (u1[1] + u2[1]) / 2;
                g_avg[1] = (u1[0] + u2[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d13 == true)
            {
                g_avg[0] = (u1[1] + u3[1]) / 2;
                g_avg[1] = (u1[0] + u3[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else
            {
                local_average = 0.0;
            }


            gat << rover_name << " with " << num_neighbors << " neighbors with Local Average Theta = " << local_average;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);

        } else if(index == 1)
        {
            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                num_neighbors++;
            }

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                num_neighbors++;
            }

            float local_average;
            if (contained_d12 == true && contained_d13 == true)
            {
                g_avg[0] = (u1[1] + u2[1] + u3[1]) / 3;
                g_avg[1] = (u1[0] + u2[0] + u3[0]) / 3;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d12 == true)
            {
                g_avg[0] = (u1[1] + u2[1]) / 2;
                g_avg[1] = (u1[0] + u2[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d13 == true)
            {
                g_avg[0] = (u1[1] + u3[1]) / 2;
                g_avg[1] = (u1[0] + u3[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else
            {
                local_average = 0.0;
            }


            gat << rover_name << " with " << num_neighbors << " neighbors with Local Average Theta = " << local_average;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);

        } else
        {
            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                num_neighbors++;
            }

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                num_neighbors++;
            }

            float local_average;
            if (contained_d12 == true && contained_d13 == true)
            {
                g_avg[0] = (u1[1] + u2[1] + u3[1]) / 3;
                g_avg[1] = (u1[0] + u2[0] + u3[0]) / 3;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d12 == true)
            {
                g_avg[0] = (u1[1] + u3[1]) / 2;
                g_avg[1] = (u1[0] + u3[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else if (contained_d13 == true)
            {
                g_avg[0] = (u2[1] + u3[1]) / 2;
                g_avg[1] = (u2[0] + u3[0]) / 2;

                local_average = atan2(g_avg[1], g_avg[0]);
            } else
            {
                local_average = 0.0;
            }


            gat << rover_name << " with " << num_neighbors << " neighbors with Local Average Theta = " << local_average;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);
        }


    } else if (n_o_r == 6) {

        // find distances between other rovers
        num_neighbors = 1;

        // array for summation of local positions
        float loc_positions[2];
        loc_positions[0] = 0.0;
        loc_positions[1] = 0.0;

        seperation[0] = 0.0;
        seperation[1] = 0.0;

        if (index == 0)
        {
            g_avg[0] = u1[1];
            g_avg[1] = u1[0];

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u2[1];
                g_avg[1] = g_avg[1] + u2[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u3[1];
                g_avg[1] = g_avg[1] + u3[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[3][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[3][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u4[1];
                g_avg[1] = g_avg[1] + u4[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[4][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[4][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u5[1];
                g_avg[1] = g_avg[1] + u5[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[0][1]).c_str(),0) - strtof((rover_data[5][1]).c_str(),0);
            ydif = strtof((rover_data[0][2]).c_str(),0) - strtof((rover_data[5][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u6[1];
                g_avg[1] = g_avg[1] + u6[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }

            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);

            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);

        } else if(index == 1)
        {
            g_avg[0] = u2[1];
            g_avg[1] = u2[0];

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u1[1];
                g_avg[1] = g_avg[1] + u1[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u3[1];
                g_avg[1] = g_avg[1] + u3[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[3][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[3][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u4[1];
                g_avg[1] = g_avg[1] + u4[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[4][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[4][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u5[1];
                g_avg[1] = g_avg[1] + u5[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[1][1]).c_str(),0) - strtof((rover_data[5][1]).c_str(),0);
            ydif = strtof((rover_data[1][2]).c_str(),0) - strtof((rover_data[5][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u6[1];
                g_avg[1] = g_avg[1] + u6[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }

            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);

            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);

        } else if(index == 2)
        {
            g_avg[0] = u3[1];
            g_avg[1] = u3[0];

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u1[1];
                g_avg[1] = g_avg[1] + u1[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u2[1];
                g_avg[1] = g_avg[1] + u2[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[3][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[3][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u4[1];
                g_avg[1] = g_avg[1] + u4[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[4][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[4][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u5[1];
                g_avg[1] = g_avg[1] + u5[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[2][1]).c_str(),0) - strtof((rover_data[5][1]).c_str(),0);
            ydif = strtof((rover_data[2][2]).c_str(),0) - strtof((rover_data[5][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u6[1];
                g_avg[1] = g_avg[1] + u6[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }


            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);

            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);
        } else if(index == 3)
        {
            g_avg[0] = u4[1];
            g_avg[1] = u4[0];

            xdif = strtof((rover_data[3][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[3][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u1[1];
                g_avg[1] = g_avg[1] + u1[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[3][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[3][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u2[1];
                g_avg[1] = g_avg[1] + u2[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[3][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[3][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u3[1];
                g_avg[1] = g_avg[1] + u3[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[3][1]).c_str(),0) - strtof((rover_data[4][1]).c_str(),0);
            ydif = strtof((rover_data[3][2]).c_str(),0) - strtof((rover_data[4][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u5[1];
                g_avg[1] = g_avg[1] + u5[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[3][1]).c_str(),0) - strtof((rover_data[5][1]).c_str(),0);
            ydif = strtof((rover_data[3][2]).c_str(),0) - strtof((rover_data[5][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u6[1];
                g_avg[1] = g_avg[1] + u6[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }


            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);

            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);
        } else if(index == 4)
        {
            g_avg[0] = u5[1];
            g_avg[1] = u5[0];

            xdif = strtof((rover_data[4][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[4][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u1[1];
                g_avg[1] = g_avg[1] + u1[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[4][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[4][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u2[1];
                g_avg[1] = g_avg[1] + u2[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[4][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[4][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u3[1];
                g_avg[1] = g_avg[1] + u3[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[4][1]).c_str(),0) - strtof((rover_data[3][1]).c_str(),0);
            ydif = strtof((rover_data[4][2]).c_str(),0) - strtof((rover_data[3][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u4[1];
                g_avg[1] = g_avg[1] + u4[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[4][1]).c_str(),0) - strtof((rover_data[5][1]).c_str(),0);
            ydif = strtof((rover_data[4][2]).c_str(),0) - strtof((rover_data[5][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u6[1];
                g_avg[1] = g_avg[1] + u6[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }


            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);

            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);
        } else if(index == 5)
        {
            g_avg[0] = u6[1];
            g_avg[1] = u6[0];

            xdif = strtof((rover_data[5][1]).c_str(),0) - strtof((rover_data[0][1]).c_str(),0);
            ydif = strtof((rover_data[5][2]).c_str(),0) - strtof((rover_data[0][2]).c_str(),0);
            d12 = sqrt(xdif*xdif + ydif*ydif);

            if (d12 <= 2)
            {
                contained_d12 = true;
                g_avg[0] = g_avg[0] + u1[1];
                g_avg[1] = g_avg[1] + u1[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d12 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[5][1]).c_str(),0) - strtof((rover_data[1][1]).c_str(),0);
            ydif = strtof((rover_data[5][2]).c_str(),0) - strtof((rover_data[1][2]).c_str(),0);
            d13 = sqrt(xdif*xdif + ydif*ydif);

            if (d13 <= 2)
            {
                contained_d13 = true;
                g_avg[0] = g_avg[0] + u2[1];
                g_avg[1] = g_avg[1] + u2[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d13 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[5][1]).c_str(),0) - strtof((rover_data[2][1]).c_str(),0);
            ydif = strtof((rover_data[5][2]).c_str(),0) - strtof((rover_data[2][2]).c_str(),0);
            d14 = sqrt(xdif*xdif + ydif*ydif);

            if (d14 <= 2)
            {
                contained_d14 = true;
                g_avg[0] = g_avg[0] + u3[1];
                g_avg[1] = g_avg[1] + u3[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d14 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[5][1]).c_str(),0) - strtof((rover_data[3][1]).c_str(),0);
            ydif = strtof((rover_data[5][2]).c_str(),0) - strtof((rover_data[3][2]).c_str(),0);
            d15 = sqrt(xdif*xdif + ydif*ydif);

            if (d15 <= 2)
            {
                contained_d15 = true;
                g_avg[0] = g_avg[0] + u4[1];
                g_avg[1] = g_avg[1] + u4[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d15 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            xdif = strtof((rover_data[5][1]).c_str(),0) - strtof((rover_data[4][1]).c_str(),0);
            ydif = strtof((rover_data[5][2]).c_str(),0) - strtof((rover_data[4][2]).c_str(),0);
            d16 = sqrt(xdif*xdif + ydif*ydif);

            if (d16 <= 2)
            {
                contained_d16 = true;
                g_avg[0] = g_avg[0] + u5[1];
                g_avg[1] = g_avg[1] + u5[0];

                loc_positions[0] = loc_positions[0] + xdif;
                loc_positions[1] = loc_positions[1] + ydif;
                num_neighbors++;

                if (d16 <= sep_distance)
                {
                    seperation[0] += xdif;
                    seperation[1] += ydif;
                }
            }

            if (num_neighbors == 1)
            {
                local_average = 0.0;
            } else
            {
                g_avg[0] = g_avg[0] / num_neighbors;
                g_avg[1] = g_avg[1] / num_neighbors;

                local_average = atan2(g_avg[1], g_avg[0]);
            }


            // normalize and then multiply by weights
            // alignment
            float norm = sqrt(g_avg[0]*g_avg[0] + g_avg[1]*g_avg[1]);
            g_avg[0] = g_avg[0]/norm * align_weight;
            g_avg[1] = g_avg[1]/norm * align_weight;
            // cohesion
            norm = sqrt(loc_positions[0]*loc_positions[0] + loc_positions[1]*loc_positions[1]);
            loc_positions[0] = -loc_positions[0]/norm * coh_weight;
            loc_positions[1] = -loc_positions[1]/norm * coh_weight;
            // separation
            norm = sqrt(seperation[0]*seperation[0] + seperation[1]*seperation[1]);
            seperation[0] = -seperation[0]/norm * sep_weight;
            seperation[1] = -seperation[1]/norm * sep_weight;

            combined_theta = atan2(g_avg[1]+loc_positions[1]+seperation[1], g_avg[0]+loc_positions[0]+seperation[0]);


            local_average_position = atan2(loc_positions[1], loc_positions[0]);

            gat << rover_name << " with " << num_neighbors << " neighbors with Combine Theta = " << combined_theta;
            pose_msg.data = gat.str();
            local_average_heading.publish(pose_msg);
        }

    }


}
