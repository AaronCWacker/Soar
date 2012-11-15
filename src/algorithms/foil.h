#ifndef FOIL_H
#define FOIL_H

#include <vector>
#include "common.h"
#include "serializable.h"
#include "relation.h"

class literal : public serializable {
public:
	literal() : negate(false) {}
	
	literal(const std::string &name, const tuple &args, bool negate)
	: name(name), args(args), negate(negate)
	{}
	
	literal(const literal &l)
	: name(l.name), args(l.args), negate(l.negate)
	{}
	
	literal &operator=(const literal &l) {
		name = l.name;
		args = l.args;
		negate = l.negate;
		return *this;
	}
	
	const std::string &get_name() const { return name; }
	const tuple &get_args() const { return args; }
	bool negated() const { return negate; }
	
	void set_arg(int i, int v) { args[i] = v; }

	int operator<<(const std::string &s);
	void serialize(std::ostream &os) const;
	void unserialize(std::istream &is);
	
private:
	std::string name;
	tuple args;
	bool negate;

	friend std::ostream &operator<<(std::ostream &os, const literal &l);
};

std::ostream &operator<<(std::ostream &os, const literal &l);

typedef std::vector<literal> clause;
typedef std::vector<clause> clause_vec;

std::ostream &operator<<(std::ostream &os, const clause &c);

class FOIL {
public:
	FOIL(const relation &pos, const relation &neg, const relation_table &rels) ;
	bool learn(clause_vec &clauses, std::vector<relation*> *uncovered);
	void gain(const literal &l, double &g, double &maxg) const;
	void foil6_rep(std::ostream &os) const;

	const relation_table &get_relations() const { return rels; }
	const relation &get_rel(const std::string &name) const;
	
private:
	double choose_literal(literal &l, int nvars);
	bool choose_clause(clause &c, relation *neg_left);
	bool tuple_satisfies_literal(const tuple &t, const literal &l);
	int false_positives(const clause &c) const;
	int false_negatives(const clause &c, relation *pos_matched) const;
	double clause_success_rate(const clause &c, relation *pos_matched) const;
	double prune_clause(clause &c) const;

private:
	/*
	 I store the test sets as vectors instead of relations because
	 it's easier to index into.
	*/
	std::vector<tuple> pos_test, neg_test;
	relation pos, neg, pos_grow, neg_grow;
	const relation_table &rels;
	int init_vars;
};

typedef std::map<int, std::set<int> > var_domains;
bool test_clause(const clause &c, const relation_table &rels, var_domains &domains);
int test_clause_vec(const clause_vec &c, const relation_table &rels, var_domains &domains);

#endif
