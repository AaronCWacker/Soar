#ifndef EM_H
#define EM_H

#include <set>
#include <vector>
#include "linear.h"
#include "common.h"
#include "timer.h"
#include "foil.h"
#include "serializable.h"
#include "relation.h"
#include "mat.h"
#include "scene_sig.h"
#include "lwr.h"

class LDA;

class EM : public serializable {
public:
	EM();
	~EM();
	
	void learn(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, const rvec &y);
	bool run(int maxiters);
	bool predict(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, int &mode, rvec &y);
	// Return the mode with the model that best fits (x, y)
	int best_mode(int target, const scene_sig &sig, const rvec &x, double y, double &besterror) const;
	bool cli_inspect(int first_arg, const std::vector<std::string> &args, std::ostream &os);
	void serialize(std::ostream &os) const;
	void unserialize(std::istream &is);
	
private:
	class train_data : public serializable {
	public:
		rvec x, y;
		int target;
		int time;
		int sig_index;
		
		std::vector<double> mode_prob; // mode_prob[i] = probability that data point belongs to mode i
		std::vector<bool> prob_stale;
		
		int mode;                      // MAP (Maximum A Posteriori) mode, should always be argmax(mode_prob[i])
		
		// the following are always associated with the MAP mode
		std::vector<int> obj_map;      // object variable in model -> object index in instance
	
		train_data() : target(-1), time(-1), sig_index(-1), mode(0) {}
		void serialize(std::ostream &os) const;
		void unserialize(std::istream &is);
	};
	
	class classifier : public serializable {
	public:
		classifier() : const_vote(0) {}
		
		int const_vote;
		clause_vec clauses;
		std::vector<relation*> residuals;
		std::vector<LDA*> ldas;
		
		void inspect(std::ostream &os) const;
		void serialize(std::ostream &os) const;
		void unserialize(std::istream &is);
	};
	
	class sig_info : public serializable {
	public:
		sig_info();
		scene_sig sig;
		std::vector<int> members;  // indexes of data points with this sig
		LWR lwr;                   // lwr model trained on all points of this sig

		void serialize(std::ostream &os) const;
		void unserialize(std::istream &is);
	};
	
	class mode_info : public serializable {
	public:
		mode_info(bool noise, const std::vector<train_data*> &data, const std::vector<sig_info*> &sigs);
		~mode_info();
		
		void serialize(std::ostream &os) const;
		void unserialize(std::istream &is);
		
		bool cli_inspect(int first, const std::vector<std::string> &args, std::ostream &os);
		void add_example(int i);
		void del_example(int i);
		void predict(const scene_sig &s, const rvec &x, const std::vector<int> &obj_map, rvec &y) const;
		void largest_const_subset(std::vector<int> &subset);
		const std::set<int> &get_noise(int sigindex) const;
		void get_noise_sigs(std::vector<int> &sigs);
		double calc_prob(int target, const scene_sig &sig, const rvec &x, double y, std::vector<int> &best_assign, double &best_error) const;
		bool update_fits();
		void init_fit(const std::vector<int> &data_inds, const mat &coefs, const rvec &inter);
		bool uniform_sig(int sig, int target) const;
		void learn_obj_clauses(const relation_table &rels);

		int size() const { return members.size(); }
		bool is_new_fit() const { return new_fit; }
		void reset_new_fit() { new_fit = false; }
		
		const std::set<int> &get_members() const { return members; }
		const scene_sig &get_sig() const { return sig; }
		
		/*
		 Each object the model is conditioned on needs to be
		 identifiable with a set of first-order Horn clauses
		 learned with FOIL.
		*/
		std::vector<clause_vec> obj_clauses;
		
		/*
		 Each pair of modes has one classifier associated with it. For
		 mode i, the classifier for it and mode j is stored in the
		 j_th element of this vector. Elements 0 - i of this vector
		 are NULL since those classifiers are already present in a
		 previous mode's classifier vector.
		*/
		std::vector<classifier*> classifiers;
		
		bool classifier_stale;
		relation member_rel;
		
	private:
		bool stale, noise, new_fit;
		const std::vector<train_data*> &data;
		const std::vector<sig_info*> &sigs;
		
		mat lin_coefs;
		rvec lin_inter;
		std::set<int> members;
		scene_sig sig;
		
		/*
		 Noise data sorted by their Y values. First element in pair is the Y value,
		 second is the index.
		*/
		std::set<std::pair<double, int> > sorted_ys;
	};
	
	void estep();
	bool mstep();
	void extend_relations(const relation_table &add, int time);
	void fill_xy(const std::vector<int> &rows, mat &X, mat &Y) const;

	bool unify_or_add_mode();
	bool remove_modes();
	bool find_new_mode_inds(int sig_ind, std::vector<int> &mode_inds, mat &coefs, rvec &inter);
	int find_linear_subset(mat &X, mat &Y, std::vector<int> &subset, mat &coefs, rvec &inter) const;
	void find_linear_subset_em(const_mat_view X, const_mat_view Y, std::vector<int> &subset) const;
	void find_linear_subset_block(const_mat_view X, const_mat_view Y, std::vector<int> &subset) const;
	
	bool map_objs(int mode, int target, const scene_sig &sig, const relation_table &rels, std::vector<int> &mapping) const;

	void update_classifier();
	void update_pair(int i, int j);
	int classify(int target, const scene_sig &sig, const relation_table &rels, const rvec &x, std::vector<int> &obj_map);
	int vote_pair(int i, int j, int target, const scene_sig &sig, const relation_table &rels, const rvec &x) const;
	LDA *learn_numeric_classifier(const relation &p, const relation &n) const;
	
	bool cli_inspect_train(int first, const std::vector<std::string> &args, std::ostream &os) const;
	bool cli_inspect_relations(int i, const std::vector<std::string> &args, std::ostream &os) const;
	bool cli_inspect_classifiers(std::ostream &os) const;

	relation_table rel_tbl;
	std::vector<train_data*> data;
	std::vector<sig_info*> sigs;
	std::vector<mode_info*> modes;
	int ndata, nmodes;
	bool use_em, use_foil, use_lda;

	/*
	 Keeps track of the minimum number of new noise examples needed before we have
	 to check for a possible new mode
	*/
	int check_after;
	
	// noise binned by signature
	std::map<int, std::set<int> > noise_by_sig;
		
	mutable timer_set timers;
};


#endif
