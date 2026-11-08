/*

Osmium -- OpenStreetMap data manipulation command line tool
https://osmcode.org/osmium-tool/

Copyright (C) 2013-2026  Jochen Topf <jochen@topf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

#include "command_tags_filter_bool.hpp"

#include "exception.hpp"
#include "tags_filter_bool/expression.hpp"
#include "util.hpp"

#include <osmium/index/relations_map.hpp>
#include <osmium/io/header.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/io/writer.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm.hpp>
#include <osmium/util/progress_bar.hpp>
#include <osmium/util/verbose_output.hpp>

#include <boost/program_options.hpp>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

void CommandTagsFilterBool::parse_and_add_expression(const std::string& source) {
    const tags_filter_bool::Expression expression{source};
    const auto entities = expression.entities();
    if (entities & osmium::osm_entity_bits::node) {
        m_filters(osmium::item_type::node).add(expression);
    }
    if (entities & osmium::osm_entity_bits::way) {
        m_filters(osmium::item_type::way).add(expression);
    }
    if (entities & osmium::osm_entity_bits::relation) {
        m_filters(osmium::item_type::relation).add(expression);
    }
    if (entities & osmium::osm_entity_bits::area) {
        m_area_filters.add(expression);
    }
}

void CommandTagsFilterBool::read_expressions_file(const std::string& file_name) {
    m_vout << "Reading expressions file...\n";
    std::ifstream file{file_name};
    if (!file.is_open()) {
        throw argument_error{"Could not open file '" + file_name + "'"};
    }

    std::size_t line_number = 0;
    for (std::string line; std::getline(file, line);) {
        ++line_number;
        line = tags_filter_bool::strip_comment(line);
        if (line.find_first_not_of(" \t\r\n") != std::string::npos) {
            try {
                parse_and_add_expression(line);
            } catch (const argument_error& e) {
                throw argument_error{file_name + ":" + std::to_string(line_number) + ": " + e.what()};
            }
        }
    }
    if (file.bad()) {
        throw argument_error{"Could not read file '" + file_name + "'"};
    }
}

bool CommandTagsFilterBool::setup(const std::vector<std::string>& arguments) {
    po::options_description opts_cmd{"COMMAND OPTIONS"};
    opts_cmd.add_options()
    ("expressions,e", po::value<std::string>(), "Read Boolean expressions from file")
    ("invert-match,i", "Invert the sense of matching, exclude objects with matching tags")
    ("omit-referenced,R", "Omit referenced objects")
    ("remove-tags,t", "Remove tags from non-matching objects")
    ("include-parents", "Include parents of the original matching objects")
    ("parent-hops", po::value<std::string>()->default_value("1"), "Maximum reverse parent hops (0 disables lookup)")
    ("parents-expression", po::value<std::vector<std::string>>()->composing(), "Boolean predicate on parents (repeatable, ORed)")
    ("parent-relationships", po::value<std::string>(), "Write occurrence-level child/parent TSV")
    ;

    const po::options_description opts_common{add_common_options()};
    const po::options_description opts_input{add_single_input_options()};
    const po::options_description opts_output{add_output_options()};

    po::options_description hidden;
    hidden.add_options()
    ("input-filename", po::value<std::string>(), "OSM input file")
    ("expression-list", po::value<std::vector<std::string>>(), "Boolean expressions")
    ;

    po::options_description desc;
    desc.add(opts_cmd).add(opts_common).add(opts_input).add(opts_output);
    po::options_description parsed_options;
    parsed_options.add(desc).add(hidden);

    po::positional_options_description positional;
    positional.add("input-filename", 1);
    positional.add("expression-list", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(arguments).options(parsed_options).positional(positional).run(), vm);
    po::notify(vm);

    if (!setup_common(vm, desc)) {
        return false;
    }
    setup_progress(vm);
    setup_input_file(vm);
    setup_output_file(vm);

    m_include_parents = vm.count("include-parents") != 0;
    if (!m_include_parents && (vm.count("parents-expression") || vm.count("parent-relationships") || !vm["parent-hops"].defaulted())) {
        throw argument_error{"Parent options require --include-parents."};
    }
    m_parent_hops = reverse_references::parse_parent_hops(vm["parent-hops"].as<std::string>());
    if (m_include_parents && (m_input_filename.empty() || m_input_filename == "-")) {
        throw argument_error{"--include-parents requires a replayable source file, not STDIN."};
    }
    if (m_include_parents) {
        std::vector<std::string> protected_files{m_input_filename};
        if (vm.count("expressions")) protected_files.push_back(vm["expressions"].as<std::string>());
        reverse_references::check_distinct_output(m_output_filename, protected_files);
    }
    if (vm.count("parents-expression")) {
        m_parent_filter.setup(vm["parents-expression"].as<std::vector<std::string>>());
    }
    if (vm.count("parent-relationships")) {
        m_relationships = vm["parent-relationships"].as<std::string>();
        if (m_relationships.empty()) throw argument_error{"Parent relationship filename must not be empty."};
        std::vector<std::string> protected_files{m_input_filename, m_output_filename};
        if (vm.count("expressions")) protected_files.push_back(vm["expressions"].as<std::string>());
        reverse_references::check_relationship_path(m_relationships, protected_files, m_output_overwrite == osmium::io::overwrite::allow);
    }

    if (vm.count("omit-referenced")) {
        m_add_referenced_objects = false;
    } else if (m_input_filename == "-") {
        throw argument_error{"Can not read OSM input from STDIN (unless --omit-referenced/-R option is used)."};
    }
    if (vm.count("invert-match")) {
        m_invert_match = true;
    }
    if (vm.count("remove-tags")) {
        m_remove_tags = true;
    }
    if (!vm.count("expression-list") && !vm.count("expressions")) {
        throw argument_error{"Please specify Boolean expressions or an expressions file."};
    }
    if (vm.count("expression-list")) {
        for (const auto& expression : vm["expression-list"].as<std::vector<std::string>>()) {
            parse_and_add_expression(expression);
        }
    }
    if (vm.count("expressions")) {
        read_expressions_file(vm["expressions"].as<std::string>());
    }
    for (const auto type : {osmium::item_type::node, osmium::item_type::way, osmium::item_type::relation}) {
        m_filter_contexts(type) = m_filters(type).make_context();
    }
    m_area_context = m_area_filters.make_context();
    return true;
}

void CommandTagsFilterBool::show_arguments() {
    show_single_input_arguments(m_vout);
    show_output_arguments(m_vout);

    m_vout << "  other options:\n";
    m_vout << "    add referenced objects: " << yes_no(m_add_referenced_objects);
    m_vout << "    invert match: " << yes_no(m_invert_match);
    m_vout << "    include parents: " << yes_no(m_include_parents);
    if (m_include_parents) m_vout << "    maximum parent hops: " << m_parent_hops << '\n';
    if (m_add_referenced_objects) {
        m_vout << "    remove tags on non-matching objects: " << yes_no(m_remove_tags);
    }
    m_vout << "  looking for tags...\n";
    m_vout << "    on nodes: "     << yes_no(!m_filters(osmium::item_type::node).empty());
    m_vout << "    on ways: "      << yes_no(!m_filters(osmium::item_type::way).empty() || !m_area_filters.empty());
    m_vout << "    on relations: " << yes_no(!m_filters(osmium::item_type::relation).empty() || !m_area_filters.empty());
}

osmium::osm_entity_bits::type CommandTagsFilterBool::get_needed_types() const {
    if (m_include_parents) {
        auto types = m_root_ids.types() | m_parent_ids.types();
        for (const auto type : {osmium::item_type::node, osmium::item_type::way, osmium::item_type::relation}) {
            if (!m_referenced_ids(type).empty()) types |= osmium::osm_entity_bits::from_item_type(type);
        }
        return types;
    }
    if (m_invert_match) {
        return osmium::osm_entity_bits::nwr;
    }
    osmium::osm_entity_bits::type types = osmium::osm_entity_bits::nothing;
    if (!m_referenced_ids(osmium::item_type::node).empty() || !m_filters(osmium::item_type::node).empty()) {
        types |= osmium::osm_entity_bits::node;
    }
    if (!m_referenced_ids(osmium::item_type::way).empty() || !m_filters(osmium::item_type::way).empty() || !m_area_filters.empty()) {
        types |= osmium::osm_entity_bits::way;
    }
    if (!m_referenced_ids(osmium::item_type::relation).empty() || !m_filters(osmium::item_type::relation).empty() || !m_area_filters.empty()) {
        types |= osmium::osm_entity_bits::relation;
    }
    return types;
}

bool CommandTagsFilterBool::matches_node(const osmium::Node& node) noexcept {
    return m_filters(osmium::item_type::node).matches(node.tags(), m_filter_contexts(osmium::item_type::node));
}

bool CommandTagsFilterBool::matches_way(const osmium::Way& way) noexcept {
    return m_filters(osmium::item_type::way).matches(way.tags(), m_filter_contexts(osmium::item_type::way)) ||
           (way.nodes().size() >= 4 && way.is_closed() && m_area_filters.matches(way.tags(), m_area_context));
}

namespace {

bool is_multipolygon(const osmium::Relation& relation) noexcept {
    const char* type = relation.tags().get_value_by_key("type");
    return type != nullptr && (!std::strcmp(type, "multipolygon") || !std::strcmp(type, "boundary"));
}

} // anonymous namespace

bool CommandTagsFilterBool::matches_relation(const osmium::Relation& relation) noexcept {
    return m_filters(osmium::item_type::relation).matches(relation.tags(), m_filter_contexts(osmium::item_type::relation)) ||
           (is_multipolygon(relation) && m_area_filters.matches(relation.tags(), m_area_context));
}

bool CommandTagsFilterBool::matches_object(const osmium::OSMObject& object) noexcept {
    switch (object.type()) {
        case osmium::item_type::node:
            return matches_node(static_cast<const osmium::Node&>(object));
        case osmium::item_type::way:
            return matches_way(static_cast<const osmium::Way&>(object));
        case osmium::item_type::relation:
            return matches_relation(static_cast<const osmium::Relation&>(object));
        default:
            break;
    }
    return false;
}

// Reference traversal is adapted from command_tags_filter.cpp. Keep it local
// to this command so upgrading the extension does not modify tags-filter.
void CommandTagsFilterBool::add_nodes(const osmium::Way& way) {
    for (const auto& nr : way.nodes()) {
        m_referenced_ids(osmium::item_type::node).set(nr.positive_ref());
    }
}

void CommandTagsFilterBool::mark_rel_ids(const osmium::index::RelationsMapIndex& rel_in_rel, osmium::unsigned_object_id_type parent_id) {
    rel_in_rel.for_each(parent_id, [&](osmium::unsigned_object_id_type member_id) {
        if (m_referenced_ids(osmium::item_type::relation).check_and_set(member_id)) {
            mark_rel_ids(rel_in_rel, member_id);
        }
    });
}

bool CommandTagsFilterBool::find_relations_in_relations() {
    m_vout << "  Reading input file to find relations in relations...\n";
    osmium::index::RelationsMapStash stash;

    ++m_count_passes;
    osmium::io::Reader reader{m_input_file, osmium::osm_entity_bits::relation};
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (const auto& relation : buffer.select<osmium::Relation>()) {
            stash.add_members(relation);
            if (m_include_parents ? (m_root_ids.contains(relation.type(), relation.id()) || m_parent_ids.contains(relation.type(), relation.id()))
                                  : matches_relation(relation) != m_invert_match) {
                m_matching_ids(osmium::item_type::relation).set(relation.positive_id());
                for (const auto& member : relation.members()) {
                    if (member.type() == osmium::item_type::node) {
                        m_referenced_ids(osmium::item_type::node).set(member.positive_ref());
                    } else if (member.type() == osmium::item_type::way) {
                        m_referenced_ids(osmium::item_type::way).set(member.positive_ref());
                    }
                }
            }
        }
    }
    reader.close();
    if (stash.empty()) {
        return false;
    }

    const auto rel_in_rel = stash.build_parent_to_member_index();
    for (const auto id : m_matching_ids(osmium::item_type::relation)) {
        mark_rel_ids(rel_in_rel, id);
    }
    return true;
}

void CommandTagsFilterBool::find_nodes_and_ways_in_relations() {
    m_vout << "  Reading input file to find nodes/ways in relations...\n";
    ++m_count_passes;
    osmium::io::Reader reader{m_input_file, osmium::osm_entity_bits::relation};
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (const auto& relation : buffer.select<osmium::Relation>()) {
            if (m_referenced_ids(osmium::item_type::relation).get(relation.positive_id())) {
                for (const auto& member : relation.members()) {
                    if (member.type() == osmium::item_type::node) {
                        m_referenced_ids(osmium::item_type::node).set(member.positive_ref());
                    } else if (member.type() == osmium::item_type::way) {
                        m_referenced_ids(osmium::item_type::way).set(member.positive_ref());
                    }
                }
            }
        }
    }
    reader.close();
}

void CommandTagsFilterBool::find_nodes_in_ways() {
    m_vout << "  Reading input file to find nodes in ways...\n";
    ++m_count_passes;
    osmium::io::Reader reader{m_input_file, osmium::osm_entity_bits::way};
    while (osmium::memory::Buffer buffer = reader.read()) {
        for (const auto& way : buffer.select<osmium::Way>()) {
            if (m_include_parents ? (m_root_ids.contains(way.type(), way.id()) || m_parent_ids.contains(way.type(), way.id()))
                                  : matches_way(way) != m_invert_match) {
                m_matching_ids(osmium::item_type::way).set(way.positive_id());
                add_nodes(way);
            } else if (m_referenced_ids(osmium::item_type::way).get(way.positive_id())) {
                add_nodes(way);
            }
        }
    }
    reader.close();
}

void CommandTagsFilterBool::find_referenced_objects() {
    m_vout << "Following references...\n";
    bool todo = m_include_parents ? (m_root_ids.has(osmium::item_type::relation) || m_parent_ids.has(osmium::item_type::relation))
                                 : (!m_filters(osmium::item_type::relation).empty() || !m_area_filters.empty() || m_invert_match);
    if (todo) {
        todo = find_relations_in_relations();
    }
    if (todo) {
        find_nodes_and_ways_in_relations();
    }
    // Inversion can select ways even when no way expressions were supplied.
    const bool ways = m_include_parents ? (m_root_ids.has(osmium::item_type::way) || m_parent_ids.has(osmium::item_type::way))
                                       : (!m_filters(osmium::item_type::way).empty() || !m_area_filters.empty() || m_invert_match);
    if (!m_referenced_ids(osmium::item_type::way).empty() || ways) {
        find_nodes_in_ways();
    }
    m_vout << "Done following references.\n";
}

void CommandTagsFilterBool::find_parent_objects() {
    m_vout << "Collecting original matching IDs for parent lookup...\n";
    ++m_count_passes;
    osmium::io::Reader seeds{m_input_file, osmium::osm_entity_bits::nwr};
    reverse_references::require_snapshot(m_input_file, seeds.header());
    while (osmium::memory::Buffer buffer = seeds.read()) {
        for (const auto& object : buffer.select<osmium::OSMObject>()) {
            if (object.id() <= 0) throw argument_error{"Parent lookup requires positive OSM IDs."};
            if (matches_object(object) != m_invert_match) m_root_ids.add(object.type(), object.id());
        }
    }
    seeds.close();
    reverse_references::Relationships relationships{m_relationships};
    m_vout << "Finding parents of the original matches, up to " << m_parent_hops << " reverse hop(s)...\n";
    auto found = reverse_references::find_parents(m_input_file, m_root_ids, m_parent_filter, m_parent_hops, &relationships);
    m_count_passes += found.passes;
    m_parent_ids = std::move(found.parents);
    relationships.close();
}

void CommandTagsFilterBool::copy_matching_objects(osmium::io::Reader& reader, osmium::io::Writer& writer) {
    m_vout << "Copying matching objects to output file...\n";
    ++m_count_passes;
    osmium::ProgressBar progress_bar{reader.file_size(), display_progress()};
    while (osmium::memory::Buffer buffer = reader.read()) {
        progress_bar.update(reader.offset());
        for (auto& object : buffer.select<osmium::OSMObject>()) {
            if (m_include_parents) {
                if (m_root_ids.contains(object.type(), object.id()) || m_parent_ids.contains(object.type(), object.id())) {
                    writer(object);
                } else if (m_add_referenced_objects && m_referenced_ids(object.type()).get(object.positive_id())) {
                    if (m_remove_tags) object.remove_tags();
                    writer(object);
                }
                continue;
            }
            if (m_matching_ids(object.type()).get(object.positive_id())) {
                writer(object);
            } else if ((!m_add_referenced_objects || object.type() == osmium::item_type::node) && matches_object(object) != m_invert_match) {
                writer(object);
            } else if (m_referenced_ids(object.type()).get(object.positive_id())) {
                if (m_remove_tags) {
                    object.remove_tags();
                }
                writer(object);
            }
        }
    }
    progress_bar.done();
}

bool CommandTagsFilterBool::run() {
    if (m_include_parents) find_parent_objects();
    m_vout << "Opening output file...\n";
    osmium::io::Writer writer{m_output_file, m_output_overwrite, m_fsync};
    if (m_add_referenced_objects) {
        find_referenced_objects();
    }

    m_vout << "Opening input file...\n";
    osmium::io::Reader reader{m_input_file, get_needed_types()};
    osmium::io::Header header{reader.header()};
    setup_header(header);
    writer.set_header(header);
    copy_matching_objects(reader, writer);

    m_vout << "Closing output file...\n";
    writer.close();
    m_vout << "Closing input file...\n";
    reader.close();
    show_memory_used();
    m_vout << "Needed " << m_count_passes << " pass(es) through the input file.\n";
    m_vout << "Done.\n";
    return true;
}
