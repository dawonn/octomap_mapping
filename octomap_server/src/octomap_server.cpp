/**
* octomap_server: A Tool to serve 3D OctoMaps in ROS (binary and as visualization)
* (inspired by the ROS map_saver)
* @author A. Hornung, University of Freiburg, Copyright (C) 2009.
* @see http://octomap.sourceforge.net/
* License: GNU GPL v2, http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
*/

/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
*
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include <ros/ros.h>
#include <visualization_msgs/MarkerArray.h>
#include <std_msgs/ColorRGBA.h>
#include <octomap_server/octomap_server.h>
#include <octomap/octomap.h>

#define USAGE "\nUSAGE: octomap_server <map.bt>\n" \
              "  map.bt: octomap 3D map file to read\n"

class OctomapServer{
public:
	OctomapServer(const std::string& filename);
	virtual ~OctomapServer();
	void readMap(const std::string& filename);
	bool serviceCallback(octomap_server::GetOctomap::Request  &req,
			octomap_server::GetOctomap::Response &res);

private:
	std_msgs::ColorRGBA heightMapColor(double h) const;
	ros::NodeHandle m_nh;
	ros::Publisher m_markerPub, m_binaryMapPub;
	ros::ServiceServer m_service;

	// (cached) map data
	octomap_server::GetOctomap::Response m_mapResponse;
	// (cached) map visualization data:
	visualization_msgs::MarkerArray m_occupiedCellsVis;

	std::string m_frameId;
	bool m_useHeightMap;
	std_msgs::ColorRGBA m_color;
	double m_colorFactor;
};


OctomapServer::OctomapServer(const std::string& filename)
  : m_nh(), m_frameId("/map"), m_useHeightMap(true),
    m_colorFactor(0.8)
{
	ros::NodeHandle private_nh("~");
	private_nh.param("frame_id", m_frameId, m_frameId);
	private_nh.param("height_map", m_useHeightMap, m_useHeightMap);
	private_nh.param("color_factor", m_colorFactor, m_colorFactor);


	double r, g, b, a;
	private_nh.param("color/r", r, 0.0);
	private_nh.param("color/g", g, 0.0);
	private_nh.param("color/b", b, 1.0);
	private_nh.param("color/a", a, 1.0);
	m_color.r = r;
	m_color.g = g;
	m_color.b = b;
	m_color.a = a;

	readMap(filename);

	m_markerPub = m_nh.advertise<visualization_msgs::MarkerArray>("occupied_cells_vis_array", 1, true);
	m_binaryMapPub = m_nh.advertise<octomap_server::OctomapBinary>("octomap_binary", 1, true);
	m_service = m_nh.advertiseService("octomap_binary", &OctomapServer::serviceCallback, this);

	// publish once as latched topic:
	m_binaryMapPub.publish(m_mapResponse.map);
	m_markerPub.publish(m_occupiedCellsVis);
}

OctomapServer::~OctomapServer(){

}

/**
 * Reads in a map file and fills internal (cached) messages accordingly
 *
 * @param filename of map file
 */
void OctomapServer::readMap(const std::string& filename){

	octomap::OcTree map(filename);

	m_mapResponse.map.header.frame_id = m_frameId;
	octomap_server::octomapMapToMsg(map, m_mapResponse.map);
	double x, y, minZ, maxZ;
	map.getMetricMin(x, y, minZ);
	map.getMetricMax(y, y, maxZ);

	// each array stores all cubes of a different size, one for each depth level:
	m_occupiedCellsVis.markers.resize(16);
	double lowestRes = map.getResolution();

	std::list<octomap::OcTreeVolume> occupiedCells;
	map.getOccupied(occupiedCells);

	// rough heuristics for expected size of cells at lowest level
	m_occupiedCellsVis.markers[0].points.reserve(occupiedCells.size());
	m_occupiedCellsVis.markers[1].points.reserve(occupiedCells.size()/2);
	m_occupiedCellsVis.markers[2].points.reserve(occupiedCells.size()/4);
	m_occupiedCellsVis.markers[3].points.reserve(occupiedCells.size()/4);


	std::list<octomap::OcTreeVolume>::iterator it;
	unsigned numVoxels = 0;
	for (it = occupiedCells.begin(); it != occupiedCells.end(); ++it){
		// which array to store cubes in?
		int idx = int(log2(it->second / lowestRes) +0.5);
		assert (idx >= 0 && unsigned(idx) < m_occupiedCellsVis.markers.size());
		geometry_msgs::Point cubeCenter;
		cubeCenter.x = it->first.x();
		cubeCenter.y = it->first.y();
		cubeCenter.z = it->first.z();

		//if (it->first.z() > 0.01)
		{
			m_occupiedCellsVis.markers[idx].points.push_back(cubeCenter);
			if (m_useHeightMap){
				double h = (1.0 - std::min(std::max((it->first.z()-minZ)/ (maxZ - minZ), 0.0), 1.0)) *m_colorFactor;
				m_occupiedCellsVis.markers[idx].colors.push_back(heightMapColor(h));
			}
			numVoxels++;

		}
	}

	for (unsigned i= 0; i < m_occupiedCellsVis.markers.size(); ++i){
		double size = lowestRes * pow(2,i);

		m_occupiedCellsVis.markers[i].header.frame_id = m_frameId;
		m_occupiedCellsVis.markers[i].header.stamp = ros::Time::now();
		m_occupiedCellsVis.markers[i].ns = "map";
		m_occupiedCellsVis.markers[i].id = i;
		m_occupiedCellsVis.markers[i].type = visualization_msgs::Marker::CUBE_LIST;
		m_occupiedCellsVis.markers[i].scale.x = size;
		m_occupiedCellsVis.markers[i].scale.y = size;
		m_occupiedCellsVis.markers[i].scale.z = size;
		m_occupiedCellsVis.markers[i].color = m_color;


		if (m_occupiedCellsVis.markers[i].points.size() > 0)
			m_occupiedCellsVis.markers[i].action = visualization_msgs::Marker::ADD;
		else
			m_occupiedCellsVis.markers[i].action = visualization_msgs::Marker::DELETE;
	}

	ROS_INFO("Octomap file %s loaded (%d nodes, %d occupied visualized).", filename.c_str(),map.size(), numVoxels);
}

bool OctomapServer::serviceCallback(octomap_server::GetOctomap::Request  &req,
			octomap_server::GetOctomap::Response &res)
{
	res = m_mapResponse;
	ROS_INFO("Sending map data on service request");

	return true;
}

std_msgs::ColorRGBA OctomapServer::heightMapColor(double h) const {

//	if (m_zMin >= m_zMax)
//		h = 0.5;
//	else{
//		h = (1.0 - std::min(std::max((h-m_zMin)/ (m_zMax - m_zMin), 0.0), 1.0)) *0.8;
//	}

	std_msgs::ColorRGBA color;
	color.a = 1.0;
	// blend over HSV-values (more colors)

	double s = 1.0;
	double v = 1.0;

	h -= floor(h);
	h *= 6;
	int i;
	double m, n, f;

	i = floor(h);
	f = h - i;
	if (!(i & 1))
		f = 1 - f; // if i is even
	m = v * (1 - s);
	n = v * (1 - s * f);

	switch (i) {
	case 6:
	case 0:
		color.r = v; color.g = n; color.b = m;
		break;
	case 1:
		color.r = n; color.g = v; color.b = m;
		break;
	case 2:
		color.r = m; color.g = v; color.b = n;
		break;
	case 3:
		color.r = m; color.g = n; color.b = v;
		break;
	case 4:
		color.r = n; color.g = m; color.b = v;
		break;
	case 5:
		color.r = v; color.g = m; color.b = n;
		break;
	default:
		color.r = 1; color.g = 0.5; color.b = 0.5;
		break;
	}

	return color;
}

int main(int argc, char** argv){
  ros::init(argc, argv, "octomap_server");
  std::string mapFilename("");
  if (argc == 2)
	  mapFilename = std::string(argv[1]);
  else{
	  ROS_ERROR("%s", USAGE);
	  exit(-1);
  }

  try{
	  OctomapServer ms(mapFilename);
	  ros::spin();
  }catch(std::runtime_error& e){
	  ROS_ERROR("map_server exception: %s", e.what());
	  return -1;
  }

  return 0;
}

