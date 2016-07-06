
#include "pnp.h"

#include <iostream>
#include <random>
#include <algorithm>

#include <Eigen/Dense>

#include "p3pf/src/p3p_solver.h"
#include "epnp/epnp.h"


namespace pnp{

// calculate the max iterations according the pnp_method_ and ransac_parameters_
uint64_t CalculateMaxInters( const double prob_all_inliers,
                             const double threshold )
{
  const double log_threshold = std::log( threshold );
  uint64_t max_iters = 0;
  if ( prob_all_inliers < 1.0 ) {

    const double num_iters = log_threshold / std::log( 1.0 - prob_all_inliers );
    max_iters = static_cast<uint64_t>( std::floor( num_iters ) );
  }
  return max_iters;
}

PnPSolver::PnPSolver()
{
  ptr_P3Pf = nullptr;
  ptr_pnp = nullptr;
}

PnPSolver::~PnPSolver()
{
  if ( ptr_P3Pf ) delete ptr_P3Pf;
  if ( ptr_pnp ) delete ptr_pnp;
}

void PnPSolver::GetMethod( PnPMethod& method ) const
{
  method = pnp_method;
}


void PnPSolver::Init( const PnPParameters& Parameters )
{
  pnp_method = Parameters.pnp_method;

  if ( P3PF == Parameters.pnp_method )
  {
    ptr_P3Pf = new( p3pf::P3Pf );
    if ( ptr_P3Pf != nullptr ){
      ptr_P3Pf->Init( *( Parameters.ptr_p3pf_params ) );
    }
    else{
      std::cerr << "Error: new p3pf failed! pnp.cc line 37" << std::endl;
      return;
    }
  }
  else{
    ptr_pnp = new( pnp::PnP );
    if ( ptr_pnp != nullptr ){
      ptr_pnp->Init( *( Parameters.ptr_pnp_params ) );
    }
    else{
      std::cerr << "Error: new PnP failed! pnp.cc line 49" << std::endl;
      return;
    }
  }
}


void PnPSolver::ComputePose(
  const std::vector<Vector2d> &points2D,
  const std::vector<Vector3d> &points3D,
  PnPResult &result ) const
{
  if ( P3P == pnp_method && nullptr != ptr_pnp ){

  }
  else if ( P3PF == pnp_method && nullptr != ptr_P3Pf ){
    // p3pf ComputePose function accept a point to result
    ptr_P3Pf->ComputePose( points2D, points3D, &result );
  }
  else if ( EPNP == pnp_method && nullptr != ptr_pnp ){
    ptr_pnp->ComputePoseEpnp( points2D, points3D, result );
  }
  else if ( P6P == pnp_method && nullptr != ptr_pnp ){

  }
  else{ 
    std::cerr << "unknown pnp method or nullptr. pnp.cc line80" << std::endl;
  }
}


/*  member function for PnPParameters class  */
void PnPParameters::InitPnPMethod( const PnPMethod method )
{
  pnp_method = method;
  ptr_p3pf_params = nullptr;
  ptr_pnp_params = nullptr;

  if ( P3PF == pnp_method ){
    ptr_p3pf_params = new P3PfParameters;
    if ( nullptr == ptr_p3pf_params ){
      std::cerr << "Error: new p3pf_param failed. pnp.cc line 88" << std::endl;
    }
  }
  else{
    ptr_pnp_params = new GpnpParameters;
    if ( nullptr == ptr_pnp_params ){
      std::cerr << "Error: new epnp_param failed. pnp.cc line 94" << std::endl;
    }
    else { 
      ptr_pnp_params->pnp_method_ = pnp_method;
      double inlier_ratio = ptr_pnp_params->ransac_parameters_.min_inlier_ratio;
      if ( P3P == method ){ 
        ptr_pnp_params->ransac_parameters_.minimal_sample_number = 3;
      }
      else if ( EPNP == method ){ 
        ptr_pnp_params->ransac_parameters_.minimal_sample_number = 5;
      }
      else if ( P6P == method ){ 
        ptr_pnp_params->ransac_parameters_.minimal_sample_number = 6;
      }
      double prob_all_inliers = std::pow( inlier_ratio, 
        ptr_pnp_params->ransac_parameters_.minimal_sample_number );

      ptr_pnp_params->ransac_parameters_.max_ransac_iterations = 
      CalculateMaxInters( prob_all_inliers,
        ptr_pnp_params->ransac_parameters_.failure_probability);
    }
  }
}

PnPParameters::PnPParameters(){
  ptr_p3pf_params = nullptr;
  ptr_pnp_params = nullptr;
}

PnPParameters::~PnPParameters()
{
  if ( ptr_p3pf_params )  delete ptr_p3pf_params;
  if ( ptr_pnp_params )   delete ptr_pnp_params;
}




/*  member function for PnP class  */
void PnP::Init( const GpnpParameters& Parameters )
{
  pnp_params_ = Parameters;
}

void PnP::ComputePoseEpnp(
  const std::vector<Vector2d> &points2D,
  const std::vector<Vector3d> &points3D,
  PnPResult &result ) const
{
  int  num_correspondences = static_cast<int>(points2D.size());
  if ( num_correspondences <= 0 ) return;

  std::mt19937 rand_num_gen;
  if ( pnp_params_.ransac_parameters_.random_seed >= 0 ) {
    rand_num_gen.seed( pnp_params_.ransac_parameters_.random_seed );
  }

  std::uniform_int_distribution<int> uniform_distribution_matches(
    0, num_correspondences - 1 );

  epnp epnp_solver;
  const auto & K = pnp_params_.K_;
  //const double uc, const double vc, const double fu, const double fv
  epnp_solver.set_internal_parameters( K( 0, 2 ), K( 1, 2 ), K( 0, 0 ), K( 1, 1 ) );
  epnp_solver.set_maximum_number_of_correspondences( num_correspondences );

  result.num_inliers_ = 0;
  double epsilon_best = pnp_params_.ransac_parameters_.min_inlier_ratio;
  double R_est[3][3], T_est[3];
  
  int num_samples = pnp_params_.ransac_parameters_.minimal_sample_number;
  if ( pnp_params_.ransac_parameters_.use_T_1_1_test ){ 
    num_samples += 1;
  }

  // inlier mask
  std::vector<bool> vec_inliers(num_correspondences, false);
  uint64_t max_iters = pnp_params_.ransac_parameters_.max_ransac_iterations;
  std::vector<Vector2d> sample_points_2D( 5 );
  std::vector<Vector3d> sample_points_3D( 5 );
  Eigen::Matrix3d rotation_matrices;
  Eigen::Vector3d translation;
  CameraPose temp_pose;
  uint64_t t = 0;

  for (  t = 0; t < max_iters; ++t )
  {
    epnp_solver.reset_correspondences();
    // Randomly select five 2D-3D matches.
    int rand_num = 0;
    for ( int i = 0; i < 5; ++i ) {
      rand_num = uniform_distribution_matches( rand_num_gen );
      sample_points_2D[i] = points2D[rand_num];
      sample_points_3D[i] = points3D[rand_num];
      epnp_solver.add_correspondence(
        points3D[rand_num][0],
        points3D[rand_num][1],
        points3D[rand_num][2],
        points2D[rand_num][0],
        points2D[rand_num][1]);
    }
    
    epnp_solver.compute_pose( R_est, T_est );

    for ( int i = 0; i < 3; i++ ){
      for ( int j = 0; j < 3; j++ ){
        rotation_matrices( i, j ) = R_est[i][j];
      }
      translation[i] = T_est[i];
    }

    temp_pose.InitializePose( rotation_matrices, translation, K );

    // If the candidate solution passes the T{1,1} test then evaluate all
    // correspondences and update the best pose.
    rand_num = uniform_distribution_matches( rand_num_gen );
    if ( !pnp_params_.ransac_parameters_.use_T_1_1_test ||
      PassesT11Test( points2D[rand_num], points3D[rand_num], temp_pose ) )
    {
      // Evaluates the poses.
      const int num_inliers = EvaluatePose( points2D, points3D, temp_pose, vec_inliers);

      // Update best model
      if ( num_inliers > result.num_inliers_ ){
        result.pose_.InitializePose( temp_pose );
        result.num_inliers_ = num_inliers;
        result.sample_points_2D_ = sample_points_2D;
        result.sample_points_3D_ = sample_points_3D;

        double inlier_ratio = static_cast<double>( result.num_inliers_ ) /
          static_cast<double>( num_correspondences );

        epsilon_best = std::max( epsilon_best, inlier_ratio );

        // update the max iteration
        double prob_all_inliers = std::pow( epsilon_best, num_samples );
        max_iters = CalculateMaxInters( prob_all_inliers,
          pnp_params_.ransac_parameters_.failure_probability );
      }

    }
  }

  result.num_generated_random_samples_ = t;

  bool refine_pose = pnp_params_.refine_pose;
  // refine pose use all inliers
  while ( refine_pose )
  {
    epnp_solver.reset_correspondences();
    for ( int i = 0; i < num_correspondences; ++i ){ 
      if ( vec_inliers[i] ){ 
        epnp_solver.add_correspondence(
          points3D[i][0],
          points3D[i][1],
          points3D[i][2],
          points2D[i][0],
          points2D[i][1] );
      }
    }
    epnp_solver.compute_pose( R_est, T_est );

    for ( int i = 0; i < 3; i++ ){
      for ( int j = 0; j < 3; j++ ){
        rotation_matrices( i, j ) = R_est[i][j];
      }
      translation[i] = T_est[i];
    }

    temp_pose.InitializePose( rotation_matrices, translation, K );
    int num_inliers = EvaluatePose( points2D, points3D, temp_pose, vec_inliers );

    // Update best model
    if ( num_inliers > result.num_inliers_ ){
      result.pose_.InitializePose( temp_pose );
      result.num_inliers_ = num_inliers;
    }
    else break;
  }

}


bool PnP::PassesT11Test(
  const Vector2d& point2D,
  const Vector3d& point3D,
  const CameraPose& pose ) const
{
  const double squared_inlier_threshold =
    pnp_params_.ransac_parameters_.squared_inlier_threshold;
  const double squared_reproj_err =
    ComputeSquaredReprojectionError( point2D, point3D, pose );
  return squared_reproj_err < squared_inlier_threshold;
}


double PnP::ComputeSquaredReprojectionError(
  const Vector2d &p_i,
  const Vector3d &p_w,
  const CameraPose &pose ) const
{
  Vector2d p_i_w;
  if ( pose.iTw( p_w, &p_i_w ) ) {
    return ( p_i - p_i_w ).squaredNorm();
  }
  else {
    // Point does not project into the image because it is behind the camera.
    return std::numeric_limits<double>::max();
  }
}

// find out the number of inliers 
int PnP::EvaluatePose(
  const std::vector<Vector2d> &points2D,
  const std::vector<Vector3d> &points3D,
  const CameraPose &pose,
  std::vector<bool>& inliers) const
{
  int inlier_count = 0;
  size_t num_corr = points2D.size();
  inliers.assign( num_corr, false );

  const double squared_inlier_threshold =
    pnp_params_.ransac_parameters_.squared_inlier_threshold;

  for ( size_t i = 0; i < num_corr; ++i )
  { 
    const double squared_reproj_err = 
      ComputeSquaredReprojectionError( points2D[i], points3D[i], pose );
    if ( squared_reproj_err < squared_inlier_threshold ){ 
      ++inlier_count;
      inliers[i] = true;
    }
  }
  return inlier_count;
}


}