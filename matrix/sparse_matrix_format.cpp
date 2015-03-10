/*
 * Copyright 2014 Open Connectome Project (http://openconnecto.me)
 * Written by Da Zheng (zhengda1936@gmail.com)
 *
 * This file is part of FlashMatrix.
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

#include <boost/format.hpp>

#include "log.h"
#include "native_file.h"

#include "sparse_matrix_format.h"
#include "matrix_config.h"

namespace fm
{

void sparse_block_2d::verify(const block_2d_size &block_size) const
{
	row_part_iterator it = get_iterator();
	size_t rel_row_id = 0;
	size_t num_rows = 0;
	while (it.has_next()) {
		const sparse_row_part &part = it.next();
		assert(part.get_num_non_zeros() <= block_size.get_num_cols());
		if (part.get_rel_row_idx() > 0)
			assert(rel_row_id < part.get_rel_row_idx());
		rel_row_id = part.get_rel_row_idx();
		num_rows++;
	}
	assert(num_rows <= block_size.get_num_rows());
}

void sparse_block_2d::append(const sparse_row_part &part)
{
	char *end = row_parts + rparts_size;
	memcpy(end, &part, part.get_size());
	assert(((size_t) rparts_size) + part.get_size()
			<= std::numeric_limits<uint32_t>::max());
	rparts_size += part.get_size();
}

sparse_matrix_index::ptr sparse_matrix_index::create(const matrix_header &header,
		const std::vector<off_t> &offs)
{
	block_2d_size block_size = header.get_2d_block_size();
	if (offs.size() != block_size.cal_num_block_rows(header.get_num_rows()) + 1) {
		BOOST_LOG_TRIVIAL(error) << "There are an incorrect number of offsets";
		return sparse_matrix_index::ptr();
	}
	void *buf = NULL;
	int ret = posix_memalign(&buf, PAGE_SIZE, get_size(offs.size()));
	if (ret) {
		BOOST_LOG_TRIVIAL(error) << "Can't allocate memory";
		return sparse_matrix_index::ptr();
	}
	sparse_matrix_index *idx = new (buf) sparse_matrix_index(header);
	for (size_t i = 0; i < offs.size(); i++) 
		idx->offs[i] = offs[i];
	return sparse_matrix_index::ptr(idx, deleter());
}

size_t sparse_matrix_index::get_num_block_rows() const
{
	block_2d_size block_size = header.get_2d_block_size();
	return block_size.cal_num_block_rows(header.get_num_rows());
}

void sparse_matrix_index::dump(const std::string &file) const
{
	FILE *f = fopen(file.c_str(), "w");
	if (f == NULL) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't open %1%: %2%")
			% file % strerror(errno);
		return;
	}

	size_t ret = fwrite(this, get_size(get_num_entries()), 1, f);
	if (ret == 0) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't write to %1%: %2%")
			% file % strerror(errno);
		fclose(f);
		return;
	}
	fclose(f);
}

off_t sparse_matrix_index::get_block_row_off(size_t idx) const
{
	if (idx >= get_num_entries()) {
		BOOST_LOG_TRIVIAL(error) << "get_block_row_off: out of range.";
		return -1;
	}
	return offs[idx];
}

sparse_matrix_index::ptr sparse_matrix_index::load(const std::string &idx_file)
{
	size_t size = safs::native_file(idx_file).get_size();
	char *data = (char *) malloc(size);
	FILE *f = fopen(idx_file.c_str(), "r");
	if (f == NULL) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't open %1%: %2%")
			% idx_file % strerror(errno);
		return sparse_matrix_index::ptr();
	}
	size_t ret = fread(data, size, 1, f);
	if (ret == 0) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't read %1%: %2%")
			% idx_file % strerror(errno);
		fclose(f);
		return sparse_matrix_index::ptr();
	}

	fclose(f);
	return ptr((sparse_matrix_index *) data, deleter());
}

void sparse_matrix_storage::verify() const
{
	block_2d_size block_size = index->get_header().get_2d_block_size();
	for (size_t i = 0; i < get_num_block_rows(); i++) {
		block_row_iterator brow_it = get_block_row_it(i);
		while (brow_it.has_next()) {
			const sparse_block_2d &block = brow_it.next();
			block.verify(block_size);
		}
	}
}

sparse_matrix_storage::ptr sparse_matrix_storage::load(const std::string &mat_file,
			sparse_matrix_index::ptr index)
{
	size_t size = safs::native_file(mat_file).get_size();
	char *data = new char[size];
	FILE *f = fopen(mat_file.c_str(), "r");
	if (f == NULL) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't open %1%: %2%")
			% mat_file % strerror(errno);
		return sparse_matrix_storage::ptr();
	}
	size_t ret = fread(data, size, 1, f);
	if (ret == 0) {
		BOOST_LOG_TRIVIAL(error) << boost::format("can't read %1%: %2%")
			% mat_file % strerror(errno);
		fclose(f);
		return sparse_matrix_storage::ptr();
	}
	fclose(f);
	return ptr(new sparse_matrix_storage(std::unique_ptr<char[]>(data), index));
}

}
