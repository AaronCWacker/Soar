#ifndef MAT_H
#define MAT_H

#include <iostream>
#include <vector>
#include "serializable.h"

/*
 By default Eigen will try to align all fixed size vectors to 128-bit
 boundaries to enable SIMD instructions on hardware such as SSE. However,
 this requires that you modify every class that has such vectors as
 members so that they are correctly allocated. This seems like more
 trouble than it's worth at the moment, so I'm disabling it.
*/
#define EIGEN_DONT_ALIGN
#include <Eigen/Dense>
//#include <Eigen/src/plugins/BlockMethods.h>
#include <Eigen/StdVector>

typedef Eigen::Vector3d vec3;
typedef Eigen::Vector4d vec4;
typedef std::vector<vec3> ptlist;

typedef Eigen::RowVectorXd rvec;
typedef Eigen::VectorXd cvec;
typedef Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> mat;

/*
 Don't try to understand what these mean, just know that you
 can treat them like regular matrices.
*/

typedef Eigen::Block<      mat, Eigen::Dynamic, Eigen::Dynamic, true,  true> mat_iblock; // i means inner_panel template arg = true
typedef Eigen::Block<      mat, Eigen::Dynamic, Eigen::Dynamic, false, true> mat_block;
typedef Eigen::Block<      mat, 1,              Eigen::Dynamic, false, true> row_block;
typedef Eigen::Block<      mat, Eigen::Dynamic, 1,              false, true> col_block;
typedef Eigen::Block<const mat, Eigen::Dynamic, Eigen::Dynamic, true,  true> const_mat_iblock;
typedef Eigen::Block<const mat, Eigen::Dynamic, Eigen::Dynamic, false, true> const_mat_block;
typedef Eigen::Block<const mat, 1,              Eigen::Dynamic, false, true> const_row_block;
typedef Eigen::Block<const mat, Eigen::Dynamic, 1,              false, true> const_col_block;

typedef Eigen::Stride<Eigen::Dynamic, 1> mat_stride;
typedef Eigen::Map<mat, Eigen::Unaligned, mat_stride> mat_map;
typedef Eigen::Map<const mat, Eigen::Unaligned, mat_stride> const_mat_map;

typedef Eigen::Block<      mat_map, Eigen::Dynamic, Eigen::Dynamic, false, true> map_block;
typedef Eigen::Block<const_mat_map, Eigen::Dynamic, Eigen::Dynamic, true,  true> const_map_iblock;
typedef Eigen::Block<const_mat_map, Eigen::Dynamic, Eigen::Dynamic, false, true> const_map_block;
typedef Eigen::Block<const_mat_map, Eigen::Dynamic, 1,              false, true> const_map_col_block;
typedef Eigen::Block<const_mat_map, 1,              Eigen::Dynamic, false, true> const_map_row_block;

/*
 If you define a function argument as "const mat &" and a block or a
 map is passed into it, the compiler will implicitly create a copy of
 the matrix underlying the block or map. Using mat_views avoids this
 problem, because they have the appropriate constructors for handling
 any type of object.
*/
class mat_view : public mat_map {
public:
	mat_view(mat &m)               : mat_map(m.data(), m.rows(), m.cols(), mat_stride(m.rowStride(), 1)) {}
	mat_view(mat &m, int r, int c) : mat_map(m.data(), r, c, mat_stride(m.rowStride(), 1)) {}
	mat_view(const mat_view &m)    : mat_map(m) {}
};

class const_mat_view : public const_mat_map {
public:
	const_mat_view(const const_mat_view &m)      : const_mat_map(m) {}
	
	// standard types
	const_mat_view(const mat &m)                 : const_mat_map(m.data(), m.rows(), m.cols(), mat_stride(m.rowStride(), 1)) {}
	const_mat_view(const rvec &v)                : const_mat_map(v.data(), 1, v.size(), mat_stride(1, 1)) {}
	const_mat_view(const cvec &v)                : const_mat_map(v.data(), v.size(), 1, mat_stride(1, 1)) {}
	const_mat_view(const mat &m, int r, int c)   : const_mat_map(m.data(), r, c, mat_stride(m.rowStride(), 1)) {}
	
	// for things like m.block
	const_mat_view(const mat_block &b)           : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const mat_iblock &b)          : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_mat_block &b)     : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_mat_iblock &b)    : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	
	// for things like m.row and m.col
	const_mat_view(const row_block &b)           : const_mat_map(b.data(), 1,        b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_row_block &b)     : const_mat_map(b.data(), 1,        b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const col_block &b)           : const_mat_map(b.data(), b.rows(), 1,        mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_col_block &b)     : const_mat_map(b.data(), b.rows(), 1,        mat_stride(b.rowStride(), 1)) {}
	
	const_mat_view(const const_mat_map &m)       : const_mat_map(m) {}
	const_mat_view(const mat_map &m)             : const_mat_map(m.data(), m.rows(), m.cols(), mat_stride(m.rowStride(), 1)) {}
	const_mat_view(const map_block &b)           : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_map_block &b)     : const_mat_map(b.data(), b.rows(), b.cols(), mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_map_col_block &b) : const_mat_map(b.data(), b.rows(), 1,        mat_stride(b.rowStride(), 1)) {}
	const_mat_view(const const_map_row_block &b) : const_mat_map(b.data(), 1,        b.cols(), mat_stride(b.rowStride(), 1)) {}
};


/*
 A matrix that can be efficiently dynamically resized. Uses a doubling
 memory allocation policy.
*/
class dyn_mat : public serializable {
public:
	dyn_mat();
	dyn_mat(int nrows, int ncols);
	dyn_mat(int nrows, int ncols, int init_row_capacity, int init_col_capacity);
	dyn_mat(const dyn_mat &other);
	dyn_mat(const_mat_view m);
	
	void resize(int nrows, int ncols);
	void append_row();
	void append_row(const rvec &row);
	void insert_row(int i);
	void insert_row(int i, const rvec &row);
	void remove_row(int i);
	void append_col();
	void append_col(const cvec &col);
	void insert_col(int i);
	void insert_col(int i, const cvec &col);
	void remove_col(int i);
	
	void serialize(std::ostream &os) const;
	void unserialize(std::istream &is);
	
	double &operator()(int i, int j) {
		assert(!released && 0 <= i && i < r && 0 <= j && j < c);
		return buf(i, j);
	}
	
	double operator()(int i, int j) const {
		assert(!released && 0 <= i && i < r && 0 <= j && j < c);
		return buf(i, j);
	}
	
	mat::RowXpr row(int i) {
		assert(!released && 0 <= i && i < r);
		return buf.row(i);
	}
	
	mat::ConstRowXpr row(int i) const {
		assert(!released && 0 <= i && i < r);
		return buf.row(i);
	}
	
	mat::ColXpr col(int j) {
		assert(!released && 0 <= j && j < c);
		return buf.col(j);
	}
	
	mat::ConstColXpr col(int j) const {
		assert(!released && 0 <= j && j < c);
		return buf.col(j);
	}
	
	mat_view get() {
		assert(!released);
		return mat_view(buf, r, c);
	}
	
	const_mat_view get() const {
		assert(!released);
		return const_mat_view(buf, r, c);
	}
	
	inline int rows() const {
		assert(!released);
		return r;
	}
	
	inline int cols() const {
		assert(!released);
		return c;
	}
	
	/*
	 The dyn_mat should no longer be used after the internal matrix is
	 released. This function is useful for avoiding redundant copying.
	*/
	mat &release() {
		assert(!released);
		released = true;
		buf.conservativeResize(r, c);
		return buf;
	}

public:
	mat buf;
	int r, c;
	bool released;
};

std::ostream& output_rvec(std::ostream &os, const rvec &v, const std::string &sep = " ");
std::ostream& output_cvec(std::ostream &os, const cvec &v, const std::string &sep = " ");
std::ostream& output_mat(std::ostream &os, const const_mat_view m);

bool normal(const_mat_view m);
bool uniform(const_mat_view X);
void randomize_vec(rvec &v, const rvec &min, const rvec &max);

/*
 Return indices of columns that have significantly different values,
 meaning the maximum absolute value of the column is greater than
 SAME_THRESH times the minimum absolute value.
*/
void get_nonuniform_cols(const_mat_view X, int ncols, std::vector<int> &cols);

/*
 Remove the static columns from the first 'ncols' columns of X. This
 will not resize the matrix. Upon completion, the cols vector will
 contain the original column indexes that were not removed.
*/
void del_uniform_cols(mat_view X, int ncols, std::vector<int> &cols);

void pick_cols(const_mat_view X, const std::vector<int> &cols, mat &result);
void pick_rows(const_mat_view X, const std::vector<int> &rows, mat &result);
void pick_cols(mat_view X, const std::vector<int> &cols);
void pick_rows(mat_view X, const std::vector<int> &rows);

/*
 Calculate the maximum difference between points in two point clouds in
 the direction of u.
 
  a         b
  .<-- d -->.        returns a positive d
 --------------> u
 
  b         a
  .<-- d -->.        returns a negative d
 --------------> u
*/
double dir_separation(const ptlist &a, const ptlist &b, const vec3 &u);

class bbox {
public:
	bbox() {
		min.setZero();
		max.setZero();
	}
	
	/* bounding box around single point */
	bbox(const vec3 &v) {
		min = v;
		max = v;
	}
	
	bbox(const ptlist &pts) {
		if (pts.size() == 0) {
			min.setZero();
			max.setZero();
		} else {
			min = pts[0];
			max = pts[0];
		
			for(int i = 1; i < pts.size(); ++i) {
				include(pts[i]);
			}
		}
	}
	
	bbox(const vec3 &min, const vec3 &max) : min(min), max(max) {}
	
	void include(const vec3 &v) {
		for(int d = 0; d < 3; ++d) {
			if (v[d] < min[d]) { min[d] = v[d]; }
			if (v[d] > max[d]) { max[d] = v[d]; }
		}
	}
	
	void include(const ptlist &pts) {
		ptlist::const_iterator i;
		for(i = pts.begin(); i != pts.end(); ++i) {
			include(*i);
		}
	}
	
	void include(const bbox &b) {
		include(b.min);
		include(b.max);
	}
	
	bool intersects(const bbox &b) const {
		int d;
		for (d = 0; d < 3; ++d) {
			if (max[d] < b.min[d] || min[d] > b.max[d]) {
				return false;
			}
		}
		return true;
	}
	
	bool contains(const bbox &b) const {
		int d;
		for (d = 0; d < 3; ++d) {
			if (max[d] < b.max[d] || min[d] > b.min[d]) {
				return false;
			}
		}
		return true;
	}
	
	void get_vals(vec3 &minv, vec3 &maxv) const
	{
		minv = min; maxv = max;
	}
	
	bool operator==(const bbox &b) const {
		return min == b.min && max == b.max;
	}
	
	bool operator!=(const bbox &b) const {
		return min != b.min || max != b.max;
	}
	
	bbox &operator=(const bbox &b) {
		min = b.min;
		max = b.max;
		return *this;
	}
	
	void reset() {
		min.setZero();
		max.setZero();
	}
	
	vec3 get_centroid() const {
		return (max + min) / 2.0;
	}
	
	void get_points(ptlist &p) const {
		p.push_back(vec3(min[0], min[1], min[2]));
		p.push_back(vec3(min[0], min[1], max[2]));
		p.push_back(vec3(min[0], max[1], min[2]));
		p.push_back(vec3(min[0], max[1], max[2]));
		p.push_back(vec3(max[0], min[1], min[2]));
		p.push_back(vec3(max[0], min[1], max[2]));
		p.push_back(vec3(max[0], max[1], min[2]));
		p.push_back(vec3(max[0], max[1], max[2]));
	}
	
	friend std::ostream& operator<<(std::ostream &os, const bbox &b);
	
private:
	vec3 min;
	vec3 max;
};

std::ostream& operator<<(std::ostream &os, const bbox &b);

class transform3 {
public:
	transform3() {}
	
	transform3(const transform3 &t) : trans(t.trans) {}
	
	transform3(char type, const vec3 &v) {
		switch(type) {
			case 'p':
				trans = Eigen::Translation<double, 3>(v);
				break;
			case 'r':
				trans = Eigen::AngleAxisd(v(2), Eigen::Vector3d::UnitZ()) *
				        Eigen::AngleAxisd(v(1), Eigen::Vector3d::UnitY()) *
				        Eigen::AngleAxisd(v(0), Eigen::Vector3d::UnitX()) ;
				break;
			case 's':
				trans = Eigen::Scaling(v);
				break;
			default:
				assert(false);
		}
	}
	
	transform3(const vec3 &p, const vec3 &r, const vec3 &s) {
		trans = Eigen::Translation<double, 3>(p) *
		        Eigen::AngleAxisd(r(2), Eigen::Vector3d::UnitZ()) *
		        Eigen::AngleAxisd(r(1), Eigen::Vector3d::UnitY()) *
		        Eigen::AngleAxisd(r(0), Eigen::Vector3d::UnitX()) *
		        Eigen::Scaling(s);
	}
	
	vec3 operator()(const vec3 &v) const {
		return trans * v;
	}
	
	transform3 operator*(const transform3 &t) const {
		transform3 r;
		r.trans = trans * t.trans;
		return r;
	}
	
	void get_matrix(mat &m) const {
		m = trans.matrix();
	}
	
private:
	Eigen::Transform<double, 3, Eigen::Affine> trans;
};

#endif
