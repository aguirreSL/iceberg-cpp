#include "native/normalize_dat.h"

namespace iceberg::native {

Eigen::MatrixXf normalize_dat(const Eigen::MatrixXf& signal) {
    if (signal.size() == 0) {
        return signal;
    }
    const float g = signal.cwiseAbs().maxCoeff();
    if (!(g > 0.0f)) {
        return signal;
    }
    // Element-wise division by g, not multiplication by 1/g: the reference
    // divides, and the two results differ in the last bit.
    return (signal.array() / g).matrix();
}

}  // namespace iceberg::native
