/**
 * @file   ros.h
 * @brief  ROS related utilities.
 * @author Gennaro Raiola
 *
 * This file is part of virtual-fixtures, a set of libraries and programs to create
 * and interact with a library of virtual guides.
 * Copyright (C) 2014-2016 Gennaro Raiola, ENSTA-ParisTech
 *
 * virtual-fixtures is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * virtual-fixtures is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with virtual-fixtures.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ROS_H
#define ROS_H

////////// STD
#include <iterator>

////////// Eigen
#include <eigen3/Eigen/Core>

////////// ROS
#include <ros/ros.h>
#include <ros/package.h>
#ifdef USE_ROS_RT_PUBLISHER
  #include <std_msgs/Float64.h>
  #include <std_msgs/Float64MultiArray.h>
  #include <realtime_tools/realtime_publisher.h>
#endif

////////// YAML-CPP
#include <yaml-cpp/yaml.h>

namespace tool_box
{

inline std::string GetYamlFilePath(std::string pkg_name)
{
    return ros::package::getPath(pkg_name) + "/config/cfg.yml"; // FIXME folder and file name are hardcoded
}

inline YAML::Node CreateYamlNodeFromPkgName(std::string pkg_name)
{
    std::string file_path = GetYamlFilePath(pkg_name);
    YAML::Node node;
    try
    {
        node = YAML::LoadFile(file_path);
    }
    catch(const std::runtime_error& e)
    {
        ROS_ERROR("Failed to create YAML Node, reason: %s",e.what());
    }
    return node;
}

class RosNode
{
    public:
        RosNode(std::string ros_node_name)
        {
          Init(ros_node_name);
        }

        RosNode()
        {
          init_ = false;
          ros_nh_ptr_ = NULL;
        }

        void Init(std::string ros_node_name)
        {
          int argc = 1;
          char* arg0 = strdup(ros_node_name.c_str());
          char* argv[] = {arg0, 0};
          ros::init(argc, argv, ros_node_name,ros::init_options::NoSigintHandler);
          free (arg0);
          if(ros::master::check())
          {
              ros_nh_ptr_ = new ros::NodeHandle(ros_node_name);
          }
          else
          {
              std::string err("roscore not found... Did you start the server?");
              throw std::runtime_error(err);
          }
          init_ = true;
        }

        ~RosNode(){if(ros_nh_ptr_!=NULL && init_ == true){ros_nh_ptr_->shutdown(); delete ros_nh_ptr_;}}

        ros::NodeHandle& GetNode()
        { 
          if(init_ == true)
            return *ros_nh_ptr_;
          else
          {
            std::string err("RosNode not initialized");
            throw std::runtime_error(err);
          }
        }

        bool Reset()
        {
          if(init_ == true)
          {
            ros_nh_ptr_->shutdown();
            delete ros_nh_ptr_;
            ros_nh_ptr_ = NULL;
            init_ = false;
            return true;
          }
          else
          {
            std::string err("RosNode not initialized");
            throw std::runtime_error(err);
            return false;
          }
        }

        bool InitDone()
        {
            return init_;
        }

    protected:
        ros::NodeHandle* ros_nh_ptr_;
        bool init_;
};


#ifdef USE_ROS_RT_PUBLISHER

template <typename T>
class RealTimePublisherVector
{
    public:

        /** Initialize the real time publisher. */
        RealTimePublisherVector(const ros::NodeHandle& ros_nh, const std::string topic_name)
        {
            // Checks
            assert(topic_name.size() > 0);
            topic_name_ = topic_name;
            pub_ptr_.reset(new rt_publisher_t(ros_nh,topic_name,10));

        }
        /** Publish the topic. */
        //inline void Publish(const Eigen::Ref<const Eigen::VectorXd>& in)
        inline void Publish(const T& in)
        {
            if(pub_ptr_ && pub_ptr_->trylock())
            {
                //pub_ptr_->msg_.header.stamp = ros::Time::now();
                int data_size = pub_ptr_->msg_.data.size();
                //assert(data_size >= in.size());
                for(int i = 0; i < data_size; i++)
                {
                    pub_ptr_->msg_.data[i] = in(i);
                }
                pub_ptr_->unlockAndPublish();
            }
        }

        /** Remove an element in the vector. */
        inline void Remove(const int idx)
        {
            if(pub_ptr_)
            {
                pub_ptr_->lock();
                pub_ptr_->msg_.data.erase(pub_ptr_->msg_.data.begin()+idx);
                pub_ptr_->unlock();
            }
        }

        /** Resize the vector. */
        inline void Resize(const int dim)
        {
            if(pub_ptr_)
            {
                pub_ptr_->lock();
                pub_ptr_->msg_.data.resize(dim);
                pub_ptr_->unlock();
            }
        }

        /** Add a new element at the back. */
        inline void PushBackEmpty()
        {
            if(pub_ptr_)
            {
                pub_ptr_->lock();
                pub_ptr_->msg_.data.push_back(0.0);
                pub_ptr_->unlock();
            }
        }

        inline std::string getTopic(){return topic_name_;}

    private:
        typedef realtime_tools::RealtimePublisher<std_msgs::Float64MultiArray> rt_publisher_t;
        std::string topic_name_;
        boost::shared_ptr<rt_publisher_t > pub_ptr_;
};


class RealTimePublisherScalar
{
    public:

        /** Initialize the real time publisher. */
        RealTimePublisherScalar(const ros::NodeHandle& ros_nh, const std::string topic_name)
        {
            // Checks
            assert(topic_name.size() > 0);
            topic_name_ = topic_name;
            pub_ptr_.reset(new rt_publisher_t(ros_nh,topic_name,10));

        }
        /** Publish the topic. */
        inline void Publish(const double& in)
        {
            if(pub_ptr_ && pub_ptr_->trylock())
            {
                pub_ptr_->msg_.data = in;
                pub_ptr_->unlockAndPublish();
            }
        }

        inline std::string getTopic(){return topic_name_;}

    private:
        typedef realtime_tools::RealtimePublisher<std_msgs::Float64> rt_publisher_t;
        std::string topic_name_;
        boost::shared_ptr<rt_publisher_t > pub_ptr_;
};

template <class RealTimePublisher_t>
class RealTimePublishers
{
    public:

        // Add a RealTimePublisher already created
        void AddPublisher(boost::shared_ptr<RealTimePublisher_t> pub_ptr, double* data_ptr)
        {
            assert(pub_ptr!=false);
            // Put it into the map with his friends
            map_[pub_ptr->getTopic()] = std::make_pair(data_ptr,pub_ptr);
        }

        // Add a new fresh RealTimePublisher
        void AddPublisher(const ros::NodeHandle& ros_nh, const std::string topic_name, double* data_ptr)
        {
            boost::shared_ptr<RealTimePublisher_t> pub_ptr = boost::make_shared<RealTimePublisher_t>(ros_nh,topic_name);
            assert(pub_ptr!=false);
            // Put it into the map with his friends
            map_[pub_ptr->getTopic()] = std::make_pair(data_ptr,pub_ptr);
        }

        // Publish!
        void PublishAll()
        {
            for(pubs_map_it_t iterator = map_.begin(); iterator != map_.end(); iterator++)
                iterator->second.second->Publish(*iterator->second.first);
        }
 /// Code used before when it was needed to change dynamically the size of the vectors to publish
/*
        // Resize a specific publisher
        void Resize(const int dim, const std::string topic_name)
        {
            map_[topic_name].second->Resize(dim);
        }

        // Remove an element from all the publishers
        void RemoveAll(const int idx)
        {
            for(pubs_map_it_t iterator = map_.begin(); iterator != map_.end(); iterator++)
                iterator->second.second->Remove(idx);
        }

        // Resize all the publishers
        void ResizeAll(const int dim)
        {
            for(pubs_map_it_t iterator = map_.begin(); iterator != map_.end(); iterator++)
                iterator->second.second->Resize(dim);
        }

        // Push back an empty value in all the publishers
        void PushBackEmptyAll()
        {
            for(pubs_map_it_t iterator = map_.begin(); iterator != map_.end(); iterator++)
                iterator->second.second->PushBackEmpty();
        }
*/

    private:

        typedef std::map<std::string,std::pair<double*,boost::shared_ptr<RealTimePublisher_t > > > pubs_map_t;
        typedef typename pubs_map_t::iterator pubs_map_it_t;

        pubs_map_t map_;
};

#endif // RT PUBLISHERS

} // namespace

#endif
