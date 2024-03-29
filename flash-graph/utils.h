#ifndef __UTILS_H__
#define __UTILS_H__

/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashGraph.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>

#include <vector>
#include <memory>

#include "graph_file_header.h"
#include "FG_basic_types.h"

namespace fg
{

class in_mem_graph;
class vertex_index;
class in_mem_vertex;
class ext_mem_undirected_vertex;
class vertex_index_construct;

namespace utils
{

/**
 * The type of edge data.
 */
enum {
	DEFAULT_TYPE,
	EDGE_COUNT,
	EDGE_TIMESTAMP,
};

class serial_subgraph;

class large_writer
{
public:
    typedef std::shared_ptr<large_writer> ptr;

    virtual ~large_writer() {
    }
    virtual int delete_file() = 0;
    virtual int rename2(const std::string &new_name) = 0;
    virtual off_t seek(off_t off, int whence) = 0;
    virtual ssize_t flush() = 0;
    virtual ssize_t write(const char *buf, size_t bytes) = 0;
    virtual size_t get_write_bytes() const = 0;
};

class large_reader
{
public:
    typedef std::shared_ptr<large_reader> ptr;

    virtual ~large_reader() {
    }
    virtual bool is_safs() = 0;
    virtual ssize_t read(char *buf, size_t bytes) = 0;
    virtual off_t seek(off_t off, int whence) = 0;
};

class large_io_creator
{
public:
    typedef std::shared_ptr<large_io_creator> ptr;
    static ptr create(bool safs, const std::string &curr_dir);

    ~large_io_creator() {
    }
    virtual large_writer::ptr create_writer(const std::string &file) = 0;
    virtual large_reader::ptr create_reader(const std::string &file) = 0;
};

/*
 * This class serializes a graph and stores it in contiguous memory.
 */
class serial_graph
{
	size_t num_edges;
	size_t num_vertices;
	size_t num_non_empty;
	std::shared_ptr<vertex_index_construct> index;
	size_t edge_data_size;
public:
	typedef std::shared_ptr<serial_graph> ptr;

	serial_graph(std::shared_ptr<vertex_index_construct> index,
			size_t edge_data_size) {
		num_edges = 0;
		num_vertices = 0;
		num_non_empty = 0;
		this->index = index;
		this->edge_data_size = edge_data_size;
	}

	virtual ~serial_graph();

	virtual void add_vertex(const in_mem_vertex &v);

	// This needs to be redefined for undirected graphs.
	virtual size_t get_num_edges() const {
		return num_edges;
	}

	size_t get_num_vertices() const {
		return num_vertices;
	}

	bool has_edge_data() const {
		return edge_data_size > 0;
	}

	size_t get_edge_data_size() const {
		return edge_data_size;
	}

	size_t get_num_non_empty_vertices() const {
		return num_non_empty;
	}

	vertex_index_construct &get_index() {
		return *index;
	}

	std::shared_ptr<vertex_index> dump_index(bool compressed) const;

	virtual void finalize_graph_file() {
	}

	virtual graph_type get_graph_type() const = 0;
	virtual void add_vertices(const serial_subgraph &subg) = 0;

	virtual std::shared_ptr<in_mem_graph> dump_graph(
			const std::string &graph_name) = 0;
};

/*
 * The interface defines a graph represented by edges.
 */
class edge_graph
{
	size_t edge_data_size;
public:
	typedef std::shared_ptr<edge_graph> ptr;

	edge_graph(size_t edge_data_size) {
		this->edge_data_size = edge_data_size;
	}

	virtual ~edge_graph() {
	}

	virtual void sort_edges() = 0;
	virtual void check_vertices(
			const std::vector<ext_mem_undirected_vertex *> &vertices,
			bool in_part, std::vector<off_t> &edge_offs) const = 0;
	virtual size_t get_num_edges() const = 0;
    virtual std::shared_ptr<serial_graph> serialize_graph(
            large_io_creator::ptr creator) const = 0;

	bool has_edge_data() const {
		return edge_data_size > 0;
	}

	size_t get_edge_data_size() const {
		return edge_data_size;
	}
};

/*
 * This interface serializes a graph onto disks.
 */
class disk_serial_graph: public serial_graph
{
    large_io_creator::ptr creator;
public:
    typedef std::shared_ptr<disk_serial_graph> ptr;

    disk_serial_graph(std::shared_ptr<vertex_index_construct> index,
            size_t edge_data_size, large_io_creator::ptr creator): serial_graph(
                index, edge_data_size) {
        this->creator = creator;
    }

    large_io_creator::ptr get_creator() {
        return creator;
    }

    virtual void check_ext_graph(const edge_graph &edge_g,
            const std::string &index_file, large_reader::ptr reader) const = 0;
    bool dump(const std::string &index_file, const std::string &graph_file,
            bool compressed_index);
    virtual bool name_graph_file(const std::string &adj_file) = 0;

    std::shared_ptr<in_mem_graph> dump_graph(
            const std::string &graph_name) {
        return std::shared_ptr<in_mem_graph>();
    }
};

/*
 * This interface serializes a graph into a single piece of memory.
 */
class mem_serial_graph: public serial_graph
{
protected:
	mem_serial_graph(std::shared_ptr<vertex_index_construct> index,
			size_t edge_data_size): serial_graph(index, edge_data_size) {
	}
public:
	typedef std::shared_ptr<mem_serial_graph> ptr;
	static ptr create(bool directed, size_t edge_data_size);
	virtual void add_empty_vertex(vertex_id_t id) = 0;
};

/*
 * Set parameters of constructing a graph.
 */

void set_num_threads(size_t num_threads);
void set_sort_buf_size(size_t size);
void set_write_buf_size(size_t size);

edge_graph::ptr parse_edge_lists(const std::vector<std::string> &edge_list_files,
		int edge_attr_type, bool directed, bool in_mem);
serial_graph::ptr construct_graph(edge_graph::ptr edge_g,
		large_io_creator::ptr creator);
}

}

#endif
