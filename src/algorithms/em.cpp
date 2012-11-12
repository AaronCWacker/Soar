#include <iostream>
#include <cstdlib>
#include <cassert>
#include <cmath>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <limits>
#include <iomanip>
#include <memory>
#include "linear.h"
#include "em.h"
#include "common.h"
#include "params.h"
#include "mat.h"
#include "serialize.h"
#include "lda.h"

using namespace std;
using namespace Eigen;

const regression_type REGRESSION_ALG = FORWARD;

/*
 Generate all possible combinations of sets of items
*/
template <typename T>
class multi_combination_generator {
public:
	multi_combination_generator(const std::vector<std::vector<T> > &elems, bool allow_repeat)
	: elems(elems), indices(elems.size(), 0), allow_repeat(allow_repeat), finished(false)
	{
		empty = false;
		if (elems.empty()) {
			empty = true;
		} else {
			for (int i = 0; i < elems.size(); ++i) {
				if (elems[i].empty()) {
					empty = true;
					break;
				}
			}
		}
	}

	void reset() {
		finished = false;
		fill(indices.begin(), indices.end(), 0);
	}

	bool next(std::vector<T> &comb) {
		if (empty) {
			return false;
		}
		
		comb.resize(elems.size());
		std::set<int> s;
		while (!finished) {
			bool has_repeat = false;
			s.clear();
			for (int i = 0; i < elems.size(); ++i) {
				comb[i] = elems[i][indices[i]];
				if (!allow_repeat) {
					std::pair<std::set<int>::iterator, bool> p = s.insert(comb[i]);
					if (!p.second) {
						has_repeat = true;
						break;
					}
				}
			}
			increment(0);
			if (allow_repeat || !has_repeat) {
				return true;
			}
		}
		return false;
	}

private:
	void increment(int i) {
		if (i >= elems.size()) {
			finished = true;
		} else if (++indices[i] >= elems[i].size()) {
			indices[i] = 0;
			increment(i + 1);
		}
	}

	const std::vector<std::vector<T> > &elems;
	std::vector<int> indices;
	bool allow_repeat, finished, empty;
};

void read_til_semi(istream &is, vector<double> &buf) {
	string s;
	while (true) {
		double x;
		is >> s;
		if (s == ";") {
			return;
		}
		if (!parse_double(s, x)) {
			assert(false);
		}
		buf.push_back(x);
	}
}

/* Box-Muller method */
double randgauss(double mean, double std) {
	double x1, x2, w;
	do {
		x1 = 2.0 * ((double) rand()) / RAND_MAX - 1.0;
		x2 = 2.0 * ((double) rand()) / RAND_MAX - 1.0;
		w = x1 * x1 + x2 * x2;
	} while (w >= 1.0);
	
	w = sqrt((-2.0 * log(w)) / w);
	return mean + std * (x1 * w);
}

void kernel1(const cvec &d, cvec &w) {
	w.resize(d.size());
	for (int i = 0; i < d.size(); ++i) {
		w(i) = exp(-d(i));
	}
}

void kernel2(const cvec &d, cvec &w, double p) {
	const double maxw = 1.0e9;
	w.resize(d.size());
	for (int i = 0; i < d.size(); ++i) {
		if (d(i) == 0.0) {
			w(i) = maxw;
		} else {
			w(i) = min(maxw, pow(d(i), p));
		}
	}
}

void predict(const mat &C, const rvec &intercepts, const rvec &x, rvec &y) {
	y = (x * C) + intercepts;
}

/*
 Upon return, X and Y will contain the training data, Xtest and Ytest the
 testing data.
*/
void split_data(
	const_mat_view X,
	const_mat_view Y,
	const vector<int> &use,
	int ntest,
	mat &Xtrain, mat &Xtest, 
	mat &Ytrain, mat &Ytest)
{
	int ntrain = use.size() - ntest;
	vector<int> test;
	sample(ntest, 0, use.size(), test);
	sort(test.begin(), test.end());
	
	int train_end = 0, test_end = 0, i = 0;
	for (int j = 0; j < use.size(); ++j) {
		if (j == test[i]) {
			Xtest.row(test_end) = X.row(use[j]);
			Ytest.row(test_end) = Y.row(use[j]);
			++test_end;
			++i;
		} else {
			Xtrain.row(train_end) = X.row(use[j]);
			Ytrain.row(train_end) = Y.row(use[j]);
			++train_end;
		}
	}
	assert(test_end == ntest && train_end == ntrain);
}

/*
 Assume that data from a single mode comes in blocks. Try to discover
 a mode by randomly fitting a line to a block of data and then finding
 all data that fit the line.
*/
void EM::find_linear_subset_block(const_mat_view X, const_mat_view Y, vector<int> &subset) const {
	function_timer t(timers.get_or_add("block_subset"));
	
	int xcols = X.cols();
	int rank = xcols + 1;
	int ndata = X.rows();
	mat Xb(rank, xcols), Yb(xcols+1, 1), coefs;
	
	int start = rand() % (ndata - rank);
	for (int i = 0; i < rank; ++i) {
		Xb.row(i) = X.row(start + i);
		Yb.row(i) = Y.row(start + i);
	}
	linreg_clean(FORWARD, Xb, Yb, coefs);

	cvec errors = (Y - (X * coefs)).col(0).array().abs();
	subset.clear();
	for (int i = 0; i < ndata; ++i) {
		if (errors(i) < MODEL_ERROR_THRESH) {
			subset.push_back(i);
		}
	}
}


/*
 Use a simple version of EM to discover a mode in noise data. This
 method works better than block_seed when data from a single mode
 doesn't come in contiguous blocks.
 
 The algorithm is:
 
 1. If input X has m non-static columns, assume it has rank = m + 1.
 2. Randomly choose 'rank' training points as the seed members for the linear
    function. Fit the function to the seed members.
 3. Compute the residuals of the function for training data. Compute a weight
    vector based on the residuals and a kernel.
 4. Refit the linear function biased based on the weight vector. Repeat until
    convergence or the function fits at least n data points.
*/
void EM::find_linear_subset_em(const_mat_view X, const_mat_view Y, vector<int> &subset) const {
	function_timer t(timers.get_or_add("em_block"));
	
	int ndata = X.rows(), xcols = X.cols();
	vector<int> init;
	cvec w(ndata), error(ndata), old_error(ndata);
	mat Xc(ndata, xcols), Yc(ndata, 1), coefs(xcols, 1);
	
	sample(xcols + 1, 0, ndata, init);
	w.setConstant(0.0);
	for (int i = 0; i < init.size(); ++i) {
		w(init[i]) = 1.0;
	}

	for (int iter = 0; iter < MINI_EM_MAX_ITERS; ++iter) {
		for (int i = 0; i < xcols; ++i) {
			Xc.col(i) = X.col(i).array() * w.array();
		}
		Yc.col(0) = Y.col(0).array() * w.array();
		if (!linreg_clean(OLS, Xc, Yc, coefs)) {
			assert(false);
		}
		
		old_error = error;
		error = (Y - (X * coefs)).col(0).array().abs();
		if (iter > 0 && (error - old_error).norm() / ndata < SAME_THRESH) {
			break;
		}
		kernel2(error, w, -3.0);
	}
	for (int i = 0; i < ndata; ++i) {
		if (error(i) < MODEL_ERROR_THRESH) {
			subset.push_back(i);
		}
	}
}

void erase_inds(vector<int> &v, const vector<int> &inds) {
	int i = 0, j = 0;
	for (int k = 0; k < v.size(); ++k) {
		if (i < inds.size() && k == inds[i]) {
			++i;
		} else {
			if (j < k) {
				v[j] = v[k];
			}
			++j;
		}
	}
	assert(i == inds.size() && j == v.size() - inds.size());
	v.resize(j);
}

int EM::find_linear_subset(mat &X, mat &Y, vector<int> &subset) const {
	function_timer t(timers.get_or_add("find_seed"));
	
	const double TEST_RATIO = 0.5;
	int largest = 0;
	
	// preprocess the data as much as possible
	vector<int> used_cols;
	clean_lr_data(X, used_cols);
	augment_ones(X);

	int ndata = X.rows(), xcols = X.cols();
	mat Xtrain(ndata, xcols), Xtest(ndata, xcols);
	mat Ytrain(ndata, 1), Ytest(ndata, 1);
	mat coefs(xcols, 1);

	vector<int> ungrouped(ndata), work;
	for (int i = 0; i < ndata; ++i) {
		ungrouped[i] = i;
	}
	
	/*
	 Outer loop ranges over sets of random initial points
	*/
	for (int iter = 0; iter < LINEAR_SUBSET_MAX_ITERS; ++iter) {
		vector<int> subset2;
		find_linear_subset_em(X.topRows(ndata), Y.topRows(ndata), subset2);
		if (subset2.size() < xcols * 2) {
			continue;
		}
		int ntest = subset2.size() * TEST_RATIO;
		int ntrain = subset2.size() - ntest;
		split_data(X, Y, subset2, ntest, Xtrain, Xtest, Ytrain, Ytest);
		if (!linreg_clean(FORWARD, Xtrain.topRows(ntrain), Ytrain.topRows(ntrain), coefs)) {
			continue;
		}
		cvec test_error = (Ytest.topRows(ntest) - (Xtest.topRows(ntest) * coefs)).col(0).array().abs();
		if (test_error.norm() / Xtest.rows() > MODEL_ERROR_THRESH) {
			/*
			 There isn't a clear linear relationship between the points, so I can't
			 consider them a single block.
			*/
			continue;
		}
		
		if (subset2.size() > largest) {
			subset.clear();
			for (int i = 0; i < subset2.size(); ++i) {
				subset.push_back(ungrouped[subset2[i]]);
			}
			largest = subset2.size();
			if (largest >= NEW_MODE_THRESH) {
				return largest;
			}
		}
		
		/*
		 Assume this group of points won't fit linearly in any other group, so they
		 can be excluded from consideration in the next iteration.
		*/
		pick_rows(X, subset2);
		erase_inds(ungrouped, subset2);
		ndata = ungrouped.size();
		if (ndata < NEW_MODE_THRESH) {
			break;
		}
	}
	return largest;
}

template <typename T>
void remove_from_vector(const vector<int> &inds, vector <T> &v) {
	int i = 0, j = 0;
	for (int k = 0; k < v.size(); ++k) {
		if (i < inds.size() && k == inds[i]) {
			++i;
		} else {
			if (k > j) {
				v[j] = v[k];
			}
			j++;
		}
	}
	assert(v.size() - inds.size() == j);
	v.resize(j);
}

void print_first_arg(const relation &r, ostream &os) {
	vec_set first;
	r.at_pos(0, first);
	join(os, first.vec(), " ") << endl;
}

EM::EM() : ndata(0), nmodes(1), use_em(true), use_foil(true), use_lda(true), check_after(NEW_MODE_THRESH)
{
	mode_info *noise = new mode_info(true, data, sigs);
	noise->classifiers.resize(1, NULL);
	modes.push_back(noise);
}

EM::~EM() {
	clear_and_dealloc(data);
	clear_and_dealloc(modes);
}


void EM::learn(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, const rvec &y) {
	int sig_index = -1;
	for (int i = 0; i < sigs.size(); ++i) {
		if (sigs[i]->sig == sig) {
			sig_index = i;
			break;
		}
	}
	
	if (sig_index < 0) {
		sig_info *si = new sig_info;
		si->sig = sig;
		sigs.push_back(si);
		sig_index = sigs.size() - 1;
	}

	em_data *dinfo = new em_data;
	dinfo->x = x;
	dinfo->y = y;
	dinfo->target = target;
	dinfo->sig_index = sig_index;
	sigs[sig_index]->members.push_back(ndata);
	
	/*
	 Remember that because the LWR object is initialized with alloc = false, it's
	 just going to store pointers to these rvecs rather than duplicate them.
	*/
	sigs[sig_index]->lwr.learn(dinfo->x, dinfo->y);
	
	dinfo->map_mode = 0;
	dinfo->mode_prob.resize(nmodes, 0.0);
	dinfo->mode_prob[0] = PNOISE;
	dinfo->prob_stale.resize(nmodes, true);
	dinfo->prob_stale[0] = false;
	data.push_back(dinfo);
	
	modes[0]->add_example(ndata);
	noise_by_sig[dinfo->sig_index].insert(ndata);
	extend_relations(rels, ndata);
	++ndata;
}

void EM::estep() {
	function_timer t(timers.get_or_add("e-step"));
	
	/*
	 For data i and mode j, if:
	 
	  * P(i, j) increases and j was not the MAP mode, or
	  * P(i, j) decreases and j was the MAP mode
	 
	 then we mark i as a point we have to recalculate the MAP mode for.
	*/
	for (int i = 0; i < ndata; ++i) {
		em_data &d = *data[i];
		bool stale = false;
		for (int j = 1; j < nmodes; ++j) {
			if (!d.prob_stale[j] && !modes[j]->is_new_fit()) {
				continue;
			}
			double prev = d.mode_prob[d.map_mode], now, error;
			now = modes[j]->calc_prob(d.target, sigs[d.sig_index]->sig, d.x, d.y(0), obj_maps[make_pair(j, i)], error);
			if ((d.map_mode == j && now < prev) || (d.map_mode != j && now > d.mode_prob[d.map_mode])) {
				stale = true;
			}
			d.mode_prob[j] = now;
			d.prob_stale[j] = false;
		}
		if (stale) {
			int prev = d.map_mode, now = argmax(d.mode_prob);
			if (now != prev) {
				d.map_mode = now;
				modes[prev]->del_example(i);
				if (prev == 0) {
					noise_by_sig[d.sig_index].erase(i);
				}
				obj_map_table::const_iterator j = obj_maps.find(make_pair(now, i));
				if (j != obj_maps.end()) {
					d.obj_map = j->second;
				}
				modes[now]->add_example(i);
				if (now == 0) {
					noise_by_sig[d.sig_index].insert(i);
				}
			}
		}
	}
	
	for (int i = 1; i < modes.size(); ++i) {
		modes[i]->reset_new_fit();
	}
}

bool EM::mstep() {
	function_timer t(timers.get_or_add("m-step"));
	
	bool changed = false;
	for (int i = 1; i < nmodes; ++i) {
		changed = changed || modes[i]->update_fits();
	}
	return changed;
}

void EM::fill_xy(const vector<int> &rows, mat &X, mat &Y) const {
	if (rows.empty()) {
		X.resize(0, 0);
		Y.resize(0, 0);
		return;
	}

	X.resize(rows.size(), data[rows[0]]->x.size());
	Y.resize(rows.size(), 1);

	for (int i = 0; i < rows.size(); ++i) {
		X.row(i) = data[rows[i]]->x;
		Y.row(i) = data[rows[i]]->y;
	}
}

/*
 I used to collapse identical data points here, but that seems to be too
 expensive. So I'm assuming all unique data points, which can be enforced as
 data comes in.
*/
bool EM::find_new_mode_inds(int sig_ind, vector<int> &mode_inds) {
	function_timer t(timers.get_or_add("new_inds"));
	
	const set<int> &n = map_get(noise_by_sig, sig_ind);
	if (n.size() < check_after) {
		return false;
	}
	vector<int> noise_inds(n.begin(), n.end());
	int ndata = noise_inds.size();
	int xdim = data[noise_inds[0]]->x.size();
	mat X(ndata, xdim), Y(ndata, 1);
	vector<int> subset;

	for (int i = 0; i < ndata; ++i) {
		X.row(i) = data[noise_inds[i]]->x;
		Y.row(i) = data[noise_inds[i]]->y;
	}
	
	int largest_linear = find_linear_subset(X, Y, subset);
	if (largest_linear >= NEW_MODE_THRESH) {
		mode_inds.clear();
		for (int i = 0; i < subset.size(); ++i) {
			mode_inds.push_back(noise_inds[subset[i]]);
		}
		return true;
	}
	return false;
}

/*
 Fit lin_coefs, lin_inter, and sig to the data in data_inds. This is not as
 efficient as it can be, since I've usually already ran a linear regression on
 this set of data before calling this function. In the future, just use the
 results from those previous regressions here.
*/
void EM::mode_info::init_fit(const vector<int> &data_inds) {
	int ndata = data_inds.size();
	const em_data &d0 = *data[data_inds[0]];
	const scene_sig &dsig = sigs[d0.sig_index]->sig;
	int target = d0.target;
	
	mat X(ndata, d0.x.size()), Y(ndata, 1);

	for (int i = 0; i < ndata; ++i) {
		const em_data &d = *data[data_inds[i]];
		assert(d.sig_index == d0.sig_index);
		X.row(i) = d.x;
		Y.row(i) = d.y;
	}
	
	mat coefs;
	linreg_d(REGRESSION_ALG, X, Y, cvec(), coefs, lin_inter);
	
	// find relevant objects (with nonzero coefficients)
	vector<int> relevant_objs;
	relevant_objs.push_back(target);
	for (int i = 0; i < dsig.size(); ++i) {
		if (i == target) {
			continue;
		}
		int start = dsig[i].start;
		int end = start + dsig[i].props.size();
		for (int j = start; j < end; ++j) {
			if (!coefs.row(j).isConstant(0.0)) {
				relevant_objs.push_back(i);
				break;
			}
		}
	}
	
	int end = 0;
	lin_coefs.resize(coefs.rows(), 1);
	sig.clear();
	for (int i = 0; i < relevant_objs.size(); ++i) {
		const scene_sig::entry &e = dsig[relevant_objs[i]];
		sig.add(e);
		int start = e.start, n = e.props.size();
		lin_coefs.block(end, 0, n, 1) = coefs.block(start, 0, n, 1);
		end += n;
	}
	lin_coefs.conservativeResize(end, 1);
	new_fit = true;
}

bool EM::unify_or_add_mode() {
	function_timer t(timers.get_or_add("new"));

	assert(check_after >= NEW_MODE_THRESH);
	if (modes[0]->size() < check_after) {
		return false;
	}
	
	int largest;
	vector<int> seed_inds;
	modes[0]->largest_const_subset(seed_inds);
	if (seed_inds.size() < NEW_MODE_THRESH) {
		largest = seed_inds.size();
		map<int, set<int> >::const_iterator i;
		for (i = noise_by_sig.begin(); i != noise_by_sig.end(); ++i) {
			seed_inds.clear();
			find_new_mode_inds(i->first, seed_inds);
			if (largest < seed_inds.size()) {
				largest = seed_inds.size();
			}
			if (largest >= NEW_MODE_THRESH) {
				break;
			}
		}
	}
	
	if (largest < NEW_MODE_THRESH) {
		check_after += (NEW_MODE_THRESH - largest);
		return false;
	}
	
	/*
	 From here I know the noise data is going to either become a new mode or unify
	 with an existing mode, so reset check_after assuming the current noise is
	 gone.
	*/
	check_after = NEW_MODE_THRESH;
	
	int seed_sig = data[seed_inds[0]]->sig_index;
	int seed_target = data[seed_inds[0]]->target;
	mat X, Y;

	/*
	 Try to add noise data to each current model and refit. If the
	 resulting model is just as accurate as the original, then just
	 add the noise to that model instead of creating a new one.
	*/
	for (int j = 1; j < nmodes; ++j) {
		mode_info &minfo = *modes[j];

		if (!minfo.uniform_sig(seed_sig, seed_target)) {
			continue;
		}

		vector<int> combined, subset;
		extend(combined, minfo.get_members());
		extend(combined, seed_inds);
		fill_xy(combined, X, Y);
		int unified_size = find_linear_subset(X, Y, subset);
		if (unified_size >= .9 * combined.size()) {
			vector<int> u(combined.size());
			for (int k = 0; k < subset.size(); ++k) {
				u[k] = combined[subset[k]];
			}
			minfo.init_fit(u);
			return true;
		}
	}
	
	mode_info *new_mode = new mode_info(false, data, sigs);
	new_mode->init_fit(seed_inds);
	modes.push_back(new_mode);
	++nmodes;
	for (int j = 0; j < ndata; ++j) {
		data[j]->mode_prob.push_back(0.0);
	}
	for (int j = 0; j < nmodes; ++j) {
		modes[j]->classifiers.resize(nmodes, NULL);
		/*
		 It's sufficient to fill the extra vector elements
		 with NULL here. The actual classifiers will be
		 allocated as needed during updates.
		*/
	}

	return true;
}

bool EM::map_objs(int mode, int target, const scene_sig &sig, const relation_table &rels, vector<int> &mapping) const {
	const mode_info &minfo = *modes[mode];
	const scene_sig &msig = minfo.get_sig();
	vector<bool> used(sig.size(), false);
	used[target] = true;
	mapping.resize(msig.empty() ? 1 : msig.size(), -1);
	
	// target always maps to target
	mapping[0] = target;
	
	var_domains domains;  
	
	// 0 = time, 1 = target, 2 = object we're searching for
	domains[0].insert(0);
	domains[1].insert(sig[target].id);
	
	for (int i = 1; i < msig.size(); ++i) {
		set<int> &d = domains[2];
		d.clear();
		for (int j = 0; j < sig.size(); ++j) {
			if (!used[j] && sig[j].type == msig[i].type) {
				d.insert(j);
			}
		}
		if (d.empty()) {
			return false;
		} else if (d.size() == 1 || minfo.obj_clauses[i].empty()) {
			mapping[i] = sig.find_id(*d.begin());
		} else {
			if (test_clause_vec(minfo.obj_clauses[i], rels, domains) < 0) {
				return false;
			}
			assert(domains[2].size() == 1);
			mapping[i] = sig.find_id(*domains[2].begin());
		}
		used[mapping[i]] = true;
	}
	return true;
}

bool EM::predict(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, int &mode, rvec &y) {
	if (ndata == 0) {
		mode = 0;
		return false;
	}
	
	vector<int> obj_map;
	mode = classify(target, sig, rels, x, obj_map);
	if (mode == 0) {
		for (int i = 0; i < sigs.size(); ++i) {
			if (sigs[i]->sig == sig) {
				if (sigs[i]->lwr.predict(x, y)) {
					return true;
				}
				break;
			}
		}
		y(0) = NAN;
		return false;
	}
	modes[mode]->predict(sig, x, obj_map, y);
	return true;
}

/*
 Remove all modes that cover fewer than 2 data points.
*/
bool EM::remove_modes() {
	if (nmodes == 1) {
		return false;
	}
	
	/*
	 i is the first free model index. If model j should be kept, all
	 information pertaining to model j will be copied to row/element i
	 in the respective matrix/vector, and i will be incremented. Most
	 efficient way I can think of to remove elements from the middle
	 of vectors. index_map associates old j's to new i's.
	*/
	vector<int> index_map(nmodes), removed;
	int i = 1;  // start with 1, noise mode (0) should never be removed
	for (int j = 1; j < nmodes; ++j) {
		if (modes[j]->size() > 2) {
			index_map[j] = i;
			if (j > i) {
				modes[i] = modes[j];
			}
			i++;
		} else {
			index_map[j] = 0;
			delete modes[j];
			removed.push_back(j);
		}
	}
	if (removed.empty()) {
		return false;
	}
	assert(i == nmodes - removed.size());
	nmodes = i;
	modes.resize(nmodes);
	for (int j = 0; j < nmodes; ++j) {
		remove_from_vector(removed, modes[j]->classifiers);
	}
	for (int j = 0; j < ndata; ++j) {
		em_data &dinfo = *data[j];
		if (dinfo.map_mode >= 0) {
			dinfo.map_mode = index_map[dinfo.map_mode];
		}
		remove_from_vector(removed, dinfo.mode_prob);
	}
	return true;
}

bool EM::run(int maxiters) {
	if (use_em) {
		for (int i = 0; i < maxiters; ++i) {
			estep();
			bool changed = mstep();
			if (!changed && !remove_modes() && !unify_or_add_mode()) {
				// reached quiescence
				return true;
			}
		}
		LOG(EMDBG) << "Reached max iterations without quiescence" << endl;
	}
	return false;
}

int EM::best_mode(int target, const scene_sig &sig, const rvec &x, double y, double &besterror) const {
	int best = -1;
	double best_prob = 0.0;
	rvec py;
	vector<int> assign;
	double error;
	for (int i = 0; i < nmodes; ++i) {
		double p = modes[i]->calc_prob(target, sig, x, y, assign, error);
		if (best == -1 || p > best_prob) {
			best = i;
			best_prob = p;
			besterror = error;
		}
	}
	return best;
}

bool EM::cli_inspect(int first, const vector<string> &args, ostream &os) {
	const_cast<EM*>(this)->update_classifier();

	if (first >= args.size()) {
		os << "modes: " << nmodes << endl;
		os << endl << "subqueries: mode ptable timing train relations classifiers use_em use_foil use_lda" << endl;
		return true;
	} else if (args[first] == "ptable") {
		table_printer t;
		for (int i = 0; i < ndata; ++i) {
			t.add_row() << i;
			t.add(data[i]->mode_prob);
		}
		t.print(os);
		return true;
	} else if (args[first] == "train") {
		return cli_inspect_train(first + 1, args, os);
	} else if (args[first] == "mode") {
		if (first + 1 >= args.size()) {
			os << "Specify a mode number (0 - " << nmodes - 1 << ")" << endl;
			return false;
		}
		int n;
		if (!parse_int(args[first+1], n) || n < 0 || n >= nmodes) {
			os << "invalid mode number" << endl;
			return false;
		}
		return modes[n]->cli_inspect(first + 2, args, os);
	} else if (args[first] == "timing") {
		timers.report(os);
		return true;
	} else if (args[first] == "relations") {
		return cli_inspect_relations(first + 1, args, os);
	} else if (args[first] == "classifiers") {
		return cli_inspect_classifiers(os);
	} else if (args[first] == "use_em") {
		return read_on_off(args, first + 1, os, use_em);
	} else if (args[first] == "use_foil") {
		return read_on_off(args, first + 1, os, use_foil);
	} else if (args[first] == "use_lda") {
		return read_on_off(args, first + 1, os, use_lda);
	}

	return false;
}

bool EM::cli_inspect_train(int first, const vector<string> &args, ostream &os) const {
	int start = 0, end = ndata - 1;
	vector<string> objs;
	bool have_start = false;
	for (int i = first; i < args.size(); ++i) {
		int x;
		if (parse_int(args[i], x)) {
			if (!have_start) {
				start = x;
				have_start = true;
			} else {
				end = x;
			}
		} else {
			objs.push_back(args[i]);
		}		
	}
	cout << "start = " << start << " end = " << end << endl;
	cout << "objs" << endl;
	join(cout, objs, " ");

	if (start < 0 || end < start || end >= ndata) {
		os << "invalid data range" << endl;
		return false;
	}

	vector<int> cols;
	table_printer t;
	t.add_row() << "N" << "CLS" << "|" << "DATA";
	for (int i = start; i <= end; ++i) {
		if (i == start || (i > start && data[i]->sig_index != data[i-1]->sig_index)) {
			const scene_sig &s = sigs[data[i]->sig_index]->sig;
			t.add_row().skip(2) << "|";
			int c = 0;
			cols.clear();
			for (int j = 0; j < s.size(); ++j) {
				if (objs.empty() || has(objs, s[j].name)) {
					for (int k = 0; k < s[j].props.size(); ++k) {
						cols.push_back(c++);
					}
					t << s[j].name;
					t.skip(s[j].props.size() - 1);
				} else {
					c += s[j].props.size();
				}
			}
			t.add_row().skip(2) << "|";
			for (int j = 0; j < s.size(); ++j) {
				if (objs.empty() || has(objs, s[j].name)) {
					const vector<string> &props = s[j].props;
					for (int k = 0; k < props.size(); ++k) {
						t << props[k];
					}
				}
			}
		}
		t.add_row();
		t << i << data[i]->map_mode << "|";
		for (int j = 0; j < cols.size(); ++j) {
			t << data[i]->x(cols[j]);
		}
		t << data[i]->y(0);
	}
	t.print(os);
	return true;
}

/*
 pos_obj and neg_obj can probably be cached and updated as data points
 are assigned to modes.
*/
void EM::mode_info::learn_obj_clauses(const relation_table &rels) {
	for (int i = 0; i < sig.size(); ++i) {
		int type = sig[i].type;
		relation pos_obj(3), neg_obj(3);
		tuple objs(2);
		set<int>::const_iterator j;
		for (j = members.begin(); j != members.end(); ++j) {
			const scene_sig &sig = sigs[data[*j]->sig_index]->sig;
			int o = sig[data[*j]->obj_map[i]].id;
			objs[0] = data[*j]->target;
			objs[1] = o;
			pos_obj.add(*j, objs);
			for (int k = 0; k < sig.size(); ++k) {
				if (sig[k].type == type && k != objs[0] && k != o) {
					objs[1] = k;
					neg_obj.add(*j, objs);
				}
			}
		}
		
		FOIL foil(pos_obj, neg_obj, rels);
		if (!foil.learn(obj_clauses[i], NULL)) {
			// respond to this situation appropriately
		}
	}
}

bool EM::mode_info::cli_inspect(int first, const vector<string> &args, ostream &os) {
	if (first >= args.size()) {
		// some kind of default action
	} else if (args[first] == "clauses") {
		table_printer t;
		for (int j = 0; j < obj_clauses.size(); ++j) {
			t.add_row() << j;
			if (obj_clauses[j].empty()) {
				t << "empty";
			} else {
				for (int k = 0; k < obj_clauses[j].size(); ++k) {
					if (k > 0) {
						t.add_row().skip(1);
					}
					t << obj_clauses[j][k];
				}
			}
		}
		t.print(os);
		return true;
	} else if (args[first] == "signature") {
		for (int i = 0; i < sig.size(); ++i) {
			os << sig[i].type << " ";
		}
		os << endl;
		return true;
	} else if (args[first] == "members") {
		join(os, members, ' ') << endl;
		return true;
	} else if (args[first] == "model") {
		if (noise) {
			os << "noise" << endl;
		} else {
			os << "coefficients" << endl;
			os << lin_coefs << endl;
			os << "intercept" << endl;
			os << lin_inter;
		}
		return true;
	}
	return false;
}

/*
 Add tuples from a single time point into the relation table
*/
void EM::extend_relations(const relation_table &add, int time) {
	set<tuple> t;
	relation_table::const_iterator i;
	for (i = add.begin(); i != add.end(); ++i) {
		const string &name = i->first;
		const relation &r = i->second;
		relation_table::iterator j = rel_tbl.find(name);
		if (j == rel_tbl.end()) {
			rel_tbl[name] = r;
		} else {
			relation &r2 = j->second;
			t.clear();
			/*
			 The assumption here is that all the tuples
			 have the same value in the first position,
			 since they're all from the same time.
			*/
			r.drop_first(t);
			set<tuple>::const_iterator k;
			for (k = t.begin(); k != t.end(); ++k) {
				r2.add(time, *k);
			}
		}
	}
}

void EM::serialize(ostream &os) const {
	serializer(os) << ndata << nmodes << data << sigs << rel_tbl;
	for (int i = 0; i < nmodes; ++i) {
		modes[i]->serialize(os);
	}
}

void EM::unserialize(istream &is) {
	unserializer(is) >> ndata >> nmodes >> data >> sigs >> rel_tbl;
	assert(data.size() == ndata);
	
	for (int i = 0; i < nmodes; ++i) {
		EM::mode_info *m = new mode_info(i == 0, data, sigs);
		m->unserialize(is);
	}
	
	for (int i = 0; i < sigs.size(); ++i) {
		sig_info &si = *sigs[i];
		for (int j = 0; j < si.members.size(); ++j) {
			const em_data &d = *data[si.members[j]];
			si.lwr.learn(d.x, d.y);
		}
	}
}

void EM::em_data::serialize(ostream &os) const {
	serializer(os) << target << sig_index << map_mode
	               << x << y << mode_prob << prob_stale << obj_map;
}

void EM::em_data::unserialize(istream &is) {
	unserializer(is) >> target >> sig_index >> map_mode
	                 >> x >> y >> mode_prob >> prob_stale >> obj_map;
}

EM::mode_info::mode_info(bool noise, const vector<em_data*> &data, const vector<sig_info*> &sigs) 
: noise(noise), data(data), sigs(sigs), member_rel(2), classifier_stale(true), new_fit(true)
{
	if (noise) {
		stale = false;
	} else {
		stale = true;
	}
}

EM::mode_info::~mode_info() {
	for (int i = 0; i < classifiers.size(); ++i) {
		delete classifiers[i];
	}
}

/*
 The fields noise, data, and sigs are initialized in the constructor, and
 therefore not (un)serialized.
*/
void EM::mode_info::serialize(ostream &os) const {
	serializer(os) << stale << new_fit << classifier_stale << members << sig
	               << classifiers << obj_clauses << member_rel << sorted_ys
	               << lin_coefs << lin_inter;
}

void EM::mode_info::unserialize(istream &is) {
	unserializer(is) >> stale >> new_fit >> classifier_stale >> members >> sig
	                 >> classifiers >> obj_clauses >> member_rel >> sorted_ys
	                 >> lin_coefs >> lin_inter;
}

double EM::mode_info::calc_prob(int target, const scene_sig &xsig, const rvec &x, double y, vector<int> &best_assign, double &best_error) const {
	if (noise) {
		return PNOISE;
	}
	
	rvec py;
	double w = 1.0;
	
	/*
	 Each mode has a signature that specifies the types and orders of
	 objects it expects for inputs. This is recorded in modes[m]->sig.
	 Call this the model signature.
	 
	 Each data point has a signature that specifies which types and
	 orders of object properties encoded by the property vector. This
	 is recorded in data[i]->sig_index. Call this the data signature.
	 
	 P(d, m) = MAX[assignment][P(d, m, assignment)] where 'assignment'
	 is a mapping of objects in the data signature to the objects in
	 the mode signature.
	*/
	
	/*
	 Create the input table for the combination generator to generate
	 all possible assignments. possibles[i] should be a list of all
	 object indices that can be assigned to position i in the model
	 signature.
	*/
	if (sig.empty()) {
		// should be constant prediction
		assert(lin_coefs.size() == 0);
		py = lin_inter;
		best_error = (y - py(0));
		best_assign.clear();
		double d = gausspdf(y, py(0), MEASURE_VAR);
		double p = (1.0 - EPSILON) * w * d;
		return p;
	}
	
	// otherwise, check all possible assignments
	vector<vector<int> > possibles(sig.size());
	possibles[0].push_back(target);
	for (int i = 1; i < sig.size(); ++i) {
		for (int j = 0; j < xsig.size(); ++j) {
			if (xsig[j].type == sig[i].type && j != target) {
				possibles[i].push_back(j);
			}
		}
	}
	multi_combination_generator<int> gen(possibles, false);
	
	/*
	 Iterate through all assignments and find the one that gives
	 highest probability.
	*/
	vector<int> assign;
	int xlen = sig.dim();
	rvec xc(xlen);
	double best_prob = -1.0;
	while (gen.next(assign)) {
		int s = 0;
		for (int i = 0; i < assign.size(); ++i) {
			const scene_sig::entry &e = xsig[assign[i]];
			int l = e.props.size();
			assert(sig[i].props.size() == l);
			xc.segment(s, l) = x.segment(e.start, l);
			s += l;
		}
		assert(s == xlen);
		
		py = (xc * lin_coefs) + lin_inter;
		double d = gausspdf(y, py(0), MEASURE_VAR);
		double p = (1.0 - EPSILON) * w * d;
		if (p > best_prob) {
			best_prob = p;
			best_assign = assign;
			best_error = y - py(0);
		}
	}
	assert(best_prob >= 0.0);
	return best_prob;
}

bool EM::mode_info::update_fits() {
	if (!stale) {
		return false;
	}
	int xcols = 0;
	for (int i = 0; i < sig.size(); ++i) {
		xcols += sig[i].props.size();
	}
	
	mat X(members.size(), xcols), Y(members.size(), 1);
	set<int>::const_iterator i;
	int j = 0;
	for (i = members.begin(); i != members.end(); ++i) {
		const em_data &d = *data[*i];
		assert(d.obj_map.size() == sig.size());
		const scene_sig &dsig = sigs[d.sig_index]->sig;
		rvec x(xcols);
		int s = 0;
		for (int k = 0; k < d.obj_map.size(); ++k) {
			const scene_sig::entry &e = dsig[d.obj_map[k]];
			int n = e.props.size();
			x.segment(s, n) = d.x.segment(e.start, n);
			s += n;
		}
		assert(s == xcols);
		X.row(j) = x;
		Y.row(j++) = d.y;
	}
	linreg_d(REGRESSION_ALG, X, Y, cvec(), lin_coefs, lin_inter);
	stale = false;
	new_fit = true;
	return true;
}

void EM::mode_info::predict(const scene_sig &dsig, const rvec &x, const vector<int> &obj_map, rvec &y) const {
	if (lin_coefs.size() == 0) {
		y = lin_inter;
		return;
	}
	
	assert(obj_map.size() == sig.size());
	rvec xc(x.size());
	int xsize = 0;
	for (int j = 0; j < obj_map.size(); ++j) {
		const scene_sig::entry &e = dsig[obj_map[j]];
		int n = e.props.size();
		xc.segment(xsize, n) = x.segment(e.start, n);
		xsize += n;
	}
	xc.conservativeResize(xsize);
	y = (xc * lin_coefs) + lin_inter;
}

void EM::mode_info::add_example(int i) {
	const em_data &d = *data[i];
	int sind = d.sig_index;
	const scene_sig &dsig = sigs[sind]->sig;

	members.insert(i);
	classifier_stale = true;
	member_rel.add(i, dsig[d.target].id);
	if (noise) {
		sorted_ys.insert(make_pair(d.y(0), i));
	} else {
		rvec y;
		predict(dsig, d.x, d.obj_map, y);
		if ((y - d.y).norm() > MODEL_ERROR_THRESH) {
			stale = true;
		}
	}
}

void EM::mode_info::del_example(int i) {
	em_data &d = *data[i];
	int sind = d.sig_index;
	const scene_sig &sig = sigs[sind]->sig;

	classifier_stale = true;
	member_rel.del(i, sig[d.target].id);
	members.erase(i);
	if (noise) {
		sorted_ys.erase(make_pair(d.y(0), i));
	}
}

void EM::mode_info::largest_const_subset(vector<int> &subset) {
	vector<int> s;
	set<pair<double, int> >::const_iterator i;
	double last = NAN;
	subset.clear();
	for (i = sorted_ys.begin(); i != sorted_ys.end(); ++i) {
		if (i->first == last) {
			subset.push_back(i->second);
		} else {
			if (s.size() > subset.size()) {
				subset = s;
			}
			last = i->first;
			s.clear();
			s.push_back(i->second);
		}
	}
}

bool EM::mode_info::uniform_sig(int sig, int target) const {
	set<int>::const_iterator i;
	for (i = members.begin(); i != members.end(); ++i) {
		if (data[*i]->sig_index != sig || data[*i]->target != target) {
			return false;
		}
	}
	return true;
}


void EM::classifier::serialize(ostream &os) const {
	serializer(os) << const_vote << clauses << residuals << ldas;
}

void EM::classifier::unserialize(istream &is) {
	unserializer(is) >> const_vote >> clauses >> residuals >> ldas;
}

void EM::classifier::inspect(ostream &os) const {
	if (clauses.empty() && (ldas.empty() || ldas.back() == NULL)) {
		os << "Constant Vote: " << const_vote << endl;
		return;
	}
	
	if (clauses.empty()) {
		os << "No clauses" << endl;
	} else {
		for (int k = 0; k < clauses.size(); ++k) {
			os << "Clause: " << clauses[k] << endl;
			if (!residuals[k]->empty()) {
				os << "False positives:" << endl;
				print_first_arg(*residuals[k], os);
				os << endl;
				if (ldas[k]) {
					os << "Numeric classifier:" << endl;
					ldas[k]->inspect(os);
					os << endl;
				}
			}
		}
	}
	os << endl;
	
	if (residuals.size() > clauses.size()) {
		assert(residuals.size() == ldas.size() && residuals.size() == clauses.size() + 1);
		os << "False negatives:" << endl;
		print_first_arg(*residuals.back(), os);
		os << endl;
		if (ldas.back()) {
			os << "Numeric classifier:" << endl;
			ldas.back()->inspect(os);
			os << endl;
		}
	}
}

bool EM::cli_inspect_relations(int i, const vector<string> &args, ostream &os) const {
	if (i >= args.size()) {
		os << rel_tbl << endl;
		return true;
	}
	const relation *r = map_getp(rel_tbl, args[i]);
	if (!r) {
		os << "no such relation" << endl;
		return false;
	}
	if (i + 1 >= args.size()) {
		os << *r << endl;
		return true;
	}

	// process pattern
	vector<int> pattern;
	for (int j = i + 1; j < args.size(); ++j) {
		if (args[j] == "*") {
			pattern.push_back(-1);
		} else {
			int obj;
			if (!parse_int(args[j], obj)) {
				os << "invalid pattern" << endl;
				return false;
			}
			pattern.push_back(obj);
		}
	}

	if (pattern.size() > r->arity()) {
		os << "pattern larger than relation arity" << endl;
		return false;
	}
	relation matches(r->arity());
	r->match(pattern, matches);
	os << matches << endl;
	return true;
}

void EM::update_classifier() {
	
	vector<bool> needs_update(modes.size(), false);
	for (int i = 0; i < modes.size(); ++i) {
		if (modes[i]->classifier_stale) {
			needs_update[i] = true;
			modes[i]->classifier_stale = false;
		}
	}
	
	for (int i = 0; i < modes.size(); ++i) {
		mode_info &minfo = *modes[i];

		if (needs_update[i]) {
			minfo.learn_obj_clauses(rel_tbl);
		}
		
		for (int j = i + 1; j < modes.size(); ++j) {
			if (needs_update[i] || needs_update[j]) {
				update_pair(i, j);
			}
		}
	}
}

LDA *EM::learn_numeric_classifier(const relation &pos, const relation &neg) const {
	if (!use_lda) {
		return NULL;
	}
	
	int npos = pos.size(), nneg = neg.size();
	int ntotal = npos + nneg;
	int pos_train = EM_LDA_TRAIN_RATIO * npos;
	if (pos_train == npos) --pos_train;
	int neg_train = EM_LDA_TRAIN_RATIO * nneg;
	if (neg_train == nneg) --neg_train;
	int ntrain = pos_train + neg_train;
	int ntest = ntotal - ntrain;
	
	if (pos_train < 2 || neg_train < 2) {
		return NULL;
	}
	
	vec_set p0, n0;
	vector<int> pi, ni;
	pos.at_pos(0, p0);
	pi = p0.vec();
	neg.at_pos(0, n0);
	ni = n0.vec();
	
	random_shuffle(pi.begin(), pi.end());
	random_shuffle(ni.begin(), ni.end());
	
	int ncols = data[pi[0]]->x.size();
	int sig = data[pi[0]]->sig_index;
	
	mat train_data(ntrain, ncols), test_data(ntest, ncols);
	vector<int> train_classes, test_classes;
	
	for (int i = 0; i < pos_train; ++i) {
		const em_data &d = *data[pi[i]];
		assert(d.sig_index == sig);
		
		train_data.row(i) = d.x;
		train_classes.push_back(1);
	}
	
	for (int i = 0; i < neg_train; ++i) {
		const em_data &d = *data[ni[i]];
		assert(d.sig_index == sig);
		
		train_data.row(pos_train + i) = d.x;
		train_classes.push_back(0);
	}
	
	LDA *lda = new LDA;
	lda->learn(train_data, train_classes);
	
	int correct = 0;
	for (int i = pos_train; i < pi.size(); ++i) {
		const em_data &d = *data[pi[i]];
		assert(d.sig_index == sig);
		
		if (lda->classify(d.x) == 1) {
			++correct;
		}
	}
	for (int i = neg_train; i < ni.size(); ++i) {
		const em_data &d = *data[ni[i]];
		assert(d.sig_index == sig);
		
		if (lda->classify(d.x) == 0) {
			++correct;
		}
	}
	
	double success_ratio = correct / static_cast<double>(ntest);
	double baseline;
	if (pi.size() > ni.size()) {
		baseline = npos / static_cast<double>(ntotal);
	} else {
		baseline = nneg / static_cast<double>(ntotal);
	}
	if (success_ratio > baseline) {
		return lda;
	}
	delete lda;
	return NULL;
}

void EM::update_pair(int i, int j) {
	function_timer t(timers.get_or_add("updt_clsfr"));
	
	assert(i < j);
	if (!modes[i]->classifiers[j]) {
		modes[i]->classifiers[j] = new classifier;
	}
	classifier &c = *(modes[i]->classifiers[j]);
	const relation &mem_i = modes[i]->member_rel;
	const relation &mem_j = modes[j]->member_rel;
	
	c.clauses.clear();
	clear_and_dealloc(c.residuals);
	clear_and_dealloc(c.ldas);
	
	c.const_vote = mem_i.size() > mem_j.size() ? 0 : 1;
	
	if (mem_i.empty() || mem_j.empty()) {
		return;
	}
	
	if (use_foil) {
		FOIL foil(mem_i, mem_j, rel_tbl);
		foil.learn(c.clauses, &c.residuals);
	} else {
		/*
		 Don't learn any clauses. Instead create a residual set for all members of i,
		 to be handled by the numeric classifier.
		*/
		c.residuals.push_back(new relation(mem_i));
	}
	
	/*
	 For each clause cl in c.clauses, if cl misclassified any of the
	 members of j in the training set as a member of i (false positive
	 for cl), train a numeric classifier to classify it correctly.
	 
	 Also train a numeric classifier to catch misclassified members of
	 i (false negatives for the entire clause vector).
	*/
	c.ldas.resize(c.residuals.size(), NULL);
	for (int k = 0; k < c.residuals.size(); ++k) {
		const relation &r = *c.residuals[k];
		if (!r.empty()) {
			if (k < c.clauses.size()) {
				// r contains misclassified members of j
				c.ldas[k] = learn_numeric_classifier(mem_i, r);
			} else {
				// r contains misclassified members of i
				c.ldas[k] = learn_numeric_classifier(r, mem_j);
			}
		}
	}
}

/*
 Return 0 to vote for i, 1 to vote for j
*/
int EM::vote_pair(int i, int j, int target, const scene_sig &sig, const relation_table &rels, const rvec &x) const {
	assert(modes[i]->classifiers[j]);
	const classifier &c = *(modes[i]->classifiers[j]);

	var_domains domains;
	domains[0].insert(0);       // rels is only for the current timestep, time should always be 0
	domains[1].insert(sig[target].id);
	int matched_clause = test_clause_vec(c.clauses, rels, domains);
	if (matched_clause >= 0) {
		if (c.ldas[matched_clause]) {
			return c.ldas[matched_clause]->classify(x);
		} else {
			return 0;
		}
	} else if (c.ldas.size() > c.clauses.size()) {
		return c.ldas.back()->classify(x);
	}
	return c.const_vote;
}

int EM::classify(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, vector<int> &obj_map) {
	LOG(EMDBG) << "classification" << endl;
	update_classifier();
	
	/*
	 The scene has to contain the objects used by the linear model of
	 a mode for it to possibly qualify for that mode.
	*/
	vector<int> possible(1, 0);
	map<int, vector<int> > mappings;
	for (int i = 1; i < modes.size(); ++i) {
		mode_info &minfo = *modes[i];
		if (minfo.get_sig().size() > sig.size()) {
			continue;
		}
		if (!map_objs(i, target, sig, rels, mappings[i])) {
			LOG(EMDBG) << "mapping failed for " << i << endl;
			continue;
		}
		possible.push_back(i);
	}
	if (possible.size() == 1) {
		LOG(EMDBG) << "only one possible mode: " << possible[0] << endl;
		obj_map = mappings[possible[0]];
		return possible[0];
	}
	
	map<int, int> votes;
	for (int i = 0; i < possible.size() - 1; ++i) {
		int a = possible[i];
		for (int j = i + 1; j < possible.size(); ++j) {
			int b = possible[j];
			LOG(EMDBG) << "for " << a << "/" << b << ": ";
			int winner = vote_pair(a, b, target, sig, rels, x);
			if (winner == 0) {
				LOG(EMDBG) << a << " wins" << endl;
				++votes[a];
			} else if (winner == 1) {
				LOG(EMDBG) << b << " wins" << endl;
				++votes[b];
			} else {
				LOG(EMDBG) << " tie" << endl;
			}
		}
	}
	
	LOG(EMDBG) << "votes:" << endl;
	map<int, int>::const_iterator i, best = votes.begin();
	for (i = votes.begin(); i != votes.end(); ++i) {
		LOG(EMDBG) << i->first << " = " << i->second << endl;
		if (i->second > best->second) {
			best = i;
		}
	}
	LOG(EMDBG) << "best mode = " << best->first << endl;
	obj_map = mappings[best->first];
	return best->first;
}

bool EM::cli_inspect_classifiers(ostream &os) const {
	for (int i = 0; i < nmodes; ++i) {
		for (int j = 0; j < nmodes; ++j) {
			classifier *c = modes[i]->classifiers[j];
			if (c) {
				os << "=== FOR MODES " << i << "/" << j << " ===" << endl;
				c->inspect(os);
			}
		}
	}
	return true;
}

EM::sig_info::sig_info() : lwr(LWR_K, false) {}

void EM::sig_info::serialize(ostream &os) const {
	serializer(os) << sig << members;
}

void EM::sig_info::unserialize(istream &is) {
	unserializer(is) >> sig >> members;
}
