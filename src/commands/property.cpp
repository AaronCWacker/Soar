#include <iostream>
#include <string>
#include "command.h"
#include "filter.h"
#include "svs.h"
#include "scene.h"

using namespace std;

class property_command : public command {
public:
	property_command(svs_state *state, Symbol *root)
	: command(state, root), root(root), val(NULL), res(NULL)
	{
		si = state->get_svs()->get_soar_interface();
		scn = state->get_scene();
	}
	
	~property_command() {
		if (val) {
			delete val;
		}
	}
	
	string description() {
		return string("property");
	}
	
	bool update_sub() {
		if (changed()) {
			if (val) {
				delete val;
			}
			if (!parse()) {
				return false;
			}
			res = val->get_result();
		}
		
		if (val) {
			if (!val->update()) {
				set_status("filter error");
				return false;
			}
			if (res->num_current() == 0) {
				set_status("no results");
				return false;
			}
			float v;
			if (!get_filter_val(res->get_current(0), v)) {
				set_status("result not of type float");
				return false;
			}

			if (!scn->set_property(id, prop, v)) {
				set_status("failed to set property");
				return false;
			}
			set_status("success");
		}
		return true;
	}
	
	bool early() { return false; }
	
	bool parse() {
		wme *idwme, *propwme, *valwme;
		
		if (!si->find_child_wme(root, "id", idwme)) {
			set_status("no object id specified");
			return false;
		}
		if (!si->get_val(si->get_wme_val(idwme), id)) {
			set_status("object id must be a string");
			return false;
		}
		
		if (!si->find_child_wme(root, "property", propwme)) {
			set_status("no property specified");
			return false;
		}
		if (!si->get_val(si->get_wme_val(propwme), prop)) {
			set_status("property name must be a string");
			return false;
		}
		
		if (!si->find_child_wme(root, "value", valwme)) {
			set_status("no value specified");
			return false;
		}
		
		val = parse_filter_spec(si, si->get_wme_val(valwme), scn);
		if (!val) {
			set_status("incorrect filter syntax");
			return false;
		}
		return true;
	}
	
private:
	Symbol         *root;
	scene          *scn;
	soar_interface *si;
	filter         *val;
	filter_result  *res;
	string          id;
	string          prop;
};

command *_make_property_command_(svs_state *state, Symbol *root) {
	return new property_command(state, root);
}
