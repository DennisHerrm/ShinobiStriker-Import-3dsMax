// ns_bp.cpp - Hilfen fuer Figurenteile
#include "ns_bp.h"
#include <cmath>

namespace ns {

void RotatorZuQuat(const double pyr[3], float q[4]) {
    // Wie FRotator::Quaternion: Pitch um Y, Yaw um Z, Roll um X
    const double d = 3.14159265358979323846 / 360.0;
    const double sp = std::sin(pyr[0] * d), cp = std::cos(pyr[0] * d);
    const double sy = std::sin(pyr[1] * d), cy = std::cos(pyr[1] * d);
    const double sr = std::sin(pyr[2] * d), cr = std::cos(pyr[2] * d);
    q[0] = (float)(cr * sp * sy - sr * cp * cy);
    q[1] = (float)(-cr * sp * cy - sr * cp * sy);
    q[2] = (float)(cr * cp * sy - sr * sp * cy);
    q[3] = (float)(cr * cp * cy + sr * sp * sy);
}

} // namespace ns
