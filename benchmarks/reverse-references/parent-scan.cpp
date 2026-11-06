// Exploration-only direct-parent scanner. Not an osmium command or library fork.
#include <osmium/index/id_set.hpp>
#include <osmium/index/nwr_array.hpp>
#include <osmium/io/any_input.hpp>
#include <osmium/io/any_output.hpp>
#include <osmium/osm.hpp>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace {

using Id = osmium::unsigned_object_id_type;

struct SortedIds {
    osmium::index::IdSetSmall<Id> ids;

    void add(Id id) { ids.set(id); }
    void finish() { ids.sort_unique(); }
    bool contains(Id id) const noexcept { return ids.get_binary_search(id); }
    bool empty() const noexcept { return ids.empty(); }
    std::size_t size() const noexcept { return ids.size(); }
};

struct HashedIds {
    std::unordered_set<Id> ids;

    void add(Id id) { ids.insert(id); }
    void finish() {}
    bool contains(Id id) const noexcept { return ids.find(id) != ids.end(); }
    bool empty() const noexcept { return ids.empty(); }
    std::size_t size() const noexcept { return ids.size(); }
};

char type_letter(osmium::item_type type) {
    switch (type) {
        case osmium::item_type::node: return 'n';
        case osmium::item_type::way: return 'w';
        case osmium::item_type::relation: return 'r';
        default: throw std::runtime_error{"Unsupported entity type"};
    }
}

std::string escape_role(const char* role) {
    std::string escaped;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(role); *p; ++p) {
        if (*p == '%' || *p == '\t' || *p == '\r' || *p == '\n') {
            constexpr char hex[] = "0123456789ABCDEF";
            escaped += '%';
            escaped += hex[*p >> 4U];
            escaped += hex[*p & 15U];
        } else {
            escaped += static_cast<char>(*p);
        }
    }
    return escaped;
}

void edge(std::ofstream& output, osmium::item_type child_type, Id child,
          const osmium::OSMObject& parent, const char* role, std::size_t index) {
    output << type_letter(child_type) << child << '\t'
           << type_letter(parent.type()) << parent.positive_id() << '\t'
           << escape_role(role) << '\t' << index << '\n';
}

template <typename Store>
int scan(const char* input, const char* seeds, const char* output, const char* edges_path) {
    osmium::nwr_array<Store> ids;
    osmium::io::Reader seed_reader{seeds, osmium::osm_entity_bits::nwr};
    while (osmium::memory::Buffer buffer = seed_reader.read()) {
        for (const auto& object : buffer.select<osmium::OSMObject>()) {
            if (object.id() <= 0) {
                throw std::runtime_error{"Prototype requires positive snapshot IDs"};
            }
            ids(object.type()).add(object.positive_id());
        }
    }
    seed_reader.close();
    for (const auto type : {osmium::item_type::node, osmium::item_type::way, osmium::item_type::relation}) {
        ids(type).finish();
    }
    std::cerr << "Seeds: " << ids(osmium::item_type::node).size() << " nodes, "
              << ids(osmium::item_type::way).size() << " ways, "
              << ids(osmium::item_type::relation).size() << " relations\n";

    // Exactly the types needed by getparents without --add-self. No source
    // nodes are materialized. Entity masks do not imply PBF random access.
    auto types = osmium::osm_entity_bits::relation;
    if (!ids(osmium::item_type::node).empty()) {
        types |= osmium::osm_entity_bits::way;
    }
    osmium::io::Reader reader{input, types};
    auto header = reader.header();
    header.set("generator", "reverse-reference-exploration");
    osmium::io::Writer writer{output, header};
    std::ofstream edges;
    if (edges_path) {
        edges.open(edges_path);
        if (!edges) {
            throw std::runtime_error{"Could not open edge output"};
        }
        edges << "child\tparent\trole_percent_escaped\tmember_index\n";
    }
    std::size_t ways = 0;
    std::size_t relations = 0;
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (const auto& object : buffer.select<osmium::OSMObject>()) {
            bool selected = false;
            std::size_t index = 0;
            if (object.type() == osmium::item_type::way) {
                for (const auto& nr : static_cast<const osmium::Way&>(object).nodes()) {
                    if (ids(osmium::item_type::node).contains(nr.positive_ref())) {
                        selected = true;
                        if (!edges_path) {
                            break;
                        }
                        edge(edges, osmium::item_type::node, nr.positive_ref(), object, "", index);
                    }
                    ++index;
                }
            } else if (object.type() == osmium::item_type::relation) {
                for (const auto& member : static_cast<const osmium::Relation&>(object).members()) {
                    if (ids(member.type()).contains(member.positive_ref())) {
                        selected = true;
                        if (!edges_path) {
                            break;
                        }
                        edge(edges, member.type(), member.positive_ref(), object, member.role(), index);
                    }
                    ++index;
                }
            }
            if (selected) {
                writer(object);
                if (object.type() == osmium::item_type::way) {
                    ++ways;
                } else {
                    ++relations;
                }
            }
        }
    }
    writer.close();
    reader.close();
    if (edges_path) {
        edges.close();
        if (!edges) {
            throw std::runtime_error{"Could not finish edge output"};
        }
    }
    std::cerr << "Parents: " << ways << " ways, " << relations << " relations\n";
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc != 5 && argc != 6) {
            std::cerr << "Usage: parent-scan hash|sorted INPUT SEEDS OUTPUT [EDGES.tsv]\n";
            return 2;
        }
        const char* edges = argc == 6 ? argv[5] : nullptr;
        if (std::string{argv[1]} == "hash") {
            return scan<HashedIds>(argv[2], argv[3], argv[4], edges);
        }
        if (std::string{argv[1]} == "sorted") {
            return scan<SortedIds>(argv[2], argv[3], argv[4], edges);
        }
        throw std::runtime_error{"Unknown membership implementation"};
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
