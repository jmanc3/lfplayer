#include <cmath>
#include <map>

#include "easing.h"

#ifndef PI
#define PI 3.1415926545
#endif

double
easeInSine(double t) {
    return sin(1.5707963 * t);
}

double
easeOutSine(double t) {
    return 1 + sin(1.5707963 * (--t));
}

double
easeInOutSine(double t) {
    return 0.5 * (1 + sin(3.1415926 * (t - 0.5)));
}

double
easeInQuad(double t) {
    return t * t;
}

double
easeOutQuad(double t) {
    return t * (2 - t);
}

double
easeInOutQuad(double t) {
    return t < 0.5 ? 2 * t * t : t * (4 - 2 * t) - 1;
}

double
easeInCubic(double t) {
    return t * t * t;
}

double
easeOutCubic(double t) {
    return 1 + (--t) * t * t;
}

double
easeInOutCubic(double t) {
    return t < 0.5 ? 4 * t * t * t : 1 + (--t) * (2 * (--t)) * (2 * t);
}

double
easeInQuart(double t) {
    t *= t;
    return t * t;
}

double
easeOutQuart(double t) {
    t = (--t) * t;
    return 1 - t * t;
}

double
easeInOutQuart(double t) {
    if (t < 0.5) {
        t *= t;
        return 8 * t * t;
    } else {
        t = (--t) * t;
        return 1 - 8 * t * t;
    }
}

double
easeInMiddle(double t) {
    double distance_from_middle = .5 - t;
    if (distance_from_middle < 0)
        distance_from_middle = -distance_from_middle;
    
    
    if (t < .5) {
        t -= t * distance_from_middle;
    }
    
    return t;
    
    
    // as distance_from_middle approaches 0, t is supposed to be repulsed from the value .5
    //
    double repulsion_amount = distance_from_middle;
    
    if (t < .5) {
        return t - repulsion_amount;
    } else {
        return t + repulsion_amount;
    }
    
    /*
    if (t < 0.5) {
        return 2 * t * t;
    } else {
        return t * (4 - 2 * t) - 1;
    }*/
}

double
easeInQuint(double t) {
    double t2 = t * t;
    return t * t2 * t2;
}

double
easeOutQuint(double t) {
    double t2 = (--t) * t;
    return 1 + t * t2 * t2;
}

double
easeInOutQuint(double t) {
    double t2;
    if (t < 0.5) {
        t2 = t * t;
        return 16 * t * t2 * t2;
    } else {
        t2 = (--t) * t;
        return 1 + 16 * t * t2 * t2;
    }
}

double
easeInExpo(double t) {
    return (pow(2, 8 * t) - 1) / 255;
}

double
easeOutExpo(double t) {
    return 1 - pow(2, -8 * t);
}

double
easeInOutExpo(double t) {
    if (t < 0.5) {
        return (pow(2, 16 * t) - 1) / 510;
    } else {
        return 1 - 0.5 * pow(2, -16 * (t - 0.5));
    }
}

double
easeInCirc(double t) {
    return 1 - sqrt(1 - t);
}

double
easeOutCirc(double t) {
    return sqrt(t);
}

double
easeInOutCirc(double t) {
    if (t < 0.5) {
        return (1 - sqrt(1 - 2 * t)) * 0.5;
    } else {
        return (1 + sqrt(2 * t - 1)) * 0.5;
    }
}

double
easeInBack(double t) {
    return t * t * (2.70158 * t - 1.70158);
}

double
easeOutBack(double t) {
    return 1 + (--t) * t * (2.70158 * t + 1.70158);
}

double
easeInOutBack(double t) {
    if (t < 0.5) {
        return t * t * (7 * t - 2.5) * 2;
    } else {
        return 1 + (--t) * t * 2 * (7 * t + 2.5);
    }
}

double
easeInElastic(double t) {
    double t2 = t * t;
    return t2 * t2 * sin(t * PI * 4.5);
}

double
easeOutElastic(double t) {
    double t2 = (t - 1) * (t - 1);
    return 1 - t2 * t2 * cos(t * PI * 4.5);
}

double
easeInOutElastic(double t) {
    double t2;
    if (t < 0.45) {
        t2 = t * t;
        return 8 * t2 * t2 * sin(t * PI * 9);
    } else if (t < 0.55) {
        return 0.5 + 0.75 * sin(t * PI * 4);
    } else {
        t2 = (t - 1) * (t - 1);
        return 1 - 8 * t2 * t2 * sin(t * PI * 9);
    }
}

double
easeInBounce(double t) {
    return pow(2, 6 * (t - 1)) * abs(sin(t * PI * 3.5));
}

double
easeOutBounce(double t) {
    return 1 - pow(2, -6 * t) * abs(cos(t * PI * 3.5));
}

#include <vector>

double 
easeSmoothIn(double t) {
    // {"anchors":[{"x":0,"y":1},{"x":0.125,"y":0.8},{"x":0.6000000000000001,"y":0}],"controls":[{"x":0.0625,"y":0.9},{"x":0.24316239968324332,"y":0.11148147583007813}]}
std::vector<float> fls = { 0, 0.027000000000000024, 0.05300000000000005, 0.07999999999999996, 0.10699999999999998, 0.133, 0.16000000000000003, 0.18700000000000006, 0.246, 0.32699999999999996, 0.397, 0.45799999999999996, 0.511, 0.5589999999999999, 0.602, 0.642, 0.677, 0.71, 0.739, 0.767, 0.792, 0.815, 0.836, 0.856, 0.874, 0.89, 0.905, 0.919, 0.9319999999999999, 0.944, 0.955, 0.964, 0.973, 0.981, 0.988, 0.994 };
    int i = std::round(t * fls.size() - 1);
    if (i < 0)
        i = 0;
    if (i > fls.size() - 1)
        i = fls.size() - 1;
    return fls[i];
}


double
easeSmooth(double t) {
    // {"anchors":[{"x":0,"y":1},{"x":0.6000000000000001,"y":0},{"x":1,"y":0}],"controls":[{"x":0.25494883375901584,"y":-0.05240741305881076},{"x":0.8,"y":0}]}
std::vector<float> fls = { 0, 0.06699999999999995, 0.131, 0.19299999999999995, 0.251, 0.30700000000000005, 0.36, 0.41000000000000003, 0.45799999999999996, 0.503, 0.546, 0.587, 0.625, 0.6619999999999999, 0.696, 0.728, 0.758, 0.786, 0.812, 0.836, 0.859, 0.879, 0.898, 0.916, 0.931, 0.945, 0.957, 0.968, 0.977, 0.985, 0.991, 0.996, 1, 1.002, 1.002, 1.002, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    int i = std::round(t * fls.size() - 1);
    if (i < 0)
        i = 0;
    if (i > fls.size() - 1)
        i = fls.size() - 1;
    return fls[i];
}

double
easeCustom(double t) {
    // {"anchors":[{"x":0,"y":1},{"x":0.1,"y":1},{"x":1,"y":0}],"controls":[{"x":0.05,"y":1},{"x":0.3958248496422399,"y":0.026269376288519632}]}
//static std::vector<float> fls = { 0, 0, 0, 0, 0, 0, 0, 0.05300000000000005, 0.10399999999999998, 0.15200000000000002, 0.19699999999999995, 0.24, 0.281, 0.31999999999999995, 0.357, 0.393, 0.42700000000000005, 0.45899999999999996, 0.49, 0.519, 0.5469999999999999, 0.5740000000000001, 0.599, 0.624, 0.647, 0.669, 0.69, 0.7110000000000001, 0.73, 0.748, 0.766, 0.783, 0.798, 0.8140000000000001, 0.8280000000000001, 0.842, 0.855, 0.867, 0.878, 0.889, 0.9, 0.91, 0.919, 0.927, 0.935, 0.943, 0.95, 0.956, 0.962, 0.968, 0.973, 0.978, 0.982, 0.985, 0.989, 0.992, 0.994, 0.996, 0.998, 0.999 };
    
    // {"anchors":[{"x":0,"y":1},{"x":0.8250000000000001,"y":0.65},{"x":1.7000000000000002,"y":0}],"controls":[{"x":0.42802575683593885,"y":0.9350925869411892},{"x":1.0664872952974775,"y":0.015648142496744788}]}
//static std::vector<float> fls = { 0, 0.0030000000000000027, 0.0050000000000000044, 0.008000000000000007, 0.01100000000000001, 0.015000000000000013, 0.018000000000000016, 0.02200000000000002, 0.026000000000000023, 0.030000000000000027, 0.03400000000000003, 0.038000000000000034, 0.04300000000000004, 0.04800000000000004, 0.052000000000000046, 0.05800000000000005, 0.06299999999999994, 0.06799999999999995, 0.07399999999999995, 0.07999999999999996, 0.08599999999999997, 0.09199999999999997, 0.09799999999999998, 0.10499999999999998, 0.11099999999999999, 0.118, 0.126, 0.133, 0.14, 0.14800000000000002, 0.15600000000000003, 0.16400000000000003, 0.17200000000000004, 0.18100000000000005, 0.18999999999999995, 0.19799999999999995, 0.20699999999999996, 0.21699999999999997, 0.22599999999999998, 0.236, 0.246, 0.256, 0.266, 0.277, 0.28700000000000003, 0.29800000000000004, 0.30900000000000005, 0.32099999999999995, 0.33199999999999996, 0.344, 0.371, 0.41200000000000003, 0.44899999999999995, 0.483, 0.515, 0.5449999999999999, 0.573, 0.599, 0.623, 0.647, 0.6679999999999999, 0.6890000000000001, 0.708, 0.727, 0.744, 0.761, 0.776, 0.791, 0.8049999999999999, 0.819, 0.831, 0.843, 0.855, 0.865, 0.876, 0.885, 0.894, 0.903, 0.911, 0.919, 0.926, 0.933, 0.9390000000000001, 0.945, 0.951, 0.956, 0.961, 0.966, 0.97, 0.974, 0.978, 0.981, 0.984, 0.987, 0.989, 0.991, 0.993, 0.995, 0.997, 0.998, 0.999, 0.999, 1 };
    
    // {"anchors":[{"x":0,"y":1},{"x":0.8250000000000001,"y":0.65},{"x":1.7000000000000002,"y":0}],"controls":[{"x":0.7754616542718363,"y":0.9781481424967448},{"x":1.0664872952974775,"y":0.015648142496744788}]}
static std::vector<float> fls = { 0, 0.0010000000000000009, 0.0010000000000000009, 0.0020000000000000018, 0.0030000000000000027, 0.0030000000000000027, 0.0040000000000000036, 0.0050000000000000044, 0.006000000000000005, 0.008000000000000007, 0.009000000000000008, 0.010000000000000009, 0.01200000000000001, 0.013000000000000012, 0.015000000000000013, 0.017000000000000015, 0.019000000000000017, 0.02100000000000002, 0.02400000000000002, 0.026000000000000023, 0.029000000000000026, 0.031000000000000028, 0.03400000000000003, 0.03700000000000003, 0.041000000000000036, 0.04400000000000004, 0.04800000000000004, 0.052000000000000046, 0.05600000000000005, 0.061000000000000054, 0.06499999999999995, 0.06999999999999995, 0.07599999999999996, 0.08099999999999996, 0.08699999999999997, 0.09399999999999997, 0.10099999999999998, 0.10899999999999999, 0.11699999999999999, 0.125, 0.135, 0.14500000000000002, 0.15700000000000003, 0.16900000000000004, 0.18400000000000005, 0.19999999999999996, 0.21899999999999997, 0.241, 0.27, 0.31200000000000006, 0.371, 0.41200000000000003, 0.44899999999999995, 0.483, 0.515, 0.5449999999999999, 0.573, 0.599, 0.623, 0.647, 0.6679999999999999, 0.6890000000000001, 0.708, 0.727, 0.744, 0.761, 0.776, 0.791, 0.8049999999999999, 0.819, 0.831, 0.843, 0.855, 0.865, 0.876, 0.885, 0.894, 0.903, 0.911, 0.919, 0.926, 0.933, 0.9390000000000001, 0.945, 0.951, 0.956, 0.961, 0.966, 0.97, 0.974, 0.978, 0.981, 0.984, 0.987, 0.989, 0.991, 0.993, 0.995, 0.997, 0.998, 0.999, 0.999, 1 };
    
    int i = std::round(t * fls.size() - 1);
    if (i < 0)
        i = 0;
    if (i > fls.size() - 1)
        i = fls.size() - 1;
    return fls[i];
}

double
easeInOutBounce(double t) {
    if (t < 0.5) {
        return 8 * pow(2, 8 * (t - 1)) * abs(sin(t * PI * 7));
    } else {
        return 1 - 8 * pow(2, -8 * t) * abs(sin(t * PI * 7));
    }
}

easingFunction
getEasingFunction(easing_functions function) {
    static std::map<easing_functions, easingFunction> easingFunctions;
    if (easingFunctions.empty()) {
        easingFunctions.insert(std::make_pair(EaseInSine, easeInSine));
        easingFunctions.insert(std::make_pair(EaseOutSine, easeOutSine));
        easingFunctions.insert(std::make_pair(EaseInOutSine, easeInOutSine));
        easingFunctions.insert(std::make_pair(EaseInQuad, easeInQuad));
        easingFunctions.insert(std::make_pair(EaseOutQuad, easeOutQuad));
        easingFunctions.insert(std::make_pair(EaseInOutQuad, easeInOutQuad));
        easingFunctions.insert(std::make_pair(EaseInCubic, easeInCubic));
        easingFunctions.insert(std::make_pair(EaseOutCubic, easeOutCubic));
        easingFunctions.insert(std::make_pair(EaseInOutCubic, easeInOutCubic));
        easingFunctions.insert(std::make_pair(EaseInQuart, easeInQuart));
        easingFunctions.insert(std::make_pair(EaseOutQuart, easeOutQuart));
        easingFunctions.insert(std::make_pair(EaseInOutQuart, easeInOutQuart));
        easingFunctions.insert(std::make_pair(EaseInMiddle, easeInMiddle));
        easingFunctions.insert(std::make_pair(EaseInQuint, easeInQuint));
        easingFunctions.insert(std::make_pair(EaseOutQuint, easeOutQuint));
        easingFunctions.insert(std::make_pair(EaseInOutQuint, easeInOutQuint));
        easingFunctions.insert(std::make_pair(EaseInExpo, easeInExpo));
        easingFunctions.insert(std::make_pair(EaseOutExpo, easeOutExpo));
        easingFunctions.insert(std::make_pair(EaseInOutExpo, easeInOutExpo));
        easingFunctions.insert(std::make_pair(EaseInCirc, easeInCirc));
        easingFunctions.insert(std::make_pair(EaseOutCirc, easeOutCirc));
        easingFunctions.insert(std::make_pair(EaseInOutCirc, easeInOutCirc));
        easingFunctions.insert(std::make_pair(EaseInBack, easeInBack));
        easingFunctions.insert(std::make_pair(EaseOutBack, easeOutBack));
        easingFunctions.insert(std::make_pair(EaseInOutBack, easeInOutBack));
        easingFunctions.insert(std::make_pair(EaseInElastic, easeInElastic));
        easingFunctions.insert(std::make_pair(EaseOutElastic, easeOutElastic));
        easingFunctions.insert(std::make_pair(EaseInOutElastic, easeInOutElastic));
        easingFunctions.insert(std::make_pair(EaseInBounce, easeInBounce));
        easingFunctions.insert(std::make_pair(EaseOutBounce, easeOutBounce));
        easingFunctions.insert(std::make_pair(EaseCustom, easeCustom));
        easingFunctions.insert(std::make_pair(EaseSmooth, easeSmooth));
        easingFunctions.insert(std::make_pair(EaseSmoothIn, easeSmoothIn));
        easingFunctions.insert(std::make_pair(EaseInOutBounce, easeInOutBounce));
    }
    
    auto it = easingFunctions.find(function);
    return it == easingFunctions.end() ? nullptr : it->second;
}
