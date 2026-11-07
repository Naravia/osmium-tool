// SPDX-License-Identifier: GPL-3.0-or-later
#include "command_get_parents_extended.hpp"

#include "exception.hpp"
#include "util.hpp"

#include <osmium/io/reader.hpp>
#include <osmium/io/writer.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/types_from_string.hpp>
#include <osmium/util/progress_bar.hpp>
#include <osmium/util/string.hpp>

#include <boost/program_options.hpp>

#include <fstream>
#include <iostream>
#include <sstream>

bool CommandGetParentsExtended::setup(const std::vector<std::string>& arguments) {
    po::options_description opts{"COMMAND OPTIONS"};
    opts.add_options()
    ("default-type", po::value<std::string>()->default_value("node"), "Type of IDs without a prefix")
    ("id-file,i", po::value<std::vector<std::string>>()->composing(), "Read typed IDs from text file")
    ("id-osm-file,I", po::value<std::vector<std::string>>()->composing(), "Read seed IDs from OSM file")
    ("add-self,s", "Also include the seeds themselves from the source")
    ("parents-expression", po::value<std::vector<std::string>>()->composing(), "Boolean predicate on parents (repeatable, ORed)")
    ("parent-relationships", po::value<std::string>(), "Write occurrence-level child/parent TSV")
    ;
    po::options_description hidden;
    hidden.add_options()
    ("input-filename", po::value<std::string>(), "OSM input")
    ("ids", po::value<std::vector<std::string>>(), "Typed seed IDs")
    ;
    po::options_description desc;
    desc.add(opts).add(add_common_options()).add(add_single_input_options()).add(add_output_options());
    po::options_description all;
    all.add(desc).add(hidden);
    po::positional_options_description positional;
    positional.add("input-filename", 1).add("ids", -1);
    po::variables_map vm;
    po::store(po::command_line_parser(arguments).options(all).positional(positional).run(), vm);
    po::notify(vm);
    if (!setup_common(vm, desc)) {
        return false;
    }
    setup_progress(vm);
    setup_input_file(vm);
    setup_output_file(vm);
    m_add_self = vm.count("add-self") != 0;
    if (!vm.count("ids") && !vm.count("id-file") && !vm.count("id-osm-file")) {
        throw argument_error{"Specify seed IDs, --id-file, or --id-osm-file."};
    }
    if (vm.count("parents-expression")) {
        m_parent_filter.setup(vm["parents-expression"].as<std::vector<std::string>>());
    }
    osmium::item_type default_type = osmium::item_type::undefined;
    const auto& type = vm["default-type"].as<std::string>();
    if (type == "node" || type == "n") default_type = osmium::item_type::node;
    if (type == "way" || type == "w") default_type = osmium::item_type::way;
    if (type == "relation" || type == "r") default_type = osmium::item_type::relation;
    if (default_type == osmium::item_type::undefined) {
        throw argument_error{"Default type must be node, way, or relation."};
    }
    auto add_id = [&](const std::string& text) {
        try {
            const auto id = osmium::string_to_object_id(text.c_str(), osmium::osm_entity_bits::nwr, default_type);
            m_seeds.add(id.first, id.second);
        } catch (const std::range_error& error) {
            throw argument_error{error.what()};
        }
    };
    std::vector<std::string> protected_files{m_input_filename};
    for (const auto* option : {"id-file", "id-osm-file"}) {
        if (vm.count(option)) {
            const auto& files = vm[option].as<std::vector<std::string>>();
            protected_files.insert(protected_files.end(), files.begin(), files.end());
        }
    }
    reverse_references::check_distinct_output(m_output_filename, protected_files);
    protected_files.push_back(m_output_filename);
    if (vm.count("parent-relationships")) {
        m_relationships = vm["parent-relationships"].as<std::string>();
        if (m_relationships.empty()) throw argument_error{"Parent relationship filename must not be empty."};
        reverse_references::check_relationship_path(m_relationships, protected_files, m_output_overwrite == osmium::io::overwrite::allow);
    }
    if (vm.count("ids")) {
        for (const auto& text : vm["ids"].as<std::vector<std::string>>()) {
            for (const auto& id : osmium::split_string(text, "\t ;,/|", true)) add_id(id);
        }
    }
    if (vm.count("id-file")) {
        auto read = [&](std::istream& input) {
            for (std::string line; std::getline(input, line);) {
                line.erase(line.find('#') == std::string::npos ? line.size() : line.find('#'));
                std::istringstream words{line};
                std::string id;
                if (words >> id) add_id(id);
            }
            if (input.bad()) throw argument_error{"Could not read seed ID file."};
        };
        for (const auto& file : vm["id-file"].as<std::vector<std::string>>()) {
            if (file == "-") {
                if (m_input_filename == "-") throw argument_error{"Can not read both source and seed IDs from STDIN."};
                read(std::cin);
            } else {
                std::ifstream input{file};
                if (!input) throw argument_error{"Could not open seed ID file '" + file + "'"};
                read(input);
            }
        }
    }
    if (vm.count("id-osm-file")) {
        for (const auto& file : vm["id-osm-file"].as<std::vector<std::string>>()) {
            if (file == "-") throw argument_error{"OSM seed input must be a named file; use --id-file for text STDIN."};
            reverse_references::read_seed_file(file, m_seeds);
        }
    }
    return true;
}

void CommandGetParentsExtended::show_arguments() {
    show_single_input_arguments(m_vout);
    show_output_arguments(m_vout);
    m_vout << "  add self: " << yes_no(m_add_self);
    for (const auto type : {osmium::item_type::node, osmium::item_type::way, osmium::item_type::relation}) {
        m_vout << "  seed " << osmium::item_type_to_name(type) << " IDs: " << m_seeds.size(type) << '\n';
    }
}

bool CommandGetParentsExtended::run() {
    reverse_references::ParentLookup lookup{m_seeds, m_parent_filter};
    auto types = lookup.types();
    if (m_add_self) types |= m_seeds.types();
    osmium::io::Reader reader{m_input_file, types};
    auto header = reader.header();
    reverse_references::require_snapshot(m_input_file, header);
    setup_header(header);
    osmium::io::Writer writer{m_output_file, header, m_output_overwrite, m_fsync};
    reverse_references::Relationships relationships{m_relationships};
    osmium::ProgressBar progress{reader.file_size(), display_progress()};
    while (osmium::memory::Buffer buffer = reader.read()) {
        progress.update(reader.offset());
        for (const auto& object : buffer.select<osmium::OSMObject>()) {
            const bool parent = lookup.matches(object, &relationships);
            if (parent || (m_add_self && m_seeds.contains(object.type(), object.id()))) writer(object);
        }
    }
    progress.done();
    relationships.close();
    writer.close();
    reader.close();
    show_memory_used();
    return true;
}
