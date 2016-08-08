#include "mechanism_manager/mechanism_manager_interface.h"
#include "mechanism_manager/mechanism_manager_server.h"
#include "mechanism_manager/mechanism_manager.h"

namespace mechanism_manager
{

  using namespace tool_box;
  using namespace Eigen;

VirtualMechanismAutom::VirtualMechanismAutom(const double phase_dot_preauto_th, const double phase_dot_th)
{
    assert(phase_dot_th > 0.0);
    assert(phase_dot_preauto_th > phase_dot_th);
    phase_dot_preauto_th_ = phase_dot_preauto_th;
    phase_dot_th_ = phase_dot_th;
    state_ = MANUAL;
    loopCnt = 0;
}

void VirtualMechanismAutom::Step(const double phase_dot,const double phase_dot_ref, bool collision_detected)
{
    if(true)
    {
        switch(state_)
        {
            case MANUAL:
                if(phase_dot >= phase_dot_preauto_th_)
                    state_ = PREAUTO;
                break;
            case PREAUTO:
                if(phase_dot <= (phase_dot_ref + phase_dot_th_))
                    state_ = AUTO;
                break;
            case AUTO:
                if(collision_detected)
                    state_ = MANUAL;
                break;
        }
    }
    else // Two states version
    {
            if((phase_dot <= (phase_dot_ref + phase_dot_th_)) && (phase_dot >= (phase_dot_ref - phase_dot_th_)))
                state_ = AUTO;
            else
                state_ = MANUAL;
    }
}

bool VirtualMechanismAutom::GetState()
{
    bool activate_vm;
    switch(state_)
    {
        case MANUAL:
            activate_vm = false;
            break;
        case PREAUTO:
            activate_vm = false;
            break;
        case AUTO:
            activate_vm = true;
            break;
    }

    if(loopCnt%1000==0)
    {
        switch(state_)
        {
            case MANUAL:
                std::cout << "****" <<std::endl;
                std::cout << "MANUAL" <<std::endl;
                break;
            case PREAUTO:
                std::cout << "****" <<std::endl;
                std::cout << "PREAUTO" <<std::endl;
                break;
            case AUTO:
                std::cout << "****" <<std::endl;
                std::cout << "AUTO" <<std::endl;
                break;
        }
    }
    loopCnt++;

    return activate_vm;
}

MechanismManagerInterface::MechanismManagerInterface(): mm_(NULL), mm_server_(NULL)
{
      //Eigen::initParallel();

      // For now the services are not so many, but in the future I should create a pool of limited workers to
      // handle the rpcs
      async_thread_insert_ = new AsyncThread();
      async_thread_delete_ = new AsyncThread();
      async_thread_save_   = new AsyncThread();

      if(!ReadConfig())
      {
        throw new std::runtime_error("MechanismManagerInterface: Can not read config file");
      }

      // Resize
      tmp_eigen_vector_.resize(position_dim_);
      robot_position_.resize(position_dim_);
      robot_velocity_.resize(position_dim_);
      f_.resize(position_dim_);

      // Clear
      tmp_eigen_vector_.fill(0.0);
      robot_position_.fill(0.0);
      robot_velocity_.fill(0.0);
      f_.fill(0.0);

      //loopCnt = 0;

      collision_detected_ = true; // Let's start not active

      mm_ = new MechanismManager(position_dim_);

      try
      {
          ros_node_.Init(ROS_PKG_NAME);
          mm_server_ = new MechanismManagerServer(this,ros_node_.GetNode());
      }
      catch(const std::runtime_error& e)
      {
         ROS_ERROR("Failed to create the MechanismManagerServer: %s",e.what());
      }
}

MechanismManagerInterface::~MechanismManagerInterface()
{
      for(int i=0;i<vm_autom_.size();i++)
        delete vm_autom_[i];

      delete async_thread_insert_;
      delete async_thread_delete_;
      delete async_thread_save_;

      if(mm_server_!=NULL)
        delete mm_server_;

      delete mm_;
}

bool MechanismManagerInterface::ReadConfig()
{
    YAML::Node main_node = CreateYamlNodeFromPkgName(ROS_PKG_NAME);
    if (const YAML::Node& curr_node = main_node["mechanism_manager_interface"])
    {
        curr_node["position_dim"] >> position_dim_;
        curr_node["phase_dot_th"] >> phase_dot_th_;
        curr_node["phase_dot_preauto_th"] >> phase_dot_preauto_th_;
        assert(position_dim_ == 1 || position_dim_ == 2);
        assert(phase_dot_th_ > 0.0);
        assert(phase_dot_preauto_th_ > phase_dot_th_);

        return true;
    }
    else
        return false;
}

void MechanismManagerInterface::InsertVM()
{
    async_thread_insert_->AddHandler(boost::bind(&MechanismManager::InsertVM, mm_));
    async_thread_insert_->Trigger();
}

void MechanismManagerInterface::InsertVM(MatrixXd& data)
{
    async_thread_insert_->AddHandler(boost::bind(static_cast<void (MechanismManager::*)(const MatrixXd&)>(&MechanismManager::InsertVM), mm_, data));
    async_thread_insert_->Trigger();
}

void MechanismManagerInterface::InsertVM(std::string& model_name)
{
    async_thread_insert_->AddHandler(boost::bind(static_cast<void (MechanismManager::*)(std::string&)>(&MechanismManager::InsertVM), mm_, model_name));
    async_thread_insert_->Trigger();
}

void MechanismManagerInterface::SaveVM(const int idx)
{
    async_thread_save_->AddHandler(boost::bind(&MechanismManager::SaveVM, mm_, idx));
    async_thread_save_->Trigger();
}

void MechanismManagerInterface::DeleteVM(const int idx)
{
    async_thread_delete_->AddHandler(boost::bind(&MechanismManager::DeleteVM, mm_, idx));
    async_thread_delete_->Trigger();
}

void MechanismManagerInterface::Update(const double* robot_position_ptr, const double* robot_velocity_ptr, double dt, double* f_out_ptr, const prob_mode_t prob_mode)
{
    assert(dt > 0.0);

    robot_position_ = VectorXd::Map(robot_position_ptr, position_dim_);
    robot_velocity_ = VectorXd::Map(robot_velocity_ptr, position_dim_);

    //if(use_active_guide_)
    //    CheckForGuideActivation(i);

    mm_->Update(robot_position_,robot_velocity_,dt,f_,prob_mode);

    VectorXd::Map(f_out_ptr, position_dim_) = f_;
}

void MechanismManagerInterface::Update(const VectorXd& robot_position, const VectorXd& robot_velocity, double dt, VectorXd& f_out, const prob_mode_t prob_mode)
{
    assert(dt > 0.0);

    assert(robot_position.size() == position_dim_);
    assert(robot_velocity.size() == position_dim_);
    assert(f_out.size() == position_dim_);
    robot_position_ = robot_position;
    robot_velocity_ = robot_velocity;

    //if(use_active_guide_)
    //    CheckForGuideActivation(i);

    mm_->Update(robot_position_,robot_velocity_,dt,f_,prob_mode);

    f_out = f_;
}

/*void MechanismManagerInterface::CheckForGuideActivation(const int idx)
{
    //const double r = vm_vector_[idx]->getR();
    const double phase_dot = vm_vector_[idx]->getPhaseDot();
    const double phase_dot_ref = vm_vector_[idx]->getPhaseDotRef();
    vm_autom_[idx]->Step(phase_dot,phase_dot_ref,collision_detected_);
    if(vm_autom_[idx]->GetState())
        vm_vector_[idx]->setActive(true);
    else
        vm_vector_[idx]->setActive(false);

    //r_(idx) = r;
}*/

void MechanismManagerInterface::Stop()
{
    mm_->Stop();
}

void MechanismManagerInterface::GetVmPosition(const int idx, double* const position_ptr)
{
    tmp_eigen_vector_ = VectorXd::Map(position_ptr, position_dim_);
    mm_->GetVmPosition(idx,tmp_eigen_vector_);
    VectorXd::Map(position_ptr, position_dim_) = tmp_eigen_vector_;
}

void MechanismManagerInterface::GetVmVelocity(const int idx, double* const velocity_ptr)
{
    tmp_eigen_vector_ = VectorXd::Map(velocity_ptr, position_dim_);
    mm_->GetVmVelocity(idx,tmp_eigen_vector_);
    VectorXd::Map(velocity_ptr, position_dim_) = tmp_eigen_vector_;
}

void MechanismManagerInterface::GetVmPosition(const int idx, Eigen::VectorXd& position)
{
    mm_->GetVmPosition(idx,position);
}

void MechanismManagerInterface::GetVmVelocity(const int idx, Eigen::VectorXd& velocity)
{
    mm_->GetVmVelocity(idx,velocity);
}

double MechanismManagerInterface::GetPhase(const int idx)
{
    return mm_->GetPhase(idx);
}
double MechanismManagerInterface::GetScale(const int idx)
{
    return mm_->GetScale(idx);
}

int MechanismManagerInterface::GetNbVms()
{
    return mm_->GetNbVms();
}

bool MechanismManagerInterface::OnVm()
{
    return mm_->OnVm();
}

} // namespace
