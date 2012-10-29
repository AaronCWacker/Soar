#ifndef PARAMS_H
#define PARAMS_H

/*
 Measurement variance for all continuous quantities reported by the
 environment. In the future, there may need to be a different variance
 associated with each measurement. For example, the variance on the
 position of an object is probably going to be different from the
 variance of its rotation, because the two measurements have different
 units and scales.
*/
#define MEASURE_VAR 1.0e-8

/* Initial lambda value for ridge regression */
#define RIDGE_LAMBDA 1e-8

/* Maximum acceptable average absolute error for a linear model */
#define MODEL_ERROR_THRESH 1e-5

/*
 A local model does not need to be refit to data if its training error
 increases by less than this factor with a new data point, assuming the
 error is less than MODEL_ERROR_THRESH
*/
#define REFIT_MUL_THRESH 1.0001

/*
 Any two numbers within this amount of each other can be considered
 identical. This is also used to "zero-out" very small values.
*/
#define SAME_THRESH 1e-15

/*
 Maximum number of times Leave One Out cross-validation will run for a
 single PCR fitting
*/
#define LOO_NTEST 30

/*
 In PCR, don't use a beta vector with a norm larger than this.
*/
#define MAX_BETA_NORM 1.0e3

/* Probability that any instance is just noise */
#define PNOISE 0.0001

/* What's this for? */
#define EPSILON 0.001

/*
 Number of noise instances required before EM tries to create a new
 mode out of them
*/
#define NEW_MODE_THRESH 200

/*
 Number of times EM tries to create a model from the same set of noise
 instances
*/
#define SEL_NOISE_MAX_TRIES 10

/*
 When trying to unify a new model with an existing one, the training
 error of the resulting model must be within this factor of the training
 error of the original
*/
#define UNIFY_MUL_THRESH 1.00001

/*
 In the controller, if the new prediction for the final state of a cached
 trajectory differs at least this much from the original prediction,
 then discard the cached trajectory, because it was computed using an
 outdated model.
*/
#define STATE_DIFF_THRESH 0.001


#define LASSO_LAMBDA 1e-8
#define LASSO_TOL 1e-10
#define LASSO_MAX_ITERS 50

#define FOIL_GROW_RATIO 0.75
#define FOIL_MIN_SUCCESS_RATE 0.9
#define FOIL_MAX_CLAUSE_LEN 5

#define EM_CONTINUOUS_CLASSIFIER_THRESH 0.05

// num neighbors to use for fallback LWR prediction
#define LWR_K 20

#endif
