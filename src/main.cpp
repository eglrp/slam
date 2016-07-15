//==============================================================================
//         Copyright 2015 INSTITUT PASCAL UMR 6602 CNRS/Univ. Clermont II
//
//          Distributed under the Boost Software License, Version 1.0.
//                 See accompanying file LICENSE.txt or copy at
//                     http://www.boost.org/LICENSE_1_0.txt
//==============================================================================

#ifdef WIN32
#include <random>
#include <vector>
#include <cmath>
#endif

#include <libv/lma/lma.hpp>
#include <eigen3/Eigen/Dense>
#include <boost/math/special_functions/sinc.hpp>
#include <sophus/se3.hpp>
#include <slam/se3_spline.h>
#include <slam/readsensor.h>
#include <slam/constraint.h>


typedef SE3Group<double> SE3Type;
#define MINMUM_DATA_NUM 10

namespace v
{
    Eigen::Matrix3d rotation_exp(const Eigen::Matrix3d &a)
    {
      double theta2 = a(0,1)*a(0,1) + a(0,2)*a(0,2) +  a(1,2)*a(1,2) + std::numeric_limits<double>::epsilon();
      double theta = std::sqrt(theta2);
      return Eigen::Matrix3d::Identity() + boost::math::sinc_pi(theta)*a+(1.0-std::cos(theta))/theta2*a*a;
     }
        
     void apply_rotation(Eigen::Matrix3d &rotation, const Eigen::Vector3d &d)
     {
       Eigen::Matrix3d skrew;
       skrew << 0, -d.z(), d.y(), d.z(), 0, -d.x(), -d.y(), d.x(), 0;
       rotation *= rotation_exp(skrew);
      }

     void apply_small_rotation(Eigen::Matrix3d &rotation, const Eigen::Vector3d &d)
     {
       Eigen::Matrix3d skrew;
       skrew << 0, -d.z(), d.y(), d.z(), 0, -d.x(), -d.y(), d.x(), 0;
       rotation *= rotation_exp(skrew);
     }
}

namespace lma
{
    template<> struct Size<Pose> {enum {value = SE3Type::num_parameters-1};};

    template<int I> void apply_small_increment(Pose &obj, double d, v::numeric_tag<I>, const Adl&){
        if (I < 3){
            Eigen::Matrix3d R = obj.rotationMatrix(); 

            v::apply_small_rotation(R, Eigen::Vector3d(I==0?d:0.0, I==1?d:0.0, I==2?d:0.0));

            obj.data[0] = R(2, 1); 
            obj.data[1] = R(0, 2);
            obj.data[2] = R(1, 0);
        }
        else{
            obj.data[I] += d;
        }
    } 

    void apply_increment(Pose &obj, const double increment[SE3Type::num_parameters-1], const Adl&){
        Eigen::Matrix3d R = obj.rotationMatrix();

        v::apply_rotation(R, Eigen::Vector3d(increment[0], increment[1], increment[2]));
        obj.data[0] = R(2, 1); 
        obj.data[1] = R(0, 2);
        obj.data[2] = R(1, 0);

        for (size_t i=3; i<6;  ++i){
            obj.data[i] += increment[i]; 
        }

    }
}

int main(int argc , char** argv)
{
    ros::init(argc, argv, "main");
    std::vector<gps_data> gps;
    std::vector<imu_data> imu;

    ros::Duration duration(10.0);
//    getData(duration, gps, imu);

    std::default_random_engine generator;
    std::uniform_real_distribution<double> distrib_radius(2-0.1,2+0.1 );
    std::uniform_real_distribution<double> distrib_angle(0., 0.2*M_PI);

    for (size_t i=0; i<50; ++i)
    {
        double radius = distrib_radius(generator);
        double angle = (0.2*i)*M_PI + distrib_angle(generator);
        gps_data temp(4+radius*cos(angle), 2+radius*sin(angle), i*0.2);
        gps.push_back(temp);
    }

/*
    int i = 0;
    while (gps.size()>0 && imu.size()>0){
       printf("gps TIME: %f\nx: %f, y: %f, z: %f\nimu TIME: %f\nvx: %f, vy: %f, vz: %f\nax:%f, ay: %f, az: %f", 
               gps[i].time, gps[i].x, gps[i].y, gps[i].z, 
               imu[i].time, imu[i].ang_vel_x, imu[i].ang_vel_y, imu[i].ang_vel_z, imu[i].acc_x, imu[i].acc_y, imu[i].acc_z);

       gps.pop_back();
       imu.pop_back();
    }
*/


    // Initialize spline
    size_t num_knots = 20; 
    double offset = gps[0].time;
    double dt = 0.5;

    SE3Type identity;
    std::vector<Pose> parameter_blocks;
    for (size_t i=0; i < num_knots; ++i){
        Eigen::Matrix<double, 6, 1> vec;
        vec <<1.0, 1.0, 1.0, gps[0].x, gps[0].y, gps[0].z;
        Pose p = Pose(vec);
        parameter_blocks.push_back(p);
    }

    // Initialize the solver
    double lambda = 0.001;
    double iteration_max = 4;
    lma::Solver<GPSConstraint> solver(lambda, iteration_max);
  
    if (gps.size() < MINMUM_DATA_NUM){
        std::cout << "Not enough data for optimization!" <<std::endl;
        return 1;
    }

    // Add constraint
    for(size_t i = 0 ; i < gps.size() ; ++i){
        int i0 = checkInterval(num_knots, offset, dt, gps[i].time);
        if (i0 == -1)
            continue;
        else{
            solver.add(GPSConstraint(gps[i], dt, offset+i0*dt, 1.0),&parameter_blocks[i0-1], &parameter_blocks[i0], &parameter_blocks[i0+1], &parameter_blocks[i0+2] ); 
        }
       
    }
/* 
    for(size_t i = 0 ; i < imu.size() ; ++i){
        int i0 = checkInterval(num_knots, offset, dt, imu[i].time);
        if (i0 == -1)
            continue;
        else{
            solver.add(IMUConstraint(imu[i], dt, offset+i0*dt, 1.0),&parameter_blocks[i0-1], &parameter_blocks[i0], &parameter_blocks[i0+1], &parameter_blocks[i0+2] ); 
        } 
    }

*/
    solver.solve(lma::DENSE,lma::enable_verbose_output());

    // Output info
    /*
    for (size_t i = 0; i < parameter_blocks.size(); ++i){
        std::cout <<parameter_blocks[i].data[4] <<
                    parameter_blocks[i].data[5] <<
                    parameter_blocks[i].data[6] << endl;
    }
    */
    return 0;
}