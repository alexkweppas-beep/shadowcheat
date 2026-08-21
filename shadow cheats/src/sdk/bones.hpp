#pragma once
#include "../math/SimpleMath.h"
#include <cstdint>

using namespace DirectX::SimpleMath;

class CPed;

namespace bones
{
    struct CBoneFast
    {
        Vector3 gHead;       char _p0[0x4]{};
        Vector3 gLeftFoot;   char _p1[0x4]{};
        Vector3 gRightFoot;  char _p2[0x4]{};
        Vector3 gLeftAnkle;  char _p3[0x4]{};
        Vector3 gRightAnkle; char _p4[0x4]{};
        Vector3 gLeftHand;   char _p5[0x4]{};
        Vector3 gRightHand;  char _p6[0x4]{};
        Vector3 gNeck;       char _p7[0x4]{};
        Vector3 gHip;
    };

    struct cSkeleton_t
    {
        struct crSkeletonData_t
        {
            uintptr_t      Ptr;
            uintptr_t      m_BoneIdTable;
            unsigned int   m_Used;
            unsigned int   m_NumBones;
            unsigned short m_BoneIdTable_Slots;
        } crSkeletonData;

        uintptr_t m_pSkeleton;
        uintptr_t Arg2;
        Matrix    Arg1;
    };

    enum BoneId : int
    {
        BONE_HEAD = 0,
        BONE_NECK,
        BONE_LEFT_HAND,
        BONE_RIGHT_HAND,
        BONE_LEFT_ANKLE,
        BONE_RIGHT_ANKLE,
        BONE_LEFT_FOOT,
        BONE_RIGHT_FOOT,
        BONE_HIP,

        BONE_PELVIS,
        BONE_SPINE_ROOT,
        BONE_SPINE0,
        BONE_SPINE1,
        BONE_SPINE2,
        BONE_SPINE3,
        BONE_LEFT_CLAVICLE,
        BONE_LEFT_UPPER_ARM,
        BONE_LEFT_FOREARM,
        BONE_RIGHT_CLAVICLE,
        BONE_RIGHT_UPPER_ARM,
        BONE_RIGHT_FOREARM,
        BONE_LEFT_THIGH,
        BONE_LEFT_CALF,
        BONE_RIGHT_THIGH,
        BONE_RIGHT_CALF,

        BONE_COUNT
    };

    
    constexpr BoneId SKEL_Head = BONE_HEAD;
    constexpr BoneId SKEL_Neck = BONE_NECK;
    constexpr BoneId SKEL_Spine = BONE_SPINE0;       
    constexpr BoneId SKEL_Pelvis = BONE_PELVIS;
    constexpr BoneId SKEL_L_UpperArm = BONE_LEFT_UPPER_ARM;
    constexpr BoneId SKEL_L_Forearm = BONE_LEFT_FOREARM;
    constexpr BoneId SKEL_L_Hand = BONE_LEFT_HAND;
    constexpr BoneId SKEL_R_UpperArm = BONE_RIGHT_UPPER_ARM;
    constexpr BoneId SKEL_R_Forearm = BONE_RIGHT_FOREARM;
    constexpr BoneId SKEL_R_Hand = BONE_RIGHT_HAND;
    constexpr BoneId SKEL_L_Thigh = BONE_LEFT_THIGH;
    constexpr BoneId SKEL_L_Calf = BONE_LEFT_CALF;
    constexpr BoneId SKEL_L_Foot = BONE_LEFT_FOOT;
    constexpr BoneId SKEL_R_Thigh = BONE_RIGHT_THIGH;
    constexpr BoneId SKEL_R_Calf = BONE_RIGHT_CALF;
    constexpr BoneId SKEL_R_Foot = BONE_RIGHT_FOOT;

    enum BoneMask : unsigned int
    {
        MASK_HEAD = 0x796E,
        MASK_NECK = 0x9995,
        MASK_PELVIS = 0x2E28,
        MASK_SPINE_ROOT = 0xE0FD,
        MASK_SPINE0 = 0x5C01,
        MASK_SPINE1 = 0x60F0,
        MASK_SPINE2 = 0x60F1,
        MASK_SPINE3 = 0x60F2,
        MASK_L_CLAVICLE = 0xFCD9,
        MASK_L_UPPER_ARM = 0xB1C5,
        MASK_L_FOREARM = 0xEEEB,
        MASK_L_HAND = 0x49D9,
        MASK_R_CLAVICLE = 0x29D2,
        MASK_R_UPPER_ARM = 0x9D4D,
        MASK_R_FOREARM = 0x6E5C,
        MASK_R_HAND = 0xDEAD,
        MASK_L_THIGH = 0xE39F,
        MASK_L_CALF = 0xF9BB,
        MASK_L_FOOT = 0x3779,
        MASK_R_THIGH = 0xCA72,
        MASK_R_CALF = 0x9000,
        MASK_R_FOOT = 0xCC4D
    };

    namespace validation
    {
        constexpr uintptr_t kPtrSentinel = 0xCCCCCCCCCCCCull;
        constexpr float     kMaxBoneFromPed = 3.0f;

        bool IsPtrSane(uintptr_t p);
        bool IsBoneSane(const Vector3& worldPos,
            const Vector3& pedPos);
    }

    bool UpdateFast(CPed* ped);
    bool UpdateDetailed(CPed* ped);
}
