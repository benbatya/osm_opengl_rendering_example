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
struct IdIndexPair {
    osmium::object_id_type pairID;
    int64_t pairIndex;

    IdIndexPair(osmium::object_id_type wID, int64_t nIndex) : pairID(wID), pairIndex(nIndex) {}

    bool operator==(const IdIndexPair &other) const { return pairID == other.pairID && pairIndex == other.pairIndex; }
};

struct IdIndexPairHash {
    std::size_t operator()(const IdIndexPair &p) const {
        return std::hash<osmium::object_id_type>()(p.pairID) ^ (std::hash<int64_t>()(p.pairIndex) << 1);
    }
};

using Id2IdIndexMap = std::unordered_map<osmium::object_id_type, std::unordered_set<IdIndexPair, IdIndexPairHash>>;
using Id2String = std::unordered_map<osmium::object_id_type, std::string>;
using Id2Index = std::unordered_map<osmium::object_id_type, int64_t>;
using Id2Id2Index = std::unordered_map<osmium::object_id_type, Id2Index>;

struct MappedWayData {
    Id2IdIndexMap node2Ways;
    OSMLoader::Id2Tags id2Tags;
};

// Map of Way -> Relationships
using Id2Ids = std::unordered_map<osmium::object_id_type, std::unordered_set<osmium::object_id_type>>;
struct RelationshipData {
    Id2Ids way2Relationships;  // all of the ways that are referenced by relationships
    Id2Ids node2Relationships; // all of the nodes that are referenced by relationships
    Id2String node2Roles;
    OSMLoader::Id2Tags id2Tags;
};

struct RelationshipHandler : public osmium::handler::Handler {
    RelationshipData relationshipData;

    bool containsTagValue(const osmium::TagList &tags, const char *key, const char *value) {
        auto tag_value = tags.get_value_by_key(key);
        return tag_value && std::strcmp(tag_value, value) == 0;
    }

    void relation(const osmium::Relation &relation) noexcept {
        if (!(containsTagValue(relation.tags(), ::TYPE_TAG, ::BOUNDARY_VALUE) ||
              containsTagValue(relation.tags(), ::BUILDING_TAG, ::YES_VALUE))) {
            return;
        }

        for (const auto &member : relation.members()) {
            if (member.type() == osmium::item_type::way) {
                // TODO: handle inner ways
                if (strcmp(member.role(), "outer") == 0) {
                    relationshipData.way2Relationships[member.ref()].insert(relation.id());
                }
            } else if (member.type() == osmium::item_type::node) {
                relationshipData.node2Relationships[member.ref()].insert(relation.id());
                relationshipData.node2Roles[member.ref()] = member.role();
            }
        }

        if (auto tag_value = relation.tags().get_value_by_key(NAME_TAG); tag_value) {
            relationshipData.id2Tags[relation.id()][NAME_TAG] = tag_value;
        }
        if (auto tag_value = relation.tags().get_value_by_key(TYPE_TAG); tag_value) {
            relationshipData.id2Tags[relation.id()][TYPE_TAG] = tag_value;
        }
    };
};

struct WayHandler : public osmium::handler::Handler {
    // Map of Node IDs -> Way IDs to be retrieved later
    const RelationshipData &inputRelationships_;

    Id2Index relationship2RingIndex{};
    Id2Id2Index way2Relationship2RingIndex{};
    MappedWayData wayData;

    // size_t largestWaySize = 0;
    // osmium::object_id_type largestWayID = 0;

    WayHandler(const RelationshipData &relationshipData) : inputRelationships_(relationshipData) {}

    bool isWayInRelationship(const osmium::Way &way) const {
        return inputRelationships_.way2Relationships.count(way.id()) > 0;
    }
    bool isWayAValidRoute(const osmium::Way &way) const { return way.tags().get_value_by_key(HIGHWAY_TAG) != nullptr; }

    void way(const osmium::Way &way) noexcept {
        if (!(isWayInRelationship(way) || isWayAValidRoute(way))) {
            return;
        }

        // if (way.nodes().size() > largestWaySize) {
        //     largestWaySize = way.nodes().size();
        //     largestWayID = way.id();
        // }

        auto &wayData = this->wayData;

        if (isWayInRelationship(way)) {
            const auto &tags = way.tags();
            if (auto tag_value = tags.get_value_by_key(TYPE_TAG); tag_value) {
                wayData.id2Tags[way.id()][TYPE_TAG] = tag_value;
            }

            std::cout << "Relationship Way " << way.id() << " is in relationship ";
            for (const auto &relationshipId : inputRelationships_.way2Relationships.at(way.id())) {
                auto &ringIndex = relationship2RingIndex[relationshipId];
                way2Relationship2RingIndex[way.id()][relationshipId] = ringIndex;
                std::cout << relationshipId << ", ringIndx=" << ringIndex << ", ";
                ++ringIndex;
            }
            std::array<char, 128> buffer;
            std::snprintf(buffer.data(), buffer.size(), " and has %lu nodes\n", way.nodes().size());
            std::cout << buffer.data();
        }

        if (isWayAValidRoute(way)) {
            const auto &tags = way.tags();

            if (auto tag_value = tags.get_value_by_key(HIGHWAY_TAG); tag_value) {
                wayData.id2Tags[way.id()][HIGHWAY_TAG] = tag_value;
            }

            if (auto tag_value = tags.get_value_by_key(NAME_TAG); tag_value) {
                wayData.id2Tags[way.id()][NAME_TAG] = tag_value;
            }
        }

        if (isWayInRelationship(way)) {
        }
        // const size_t offset = isWayInRelationship(way) ? way.nodes().size() : 0;
        for (size_t ii = 0; ii < way.nodes().size(); ++ii) {
            const auto &node_ref = way.nodes()[ii];
            // Assume that we only get po
            assert(node_ref.ref() > 0);
            auto &nodeMap = wayData.node2Ways[node_ref.ref()];
            nodeMap.emplace(way.id(), ii); // + offset);
        }
    }
};

struct NodeHandler : public osmium::handler::Handler {

    const osmium::Box &bounds_;
    const MappedWayData &wayData_;
    const RelationshipData &relationshipData_;
    const Id2Id2Index &way2Relationship2RingIndex_;

    OSMLoader::Id2Route routes_;
    OSMLoader::Id2Area areas_;

    NodeHandler(const osmium::Box &bounds, const MappedWayData &wayData, const RelationshipData &relationshipData,
                const Id2Id2Index &way2Relationship2RingIndex)
        : bounds_(bounds), wayData_(wayData), relationshipData_(relationshipData),
          way2Relationship2RingIndex_(way2Relationship2RingIndex) {}

    void node(const osmium::Node &node) noexcept {
        if (!node.location().valid()) {
            return;
        }

        // NOTE: maybe bounds needs to be expanded to include more nodes?
        if (!bounds_.contains(node.location())) {
            return;
        }

        // check if node is in relationship
        if (auto it = relationshipData_.node2Relationships.find(node.id());
            it != relationshipData_.node2Relationships.end()) {
            for (const auto &relationshipId : it->second) {
                auto &area = areas_[relationshipId];
                OSMLoader::AreaNode aNode{
                    .id = node.id(),
                    .role = relationshipData_.node2Roles.at(node.id()),
                    .location = node.location(),
                };
                area.nodes.push_back(aNode);
            }
        }

        // check if node is in a way
        if (auto it = wayData_.node2Ways.find(node.id()); it != wayData_.node2Ways.end()) {
            // This node is part of one or more requested ways
            for (const auto &way : it->second) {
                if (relationshipData_.way2Relationships.count(way.pairID) > 0) {
                    for (const auto &relationshipId : relationshipData_.way2Relationships.at(way.pairID)) {
                        auto &area = areas_[relationshipId];
                        const auto &ringIdx = way2Relationship2RingIndex_.at(way.pairID).at(relationshipId);
                        if (ringIdx >= area.outerRings.size()) {
                            area.outerRings.resize(ringIdx + 1);
                        }
                        auto &outerRing = area.outerRings.at(ringIdx);
                        populateWay(node, way.pairIndex, outerRing);
                    }
                } else {
                    // Find or create the route for this wayID
                    auto &route = routes_[way.pairID];
                    populateWay(node, way.pairIndex, route.nodes);
                    route.id = way.pairID;
                    if (wayData_.id2Tags.count(way.pairID) > 0) {
                        const auto &tags = wayData_.id2Tags.at(way.pairID);
                        route.tags[NAME_TAG] = tags.count(NAME_TAG) > 0 ? tags.at(NAME_TAG) : "";
                        route.tags[HIGHWAY_TAG] = tags.count(HIGHWAY_TAG) > 0 ? tags.at(HIGHWAY_TAG) : "";
                    }
                }
            }
        }
    }

    void populateWay(const osmium::Node &node, const int nodeIndex, OSMLoader::Coordinates &nodes) {
        if (nodes.size() <= nodeIndex) {
            nodes.resize(nodeIndex + 1);
        }
        nodes[nodeIndex] = node.location();
    }
};

bool cleanupWay(OSMLoader::Coordinates &nodes) {
    auto new_end =
        std::remove_if(nodes.begin(), nodes.end(), [](const OSMLoader::Coordinate &loc) { return !loc.valid(); });
    nodes.erase(new_end, nodes.end());

    return nodes.empty();
}

} // namespace

std::optional<OSMLoader::OSMData> OSMLoader::getData(const CoordinateBounds &bounds) const {
    OSMData data;

    if (filepath_.empty()) {
        std::cerr << "No input file specified." << std::endl;
        return data;
    }

    try {
        const osmium::io::File input_file{filepath_};

        // 1) Generate a mapping of ways&nodes to relationships
        osmium::io::Reader relationshipReader{input_file, osmium::osm_entity_bits::relation};
        RelationshipHandler relationshipHandler;
        osmium::apply(relationshipReader, relationshipHandler);
        relationshipReader.close();
        const auto &relationshipData = relationshipHandler.relationshipData;

        // 2) generate a mapping of node to ways
        osmium::io::Reader wayReader{input_file, osmium::osm_entity_bits::way};
        WayHandler wayHandler(relationshipData);
        osmium::apply(wayReader, wayHandler);
        wayReader.close();
        const auto &wayData = wayHandler.wayData;

        // std::cout << "Largest way " << wayHandler.largestWayID << ", size: " << wayHandler.largestWaySize <<
        // std::endl;

        //
        // 2) find the nodes which were requested in (1) and are within bounds
        // and build a buffer to hold them
        osmium::io::Reader nodeReader{input_file, osmium::osm_entity_bits::node};
        NodeHandler nodeHandler(bounds, wayData, relationshipData, wayHandler.way2Relationship2RingIndex);
        osmium::apply(nodeReader, nodeHandler);
        nodeReader.close();
        auto &routes = nodeHandler.routes_;
        auto &areas = nodeHandler.areas_;

        // clean up routes to remove any incomplete ways
        for (auto it = routes.begin(); it != routes.end();) {
            auto &way = it->second;
            if (cleanupWay(way.nodes)) {
                it = routes.erase(it);
            } else {
                ++it;
            }

            // auto new_end =
            //     std::remove_if(way.nodes.begin(), way.nodes.end(), [](const Coordinate &loc) { return !loc.valid();
            //     });
            // way.nodes.erase(new_end, way.nodes.end());

            // if (way.nodes.empty()) {
            //     it = routes.erase(it);
            // } else {
            //     ++it;
            // }
        }

        for (auto &[k, v] : areas) {
            for (auto it = v.outerRings.begin(); it != v.outerRings.end();) {
                if (cleanupWay(*it)) {
                    std::cout << "cleaning up area " << k << " outer ring " << std::distance(v.outerRings.begin(), it)
                              << std::endl;
                    it = v.outerRings.erase(it);
                } else {
                    ++it;
                }
            }
            // If there's no valid outerRing, then remove the area
            if (v.outerRings.empty()) {
                std::cout << "Erasing area " << k << " since it has no valid outer ring" << std::endl;
                areas.erase(k);
            }
        }

        // // move routes -> Area_t::outerRing
        // for (const auto &way : relationshipData.way2Relationships) {
        //     if (!routes.count(way.first)) {
        //         continue;
        //     }
        //     const auto &route = routes.at(way.first);
        //     for (auto &area : way.second) {
        //         if (areas.count(area)) {
        //             // TODO: figure out how to allow more then one outerRing
        //             auto &outerRing = areas.at(area).outerRing;
        //             outerRing.reserve(outerRing.size() + route.nodes.size());
        //             std::copy(route.nodes.begin(), route.nodes.end(), std::back_inserter(outerRing));
        //         }
        //     }
        //     // TODO: maybe allow a way to be both a route and an area outer/inner
        //     routes.erase(way.first);
        // }

        // for (auto &area : areas) {
        //     auto &outerRing = area.second.outerRing;
        //     if (outerRing.empty()) {
        //         std::cout << "Area " << area.first << " has no outer ring" << std::endl;
        //         continue;
        //     }
        //     // close the area
        //     if (outerRing.front() != outerRing.back()) {
        //         std::cout << "Closing area " << area.first << " since beginning and end vertices do not match\n";
        //         // outerRing.push_back(outerRing.front());
        //     }
        // }

        // Uncomment for data analysis
        // std::unordered_map<std::string, uint32_t> types;
        // for (const auto &entry : routes) {
        //     types[entry.second.type] += 1;
        // }
        // std::cout << "Highway types:" << std::endl;
        // for (const auto &type : types) {
        //     std::cout << type.first << ": " << type.second << std::endl;
        // }

        return std::make_pair(routes, areas);

    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
    }

    return std::nullopt;
}
