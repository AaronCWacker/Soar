#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <iterator>
#include <utility>
#include <algorithm>
#include <fstream>

#include "svs.h"
#include "command.h"
#include "sgnode.h"
#include "soar_interface.h"
#include "scene.h"
#include "common.h"
#include "model.h"
#include "filter_table.h"

using namespace std;

typedef map<wme*,command*>::iterator cmd_iter;


svs_interface *make_svs(agent *a) {
	return new svs(a);
}

sgwme::sgwme(soar_interface *si, Symbol *ident, sgwme *parent, sgnode *node) 
: soarint(si), id(ident), parent(parent), node(node)
{
	node->listen(this);
	name_wme = soarint->make_wme(id, si->get_common_syms().id, node->get_name());
	
	if (node->is_group()) {
		group_node *g = node->as_group();
		for (int i = 0; i < g->num_children(); ++i) {
			add_child(g->get_child(i));
		}
	}
}

sgwme::~sgwme() {
	map<sgwme*,wme*>::iterator i;

	if (node) {
		node->unlisten(this);
	}
	soarint->remove_wme(name_wme);
	for (i = childs.begin(); i != childs.end(); ++i) {
		i->first->parent = NULL;
		soarint->remove_wme(i->second);
	}
	if (parent) {
		map<sgwme*, wme*>::iterator ci = parent->childs.find(this);
		assert(ci != parent->childs.end());
		soarint->remove_wme(ci->second);
		parent->childs.erase(ci);
	}
}

void sgwme::node_update(sgnode *n, sgnode::change_type t, int added_child) {
	group_node *g;
	switch (t) {
		case sgnode::CHILD_ADDED:
			g = node->as_group();
			add_child(g->get_child(added_child));
			break;
		case sgnode::DELETED:
			node = NULL;
			delete this;
			break;
		default:
			break;
	};
}

void sgwme::add_child(sgnode *c) {
	char letter;
	string cname = c->get_name();
	sgwme *child;
	
	if (cname.size() == 0 || !isalpha(cname[0])) {
		letter = 'n';
	} else {
		letter = cname[0];
	}
	wme *cid_wme = soarint->make_id_wme(id, "child");
	
	child = new sgwme(soarint, soarint->get_wme_val(cid_wme), this, c);
	childs[child] = cid_wme;
}

svs_state::svs_state(svs *svsp, Symbol *state, soar_interface *si)
: svsp(svsp), parent(NULL), state(state), si(si), level(0),
  scene_num(-1), scene_num_wme(NULL), scn(NULL), scene_link(NULL), model_link(NULL)
{
	assert (si->is_top_state(state));
	outspec = svsp->get_output_spec();
	init();
}

svs_state::svs_state(Symbol *state, svs_state *parent)
: parent(parent), state(state), svsp(parent->svsp), si(parent->si),
  outspec(parent->outspec), level(parent->level+1), scene_num(-1),
  scene_num_wme(NULL), scn(NULL), scene_link(NULL), model_link(NULL)
{
	assert (si->get_parent_state(state) == parent->state);
	init();
}

svs_state::~svs_state() {
	map<wme*, command*>::iterator i;
	for (i = curr_cmds.begin(); i != curr_cmds.end(); ++i) {
		delete i->second;
	}
	
	delete scn; // results in root being deleted also
	delete mmdl;
	
	if (model_link) {
		svsp->set_model_root(NULL);
	}
}

void svs_state::init() {
	string name;
	common_syms &cs = si->get_common_syms();
	
	si->get_name(state, name);
	svs_link = si->get_wme_val(si->make_id_wme(state, cs.svs));
	cmd_link = si->get_wme_val(si->make_id_wme(svs_link, cs.cmd));
	scene_link = si->get_wme_val(si->make_id_wme(svs_link, cs.scene));
	if (parent) {
		scn = parent->scn->clone(name, svsp->get_drawer());
	} else {
		scn = new scene(name, svsp->get_drawer());
	}
	root = new sgwme(si, scene_link, (sgwme*) NULL, scn->get_root());
	mmdl = new multi_model(svsp->get_models());
	learn_models = false;
	test_models = false;
	
	if (!parent) {
		model_link = si->get_wme_val(si->make_id_wme(svs_link, cs.models));
		svsp->set_model_root(model_link);
	}
}

void svs_state::update_scene_num() {
	long curr;
	if (scene_num_wme) {
		if (!si->get_val(si->get_wme_val(scene_num_wme), curr)) {
			exit(1);
		}
		if (curr == scene_num) {
			return;
		}
		si->remove_wme(scene_num_wme);
	}
	if (scene_num >= 0) {
		scene_num_wme = si->make_wme(svs_link, "scene-num", scene_num);
	}
}

void svs_state::update_cmd_results(bool early) {
	cmd_iter i;
	if (early) {
		set_default_output();
	}
	for (i = curr_cmds.begin(); i != curr_cmds.end(); ++i) {
		if (i->second->early() == early) {
			i->second->update();
		}
	}
}

void svs_state::process_cmds() {
	wme_list all;
	wme_list::iterator i;
	cmd_iter j;

	si->get_child_wmes(cmd_link, all);

	for (j = curr_cmds.begin(); j != curr_cmds.end(); ) {
		if ((i = find(all.begin(), all.end(), j->first)) == all.end()) {
			delete j->second;
			curr_cmds.erase(j++);
		} else {
			all.erase(i);
			++j;
		}
	}
	
	// all now contains only new commands
	for (i = all.begin(); i != all.end(); ++i) {
		command *c = make_command(this, *i);
		if (c) {
			curr_cmds[*i] = c;
		} else {
			string attr;
			si->get_val(si->get_wme_attr(*i), attr);
			cerr << "could not create command " << attr << endl;
		}
	}
}

void svs_state::clear_scene() {
	scn->clear();
}

void svs_state::update_models() {
	function_timer t(timers.get_or_add("model"));
	scene_sig curr_sig, out_names;
	output_spec::const_iterator i;
	rvec curr_pvals, out;
	relation_table curr_rels;
	
	if (level > 0) {
		/* No legitimate information to learn from imagined states */
		return;
	}
	
	scn->get_properties(curr_pvals);
	get_output(out);
	curr_sig = scn->get_signature();
	scn->calc_relations(curr_rels);
	
	// add an entry to the signature for the output
	scene_sig::entry out_entry;
	out_entry.id = -2;
	out_entry.name = "output";
	out_entry.type = -1;
	for (int i = 0; i < outspec->size(); ++i) {
		out_entry.props.push_back(outspec->at(i).name);
	}
	curr_sig.add(out_entry);
	
	if (prev_sig == curr_sig) {
		rvec x(prev_pvals.size() + out.size());
		if (out.size() > 0) {      // work-around for eigen bug when out.size() == 0
			x << prev_pvals, out;
		} else {
			x = prev_pvals;
		}
		if (test_models) {
			mmdl->test(curr_sig, prev_rels, x, curr_pvals);
		}
		if (learn_models) {
			mmdl->learn(curr_sig, prev_rels, x, curr_pvals);
		}
	}
	prev_sig = curr_sig;
	prev_rels = curr_rels;
	prev_pvals = curr_pvals;
}

void svs_state::set_output(const rvec &out) {
	assert(out.size() == outspec->size());
	next_out = out;
}

void svs_state::set_default_output() {
	next_out.resize(outspec->size());
	for (int i = 0; i < outspec->size(); ++i) {
		next_out[i] = (*outspec)[i].def;
	}
}

bool svs_state::get_output(rvec &out) const {
	if (next_out.size() != outspec->size()) {
		out.resize(outspec->size());
		for (int i = 0; i < outspec->size(); ++i) {
			out[i] = (*outspec)[i].def;
		}
		return false;
	} else {
		out = next_out;
		return true;
	}
}

bool svs_state::cli_inspect(int first_arg, const vector<string> &args, ostream &os) {
	if (first_arg >= args.size() || args[first_arg] == "help") {
		os << "available subqueries: atoms prediction out timing command" << endl;
		return false;
	}
	if (args[first_arg] == "prediction") {
		return mmdl->cli_inspect(first_arg + 1, args, os);
	} else if (args[first_arg] == "props") {
		const scene_sig &s = scn->get_signature();
		rvec v;
		scn->get_properties(v);
		table_printer t;
		int c = 0;
		for (int i = 0; i < s.size(); ++i) {
			for (int j = 0; j < s[i].props.size(); ++j) {
				t.add_row() << c;
				if (j == 0) {
					t << s[i].name;
				} else {
					t.skip(1);
				}
				t << s[i].props[j] << v(c);
				c++;
			}
		}
		t.print(os);
		return true;
	} else if (args[first_arg] == "out") {
		if (next_out.size() == 0) {
			os << "no output" << endl;
		} else {
			table_printer t;
			for (int i = 0; i < next_out.size(); ++i) {
				t.add_row() << (*outspec)[i].name << next_out(i);
			}
			t.print(os);
		}
		return true;
	} else if (args[first_arg] == "relations") {
		report_relations(first_arg + 1, args, os);
		return true;
	} else if (args[first_arg] == "timing") {
		timers.report(os);
		return true;
	} else if (args[first_arg] == "command") {
		if (first_arg == args.size() - 1) {
			os << "specify a command id" << endl;
			return false;
		}
		map<wme*, command*>::const_iterator i;
		for (i = curr_cmds.begin(); i != curr_cmds.end(); ++i) {
			string id;
			if (!si->get_name(si->get_wme_val(i->first), id)) {
				assert(false);
			}
			if (id == args[first_arg+1]) {
				i->second->cli_inspect(os);
				return true;
			}
		}
		os << "no such command" << endl;
		return false;
	} else if (args[first_arg] == "model") {
		return mmdl->cli_inspect(first_arg + 1, args, os);
	} else if (args[first_arg] == "learn_models") {
		return read_on_off(args, first_arg + 1, os, learn_models);
	} else if (args[first_arg] == "test_models") {
		return read_on_off(args, first_arg + 1, os, test_models);
	}
	
	os << "no such query" << endl;
	return false;
}

void svs_state::report_relations(int first_arg, const vector<string> &args, ostream &os) const {
	relation_table rels;
	relation_table::const_iterator i, begin, end;
	relation::const_iterator j;
	scn->calc_relations(rels);
	bool print_names;
	
	if (first_arg < args.size()) {
		begin = end = rels.find(args[first_arg]);
		if (end == rels.end()) {
			os << "relation not found" << endl;
		} else {
			++end;
		}
		print_names = false;
	} else {
		begin = rels.begin();
		end = rels.end();
		print_names = true;
	}
	
	for (i = begin; i != end; ++i) {
		const relation &r = i->second;
		table_printer t;
		
		if (print_names)
			os << i->first << endl;
		for (j = r.begin(); j != r.end(); ++j) {
			t.add_row();
			for (int k = 1; k < j->size(); ++k) {
				sgnode *n = scn->get_node((*j)[k]);
				assert(n != NULL);
				t << n->get_name();
			}
		}
		t.print(os);
	}
}

svs::svs(agent *a)
: learn(false), model_root(NULL)
{
	si = new soar_interface(a);
}

svs::~svs() {
	vector<svs_state*>::iterator i;
	for (i = state_stack.begin(); i != state_stack.end(); ++i) {
		delete *i;
	}
	delete si;
	map<string, model*>::iterator j;
	for (j = models.begin(); j != models.end(); ++j) {
		delete j->second;
	}
}

void svs::state_creation_callback(Symbol *state) {
	string type, msg;
	svs_state *s;
	
	if (state_stack.empty()) {
		s = new svs_state(this, state, si);
	} else {
		s = new svs_state(state, state_stack.back());
	}
	
	state_stack.push_back(s);
}

void svs::state_deletion_callback(Symbol *state) {
	svs_state *s;
	s = state_stack.back();
	assert(state == s->get_state());
	state_stack.pop_back();
	delete s;
}

void svs::proc_input(svs_state *s) {
	for (int i = 0; i < env_inputs.size(); ++i) {
		strip(env_inputs[i], " \t");
		if (env_inputs[i][0] == 'o') {
			int err = parse_output_spec(env_inputs[i]);
			if (err >= 0) {
				cerr << "error in output description at field " << err << endl;
			}
		} else {
			s->get_scene()->parse_sgel(env_inputs[i]);
		}
	}
	env_inputs.clear();
}

void svs::output_callback() {
	function_timer t(timers.get_or_add("output"));
	
	vector<svs_state*>::iterator i;
	string sgel;
	svs_state *topstate = state_stack.front();
	
	for (i = state_stack.begin(); i != state_stack.end(); ++i) {
		(**i).process_cmds();
	}
	for (i = state_stack.begin(); i != state_stack.end(); ++i) {
		(**i).update_cmd_results(true);
	}
	
	/* environment IO */
	rvec out;
	topstate->get_output(out);
	
	assert(outspec.size() == out.size());
	
	stringstream ss;
	for (int i = 0; i < outspec.size(); ++i) {
		ss << outspec[i].name << " " << out[i] << endl;
	}
	env_output = ss.str();
}

void svs::input_callback() {
	function_timer t(timers.get_or_add("input"));
	
	svs_state *topstate = state_stack.front();
	proc_input(topstate);
	if (learn) {
		topstate->update_models();
	}
	
	vector<svs_state*>::iterator i;
	for (i = state_stack.begin(); i != state_stack.end(); ++i) {
		(**i).update_cmd_results(false);
	}
}

/*
 This is a naive implementation. If this method is called concurrently
 with proc_input, the env_inputs vector will probably become
 inconsistent. This eventually needs to be replaced by a thread-safe FIFO.
*/
void svs::add_input(const string &in) {
	split(in, "\n", env_inputs);
}

string svs::get_output() const {
	return env_output;
}

bool svs::do_command(const vector<string> &args, stringstream &out) {
	if (args.size() < 2) {
		out << "subqueries are timing filters log model, or a state level to inspect state [0 - " << state_stack.size() - 1 << "]" << endl;
		return false;
	}
	if (args[1] == "timing") {
		timers.report(out);
		return true;
	} else if (args[1] == "filters") {
		get_filter_table().get_timers().report(out);
		return true;
	} else if (args[1] == "log") {
		if (args.size() < 3) {
			for (int i = 0; i < NUM_LOG_TYPES; ++i) {
				out << log_type_names[i] << (LOG.is_on(static_cast<log_type>(i)) ? " on" : " off") << endl;
			}
			return true;
		}
		if (args[2] == "on") {
			if (args.size() < 4) {
				for (int i = 0; i < NUM_LOG_TYPES; ++i) {
					LOG.turn_on(static_cast<log_type>(i));
				}
				return true;
			} else {
				for (int i = 0; i < NUM_LOG_TYPES; ++i) {
					if (args[3] == log_type_names[i]) {
						LOG.turn_on(static_cast<log_type>(i));
						return true;
					}
				}
				out << "no such log" << endl;
				return false;
			}
		} else if (args[2] == "off") {
			if (args.size() < 4) {
				for (int i = 0; i < NUM_LOG_TYPES; ++i) {
					LOG.turn_off(static_cast<log_type>(i));
				}
				return true;
			} else {
				for (int i = 0; i < NUM_LOG_TYPES; ++i) {
					if (args[3] == log_type_names[i]) {
						LOG.turn_off(static_cast<log_type>(i));
						return true;
					}
				}
				out << "no such log" << endl;
				return false;
			}
		} else {
			out << "expecting on/off" << endl;
			return false;
		}
	} else if (args[1] == "learn") {
		return read_on_off(args, 2, out, learn);
	} else if (args[1] == "model") {
		map<string, model*>::const_iterator i;
		if (args.size() > 2) {
			if ((i = models.find(args[2])) == models.end()) {
				out << "no such model" << endl;
				return false;
			}
			return i->second->cli_inspect(3, args, out);
		}
		for (i = models.begin(); i != models.end(); ++i) {
			out << i->first << "\t" << i->second->get_type() << endl;
		}
		return true;
	}
	
	int level;
	if (!parse_int(args[1], level)) {
		out << "no such query" << endl;
		return false;
	} else if (level < 0 || level >= state_stack.size()) {
		out << "invalid level" << endl;
		return false;
	}
	return state_stack[level]->cli_inspect(2, args, out);
}

int svs::parse_output_spec(const string &s) {
	vector<string> fields;
	vector<double> vals(4);
	output_dim_spec sp;
	char *end;
	
	split(s, " \t\n", fields);
	assert(fields[0] == "o");
	if ((fields.size() - 1) % 5 != 0) {
		return fields.size();
	}
	
	output_spec new_spec;
	for (int i = 1; i < fields.size(); i += 5) {
		sp.name = fields[i];
		for (int j = 0; j < 4; ++j) {
			if (!parse_double(fields[i + j + 1], vals[j])) {
				return i + j + 1;
			}
		}
		sp.min = vals[0];
		sp.max = vals[1];
		sp.def = vals[2];
		sp.incr = vals[3];
		new_spec.push_back(sp);
	}
	outspec = new_spec;
	return -1;
}

bool svs::add_model(const string &name, model *m) {
	if (models.find(name) != models.end()) {
		return false;
	}
	models[name] = m;
	if (model_root) {
		Symbol *id = si->get_wme_val(si->make_id_wme(model_root, m->get_name()));
		m->set_wm_root(id);
	}
	return true;
}

void svs::set_model_root(Symbol *root) {
	model_root = root;
	if (model_root) {
		std::map<std::string, model*>::iterator i;
		for (i = models.begin(); i != models.end(); ++i) {
			model *m = i->second;
			Symbol *id = si->get_wme_val(si->make_id_wme(model_root, m->get_name()));
			m->set_wm_root(id);
		}
	}
}
