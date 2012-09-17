#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <iterator>
#include <limits>
#include "model.h"

using namespace std;

void slice(const rvec &source, rvec &target, const vector<int> &indexes) {
	target.resize(indexes.size());
	for (int i = 0; i < indexes.size(); ++i) {
		target(i) = source(indexes[i]);
	}
}

void dassign(const rvec &source, rvec &target, const vector<int> &indexes) {
	assert(source.size() == indexes.size());
	for (int i = 0; i < indexes.size(); ++i) {
		assert(0 <= indexes[i] && indexes[i] < target.size());
		target[indexes[i]] = source[i];
	}
}

model::model(const std::string &name, const std::string &type) 
: name(name), type(type)
{}

bool model::test(const state_sig &sig, const rvec &x, const rvec &y, const relation_table &rels, rvec &prediction) {
	return predict(sig, x, rels, prediction);
}

bool model::cli_inspect(int first_arg, const vector<string> &args, ostream &os) {
	if (first_arg < args.size()) {
		if (args[first_arg] == "save") {
			if (first_arg + 1 >= args.size()) {
				os << "need a file name" << endl;
				return false;
			}
			string path = args[first_arg + 1];
			ofstream f(path.c_str());
			if (!f.is_open()) {
				os << "cannot open file " << path << " for writing" << endl;
				return false;
			}
			serialize(f);
			f.close();
			os << "saved to " << path << endl;
			return true;
		} else if (args[first_arg] == "load") {
			if (first_arg + 1 >= args.size()) {
				os << "need a file name" << endl;
				return false;
			}
			string path = args[first_arg + 1];
			ifstream f(path.c_str());
			if (!f.is_open()) {
				os << "cannot open file " << path << " for reading" << endl;
				return false;
			}
			unserialize(f);
			f.close();
			os << "loaded from " << path << endl;
			return true;
		}
	}
	return cli_inspect_sub(first_arg, args, os);
}

multi_model::multi_model(map<string, model*> *model_db) : model_db(model_db) {}

multi_model::~multi_model() {
	std::list<model_config*>::iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		delete *i;
	}
}

void multi_model::find_targets(const vector<int> &yinds, state_sig &sig) {
	for (int i = 0; i < sig.size(); ++i) {
		sig[i].target = -1;
	}
	int ntargets = 0;
	for (int i = 0; i < yinds.size(); ++i) {
		for (int j = 0; j < sig.size(); ++j) {
			if (sig[j].start <= yinds[i] && 
			    yinds[i] < sig[j].start + sig[j].length &&
				sig[j].target == -1) 
			{
				sig[j].target = ntargets++;
				break;
			}
		}
	}
}

bool multi_model::predict(const state_sig &sig, const rvec &x, rvec &y, const relation_table &rels) {
	std::list<model_config*>::const_iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		model_config *cfg = *i;
		rvec xp, yp(cfg->ally ? y.size() : cfg->yinds.size());
		bool success;
		state_sig sig2 = sig;

		find_targets(cfg->yinds, sig2);
		if (cfg->allx) {
			xp = x;
		} else {
			assert(false); // don't know what to do with the signature when we have to slice
			slice(x, xp, cfg->xinds);
		}
		if (!cfg->mdl->predict(sig2, xp, rels, yp)) {
			return false;
		}
		if (cfg->ally) {
			assert(false); // not sure how to handle this case
			y = yp;
		} else {
			dassign(yp, y, cfg->yinds);
		}
	}
	return true;
}

void multi_model::learn(const state_sig &sig, const rvec &x, const rvec &y, int time) {
	std::list<model_config*>::iterator i;
	int j;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		model_config *cfg = *i;
		rvec xp, yp;
		state_sig sig2 = sig;
		
		if (cfg->allx) {
			xp = x;
		} else {
			assert(false); // don't know what to do with the signature when we have to slice
			slice(x, xp, cfg->xinds);
		}
		if (cfg->ally) {
			yp = y;
		} else {
			slice(y, yp, cfg->yinds);
		}
		find_targets(cfg->yinds, sig2);
		cfg->mdl->learn(sig2, xp, yp, time);
	}
}

bool multi_model::test(const state_sig &sig, const rvec &x, const rvec &y, const relation_table &rels) {
	rvec predicted(y.size());
	predicted.setConstant(0.0);
	test_x.push_back(x);
	test_y.push_back(y);
	test_rels.push_back(rels);
	reference_vals.push_back(y);

	if (!predict(sig, x, predicted, rels)) {
		predicted_vals.push_back(rvec());
		return false;
	}
	predicted_vals.push_back(predicted);
	return true;
}

string multi_model::assign_model
( const string &name, 
  const vector<string> &inputs, bool all_inputs,
  const vector<string> &outputs, bool all_outputs )
{
	model *m;
	model_config *cfg;
	if (!map_get(*model_db, name, m)) {
		return "no model";
	}
	
	cfg = new model_config();
	cfg->name = name;
	cfg->mdl = m;
	cfg->allx = all_inputs;
	cfg->ally = all_outputs;
	
	if (all_inputs) {
		if (m->get_input_size() >= 0 && m->get_input_size() != prop_vec.size()) {
			return "size mismatch";
		}
	} else {
		if (m->get_input_size() >= 0 && m->get_input_size() != inputs.size()) {
			return "size mismatch";
		}
		cfg->xprops = inputs;
		if (!find_indexes(inputs, cfg->xinds)) {
			delete cfg;
			return "property not found";
		}
	}
	if (all_outputs) {
		if (m->get_output_size() >= 0 && m->get_output_size() != prop_vec.size()) {
			return "size mismatch";
		}
	} else {
		if (m->get_output_size() >= 0 && m->get_output_size() != outputs.size()) {
			return "size mismatch";
		}
		cfg->yprops = outputs;
		if (!find_indexes(outputs, cfg->yinds)) {
			delete cfg;
			return "property not found";
		}
	}
	active_models.push_back(cfg);
	return "";
}

void multi_model::unassign_model(const string &name) {
	std::list<model_config*>::iterator i;
	for (i = active_models.begin(); i != active_models.end(); ++i) {
		if ((**i).name == name) {
			active_models.erase(i);
			return;
		}
	}
}

bool multi_model::find_indexes(const vector<string> &props, vector<int> &indexes) {
	vector<string>::const_iterator i;

	for (i = props.begin(); i != props.end(); ++i) {
		int index = find(prop_vec.begin(), prop_vec.end(), *i) - prop_vec.begin();
		if (index == prop_vec.size()) {
			LOG(WARN) << "PROPERTY NOT FOUND " << *i << endl;
			return false;
		}
		indexes.push_back(index);
	}
	return true;
}

bool multi_model::report_error(int i, const vector<string> &args, ostream &os) const {
	if (reference_vals.empty()) {
		os << "no model error data" << endl;
		return false;
	}
	
	int dim = -1, start = 0, end = reference_vals.size() - 1;
	bool list = false, histo = false;
	if (i < args.size() && args[i] == "list") {
		list = true;
		++i;
	} else if (i < args.size() && args[i] == "histogram") {
		histo = true;
		++i;
	}
	if (i >= args.size()) {
		os << "specify a dimension" << endl;
		return false;
	}
	if (!parse_int(args[i], dim)) {
		dim = -1;
		for (int j = 0; j < prop_vec.size(); ++j) {
			if (prop_vec[j] == args[i]) {
				dim = j;
				break;
			}
		}
	}
	if (dim < 0) {
		os << "invalid dimension" << endl;
		return false;
	}
	if (++i < args.size()) {
		if (!parse_int(args[i], start)) {
			os << "require integer start time" << endl;
			return false;
		}
		if (start < 0 || start >= reference_vals.size()) {
			os << "start time must be in [0, " << reference_vals.size() - 1 << "]" << endl;
			return false;
		}
	}
	if (++i < args.size()) {
		if (!parse_int(args[i], end)) {
			os << "require integer end time" << endl;
			return false;
		}
		if (end <= start || end >= reference_vals.size()) {
			os << "end time must be in [start time, " << reference_vals.size() - 1 << "]" << endl;
			return false;
		}
	}
	
	if (list) {
		os << "num real pred error null norm" << endl;
		for (int j = start; j <= end; ++j) {
			os << setw(4) << j << " ";
			if (dim >= reference_vals[j].size() || dim >= predicted_vals[j].size()) {
				os << "NA" << endl;
			} else {
				double error, null_error, norm_error;
				error = fabs(reference_vals[j](dim) - predicted_vals[j](dim));
				if (j > 0 && (null_error = fabs(reference_vals[j-1](dim) - reference_vals[j](dim))) > 0) {
					norm_error = error / null_error;
				} else {
					null_error = -1;
					norm_error = -1;
				}
				os << reference_vals[j](dim) << " " << predicted_vals[j](dim) << " " 
				   << error << " ";
				if (null_error < 0) {
					os << "NA ";
				} else {
					os << null_error << " ";
				}
				if (norm_error < 0) {
					os << "NA";
				} else {
					os << norm_error;
				}
				os << endl;
			}
		}
	} else if (histo) {
		vector<double> errors;
		for (int j = start; j <= end; ++j) {
			errors.push_back(fabs(reference_vals[j](dim) - predicted_vals[j](dim)));
		}
		histogram(errors, 10, os) << endl;
	} else {
		double mean, mode, std, min, max;
		error_stats_by_dim(dim, start, end, mean, mode, std, min, max);
		os << "mean " << mean << endl
		   << "std  " << std << endl
		   << "mode " << mode << endl
		   << "min  " << min << endl
		   << "max  " << max << endl;
	}
	return true;
}

void multi_model::error_stats_by_dim(int dim, int start, int end, double &mean, double &mode, double &std, double &min, double &max) const {
	assert(dim >= 0 && start >= 0 && end <= reference_vals.size());
	double total = 0.0;
	min = INFINITY; 
	max = 0.0;
	std = 0.0;
	vector<double> ds;
	for (int i = start; i <= end; ++i) {
		if (dim >= reference_vals[i].size() || dim >= predicted_vals[i].size()) {
			continue;
		}
		double r = reference_vals[i](dim);
		double p = predicted_vals[i](dim);
		double d = fabs(r - p);
		ds.push_back(d);
		total += d;
		if (d < min) {
			min = d;
		}
		if (d > max) {
			max = d;
		}
	}
	mean = total / ds.size();
	for (int i = 0; i < ds.size(); ++i) {
		std += pow(ds[i] - mean, 2);
	}
	std = sqrt(std / ds.size());
	sort(ds.begin(), ds.end());
	mode = ds[ds.size() / 2];
}

void multi_model::report_model_config(model_config* c, ostream &os) const {
	const char *indent = "  ";
	os << c->name << endl;
	if (c->allx) {
		os << indent << "xdims: all" << endl;
	} else {
		os << indent << "xdims: ";
		for (int i = 0; i < c->xprops.size(); ++i) {
			os << c->xprops[i] << " ";
		}
		os << endl;
	}
	if (c->ally) {
		os << indent << "ydims: all" << endl;
	} else {
		os << indent << "ydims: ";
		for (int i = 0; i < c->yprops.size(); ++i) {
			os << c->yprops[i] << " ";
		}
		os << endl;
	}
}

bool multi_model::cli_inspect(int i, const vector<string> &args, ostream &os) const {
	if (i >= args.size()) {
		os << "available subqueries are: assignment error" << endl;
		return false;
	}
	if (args[i] == "assignment") {
		std::list<model_config*>::const_iterator j;
		for (j = active_models.begin(); j != active_models.end(); ++j) {
			report_model_config(*j, os);
		}
		return true;
	} else if (args[i] == "error") {
		return report_error(++i, args, os);
	}
	os << "no such query" << endl;
	return false;
}
