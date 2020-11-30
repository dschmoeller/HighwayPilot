#include <uWS/uWS.h>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"

// for convenience
using nlohmann::json;
using std::string;
using std::vector;
using std::cout; 
using std::endl; 


// Define reference velocity and desired AV lane
int av_lane = 1; // Middle lane
double av_ref_vel = 0; // [m/s]
double av_max = 20; // [m/s]

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  std::ifstream in_map_(map_file_.c_str(), std::ifstream::in);

  string line;
  while (getline(in_map_, line)) {
    std::istringstream iss(line);
    double x;
    double y;
    float s;
    float d_x;
    float d_y;
    iss >> x;
    iss >> y;
    iss >> s;
    iss >> d_x;
    iss >> d_y;
    map_waypoints_x.push_back(x);
    map_waypoints_y.push_back(y);
    map_waypoints_s.push_back(s);
    map_waypoints_dx.push_back(d_x);
    map_waypoints_dy.push_back(d_y);
  }


  h.onMessage([&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,
               &map_waypoints_dx,&map_waypoints_dy]
              (uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
               uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
          // Main car's localization Data
          double car_x = j[1]["x"];
          double car_y = j[1]["y"];
          double car_s = j[1]["s"];
          double car_d = j[1]["d"];
          double car_yaw = j[1]["yaw"];
          double car_speed = j[1]["speed"];

          // Previous path data given to the Planner
          auto previous_path_x = j[1]["previous_path_x"];
          auto previous_path_y = j[1]["previous_path_y"];
          // Previous path's end s and d values 
          double end_path_s = j[1]["end_path_s"];
          double end_path_d = j[1]["end_path_d"];

          // Sensor Fusion Data, a list of all other cars on the same side 
          //   of the road.
          auto sensor_fusion = j[1]["sensor_fusion"];

          json msgJson;

          vector<double> next_x_vals;
          vector<double> next_y_vals;
          
          // (1) Variable Definitions
          int number_pts = 100; 
          int path_size = previous_path_x.size();
          // For building splines
          vector<double> spline_points_x; 
          vector<double> spline_points_y; 
          double start_ref_x = car_x; 
          double start_ref_y = car_y; 
          double start_ref_yaw = deg2rad(car_yaw); 

          // (2) Write already calculated points back from previous iteration
          // and increment speed if v_desired hasn't reached 
          for (int i = 0; i < path_size; ++i) {
            next_x_vals.push_back(previous_path_x[i]);
            next_y_vals.push_back(previous_path_y[i]);
          }
          // Increment speed
          if (av_ref_vel < av_max){
            av_ref_vel += 0.1; 
          }

          // (3) Set two starting anchor points for spline calculation
          // Distinguish inital starting mode and running mode
          if (path_size < 2) { 
            // Identify (assumed) previous point as anchor point for the spline
            // Use any previous point along heading line --> 1 * sin/cos
            double prev_x = car_x - cos(car_yaw); 
            double prev_y = car_y - sin(car_yaw);
            spline_points_x.push_back(prev_x); 
            spline_points_x.push_back(car_x); 
            spline_points_y.push_back(prev_y); 
            spline_points_y.push_back(car_y); 
          } 
          else {
            // --> Use the two most future waypoint as new starting/anchor points
            start_ref_x = previous_path_x[path_size-1];
            start_ref_y = previous_path_y[path_size-1];
            double prev_x = previous_path_x[path_size-2];
            double prev_y = previous_path_y[path_size-2];
            start_ref_yaw = atan2(start_ref_y - prev_y, start_ref_x - prev_x);
            // Add previous and current car points as anchor to the spline list
            spline_points_x.push_back(prev_x); 
            spline_points_x.push_back(start_ref_x); 
            spline_points_y.push_back(prev_y); 
            spline_points_y.push_back(start_ref_y); 
          }

          // (4) Add three more far away waypoints in order to define the splines
          double waypoint1 = 60.0; 
          double waypoint2 = 90.0; 
          double waypoint3 = 120.0; 
          vector<double> wp1 = getXY(car_s + waypoint1, (2+4*av_lane), map_waypoints_s, map_waypoints_x, map_waypoints_y); 
          vector<double> wp2 = getXY(car_s + waypoint2, (2+4*av_lane), map_waypoints_s, map_waypoints_x, map_waypoints_y); 
          vector<double> wp3 = getXY(car_s + waypoint3, (2+4*av_lane), map_waypoints_s, map_waypoints_x, map_waypoints_y); 
          spline_points_x.push_back(wp1[0]); 
          spline_points_x.push_back(wp2[0]);
          spline_points_x.push_back(wp3[0]);
          spline_points_y.push_back(wp1[1]); 
          spline_points_y.push_back(wp2[1]);
          spline_points_y.push_back(wp3[1]);
          
          // (5) Transform all five waypoints from global into AV coordinate system
          // Applying rotation and translation
          for (int i = 0; i < spline_points_x.size(); i++){
            // Translation
            double shift_x = spline_points_x[i] - start_ref_x; 
            double shift_y = spline_points_y[i] - start_ref_y; 
            // Applying translatin and rotation
            spline_points_x[i] = (shift_x*cos(0-start_ref_yaw) - shift_y*sin(0-start_ref_yaw));
            spline_points_y[i] = (shift_x*sin(0-start_ref_yaw) + shift_y*cos(0-start_ref_yaw));  

          }
          // (6) Define the spline
          tk::spline s; 
          s.set_points(spline_points_x, spline_points_y); 

          // (7) Use spline to sample points onto the trajectory   
          // Linearize spline in order to calculate incremental x values dependend on desired velocity
          double target_x = waypoint1; 
          double target_y = s(target_x); 
          double target_dist = sqrt(target_x*target_x + target_y*target_y); 
          // Remember previous point when going along the spline curve
          double x_add_on = 0; 
          // Iterate over to be filled up points  
          for (int i = 0; i < (number_pts - path_size); ++i) {
            // Sample points from spline using linearized x values
            // Identify equally distributed (vel dependend) x points on triangle
            double N = (target_dist/(0.02*av_ref_vel)); 
            double increment = target_x/N;  
            double x_point = x_add_on + increment; 
            double y_point = s(x_point); 
            x_add_on = x_point; 
            // Rotate x,y point back to Map coordinate system (They're still in AV COS)
            double x_tmp = x_point; 
            double y_tmp = y_point; 
            x_point = x_tmp*cos(start_ref_yaw) - y_tmp*sin(start_ref_yaw); 
            y_point = x_tmp*sin(start_ref_yaw) + y_tmp*cos(start_ref_yaw); 
            x_point += start_ref_x; 
            y_point += start_ref_y; 
            // Add point to Simulator buffer 
            next_x_vals.push_back(x_point);
            next_y_vals.push_back(y_point);
          }
         /*
          cout << "Points on next_x_vals are: "; 
          for (auto x : next_x_vals){
            cout << x << " "; 
          }
          cout << endl; */
      
          msgJson["next_x"] = next_x_vals;
          msgJson["next_y"] = next_y_vals;

          auto msg = "42[\"control\","+ msgJson.dump()+"]";

          ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
        }  // end "telemetry" if
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }  // end websocket if
  }); // end h.onMessage

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  
  h.run();
}