#include "mechanism_manager/mechanism_manager.h"
#include "../../../../install_isolated/include/mechanism_manager/mechanism_manager.h"

namespace mechanism_manager
{
  
  using namespace virtual_mechanism_gmr;
  using namespace DmpBbo;
  using namespace tool_box;
  using namespace Eigen;
  

bool MechanismManager::ReadConfig(std::string file_path) // FIXME Switch to ros param server
{
	YAML::Node main_node;
	
	try
	{
	    main_node = YAML::LoadFile(file_path);
	}
	catch(...)
	{
	    return false;
	}
	
	// Retrain the parameters from yaml file
	std::vector<std::string> model_names;
	std::string models_path(pkg_path_+"/models/");
	std::string prob_mode_string;
	
	main_node["models"] >> model_names;
	main_node["prob_mode"] >> prob_mode_string;
	main_node["use_weighted_dist"] >> use_weighted_dist_;
	main_node["use_active_guide"] >> use_active_guide_;
	
	if (prob_mode_string == "hard")
	    prob_mode_ = HARD;
	else if (prob_mode_string == "potential")
	    prob_mode_ = POTENTIAL;
	else if (prob_mode_string == "soft")
	    prob_mode_ = SOFT;
	else
	    prob_mode_ = POTENTIAL; // Default
	
	// Create the virtual mechanisms starting from the GMM models
 	for(int i=0;i<model_names.size();i++)
	{
	    std::vector<std::vector<double> > data;
	    ReadTxtFile((models_path+model_names[i]).c_str(),data);
	    ModelParametersGMR* model_parameters_gmr = ModelParametersGMR::loadGMMFromMatrix(models_path+model_names[i]);
	    boost::shared_ptr<fa_t> fa_tmp_shr_ptr(new FunctionApproximatorGMR(model_parameters_gmr)); // Convert to shared pointer
	    vm_vector_.push_back(new vm_t(dim_,fa_tmp_shr_ptr)); 
	}
	return true;
}
  
MechanismManager::MechanismManager()
{
      dim_ = 3; // NOTE Cartesian dimension is fixed, xyz
      
      pkg_path_ = ros::package::getPath("mechanism_manager");	
      std::string config_file_path(pkg_path_+"/config/cfg.yml"); //FIXME load it from param server
      
      if(ReadConfig(config_file_path))
	  ROS_INFO("Loaded config file: %s",config_file_path.c_str());
      else
	  ROS_ERROR("Can not load config file: %s",config_file_path.c_str());
      
      // Number of virtual mechanisms
      vm_nb_ = vm_vector_.size();
      assert(vm_nb_ >= 1);
      
      // Initialize the virtual mechanisms and support vectors
      for(int i=0; i<vm_nb_;i++)
      {
         vm_vector_[i]->Init();
         vm_vector_[i]->setWeightedDist(use_weighted_dist_[i]);
         vm_state_.push_back(VectorXd(dim_));
         vm_state_dot_.push_back(VectorXd(dim_));
      }
      
      // Some Initializations
      active_guide_.resize(vm_nb_,false);
      scales_.resize(vm_nb_);
      phase_.resize(vm_nb_);
      robot_position_.resize(dim_);
      
      // Clear
      scales_ .fill(0.0);
      phase_.fill(0.0);
      robot_position_.fill(0.0);
      
      #ifdef USE_ROS_RT_PUBLISHER
      try
      {
	  ros_node_.Init("mechanism_manager");
	  rt_publishers_values_.AddPublisher(ros_node_.GetNode(),"phase",phase_.size(),&phase_);
	  rt_publishers_values_.AddPublisher(ros_node_.GetNode(),"scales",scales_.size(),&scales_);
	  //rt_publishers_pose_.AddPublisher(ros_node_.GetNode(),"tracking_reference",tracking_reference_.size(),&tracking_reference_);
	  rt_publishers_path_.AddPublisher(ros_node_.GetNode(),"robot_pos",robot_position_.size(),&robot_position_);
	  for(int i=0; i<vm_nb_;i++)
	  {
	    std::string topic_name = "vm_pos_" + std::to_string(i+1);
	    rt_publishers_path_.AddPublisher(ros_node_.GetNode(),topic_name,vm_state_[i].size(),&vm_state_[i]);
	    //topic_name = "vm_ker_" + std::to_string(i+1);
	    //boost::shared_ptr<RealTimePublisherMarkers> tmp_ptr = boost::make_shared<RealTimePublisherMarkers>(ros_node_.GetNode(),topic_name,root_name_);
	    //rt_publishers_markers_.AddPublisher(tmp_ptr,&vm_kernel_[i]);
	  } 
      }
      catch(const std::runtime_error& e)
      {
	   ROS_ERROR("Failed to create the real time publishers: %s",e.what());
      }
      #endif
      
      // Define the scale threshold to check when a guide is more "probable"
      scale_threshold_ = 1.0/static_cast<double>(vm_nb_) + 0.2;
}
  
MechanismManager::~MechanismManager()
{
      for(int i=0;i<vm_vector_.size();i++)
	  delete vm_vector_[i];
}

/*void MechanismManager::MoveForward()
{
    for(int i=0; i<vm_nb_;i++)
      vm_vector_[i].move_forward_ = true;
}

void MechanismManager::MoveBackward()
{
    for(int i=0; i<vm_nb_;i++)
      vm_vector_[i].move_forward_ = false;
}*/

void MechanismManager::Update(const VectorXd& robot_position, const VectorXd& robot_velocity, double dt, VectorXd& f_out, bool force_applied, bool move_forward)
{
    for(int i=0; i<vm_nb_;i++)
    {
      if(move_forward)
	vm_vector_[i]->moveForward();
      else
	vm_vector_[i]->moveBackward();
    }
    Update(robot_position,robot_velocity,dt,f_out,force_applied);
}

void MechanismManager::Update(const VectorXd& robot_position, const VectorXd& robot_velocity, double dt, VectorXd& f_out, bool force_applied)
{
    for(int i=0; i<vm_nb_;i++)
    {
      if (force_applied == false && scales_(i) > scale_threshold_ && use_active_guide_[i] == true)
      {
	      vm_vector_[i]->setActive(true);
	      active_guide_[i] = true;
	      //std::cout << "Active " <<i<< std::endl;
      }
      else
	  {
	      vm_vector_[i]->setActive(false);
	      active_guide_[i] = false;
	      //std::cout << "Deactive "<<i<< std::endl;
	  }
    }
    Update(robot_position,robot_velocity,dt,f_out);
}

void MechanismManager::Update(const VectorXd& robot_position, const VectorXd& robot_velocity, double dt, VectorXd& f_out)
{
	assert(robot_position.size() == dim_);
	assert(robot_velocity.size() == dim_);
	assert(dt > 0.0);
	assert(f_out.size() == dim_);
	
	robot_position_ = robot_position; // For plot pourpose
	
	// Update the virtual mechanisms states, compute single probabilities
	for(int i=0; i<vm_nb_;i++)
	{
	  vm_vector_[i]->Update(robot_position,robot_velocity,dt);
	  
	  switch(prob_mode_) 
	  {
	    case HARD:
	      scales_(i) = vm_vector_[i]->getProbability(robot_position);
	      break;
	    case POTENTIAL:
	      scales_(i) = std::exp(-10*vm_vector_[i]->getDistance(robot_position));
	      break;
	    case SOFT:
	      scales_(i) = vm_vector_[i]->getProbability(robot_position);
	      break;
	    default:
	      break;
	  }
	  
	  // Take the phase for each vm (For plots)
	  phase_(i) = vm_vector_[i]->getPhase();
	}
	
	sum_ = scales_.sum();
	f_out.fill(0.0); // Reset the force
	
	for(int i=0; i<vm_nb_;i++)
	{
	  // Compute the conditional probabilities
	  switch(prob_mode_) 
	  {
	    case HARD:
	      scales_(i) =  scales_(i)/sum_;
	      break;
	    case POTENTIAL:
	      scales_(i) = scales_(i);
	      break;
	    case SOFT:
	      scales_(i) = std::exp(-10*vm_vector_[i]->getDistance(robot_position)) * scales_(i)/sum_;
	      break;
	    default:
	      break;
	  }

	  // Compute the force from the vms
	  vm_vector_[i]->getState(vm_state_[i]);
	  vm_vector_[i]->getStateDot(vm_state_dot_[i]);
	  
	  //vm_vector_[i]->getLocalKernel(vm_kernel_[i]);
	  //K_ = vm_vector_[i]->getK();
	  //B_ = vm_vector_[i]->getB();
	  
	  
	  f_out += scales_(i) * (vm_vector_[i]->getK() * (vm_state_[i] - robot_position) + vm_vector_[i]->getB() * (vm_state_dot_[i] - robot_velocity)); // Sum over all the vms
	    
	}

	//UpdateTrackingReference(robot_position);
	
	#ifdef USE_ROS_RT_PUBLISHER
	rt_publishers_values_.PublishAll();
	rt_publishers_pose_.PublishAll();
	rt_publishers_path_.PublishAll();
	#endif
}
  
}
