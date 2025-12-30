/*

  EXAMPLE osmium_count

  Counts the number of nodes, ways, and relations in the input file.

  DEMONSTRATES USE OF:
  * OSM file input
  * your own handler
  * the memory usage utility class

  SIMPLER EXAMPLES you might want to understand first:
  * osmium_read

  LICENSE
  The code in this example file is released into the Public Domain.

*/

#include "osm_loader.h"

// Only work with XML input files here
#include <osmium/io/xml_input.hpp>

// We want to use the handler interface
#include <osmium/handler.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>

// Utility class gives us access to memory usage information
#include <osmium/util/memory.hpp>

// For osmium::apply()
#include <osmium/visitor.hpp>

// efficient node location storage for ways
#include <osmium/index/map/sparse_mem_array.hpp>

// location handler for ways
#include <osmium/handler/node_locations_for_ways.hpp>

#include <algorithm>
#include <cstdint> // for std::uint64_t
#include <exception>
#include <iostream> // for std::cout, std::cerr
#include <unordered_set>
namespace {
using NodeID = osmium::object_id_type;
struct WayNodePair {
    osmium::object_id_type wayID;
    size_t nodeIndex;

    WayNodePair(osmium::object_id_type wID, size_t nIndex)
        : wayID(wID), nodeIndex(nIndex) {}

    bool operator==(const WayNodePair &other) const {
        return wayID == other.wayID && nodeIndex == other.nodeIndex;
    }
};

struct WayNodePairHash {
    std::size_t operator()(const WayNodePair &p) const {
        return std::hash<osmium::object_id_type>()(p.wayID) ^
               (std::hash<size_t>()(p.nodeIndex) << 1);
    }
};

using NodeWaysMap =
    std::unordered_map<NodeID,
                       std::unordered_set<WayNodePair, WayNodePairHash>>;
using WayNameMap = std::unordered_map<osmium::object_id_type, std::string>;
struct MappedWayData {
    NodeWaysMap nodeWaysMap;
    WayNameMap wayNames;
};

struct NodeWayMapper : public osmium::handler::Handler {
    // Map of Node IDs -> Way IDs to be retrieved later
    // NodeWaysMap requestedNodes_{};
    // WayNameMap wayNames_;
    MappedWayData wayData_;

    NodeWayMapper() = default;

    void way(const osmium::Way &way) noexcept {
        // filter out ways which are not tagged as highway
        auto tag_value = way.tags().get_value_by_key("highway");
        if (!tag_value) {
            return;
        }

        tag_value = way.tags().get_value_by_key("name");
        if (tag_value) {
            wayData_.wayNames[way.id()] = tag_value;
        }

        for (size_t ii = 0; ii < way.nodes().size(); ++ii) {
            const auto &node_ref = way.nodes()[ii];
            // Assume that we only get po
            assert(node_ref.ref() > 0);
            auto &nodeMap = wayData_.nodeWaysMap[node_ref.ref()];
            nodeMap.emplace(way.id(), ii);
            wayData_.wayNames[way.id()] = tag_value ? tag_value : "";
        }
    }
};

struct NodeReducer : public osmium::handler::Handler {

    const osmium::Box &bounds_;
    const MappedWayData &wayData_;

    OSMLoader::Ways &routes_;

    NodeReducer(const osmium::Box &bounds, const MappedWayData &wayData,
                OSMLoader::Ways &routes)
        : bounds_(bounds), wayData_(wayData), routes_(routes) {}

    void node(const osmium::Node &node) noexcept {
        if (!node.location().valid()) {
            return;
        }

        // NOTE: maybe bounds needs to be expanded to include more nodes?
        if (!bounds_.contains(node.location())) {
            return;
        }

        auto it = wayData_.nodeWaysMap.find(node.id());
        if (it == wayData_.nodeWaysMap.end()) {
            return;
        }
        // This node is part of one or more requested ways
        for (const auto &way : it->second) {
            // Find or create the route for this wayID
            auto &route = routes_[way.wayID];
            if (route.nodes.size() <= way.nodeIndex) {
                route.nodes.resize(way.nodeIndex + 1);
            }
            route.nodes[way.nodeIndex] = node.location();
            route.id = way.wayID;
            if (route.name.empty()) {
                route.name = wayData_.wayNames.count(way.wayID) > 0
                                 ? wayData_.wayNames.at(way.wayID)
                                 : "";
            }
        }
    }
};

} // namespace

OSMLoader::Ways OSMLoader::getWays(const CoordinateBounds &bounds) const {
    Ways routes;

    if (filepath_.empty()) {
        std::cerr << "No input file specified." << std::endl;
        return routes;
    }

    try {
        const osmium::io::File input_file{filepath_};
        // 1) generate a mapping of node to ways
        osmium::io::Reader reader{input_file, osmium::osm_entity_bits::way};

        NodeWayMapper wayHandler;
        osmium::apply(reader, wayHandler);
        reader.close();

        // 2) find the nodes which were requested in (1) and are within bounds
        // and build a buffer to hold them
        osmium::io::Reader nodeReader{input_file,
                                      osmium::osm_entity_bits::node};
        NodeReducer reducer(bounds, wayHandler.wayData_, routes);
        osmium::apply(nodeReader, reducer);
        nodeReader.close();

    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    // clean up routes to remove any incomplete ways
    for (auto it = routes.begin(); it != routes.end();) {
        auto &way = it->second;
        auto new_end =
            std::remove_if(way.nodes.begin(), way.nodes.end(),
                           [](const Coordinate &loc) { return !loc.valid(); });
        way.nodes.erase(new_end, way.nodes.end());

        if (way.nodes.empty()) {
            it = routes.erase(it);
        } else {
            ++it;
        }
    }

    return routes;
}
