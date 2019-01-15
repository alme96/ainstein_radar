#include <OGRE/OgreVector3.h>
#include <OGRE/OgreSceneNode.h>
#include <OGRE/OgreSceneManager.h>

#include <ros/console.h>
#include <geometry_msgs/Vector3.h>

#include "radar_visual.h"

namespace rviz_radar_plugin
{

RadarVisual::RadarVisual( Ogre::SceneManager* scene_manager, Ogre::SceneNode* parent_node )
{
  scene_manager_ = scene_manager;

  // Ogre::SceneNode s form a tree, with each node storing the
  // transform (position and orientation) of itself relative to its
  // parent.  Ogre does the math of combining those transforms when it
  // is time to render.
  //
  // Here we create a node to store the pose of the Radar's header frame
  // relative to the RViz fixed frame.
  frame_node_ = parent_node->createChildSceneNode();

  // Set showing speed arrows to false by default:
  show_speed_arrows_ = false;
}

RadarVisual::~RadarVisual()
{
  // Destroy the frame node since we don't need it anymore.
  scene_manager_->destroySceneNode( frame_node_ );
}

void RadarVisual::setMessageRaw( const radar_sensor_msgs::RadarData::ConstPtr& msg )
{
  // Clear the target shapes vector:
  radar_target_shapes_raw_.clear();
  
  // Fill the target shapes from RadarData message:
  for( const auto& target : msg->raw_targets )
    {
      if( target.range > min_range_ && target.range < max_range_ )
	{
	  // Create the new target shape, fill it and push back:
	  boost::shared_ptr<TargetVisual> s( new TargetVisual( scene_manager_, frame_node_ ) );
	  
	  // Compute the target's Cartesian position:
	  s->pos.setPosition( Ogre::Vector3(cos( ( M_PI / 180.0 ) * target.azimuth ) * cos( ( M_PI / 180.0 ) * target.elevation ) * target.range,
					sin( ( M_PI / 180.0 ) * target.azimuth ) * cos( ( M_PI / 180.0 ) * target.elevation ) * target.range,
					sin( ( M_PI / 180.0 ) * target.elevation ) * target.range ) );
	  // Set the target speed arrow length:
	  if( show_speed_arrows_ )
	    {
	      s->speed.set( std::abs( target.speed ), // shaft length
			    0.01, // shaft diameter
			    0.1, // arrow head length
			    0.05 ); // arrow head diameter
	    }
	  else
	    {
	      s->speed.set( 0.0, 0.0, 0.0, 0.0 );
	    }

	  // Set the target speed arrow position to the target position:
	  s->speed.setPosition( s->pos.getPosition() );

	  // The target speed points toward the radar sensor:
	  s->speed.setDirection( s->pos.getPosition() /
				 std::copysign( target.range, target.speed ) );
	  
	  radar_target_shapes_raw_.push_back( s );
	}
    }
}

void RadarVisual::clearMessageRaw( void )
{
  radar_target_shapes_raw_.clear();
}
  
// Position and orientation are passed through to the SceneNode.
void RadarVisual::setFramePosition( const Ogre::Vector3& position )
{
  frame_node_->setPosition( position );
}

void RadarVisual::setFrameOrientation( const Ogre::Quaternion& orientation )
{
  frame_node_->setOrientation( orientation );
}
  
// Color is passed through to the Shape object.
void RadarVisual::setColorRaw( float r, float g, float b, float a )
{
  for( const auto& shape : radar_target_shapes_raw_ )
    {
      shape->pos.setColor( Ogre::ColourValue( r, g, b, a) );
      shape->speed.setColor( Ogre::ColourValue( r, g, b, a) );
    }
}

// Scale is passed through to the Shape object.
void RadarVisual::setScaleRaw( float scale )
{
  for( const auto& shape : radar_target_shapes_raw_ )
    {
      shape->pos.setScale( Ogre::Vector3( scale, scale, scale ) );
    }
}

  void RadarVisual::updateTargets( void )
  {
    radar_target_shapes_raw_.erase( std::remove_if( radar_target_shapes_raw_.begin(),
						    radar_target_shapes_raw_.end(),
						    [this]( boost::shared_ptr<TargetVisual> s )
						    {
						      Ogre::Vector3 pos = s->pos.getPosition();
						      return ( pos.length() > max_range_ ||
							       pos.length() < min_range_ );
						    }), radar_target_shapes_raw_.end() );
  }
 
  void RadarVisual::setMinRange( float min_range )
  {
    min_range_ = min_range;
  }
    
  void RadarVisual::setMaxRange( float max_range )
  {
    max_range_ = max_range;
  }

  void RadarVisual::setShowSpeedArrows( bool show_speed_arrows )
  {
    // Store the desired state:
    show_speed_arrows_ = show_speed_arrows;

    // Update all the existing arrows if switched off:
    if( !show_speed_arrows_ )
      {
	for( const auto& t : radar_target_shapes_raw_ )
	  {
	    t->speed.set( 0.0, 0.0, 0.0, 0.0 ); // arrow head diameter
	  }
      }
  }
 
} // end namespace rviz_radar_plugin

