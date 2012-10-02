#include <cstdlib>
#include <map>
#include <iterator>
#include <iostream>
#include <sstream>
#include <limits>
#include <utility>
#include "scene.h"
#include "sgnode.h"
#include "common.h"
#include "drawer.h"
#include "filter.h"
#include "filter_table.h"
#include "serialize.h"

using namespace std;

const string root_name = "world";

/*
 Making this a global variable insures that all nodes in all scenes
 will have unique identifiers.
*/
int node_counter = 0;

/*
 Native properties are currently the position, rotation, and scaling
 transforms of a node, named px, py, pz, rx, ry, rz, sx, sy, sz.
*/
const int NUM_NATIVE_PROPS = 9;
const char *NATIVE_PROPS[] = { "px", "py", "pz", "rx", "ry", "rz", "sx", "sy", "sz" };

bool is_native_prop(const string &name, char &type, int &dim) {
	int d;
	if (name.size() != 2) {
		return false;
	}
	if (name[0] != 'p' && name[0] != 'r' && name[0] != 's') {
		return false;
	}
	d = name[1] - 'x';
	if (d < 0 || d > 2) {
		return false;
	}
	type = name[0];
	dim = d;
	return true;
}

bool parse_vec3(vector<string> &f, int &start, vec3 &v, string &error) {
	if (start + 3 > f.size()) {
		start = f.size();
		error = "expecting a number";
		return false;
	}
	for (int i = 0; i < 3; ++start, ++i) {
		if (!parse_double(f[start], v[i])) {
			error = "expecting a number";
			return false;
		}
	}
	return true;
}

bool parse_verts(vector<string> &f, int &start, ptlist &verts, string &error) {
	verts.clear();
	while (start < f.size()) {
		vec3 v;
		int i = start;
		if (!parse_vec3(f, start, v, error)) {
			return (i == start);  // end of list
		}
		verts.push_back(v);
	}
	return true;
}

bool parse_transforms(vector<string> &f, int &start, vec3 &pos, vec3 &rot, vec3 &scale, string &error) {	
	vec3 t;
	char type;
	
	if (f[start] != "p" && f[start] != "r" && f[start] != "s") {
		error = "expecting p, r, or s";
		return false;
	}
	type = f[start++][0];
	if (!parse_vec3(f, start, t, error)) {
		return false;
	}
	switch (type) {
		case 'p':
			pos = t;
			break;
		case 'r':
			rot = t;
			break;
		case 's':
			scale = t;
			break;
		default:
			assert(false);
	}
	return true;
}

void scene_sig::entry::serialize(ostream &os) const {
	serializer(os) << id << name << type << start << target << props;
}

void scene_sig::entry::unserialize(istream &is) {
	unserializer(is) >> id >> name >> type >> start >> target >> props;
}

void scene_sig::serialize(ostream &os) const {
	::serialize(s, os);
}

void scene_sig::unserialize(istream &is) {
	::unserialize(s, is);
}

void scene_sig::add(const scene_sig::entry &e) {
	int curr_dim = dim();
	s.push_back(e);
	s.back().start = curr_dim;
}

int scene_sig::dim() const {
	int d = 0;
	for (int i = 0; i < s.size(); ++i) {
		d += s[i].props.size();
	}
	return d;
}

bool scene_sig::get_dim(const string &obj, const string &prop, int &obj_ind, int &prop_ind) const {
	for (int i = 0; i < s.size(); ++i) {
		const entry &e = s[i];
		if (e.name == obj) {
			for (int j = 0; j < e.props.size(); ++j) {
				if (e.props[j] == prop) {
					obj_ind = i;
					prop_ind = e.start + j;
					return true;
				}
			}
			return false;
		}
	}
	return false;
}

scene::scene(const string &name, drawer *d) 
: name(name), draw(d)
{
	root = new group_node(root_name);
	root_id = node_counter++;
	nodes[root_id].node = root;
	root->listen(this);
	node_ids[root_name] = root_id;
	dirty = true;
	sig_dirty = true;
}

scene::~scene() {
	delete root;
}

scene *scene::clone() const {
	scene *c = new scene(name, NULL);  // don't display copies
	string name;
	std::vector<sgnode*> all_nodes;
	std::vector<sgnode*>::const_iterator i;
	
	c->root = root->clone()->as_group();
	c->root->walk(all_nodes);
	
	for(int i = 0; i < all_nodes.size(); ++i) {
		sgnode *n = all_nodes[i];
		const node_info *info = get_node_info(n->get_name());
		node_info &cinfo = c->nodes[node_counter++];
		cinfo.node = n;
		cinfo.props = info->props;
		n->listen(c);
		if (!n->is_group()) {
			c->cdetect.add_node(n);
		}
	}
	
	return c;
}

scene::node_info *scene::get_node_info(int i) {
	node_info *info = map_get(nodes, i);
	return info;
}

const scene::node_info *scene::get_node_info(int i) const {
	const node_info *info = map_get(nodes, i);
	return info;
}

scene::node_info *scene::get_node_info(const string &name) {
	int i;
	if (!map_get(node_ids, name, i)) {
		return NULL;
	}
	return get_node_info(i);
}

const scene::node_info *scene::get_node_info(const string &name) const {
	int i;
	if (!map_get(node_ids, name, i)) {
		return NULL;
	}
	return get_node_info(i);
}

sgnode *scene::get_node(const string &name) {
	node_info *info = get_node_info(name);
	if (!info) {
		return NULL;
	}
	return info->node;
}

sgnode const *scene::get_node(const string &name) const {
	const node_info *info = get_node_info(name);
	if (!info) {
		return NULL;
	}
	return info->node;
}

sgnode *scene::get_node(int i) {
	node_info *info = get_node_info(i);
	if (!info) {
		return NULL;
	}
	return info->node;
}

const sgnode *scene::get_node(int i) const {
	const node_info *info = get_node_info(i);
	if (!info) {
		return NULL;
	}
	return info->node;
}

group_node *scene::get_group(const string &name) {
	sgnode *n = get_node(name);
	if (n) {
		return n->as_group();
	}
	return NULL;
}

void scene::get_all_nodes(vector<sgnode*> &n) {
	node_table::const_iterator i;
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		if (i->first != root_id) {
			n.push_back(i->second.node);
		}
	}
}

void scene::get_all_nodes(vector<const sgnode*> &n) const {
	n.reserve(nodes.size());
	node_table::const_iterator i;
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		n.push_back(i->second.node);
	}
}

void scene::get_all_node_indices(vector<int> &inds) const {
	node_table::const_iterator i;
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		inds.push_back(i->first);
	}
}

void scene::get_nodes(const vector<int> &inds, vector<const sgnode*> &n) const {
	n.resize(inds.size(), NULL);
	for (int i = 0; i < inds.size(); ++i) {
		const node_info *info = map_get(nodes, inds[i]);
		assert(info);
		n[i] = info->node;
	}
}

bool scene::add_node(const string &name, sgnode *n) {
	group_node *par = get_group(name);
	if (!par) {
		return false;
	}
	par->attach_child(n);
	/* rest is handled in node_update */
	return true;
}

bool scene::del_node(const string &name) {
	delete get_node_info(name)->node;
	/* rest is handled in node_update */
	return true;
}

void scene::clear() {
	for (int i = 0; i < root->num_children(); ++i) {
		delete root->get_child(i);
	}
}

int scene::parse_add(vector<string> &f, string &error) {
	sgnode *n = NULL;
	group_node *par = NULL;
	vec3 pos = vec3::Zero(), rot = vec3::Zero(), scale = vec3::Constant(1.0);

	if (f.size() < 2) {
		return f.size();
	}
	if (get_node(f[0])) {
		error = "node already exists";
		return 0;
	}
	par = get_group(f[1]);
	if (!par) {
		error = "parent node does not exist";
		return 1;
	}
	
	int p = 2;
	while (p < f.size()) {
		if (n != NULL && (f[p] == "v" || f[p] == "b")) {
			error = "more than one geometry specified";
			return p;
		}
		if (f[p] == "v") {
			ptlist verts;
			if (!parse_verts(f, ++p, verts, error)) {
				return p;
			}
			n = new convex_node(f[0], verts);
		} else if (f[p] == "b") {
			++p;
			double radius;
			if (p >= f.size() || !parse_double(f[p], radius)) {
				error = "invalid radius";
				return p;
			}
			n = new ball_node(f[0], radius);
			++p;
		} else if (!parse_transforms(f, p, pos, rot, scale, error)) {
			return p;
		}
	}
	
	if (!n) {
		n = new group_node(f[0]);
	}
	n->set_trans(pos, rot, scale);
	par->attach_child(n);
	return -1;
}

int scene::parse_del(vector<string> &f, string &error) {
	if (f.size() < 1) {
		error = "expecting node name";
		return f.size();
	}
	if (!del_node(f[0])) {
		error = "node does not exist";
		return 0;
	}
	return -1;
}

int scene::parse_change(vector<string> &f, string &error) {
	int p;
	sgnode *n;
	vec3 pos, rot, scale;

	if (f.size() < 1) {
		error = "expecting node name";
		return f.size();
	}
	if (!(n = get_node(f[0]))) {
		error = "node does not exist";
		return 0;
	}
	n->get_trans(pos, rot, scale);
	p = 1;
	while (p < f.size()) {
		if (!parse_transforms(f, p, pos, rot, scale, error)) {
			return p;
		}
	}
	n->set_trans(pos, rot, scale);
	return -1;
}

int scene::parse_property(vector<string> &f, string &error) {
	int i, p = 0;
	if (p >= f.size()) {
		error = "expecting node name";
		return p;
	}
	if (!map_get(node_ids, f[p++], i) || !nodes[i].node) {
		error = "node does not exist";
		return p;
	}
	if (p >= f.size()) {
		error = "expecting property name";
		return p;
	}
	string prop = f[p++];
	
	double val;
	if (!parse_double(f[p], val)) {
		error = "expecting a number";
		return p;
	}
	nodes[i].props[prop] = val;
	return -1;
}

void scene::parse_sgel(const string &s) {
	vector<string> lines, fields;
	vector<string>::iterator i;
	char cmd;
	int errfield;
	string error;
	
	LOG(SGEL) << "received sgel" << endl << "---------" << endl << s << endl << "---------" << endl;
	split(s, "\n", lines);
	for (i = lines.begin(); i != lines.end(); ++i) {
		split(*i, " \t", fields);
		
		if (fields.size() == 0)
			continue;
		if (fields[0].size() != 1) {
			cerr << "expecting a|d|c|p|t at beginning of line '" << *i << "'" << endl;
			exit(1);
		}
		
		cmd = fields[0][0];
		fields.erase(fields.begin());
		error = "unknown error";
		
		switch(cmd) {
			case 'a':
				errfield = parse_add(fields, error);
				break;
			case 'd':
				errfield = parse_del(fields, error);
				break;
			case 'c':
				errfield = parse_change(fields, error);
				break;
			case 'p':
				errfield = parse_property(fields, error);
				break;
			default:
				cerr << "expecting a|d|c|p|t at beginning of line '" << *i << "'" << endl;
				exit(1);
		}
		
		if (errfield >= 0) {
			cerr << "error in field " << errfield + 1 << " of line '" << *i << "': " << error << endl;
			exit(1);
		}
	}
}

void scene::get_property_names(int i, vector<string> &names) const {
	const node_info *info = get_node_info(i);
	assert(info);
	
	for (int j = 0; j < NUM_NATIVE_PROPS; ++j) {
		names.push_back(NATIVE_PROPS[j]);
	}
	
	property_map::const_iterator k = info->props.begin();
	property_map::const_iterator end = info->props.end();
	for (; k != end; ++k) {
		names.push_back(k->first);
	}
}

void scene::get_properties(rvec &vals) const {
	node_table::const_iterator i;
	property_map::const_iterator j;
	int k = 0;
	
	vals.resize(get_dof());
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		const node_info &info = i->second;
		for (const char *t = "prs"; *t != '\0'; ++t) {
			vec3 trans = info.node->get_trans(*t);
			vals.segment(k, 3) = trans;
			k += 3;
		}
		for (j = info.props.begin(); j != info.props.end(); ++j) {
			vals[k++] = j->second;
		}
	}
}

bool scene::get_property(const string &name, const string &prop, float &val) const {
	property_map::const_iterator j;
	char type; int d;

	const node_info *info = get_node_info(name);
	if (is_native_prop(prop, type, d)) {
		val = info->node->get_trans(type)[d];
	} else {
		if ((j = info->props.find(prop)) == info->props.end()) {
			return false;
		}
		val = j->second;
	}
	return true;
}

bool scene::add_property(const string &name, const string &prop, float val) {
	property_map::iterator j;
	char type; int d;

	node_info *info = get_node_info(name);
	if (is_native_prop(prop, type, d)) {
		return false;
	} else {
		if ((j = info->props.find(prop)) != info->props.end()) {
			return false;
		}
		info->props[prop] = val;
	}
	return true;
}

bool scene::set_property(const string &obj, const string &prop, float val) {
	char type; int d;
	node_info *info = get_node_info(name);
	assert(info);
	if (is_native_prop(prop, type, d)) {
		vec3 trans = info->node->get_trans(type);
		trans[d] = val;
		info->node->set_trans(type, trans);
	} else {
		property_map::iterator i;
		if ((i = info->props.find(prop)) == info->props.end()) {
			return false;
		}
		i->second = val;
	}
	return true;
}

bool scene::set_properties(const rvec &vals) {
	node_table::iterator i;
	const char *types = "prs";
	vec3 trans;
	
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		node_info &info = i->second;
		int k1, k2, l = 0;
		for (k1 = 0; k1 < 3; ++k1) {
			for (k2 = 0; k2 < 3; ++k2) {
				trans[k2] = vals[l++];
				if (l >= vals.size()) {
					return false;
				}
			}
			info.node->set_trans(types[k1], trans);
		}
		
		property_map::iterator j;
		for (j = info.props.begin(); j != info.props.end(); ++j) {
			j->second = vals[l++];
			if (l >= vals.size()) {
				return false;
			}
		}
	}
	return true;
}

void scene::remove_property(const std::string &name, const std::string &prop) {
	node_info *info = get_node_info(name);
	property_map::iterator j = info->props.find(prop);
	assert(j != info->props.end());
	info->props.erase(j);
}

int scene::num_nodes() const {
	return nodes.size();
}

int scene::get_dof() const {
	int dof = 0;
	node_table::const_iterator i;
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		dof += NUM_NATIVE_PROPS + i->second.props.size();
	}
	return dof;
}

void scene::node_update(sgnode *n, sgnode::change_type t, int added_child) {
	sgnode *child;
	group_node *g;
	int i;
	
	switch (t) {
		case sgnode::CHILD_ADDED:
			g = n->as_group();
			child = g->get_child(added_child);
			child->listen(this);
			i = node_counter++;
			node_ids[child->get_name()] = i;
			nodes[i].node = child;
			sig_dirty = true;
			if (!child->is_group()) {
				cdetect.add_node(child);
			}
			if (draw) {
				draw->add(name, child);
			}
			break;
		case sgnode::DELETED:
			if (!n->is_group()) {
				cdetect.del_node(n);
			}
			if (!map_get(node_ids, n->get_name(), i)) {
				assert(false);
			}
			nodes.erase(i);
			node_ids.erase(n->get_name());
			sig_dirty = true;
			if (draw && n->get_name() != "world") {
				draw->del(name, n);
			}
			break;
		case sgnode::SHAPE_CHANGED:
			if (!n->is_group()) {
				cdetect.update_points(n);
				if (draw) {
					draw->change(name, n, drawer::SHAPE);
				}
			}
			break;
		case sgnode::TRANSFORM_CHANGED:
			if (!n->is_group()) {
				cdetect.update_transform(n);
			}
			if (draw) {
				draw->change(name, n, drawer::POS | drawer::ROT | drawer::SCALE);
			}
			break;
	}
	dirty = true;
}

bool scene::intersects(const sgnode *a, const sgnode *b) const {
	if (a == b) {
		return true;
	}
	const collision_table &c = const_cast<scene*>(this)->cdetect.get_collisions();
	return c.find(make_pair(a, b)) != c.end() || c.find(make_pair(b, a)) != c.end();
}

void scene::calc_relations(relation_table &rels) const {
	get_filter_table().update_relations(this, 0, rels);
}

void scene::print_relations(ostream &os) const {
	relation_table rels;
	relation_table::const_iterator i;
	get_filter_table().update_relations(this, 0, rels);
	for (i = rels.begin(); i != rels.end(); ++i) {
		set<tuple> args;
		set<tuple>::iterator j;
		i->second.drop_first(args);
		for (j = args.begin(); j != args.end(); ++j) {
			os << i->first << "(";
			for (int k = 0; k < j->size() - 1; ++k) {
				os << get_node((*j)[k])->get_name() << ",";
			}
			os << get_node(j->back())->get_name() << ")" << endl;
		}
	}
}

void scene::update_sig() const {
	sig.clear();
	node_table::const_iterator i;
	for (i = nodes.begin(); i != nodes.end(); ++i) {
		scene_sig::entry e;
		e.id = i->first;
		e.name = i->second.node->get_name();
		get_property_names(i->first, e.props);
		if (i == nodes.begin()) {
			e.type = 0;
		} else {
			e.type = e.props.size(); // have to change this later
		}
		sig.add(e);
	}
}

const scene_sig &scene::get_signature() const {
	if (sig_dirty) {
		update_sig();
	}
	return sig;
}

