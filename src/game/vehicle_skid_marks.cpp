// C_Vehicle: a sliding wheel's skid marks (original 0x004fcbd0 LaySkidMark; the ring's end of a
// mark, EndSkidMark 0x004fe610, is in game/vehicle_drive.cpp).
#include "game/vehicle.h"

#include <cmath>

#include "game/collision.h"                             // COLLISION_FLAG_WATER
#include "game/pending.h"                               // g_carSurfaces
#include "ls3d/i3d.h"

namespace {

// The SDK's normalization inlined with its sum of squares in either order (a (nearly) zero vector
// becomes a unit vector along its first non-zero axis: x, then z, then y).
void Normalize(S_vector &v, float square)
{
    if (std::fabs(square - 1.0f) < 1e-8f)
        return;
    if (!(square < 1e-8f)) {
        const float k = 1.0f / std::sqrt(square);
        v = S_vector(k * v.x, k * v.y, k * v.z);
    } else if (!(v.x == 0.0f)) {
        v.x = v.x < 0.0f ? -1.0f : 1.0f;
    } else if (!(v.z == 0.0f)) {
        v.z = v.z < 0.0f ? -1.0f : 1.0f;
    } else {
        v.y = v.y < 0.0f ? -1.0f : 1.0f;
    }
}
void NormalizeXYZ(S_vector &v) { Normalize(v, (v.x * v.x + v.y * v.y) + v.z * v.z); }
void NormalizeZYX(S_vector &v) { Normalize(v, (v.z * v.z + v.y * v.y) + v.x * v.x); }

// a x b.
S_vector Cross(const S_vector &a, const S_vector &b)
{
    return S_vector(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

}  // namespace

void C_Vehicle::LaySkidMark(int wheel)                                          // 0x004fcbd0
{
    S_VehicleWheel *w = m_wheels[wheel];
    // (Where this loops, the original calls itself.)
    for (;;) {
        if ((w->flags & WHEEL_DUMMY) || !(w->flags & WHEEL_ON_GROUND) ||
            !g_carSurfaces[g_carSurfaceOfMaterial[w->surface]].skidMarks || !w->ground ||
            (w->ground->flags & COLLISION_FLAG_WATER) || w->unknown21C < 0.0f) {
            EndSkidMark(wheel);
            return;
        }
        const uint32_t count = static_cast<uint32_t>(w->skidMarks.size());
        const uint32_t current = w->skidMark;
        if (count <= current)
            return;
        const uint32_t previous = current > 0 ? current - 1 : count - 1;
        S_VehicleSkidMark &mark = w->skidMarks[current];
        if (!mark.active)
            mark.frame0->SetOn(true);
        // The contact lifted 0.05 along the ground's normal.
        const S_vector &normal = w->unknown080;
        const S_vector lift(normal.x * 0.05f, normal.y * 0.05f, normal.z * 0.05f);
        const S_vector contact(lift.x + w->unknown050.x, lift.y + w->unknown050.y, lift.z + w->unknown050.z);

        if (!mark.active) {
            // A new mark: its first segment at the contact, the mark along the contact's velocity.
            mark.active = true;
            w->skidStart = contact;
            mark.frame0->SetPosInline(contact);
            mark.frame0->SetScale(S_vector(w->width, 1.0f, 0.01f));
            mark.frame0->Update();
            mark.fresh = true;
            w->skidDir = w->unknown0A0;
            NormalizeXYZ(w->skidDir);
            return;
        }

        // The segment stretches to the contact: from 5 cm on, the mark ends past 5 m.
        S_vector start = w->skidStart;
        S_vector stretch(contact.x - start.x, contact.y - start.y, contact.z - start.z);
        float length = stretch.Magnitude();
        if (length < 0.05f)
            return;
        if (length > 5.0f) {
            EndSkidMark(wheel);
            return;
        }
        S_vector dir = stretch;
        NormalizeZYX(dir);
        // Against the mark's direction it ends and a new one starts.
        const S_vector &velocity = w->unknown0A0;
        if ((velocity.y * w->skidDir.y + velocity.z * w->skidDir.z) + velocity.x * w->skidDir.x < 0.0f) {
            EndSkidMark(wheel);
            continue;
        }

        if (!mark.fresh) {
            // The corner piece of the segment before at the corner of its end the strip turns
            // about (the left one turning towards it, else the right): along the bisector, as
            // wide as the gap the turn opens there; the segment starts half a width in from it.
            const float half = w->width * 0.5f;
            const S_vector side = Cross(dir, normal);
            const S_vector across(w->skidLeft.x - w->skidRight.x, w->skidLeft.y - w->skidRight.y,
                                  w->skidLeft.z - w->skidRight.z);
            const float acrossLength = across.Magnitude();
            S_vector acrossDir = across;
            NormalizeXYZ(acrossDir);
            const S_vector bisector(side.x + acrossDir.x, side.y + acrossDir.y, side.z + acrossDir.z);
            const S_vector *pivot;
            S_vector cornerDir, gap;
            if (!((acrossDir.x * dir.x + dir.y * acrossDir.y) + dir.z * acrossDir.z < 0.0f)) {
                pivot = &w->skidLeft;
                start = S_vector(w->skidLeft.x - side.x * half, w->skidLeft.y - side.y * half,
                                 w->skidLeft.z - side.z * half);
                cornerDir = S_vector(-bisector.x, -bisector.y, -bisector.z);
                const S_vector outer(w->skidLeft.x - side.x * w->width, w->skidLeft.y - side.y * w->width,
                                   w->skidLeft.z - side.z * w->width);
                gap = S_vector(w->skidRight.x - outer.x, w->skidRight.y - outer.y, w->skidRight.z - outer.z);
            } else {
                pivot = &w->skidRight;
                start = S_vector(side.x * half + w->skidRight.x, side.y * half + w->skidRight.y,
                                 side.z * half + w->skidRight.z);
                cornerDir = bisector;
                const S_vector outer(side.x * w->width + w->skidRight.x, side.y * w->width + w->skidRight.y,
                                   side.z * w->width + w->skidRight.z);
                gap = S_vector(w->skidLeft.x - outer.x, w->skidLeft.y - outer.y, w->skidLeft.z - outer.z);
            }
            const float gapHalf = gap.Magnitude() * 0.5f;
            const float cornerLength = std::sqrt(acrossLength * acrossLength - gapHalf * gapHalf);
            S_VehicleSkidMark &before = w->skidMarks[previous];
            if (!before.cornerShown) {
                before.frame1->SetOn(true);
                before.cornerShown = true;
            }
            before.frame1->SetPosInline(*pivot);
            before.frame1->SetLocalDir3(cornerDir, normal);
            before.frame1->SetScale(S_vector(gapHalf + gapHalf, 1.0f, cornerLength));
            before.frame1->Update();
            stretch = S_vector(contact.x - start.x, contact.y - start.y, contact.z - start.z);
            length = stretch.Magnitude();
            dir = stretch;
            NormalizeZYX(dir);
        }

        if (length <= 0.3f) {
            mark.frame0->SetPosInline(start);
            mark.frame0->SetLocalDir3(dir, normal);
            mark.frame0->SetScale(S_vector(w->width, 1.0f, length));
            mark.frame0->Update();
            return;
        }

        // A full segment (0.3 m) closes, its end corners kept; the next starts at its end, along
        // the contact's velocity, and stretches on.
        S_vector left = Cross(dir, normal);
        NormalizeXYZ(left);
        const float half = w->width * 0.5f;
        left = S_vector(half * left.x, half * left.y, half * left.z);
        const S_vector right(-left.x, -left.y, -left.z);
        mark.frame0->SetPosInline(start);
        mark.frame0->SetLocalDir3(dir, normal);
        mark.frame0->SetScale(S_vector(w->width, 1.0f, 0.3f));
        mark.frame0->Update();
        mark.active = false;
        mark.fresh = false;
        const uint32_t next = current + 1 < count ? current + 1 : 0;
        w->skidMark = next;
        const S_vector step(dir.x * 0.3f, dir.y * 0.3f, dir.z * 0.3f);
        start = S_vector(start.x + step.x, step.y + start.y, step.z + start.z);
        w->skidLeft = S_vector(start.x + left.x, start.y + left.y, start.z + left.z);
        w->skidRight = S_vector(start.x + right.x, start.y + right.y, start.z + right.z);
        w->skidStart = start;
        S_VehicleSkidMark &following = w->skidMarks[next];
        following.frame0->SetPosInline(start);
        following.frame0->SetScale(S_vector(w->width, 1.0f, 0.01f));
        following.frame0->Update();
        if (!following.active)
            following.frame0->SetOn(true);
        following.active = true;
        following.fresh = false;
        w->skidDir = w->unknown0A0;
        NormalizeXYZ(w->skidDir);
    }
}
