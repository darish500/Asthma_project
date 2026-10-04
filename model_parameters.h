#ifndef MODEL_PARAMETERS_H
#define MODEL_PARAMETERS_H

// Number of features in the model
const int NUM_FEATURES = 13;

// Model coefficients (weights) for each feature
const float coefficients[] = {
    -0.62524159,
    1.08139560,
    0.63547656,
    -0.10971600,
    -0.00606782,
    1.91469940,
    0.02278786,
    -0.11629380,
    1.14295606,
    1.28901258,
    0.90666421,
    0.20272414,
    0.97829090
};

// Model intercept (bias)
const float intercept = 7.63267664;

#endif // MODEL_PARAMETERS_H
