#pragma once

#include <osmium/osm/box.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/types.hpp>

#include <optional>
#include <string>
#include <vector>

constexpr auto NAME_TAG = "name";
constexpr auto HIGHWAY_TAG = "highway";
constexpr auto TYPE_TAG = "type";
constexpr auto BOUNDARY_VALUE = "boundary";

class OSMLoader {
  public:
    OSMLoader() = default;

    void setFilepath(const std::string &filepath) { filepath_ = filepath; }
    bool Count();

    // Using definition of Location:
    // https://osmcode.org/libosmium/manual.html#locations
    using Coordinate = osmium::Location;
    using Coordinates = std::vector<Coordinate>;
    using Tags = std::unordered_map<std::string, std::string>;
    using Id2Tags = std::unordered_map<osmium::object_id_type, Tags>;

    // Represents both Areas (closed=true) and Ways (closed=false)
    // Areas will have the first and last nodes match to ensure that it is closed
    struct Route_t {
        osmium::object_id_type id{0};
        Coordinates nodes;
        Tags tags;
    };
    using Id2Route = std::unordered_map<osmium::object_id_type, Route_t>;

    struct AreaNode {
        osmium::object_id_type id{0};
        std::string role;
        osmium::Location location;
    };

    struct Area_t {
        osmium::object_id_type id{0};
        Coordinates outerRing;
        // std::vector<Coordinates> innerRings;
        std::vector<AreaNode> nodes{};
        Tags tags;
    };
    using Id2Area = std::unordered_map<osmium::object_id_type, Area_t>;

    using CoordinateBounds = osmium::Box;
    /**
     * Get ways within the specified coordinate bounds.
     * @param bounds The coordinate bounds (min and max coordinates).
     * @return A vector of routes, where each route is represented as a vector
     * of coordinates
     */
    using OSMData = std::pair<Id2Route, Id2Area>;
    std::optional<OSMData> getData(const CoordinateBounds &bounds) const;

  protected:
    std::string filepath_{};
};