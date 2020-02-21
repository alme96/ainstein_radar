/*
  Copyright <2018-2019> <Ainstein, Inc.>

  Redistribution and use in source and binary forms, with or without modification, are permitted
  provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this list of
  conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice, this list of
  conditions and the following disclaimer in the documentation and/or other materials provided
  with the distribution.

  3. Neither the name of the copyright holder nor the names of its contributors may be used to
  endorse or promote products derived from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "ainstein_radar_filters/tracking_filter.h"

namespace ainstein_radar_filters
{
const int TrackingFilter::max_tracked_targets = 100;

void TrackingFilter::initialize(void)
{
  // Reserve space for the maximum number of target Kalman Filters:
  filters_.reserve(TrackingFilter::max_tracked_targets);

  // Launch the periodic filter update thread:
  filter_process_thread_ =
	  std::unique_ptr<std::thread>(new std::thread(&TrackingFilter::processFiltersLoop, this, filter_process_rate_));
}

void TrackingFilter::processFiltersLoop(double frequency)
{
  // Enter the main filters update loop:
  std::chrono::system_clock::time_point time_now, time_prev;
  double dt;
  bool first_time = true;
  while (is_running_)
  {
	// Compute the actual delta time:
	if (first_time)
	{
	  time_prev = std::chrono::system_clock::now();
	  first_time = false;
	}

	time_now = std::chrono::system_clock::now();

	dt = (time_now - time_prev).count();

	// Block callback from modifying the filters
	mutex_.lock();

	// Remove filters which have not been updated in specified time:
	if (print_debug_)
	{
	  std::cout << "Number of filters before pruning: " << filters_.size() << std::endl;
	}
	if (filters_.size() > 0)
	{
	  filters_.erase(
		  std::remove_if(filters_.begin(), filters_.end(),
						 [&](const RadarTargetKF& kf) { return (kf.getTimeSinceUpdate() > filter_timeout_); }),
		  filters_.end());
	}
	if (print_debug_)
	{
	  std::cout << "Number of filters after pruning: " << filters_.size() << std::endl;
	}

	// Run process model for each filter:
	for (auto& kf : filters_)
	{
	  kf.process(dt);
	}

	// Add tracked targets for filters which have been running for specified time:
	tracked_targets_.clear();

	// Clear the tracked "clusters":
	tracked_clusters_.clear();

	ainstein_radar_filters::RadarTarget tracked_target;
	int target_id = 0;
	for (int i = 0; i < filters_.size(); ++i)
	{
	  if (filters_.at(i).getTimeSinceStart() >= filter_min_time_)
	  {
		// Fill the tracked targets message:
		RadarTargetKF::FilterState state = filters_.at(i).getState();
		tracked_target = RadarTarget(state.range, state.speed, state.azimuth, state.elevation);
		tracked_target.target_id = target_id;
		tracked_targets_.targets.push_back(tracked_target);

		// Fill the tracked target clusters message:
		tracked_clusters_.push_back(filter_targets_.at(i));

		// msg_tracked_boxes_.boxes.push_back( getBoundingBox( tracked_target, filter_targets_.at( i ) ) );

		++target_id;
	  }
	}

	// Release lock on filter state
	mutex_.unlock();

	// Publish the tracked targets:
	pub_radar_data_tracked_.publish(msg_tracked_targets_);

	// Publish the bounding boxes:
	pub_bounding_boxes_.publish(msg_tracked_boxes_);

	// Store the current time and velocity:
	time_prev = time_now;

	// Spin once to handle callbacks:
	ros::spinOnce();

	// Sleep to maintain desired freq:
	process_filters_rate.sleep();
  }
}

void TrackingFilter::updateFilters(const std::vector<RadarTarget>& targets)
{
  // Reset the measurement count vector for keeping track of which measurements get used:
  meas_count_vec_.resize(targets.size());
  std::fill(meas_count_vec_.begin(), meas_count_vec_.end(), 0);

  // Block update loop from modifying the filters
  mutex_.lock();

  // Resize the targets associated with each filter and set headers:
  filter_targets_.clear();
  filter_targets_.resize(filters_.size());
  for (auto& targets : filter_targets_)
  {
	targets.header.stamp = ros::Time::now();
	targets.header.frame_id = frame_id_;
  }

  // Pass the raw detections to the filters for updating:
  for (int i = 0; i < filters_.size(); ++i)
  {
	ROS_DEBUG_STREAM(filters_.at(i));
	for (int j = 0; j < targets.size(); ++j)
	{
	  // Only use this target if it hasn't already been used by a filter:
	  if (meas_count_vec_.at(j) == 0)
	  {
		// Check whether the target should be used as measurement by this filter:
		RadarTarget t = targets.at(j);
		Eigen::Vector4d z = filters_.at(i).computePredMeas(filters_.at(i).getState());
		Eigen::Vector4d y = Eigen::Vector4d(t.range, t.speed, t.azimuth, t.elevation);

		// Compute the normalized measurement error (squared):
		double meas_err =
			(y - z).transpose() * filters_.at(i).computeMeasCov(filters_.at(i).getState()).inverse() * (y - z);

		ROS_DEBUG_STREAM("Meas Cov Inv: " << filters_.at(i).computeMeasCov(filters_.at(i).getState()).inverse()
										  << std::endl);

		ROS_DEBUG_STREAM("Target " << j << " meas_err: " << meas_err);
		ROS_DEBUG_STREAM("Target " << j << ": " << std::endl << t);

		// Allow the measurement through the validation gate based on threshold:
		if (meas_err < filter_val_gate_thresh_)
		{
		  filters_.at(i).update(t);
		  ++meas_count_vec_.at(j);

		  // Store the target associated with the filter:
		  filter_targets_.at(i).targets.push_back(t);
		}
	  }
	}
  }

  ROS_DEBUG_STREAM("meas_count_vec_: ");
  for (const auto& ind : meas_count_vec_)
  {
	ROS_DEBUG_STREAM(ind << " ");
  }
  ROS_DEBUG_STREAM(std::endl);

  // Iterate through targets and push back new KFs for unused measurements:
  std::vectror<RadarTarget> arr;
  for (int i = 0; i < meas_count_vec_.size(); ++i)
  {
	if (meas_count_vec_.at(i) == 0)
	{
	  std::cout << "Pushing back: " << targets.at(i) << std::endl;
	  filters_.emplace_back(targets.at(i), nh_, nh_private_);

	  // Make sure to push back an empty array of targets associated with the new filter
	  filter_targets_.push_back(arr);
	}
  }

  // Release lock on filter state
  mutex_.unlock();
}

}  // namespace ainstein_radar_filters
