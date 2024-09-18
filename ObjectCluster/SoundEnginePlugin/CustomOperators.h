#pragma once
#include <AK/SoundEngine/Common/AkTypes.h>

// Operator overloads for AkVector
inline bool operator==(const AkVector& a, const AkVector& b) {
    return a.X == b.X && a.Y == b.Y && a.Z == b.Z;
}

inline bool operator!=(const AkVector& a, const AkVector& b) {
    return a.X != b.X || a.Y != b.Y || a.Z != b.Z;
}

inline bool operator<(const AkVector& a, const AkVector& b) {
    if (a.X != b.X) return a.X < b.X;
    if (a.Y != b.Y) return a.Y < b.Y;
    return a.Z < b.Z;
}

// Operator overloads for AkTransform
inline bool operator==(const AkTransform& a, const AkTransform& b) {
    return a.OrientationFront() == b.OrientationFront() &&
        a.OrientationTop() == b.OrientationTop() &&
        a.Position() == b.Position();
}

inline bool operator<(const AkTransform& a, const AkTransform& b) {
    if (a.OrientationFront() != b.OrientationFront()) return a.OrientationFront() < b.OrientationFront();
    if (a.OrientationTop() != b.OrientationTop()) return a.OrientationTop() < b.OrientationTop();
    return a.Position() < b.Position();
}