#include "bones.hpp"
#include "CPed.hpp"
#include "offsets.hpp"
#include "math_utils.hpp"
#include "../memory/memory_access.hpp"

#include <unordered_map>
#include <cmath>
#include <mutex>

namespace bones
{
    namespace
    {
        constexpr uintptr_t kSentinel = 0xCCCCCCCCCCCCull;



        bool BuildSkeleton(uintptr_t pedBase, cSkeleton_t& outSkel)
        {
            if (!pedBase) return false;

            const uintptr_t fragInst = read<uintptr_t>(pedBase + offsets::ped::FragInst);
            if (!fragInst || fragInst >= kSentinel) return false;

            const uintptr_t skelHolder = read<uintptr_t>(fragInst + offsets::skeleton::FragInstSkel);
            if (!skelHolder || skelHolder >= kSentinel) return false;

            outSkel.m_pSkeleton = read<uintptr_t>(skelHolder + offsets::skeleton::SkeletonPtr);
            if (!outSkel.m_pSkeleton || outSkel.m_pSkeleton >= kSentinel) return false;

            outSkel.crSkeletonData.Ptr = read<uintptr_t>(outSkel.m_pSkeleton);
            if (!outSkel.crSkeletonData.Ptr || outSkel.crSkeletonData.Ptr >= kSentinel) return false;

            outSkel.crSkeletonData.m_Used     = read<unsigned int>  (outSkel.crSkeletonData.Ptr + offsets::skeleton::crSkel_Used);
            outSkel.crSkeletonData.m_NumBones = read<unsigned int>  (outSkel.crSkeletonData.Ptr + offsets::skeleton::crSkel_NumBones);
            outSkel.crSkeletonData.m_BoneIdTable_Slots = read<unsigned short>(outSkel.crSkeletonData.Ptr + offsets::skeleton::crSkel_BoneIdTableSlots);
            outSkel.crSkeletonData.m_BoneIdTable = read<uintptr_t>  (outSkel.crSkeletonData.Ptr + offsets::skeleton::crSkel_BoneIdTable);

            outSkel.Arg1 = read<Matrix>   (read<uintptr_t>(outSkel.m_pSkeleton + offsets::skeleton::mat_Arg1Ptr));
            outSkel.Arg2 = read<uintptr_t>(outSkel.m_pSkeleton + offsets::skeleton::mat_Arg2Ptr);

            return true;
        }

        bool GetBoneIndex(CPed* ped, cSkeleton_t& skel, unsigned int boneId, int& outIdx)
        {
            if (ped->LastSkeletonPtr != skel.crSkeletonData.Ptr) {
                ped->LastSkeletonPtr = skel.crSkeletonData.Ptr;
                for (int i = 0; i < BONE_COUNT; ++i) {
                    ped->BoneIndexCache[i] = -1;
                }
            }

            // Map the mask (boneId) to an index in our BoneIndexCache array
            int cacheIndex = -1;
            switch(boneId) {
                case MASK_HEAD: cacheIndex = BONE_HEAD; break;
                case MASK_NECK: cacheIndex = BONE_NECK; break;
                case MASK_PELVIS: cacheIndex = BONE_PELVIS; break;
                case MASK_L_CLAVICLE: cacheIndex = BONE_LEFT_CLAVICLE; break;
                case MASK_L_UPPER_ARM: cacheIndex = BONE_LEFT_UPPER_ARM; break;
                case MASK_L_FOREARM: cacheIndex = BONE_LEFT_FOREARM; break;
                case MASK_L_HAND: cacheIndex = BONE_LEFT_HAND; break;
                case MASK_R_CLAVICLE: cacheIndex = BONE_RIGHT_CLAVICLE; break;
                case MASK_R_UPPER_ARM: cacheIndex = BONE_RIGHT_UPPER_ARM; break;
                case MASK_R_FOREARM: cacheIndex = BONE_RIGHT_FOREARM; break;
                case MASK_R_HAND: cacheIndex = BONE_RIGHT_HAND; break;
                case MASK_L_THIGH: cacheIndex = BONE_LEFT_THIGH; break;
                case MASK_L_CALF: cacheIndex = BONE_LEFT_CALF; break;
                case MASK_L_FOOT: cacheIndex = BONE_LEFT_FOOT; break;
                case MASK_R_THIGH: cacheIndex = BONE_RIGHT_THIGH; break;
                case MASK_R_CALF: cacheIndex = BONE_RIGHT_CALF; break;
                case MASK_R_FOOT: cacheIndex = BONE_RIGHT_FOOT; break;
            }

            if (cacheIndex != -1 && ped->BoneIndexCache[cacheIndex] != -1) {
                outIdx = ped->BoneIndexCache[cacheIndex];
                return true;
            }

            if (skel.crSkeletonData.m_Used != 0)
            {
                const unsigned short slots = skel.crSkeletonData.m_BoneIdTable_Slots;
                if (slots == 0) return false;

                uintptr_t entry = read<uintptr_t>(skel.crSkeletonData.m_BoneIdTable + 0x8 * (boneId % slots));

                int hops = 0;
                for (uintptr_t i = entry; i != 0; i = read<uintptr_t>(i + 0x8))
                {
                    if (++hops > 16) break;
                    if (i >= kSentinel) break;

                    const int key = read<int>(i);
                    if (static_cast<int>(boneId) == key)
                    {
                        outIdx = read<int>(i + 0x4);
                        if (cacheIndex != -1) ped->BoneIndexCache[cacheIndex] = outIdx;
                        return true;
                    }
                }
                return false;
            }

            if (boneId < skel.crSkeletonData.m_NumBones)
            {
                outIdx = static_cast<int>(boneId);
                if (cacheIndex != -1) ped->BoneIndexCache[cacheIndex] = outIdx;
                return true;
            }

            if (cacheIndex != -1) {
                ped->BoneIndexCache[cacheIndex] = -1;
            }
            return false;
        }

        Vector3 GetBonePosComplex(CPed* ped, cSkeleton_t& skel, unsigned int mask)
        {
            if (!skel.m_pSkeleton || !skel.Arg2) return Vector3(0, 0, 0);

            int boneIdx = 0;
            if (!GetBoneIndex(ped, skel, mask, boneIdx)) return Vector3(0, 0, 0);

            const Matrix v = skel.Arg1;
            const Matrix r = read<Matrix>(skel.Arg2 + (static_cast<uint64_t>(boneIdx) << 6));

            const Vector3 row1(v._11, v._12, v._13);
            const Vector3 row2(v._21, v._22, v._23);
            const Vector3 row3(v._31, v._32, v._33);
            const Vector3 row4(v._41, v._42, v._43);
            const Vector3 br (r._41, r._42, r._43);

            return Vector3(
                row1.x * br.x + row4.x + row2.x * br.y + row3.x * br.z,
                row1.y * br.x + row4.y + row2.y * br.y + row3.y * br.z,
                row1.z * br.x + row4.z + row2.z * br.y + row3.z * br.z);
        }

        // ─── Чтение двух костей за один read (128 байт)
        struct TwoBones { Vector3 a, b; };

        TwoBones GetTwoBonePosComplex(CPed* ped, cSkeleton_t& skel,
                                      unsigned int maskA, unsigned int maskB)
        {
            if (!skel.m_pSkeleton || !skel.Arg2) return {{}, {}};

            int idxA = 0, idxB = 0;
            bool okA = GetBoneIndex(ped, skel, maskA, idxA);
            bool okB = GetBoneIndex(ped, skel, maskB, idxB);

            const Matrix v = skel.Arg1;
            const Vector3 row1(v._11, v._12, v._13);
            const Vector3 row2(v._21, v._22, v._23);
            const Vector3 row3(v._31, v._32, v._33);
            const Vector3 row4(v._41, v._42, v._43);

            auto CalcPos = [&](const Matrix& r) -> Vector3 {
                const Vector3 br(r._41, r._42, r._43);
                return Vector3(
                    row1.x * br.x + row4.x + row2.x * br.y + row3.x * br.z,
                    row1.y * br.x + row4.y + row2.y * br.y + row3.y * br.z,
                    row1.z * br.x + row4.z + row2.z * br.y + row3.z * br.z);
            };

            TwoBones result{};

            // Индексы соседние — один read 128 байт
            if (okA && okB && std::abs(idxA - idxB) <= 1) {
                int lo = std::min(idxA, idxB);
                struct TwoMat { Matrix m0, m1; };
                const TwoMat two = read<TwoMat>(skel.Arg2 + static_cast<uint64_t>(lo) * 64);
                result.a = okA ? CalcPos(idxA == lo ? two.m0 : two.m1) : Vector3();
                result.b = okB ? CalcPos(idxB == lo ? two.m0 : two.m1) : Vector3();
            } else {
                // Fallback: раздельное чтение
                if (okA) {
                    result.a = CalcPos(read<Matrix>(skel.Arg2 + static_cast<uint64_t>(idxA) * 64));
                }
                if (okB) {
                    result.b = CalcPos(read<Matrix>(skel.Arg2 + static_cast<uint64_t>(idxB) * 64));
                }
            }

            return result;
        }
    } // anonymous namespace

    bool UpdateFast(CPed* ped)
    {
        if (!ped || !validation::IsPtrSane(ped->ptr)) return false;

        ped->m_pBoneMatrix = read<Matrix>(ped->ptr + offsets::ped::BoneMatrix);

        CBoneFast raw = read<CBoneFast>(ped->ptr + offsets::ped::BoneList);

        Vector3 head   = raw.gHead;
        Vector3 lFoot  = raw.gLeftFoot;
        Vector3 rFoot  = raw.gRightFoot;
        Vector3 lAnkle = raw.gLeftAnkle;
        Vector3 rAnkle = raw.gRightAnkle;
        Vector3 lHand  = raw.gLeftHand;
        Vector3 rHand  = raw.gRightHand;
        Vector3 neck   = raw.gNeck;
        Vector3 hip    = raw.gHip;

        ped->BoneList[BONE_HEAD]        = Vec3_Transform(&head,   &ped->m_pBoneMatrix);
        ped->BoneList[BONE_NECK]        = Vec3_Transform(&neck,   &ped->m_pBoneMatrix);
        ped->BoneList[BONE_LEFT_HAND]   = Vec3_Transform(&lHand,  &ped->m_pBoneMatrix);
        ped->BoneList[BONE_RIGHT_HAND]  = Vec3_Transform(&rHand,  &ped->m_pBoneMatrix);
        ped->BoneList[BONE_LEFT_ANKLE]  = Vec3_Transform(&lAnkle, &ped->m_pBoneMatrix);
        ped->BoneList[BONE_RIGHT_ANKLE] = Vec3_Transform(&rAnkle, &ped->m_pBoneMatrix);
        ped->BoneList[BONE_LEFT_FOOT]   = Vec3_Transform(&lFoot,  &ped->m_pBoneMatrix);
        ped->BoneList[BONE_RIGHT_FOOT]  = Vec3_Transform(&rFoot,  &ped->m_pBoneMatrix);
        ped->BoneList[BONE_HIP]         = Vec3_Transform(&hip,    &ped->m_pBoneMatrix);
        return true;
    }

    bool UpdateDetailed(CPed* ped)
    {
        if (!ped || !validation::IsPtrSane(ped->ptr)) return false;

        cSkeleton_t skel{};
        if (!BuildSkeleton(ped->ptr, skel)) return false;

        ped->skeleton = skel;

        if (!UpdateFast(ped)) return false;

        // Читаем по 2 кости за вызов — вдвое меньше memory reads
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_HEAD,        MASK_NECK);
            ped->BoneList[BONE_HEAD] = r.a;  ped->BoneList[BONE_NECK] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_L_HAND,      MASK_R_HAND);
            ped->BoneList[BONE_LEFT_HAND] = r.a;  ped->BoneList[BONE_RIGHT_HAND] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_L_FOOT,      MASK_R_FOOT);
            ped->BoneList[BONE_LEFT_FOOT] = r.a;  ped->BoneList[BONE_RIGHT_FOOT] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_PELVIS,      MASK_L_UPPER_ARM);
            ped->BoneList[BONE_PELVIS] = r.a;  ped->BoneList[BONE_LEFT_UPPER_ARM] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_L_FOREARM,   MASK_R_UPPER_ARM);
            ped->BoneList[BONE_LEFT_FOREARM] = r.a;  ped->BoneList[BONE_RIGHT_UPPER_ARM] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_R_FOREARM,   MASK_L_THIGH);
            ped->BoneList[BONE_RIGHT_FOREARM] = r.a;  ped->BoneList[BONE_LEFT_THIGH] = r.b;
        }
        {
            auto r = GetTwoBonePosComplex(ped, skel, MASK_L_CALF,      MASK_R_THIGH);
            ped->BoneList[BONE_LEFT_CALF] = r.a;  ped->BoneList[BONE_RIGHT_THIGH] = r.b;
        }
        // Последняя кость одна — обычный вызов
        ped->BoneList[BONE_RIGHT_CALF] = GetBonePosComplex(ped, skel, MASK_R_CALF);

        return true;
    }

    namespace validation
    {
        bool IsPtrSane(uintptr_t p)
        {
            return p >= 0x10000ull && p < kPtrSentinel;
        }

        bool IsBoneSane(const Vector3& worldPos, const Vector3& pedPos)
        {
            if (!std::isfinite(worldPos.x) ||
                !std::isfinite(worldPos.y) ||
                !std::isfinite(worldPos.z)) return false;

            if (worldPos == Vector3(0.f, 0.f, 0.f)) return false;

            const float dx = worldPos.x - pedPos.x;
            const float dy = worldPos.y - pedPos.y;
            const float dz = worldPos.z - pedPos.z;
            return (dx*dx + dy*dy + dz*dz) <= (kMaxBoneFromPed * kMaxBoneFromPed);
        }
    }
}
