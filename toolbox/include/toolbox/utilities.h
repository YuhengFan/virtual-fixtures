/**
 * @file   utilities.h
 * @brief  Utility functions and classes.
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

#ifndef UTILITIES_H
#define UTILITIES_H

////////// STD
#include <iostream>
#include <fstream>
#include <atomic>

////////// Eigen
#include <eigen3/Eigen/Core>

////////// BOOST
#include <boost/bind.hpp>
#include <boost/thread.hpp>

////////// YAML-CPP
#include <yaml-cpp/yaml.h>


/// YAML backport to use the old operator >>
template <typename _T >
void operator >>(const YAML::Node& input, _T& value) {
    value = input.as<_T>();
}
template <typename _T >
void operator >> (const YAML::Node &node, std::vector<_T> & v)
{
    for(unsigned i = 0; i < node.size(); i++){
        v.push_back(node[i].as<_T>());
    }
}

namespace tool_box
{

template <class T>
class SharedData: public T
{
    typedef boost::recursive_mutex mutex_t;
public:
    SharedData():obj_()
    {
    }
    inline void WriteTryLock(T& obj)
    {
        boost::unique_lock<mutex_t> guard(mtx_, boost::defer_lock);
        if(mtx_.try_lock())
            obj_ = obj;
    }
    inline T& ReadTryLock()
    {
        boost::unique_lock<mutex_t> guard(mtx_, boost::defer_lock);
        if(guard.try_lock())
            return obj_;
    }
    inline void WriteLock(T& obj)
    {
        boost::unique_lock<mutex_t> guard(mtx_, boost::defer_lock);
        mtx_.lock();
        obj_ = obj;
        mtx_.unlock();
    }
    inline T& ReadLock()
    {
        boost::unique_lock<mutex_t> guard(mtx_, boost::defer_lock);
        guard.lock();
        return obj_;
    }
private:
    mutex_t mtx_;
    T obj_;
};

class AsyncThread
{
    typedef boost::function<void ()> funct_t;
    public:
        AsyncThread()
        {
            trigger_ = false;
            stop_loop_ = false;
            loop_ = boost::thread(boost::bind(&AsyncThread::Loop, this));
        }
        ~AsyncThread()
        {
            stop_loop_ = true;
            callback_.join();
            loop_.join();
        }
        inline void AddHandler(funct_t f)
        {
            if(trigger_ == true)
                std::cerr<< "Service thread already busy." << std::endl;
            else
                f_ = f;
        }
        inline void Trigger()
        {
            trigger_ = true;
        }
        inline void Loop()
        {
            while(!stop_loop_)
            {
                if(trigger_)
                {
                    callback_ =  boost::thread(f_);
                    callback_.join();
                    trigger_ = false;
                }
                boost::this_thread::sleep(boost::posix_time::milliseconds(100));
            }
            return;
        }

    private:
        funct_t f_;
        std::atomic<bool> trigger_;
        std::atomic<bool> stop_loop_;
        boost::thread loop_;
        boost::thread callback_;
};


/*class ThreadsPool
{
    typedef boost::function<void ()> funct_t;
    public:
        ThreadsPool(int n_threads)
        {
            for(int i = 0; i<n_threads; i++)
                pool_.push_back(new AsyncThread());
        }
        ~ThreadsPool()
        {
            for(int i = 0; i<pool_.size(); i++)
                delete pool_[i];
            //polling_thread_.join();
        }

        inline void DoAsyncWork(funct_t f) //Don't wait
        {
           //polling_thread_ = boost::thread(boost::bind(&ThreadsPool::AssignWork, this, f));
           boost::thread(boost::bind(&ThreadsPool::AssignWork, this, f));
        }

        inline void DoSyncWork(funct_t f) //Wait
        {
           //polling_thread_ = boost::thread(boost::bind(&ThreadsPool::AssignWork, this, f));
           //polling_thread_.join();
           //boost::thread thread = boost::thread(boost::bind(&ThreadsPool::AssignWork, this, f));
           //thread.join();
           AssignWork(f);
        }

    private:
        inline void AssignWork(funct_t f)
        {
            int i = 0;
            do
            {
                if(pool_[i]->busy_)
                {
                    i++;
                    boost::this_thread::sleep(boost::posix_time::milliseconds(100)); // Dodo for a little
                }
                else
                {
                    pool_[i]->AddHandler(f);
                    pool_[i]->Trigger();
                    pool_[i]->Wait();
                    break;
                }
                if(i == pool_.size())
                    i = 0;
            }
            while(true);

        }

        std::vector<AsyncThread* > pool_;
        //boost::thread polling_thread_;
};*/

/// Eigen containers manipulation
inline void Delete(const int idx, Eigen::VectorXd& vect)
{
    int n = vect.size()-idx-1;
    vect.segment(idx,n) = vect.tail(n);
    vect.conservativeResize(vect.size()-1);
}

inline void PushBack(const double value, Eigen::VectorXd& vect)
{
    int n = vect.size();
    vect.conservativeResize(n+1,Eigen::NoChange);
    vect(n) = value;
}

inline void PushBack(const Eigen::ArrayXd vect, Eigen::MatrixXd& mat)
{
    assert(vect.size() == mat.cols());
    int n = mat.rows();
    mat.conservativeResize(n+1,Eigen::NoChange);
    mat.row(n) = vect;
}

inline bool CropData(Eigen::MatrixXd& data, const double dt = 0.1, const double dist_min = 0.01)
{
    Eigen::MatrixXd data_tmp = data;
    data.resize(0,data_tmp.cols());

    for(int i = 0; i < data_tmp.rows()-1; i++)
        if((data_tmp.row(i+1) - data_tmp.row(i)).norm() > dt*dist_min)
        {
            PushBack(data_tmp.row(i),data);
        }

    if(data.rows() == 0)
    {
        std::cerr << "Data is empty, did you move the robot?" << std::endl;
        return false;
    }
    else
        return true;
}

/// Text file io
template<typename Scalar, int RowsAtCompileTime, int ColsAtCompileTime>
inline void ReadTxtFile(const std::string filename, Eigen::Matrix<Scalar,RowsAtCompileTime,ColsAtCompileTime>& m)
{

  // General structure
  // 1. Read file contents into vector<double> and count number of lines
  // 2. Initialize matrix
  // 3. Put data in vector<double> into matrix

  std::ifstream input(filename.c_str());
  if (input.fail())
  {
    std::cerr << "ERROR. Cannot find file '" << filename << "'." << std::endl;
    m = Eigen::Matrix<Scalar,RowsAtCompileTime,ColsAtCompileTime>(0,0);
  }
  std::string line;
  Scalar d;

  std::vector<Scalar> v;
  int n_rows = 0;
  while (getline(input, line))
  {
    ++n_rows;
    std::stringstream input_line(line);
    while (!input_line.eof())
    {
      input_line >> d;
      v.push_back(d);
    }
  }
  input.close();

  int n_cols = v.size()/n_rows;
  m = Eigen::Matrix<Scalar,RowsAtCompileTime,ColsAtCompileTime>(n_rows,n_cols);

  for (int i=0; i<n_rows; i++)
    for (int j=0; j<n_cols; j++)
      m(i,j) = v[i*n_cols + j];

}

template<typename value_t>
void ReadTxtFile(const std::string filename,std::vector<std::vector<value_t> >& values ) {
    std::string line;
    values.clear();
    std::ifstream myfile (filename.c_str());
    std::istringstream iss;
    std::size_t i=0;
    std::size_t nb_vals=0;
    if (myfile.is_open())
    {
        while (getline(myfile,line)) {
            values.push_back(std::vector<value_t>());;
            std::vector<value_t>& v = values[i];
            iss.clear();
            iss.str(line);
            std::copy(std::istream_iterator<value_t>(iss),std::istream_iterator<value_t>(), std::back_inserter(v));
            nb_vals+=v.size();
            i++;
        }
    std::cout << "File ["<<filename<<"] read with success  ["<<nb_vals<<" values, "<<i<<" lines] "<<std::endl;
    }
    else{
     std::cerr << "Unable to open file : ["<<filename<<"]"<<std::endl;
    }
    myfile.close();
}

template<typename value_t>
void WriteTxtFile(const std::string filename, std::vector<value_t>& values) {
    std::ofstream myfile (filename.c_str());
    std::size_t row = 0;
    std::size_t nb_rows = values.size();
    if (myfile.is_open())
    {
        while(row < nb_rows) {
        myfile << values[row] << "\n";
            row++;
        }
    std::cout << "File ["<<filename<<"] write with success  ["<<nb_rows<<" rows ] "<<std::endl;
    }
    else{
     std::cerr << "Unable to open file : ["<<filename<<"]"<<std::endl;
    }
    myfile.close();
}

inline void WriteTxtFile(const std::string filename, Eigen::VectorXd& values) {
    std::ofstream myfile (filename.c_str());
    std::size_t row = 0;
    std::size_t nb_rows = values.size();
    if (myfile.is_open())
    {
        while(row < nb_rows) {
        myfile << values(row) << "\n";
            row++;
        }
    std::cout << "File ["<<filename<<"] write with success  ["<<nb_rows<<" rows ] "<<std::endl;
    }
    else{
     std::cout << "Unable to open file : ["<<filename<<"]"<<std::endl;
    }
    myfile.close();
}

template<typename value_t>
void WriteTxtFile(const std::string filename, std::vector<std::vector<value_t> >& values ) {
    std::ofstream myfile (filename.c_str());
    std::size_t row = 0;
    std::size_t nb_rows = values.size();
    std::size_t col = 0;
    std::size_t nb_cols = values[0].size();

    if (myfile.is_open())
    {
        while(row < nb_rows) {
            while(col < nb_cols) {
                myfile << values[row][col] << " ";
                col++;
            }
            col = 0;
            row++;
            myfile << "\n";
        }
    std::cout << "File ["<<filename<<"] write with success  ["<<nb_rows<<" rows ]["<<nb_cols<<" cols ] "<<std::endl;
    }
    else{
     std::cerr << "Unable to open file : ["<<filename<<"]"<<std::endl;
    }
    myfile.close();
}

inline void WriteTxtFile(const std::string filename, Eigen::MatrixXd& values ) {
    std::ofstream myfile (filename.c_str());
    std::size_t row = 0;
    std::size_t nb_rows = values.rows();
    std::size_t col = 0;
    std::size_t nb_cols = values.cols();

    if (myfile.is_open())
    {
        while(row < nb_rows) {
            while(col < nb_cols) {
                myfile << values(row,col) << " ";
                col++;
            }
            col = 0;
            row++;
            myfile << "\n";
        }
    std::cout << "File ["<<filename<<"] write with success  ["<<nb_rows<<" rows ]["<<nb_cols<<" cols ] "<<std::endl;
    }
    else{
     std::cerr << "Unable to open file : ["<<filename<<"]"<<std::endl;
    }
    myfile.close();
}

} // namespace

#endif
