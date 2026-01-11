#include "cbase.h"

#include "in_buttons.h"
#include "mom_gamemovement.h"
#include "mom_player_shared.h"
#include "mom_shareddefs.h"
#include "movevars_shared.h"
#include "coordsize.h"
#include "mom_system_gamemode.h"

#ifdef CLIENT_DLL
#include "c_mom_triggers.h"
#else
#include "env_player_surface_trigger.h"
#include "momentum/mom_triggers.h"
#include "momentum/mom_system_saveloc.h"
#include "momentum/mom_timer.h"
#endif

#include "tier0/memdbgon.h"

#define NO_REFL_NORMAL_CHANGE -2.0f // not used
#define BHOP_DELAY_TIME 15 // Time to delay successive bhops by, in ticks
#define STOP_EPSILON 0.1
#define MAX_CLIP_PLANES 5

#define STAMINA_MAX 100.0f
#define STAMINA_COST_JUMP 25.0f
#define STAMINA_COST_FALL 20.0f // not used
#define STAMINA_RECOVER_RATE 19.0f
#define CS_WALK_SPEED 135.0f

#define DUCK_SPEED_MULTIPLIER 0.34f

#define GROUND_FACTOR_MULTIPLIER 301.99337741082998788946739227784f // not used

#define NON_JUMP_VELOCITY ((g_pGameModeSystem->IsTF2BasedMode()) ? 250.0f : 140.0f)

#define PK_VIEWPUNCH_SCALE 15.0f

// remove this eventually
ConVar sv_slope_fix("sv_slope_fix", "1");
ConVar sv_ramp_fix("sv_ramp_fix", "1");
ConVar sv_ramp_bumpcount("sv_ramp_bumpcount", "8", 0, "Helps with fixing surf/ramp bugs", true, 4, true, 16);
ConVar sv_ramp_initial_retrace_length("sv_ramp_initial_retrace_length", "0.2", 0,
                                      "Amount of units used in offset for retraces", true, 0.2f, true, 5.f);
ConVar sv_jump_z_offset("sv_jump_z_offset", "1.5", 0, "Amount of units in axis z to offset every time a player jumps",
                        true, 0.0f, true, 5.f);

ConVar sv_ladder_dampen("sv_ladder_dampen", "0.2", FCVAR_REPLICATED,
                        "Amount to dampen perpendicular movement on a ladder", true, 0.0f, true, 1.0f);
ConVar sv_ladder_angle("sv_ladder_angle", "-0.707", FCVAR_REPLICATED,
                       "Cos of angle of incidence to ladder perpendicular for applying ladder_dampen", true, -1.0f,
                       true, 1.0f);

ConVar sv_rngfix_enable("sv_rngfix_enable", "0", FCVAR_MAPPING);

#ifndef CLIENT_DLL
#include "env_player_surface_trigger.h"
static ConVar dispcoll_drawplane("dispcoll_drawplane", "0");
#endif

QAngle SampleViewPunch(ViewPunchEvent event)
{
    const int index = static_cast<int>(event);

    struct ViewPunchRange
    {
        QAngle min;
        QAngle max;
    };

    static const ViewPunchRange viewPunchRanges[static_cast<int>(ViewPunchEvent::COUNT)] = {
        {QAngle(2.2f, -0.7f, 0.85f), QAngle(3.4f, 0.7f, 1.5f)},
        {QAngle(5.58f, -0.7f, -1.2f), QAngle(6.88f, 0.7f, 0.9f)},
        {QAngle(2.2f, 0.0f, -0.62f), QAngle(3.4f, 0.0f, 0.62f)},
        {QAngle(-1.93f, -0.33f, 6.2f), QAngle(-2.42f, 0.33f, 6.67f)},
    };

    const auto &range = viewPunchRanges[index];

    return QAngle(SharedRandomFloat("PKViewPunchX", range.min.x, range.max.x),
                  SharedRandomFloat("PKViewPunchY", range.min.y, range.max.y),
                  SharedRandomFloat("PKViewPunchZ", range.min.z, range.max.z));
}

CMomentumGameMovement::CMomentumGameMovement() : m_pPlayer(nullptr) {}

void CMomentumGameMovement::ProcessMovement(CBasePlayer *pPlayer, CMoveData *data)
{
    m_pPlayer = ToCMOMPlayer(pPlayer);
    Assert(m_pPlayer);

    BaseClass::ProcessMovement(pPlayer, data);
}

float CMomentumGameMovement::LadderDistance() const
{
    if (player->GetMoveType() == MOVETYPE_LADDER)
        return 10.0f;
    return 2.0f;
}

bool CMomentumGameMovement::GameHasLadders() const
{
    return !g_pGameModeSystem->IsTF2BasedMode();
}

void CMomentumGameMovement::DecayPunchAngle()
{
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        BaseClass::DecayPunchAngle();
        return;
    }

    Vector vPunchAngle;

    vPunchAngle.x = m_pPlayer->m_Local.m_vecPunchAngle->x;
    vPunchAngle.y = m_pPlayer->m_Local.m_vecPunchAngle->y;
    vPunchAngle.z = m_pPlayer->m_Local.m_vecPunchAngle->z;

    float len = VectorNormalize(vPunchAngle);
    len -= (10.0 + len * 0.5) * gpGlobals->frametime;
    len = max(len, 0.0);
    VectorScale(vPunchAngle, len, vPunchAngle);

    m_pPlayer->m_Local.m_vecPunchAngle.Set(0, vPunchAngle.x);
    m_pPlayer->m_Local.m_vecPunchAngle.Set(1, vPunchAngle.y);
    m_pPlayer->m_Local.m_vecPunchAngle.Set(2, vPunchAngle.z);
}

float CMomentumGameMovement::LadderLateralMultiplier() const { return mv->m_nButtons & IN_DUCK ? 1.0f : 0.5f; }

bool CMomentumGameMovement::IsValidMovementTrace(trace_t &tr)
{
    trace_t stuck;

    // Apparently we can be stuck with pm.allsolid without having valid plane info ok..
    if (tr.allsolid || tr.startsolid)
    {
        return false;
    }

    // Maybe we don't need this one
    if (CloseEnough(tr.fraction, 0.0f, FLT_EPSILON))
    {
        return false;
    }

    if (CloseEnough(tr.fraction, 0.0f, FLT_EPSILON) &&
        CloseEnough(tr.plane.normal, Vector(0.0f, 0.0f, 0.0f), FLT_EPSILON))
    {
        return false;
    }

    // Is the plane deformed or some stupid shit?
    if (fabs(tr.plane.normal.x) > 1.0f || fabs(tr.plane.normal.y) > 1.0f || fabs(tr.plane.normal.z) > 1.0f)
    {
        return false;
    }

    TracePlayerBBox(tr.endpos, tr.endpos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, stuck);
    if (stuck.startsolid || !CloseEnough(stuck.fraction, 1.0f, FLT_EPSILON))
    {
        return false;
    }

    return true;
}

float CMomentumGameMovement::ClimbSpeed() const
{
    return (mv->m_nButtons & IN_DUCK ? BaseClass::ClimbSpeed() * DUCK_SPEED_MULTIPLIER : BaseClass::ClimbSpeed());
}

void CMomentumGameMovement::WalkMove()
{
    int i;

    Vector wishvel;
    float spd;
    float fmove, smove;
    Vector wishdir;
    float wishspeed;

    Vector dest;
    trace_t pm;
    Vector forward, right, up;

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_KZ))
    {
        if (m_pPlayer->m_flStamina > 0)
        {
            float flRatio = (STAMINA_MAX - ((m_pPlayer->m_flStamina / 1000.0f) * STAMINA_RECOVER_RATE)) / STAMINA_MAX;

            // This Goldsrc code was run with variable timesteps and it had framerate dependencies.
            // People looking at Goldsrc for reference are usually
            // (these days) measuring the stoppage at 60fps or greater, so we need
            // to account for the fact that Goldsrc was applying more stopping power
            // since it applied the slowdown across more frames.
            float flReferenceFrametime = 1.0f / 70.0f;
            float flFrametimeRatio = gpGlobals->frametime / flReferenceFrametime;

            flRatio = pow(flRatio, flFrametimeRatio);

            mv->m_vecVelocity.x *= flRatio;
            mv->m_vecVelocity.y *= flRatio;
        }
    }

    AngleVectors(mv->m_vecViewAngles, &forward, &right, &up); // Determine movement angles

    CHandle<CBaseEntity> oldground;
    oldground = player->GetGroundEntity();

    // Copy movement amounts
    fmove = mv->m_flForwardMove;
    smove = mv->m_flSideMove;

    // Zero out z components of movement vectors
    if (forward[2] != 0)
    {
        forward[2] = 0;
        VectorNormalize(forward);
    }

    if (right[2] != 0)
    {
        right[2] = 0;
        VectorNormalize(right);
    }

    for (i = 0; i < 2; i++) // Determine x and y parts of velocity
        wishvel[i] = forward[i] * fmove + right[i] * smove;

    wishvel[2] = 0.0f; // Zero out z part of velocity

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) &&
        m_pPlayer->m_bIsPowerSliding &&
        (sv_pk_slide_lock.GetBool()
#ifndef CLIENT_DLL
        || (player->GetGroundVPhysics() && wishvel.Length2D() == 0.0f)
#endif
         ))
    {
        // can't change direction in slide if sv_pk_slide_lock on.
        // Also there's some weird behaviour with sliding on 
        // physics objects which can be fixed by pretending they 
        // are holding the button down 
        VectorCopy(mv->m_vecVelocity, wishdir);
    }
    else
    {
        VectorCopy(wishvel, wishdir); // Determine maginitude of speed of move
    }


    wishspeed = VectorNormalize(wishdir);

    //
    // Clamp to server defined max speed
    //
    if ((wishspeed != 0.0f) && (wishspeed > mv->m_flMaxSpeed))
    {
        VectorScale(wishvel, mv->m_flMaxSpeed / wishspeed, wishvel);
        wishspeed = mv->m_flMaxSpeed;
    }

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && ((mv->m_nButtons & IN_DUCK) || (player->GetFlags() & FL_DUCKING)))
    {
        wishspeed = PK_CROUCH_SPEED;
    }

    float oldspeed = mv->m_vecVelocity.Length2D();

    // Set pmove velocity
    Accelerate(wishdir, wishspeed, sv_accelerate.GetFloat());

    // Cap ground speed if the speed is gained from the above Accelerate()
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        if (mv->m_vecVelocity.Length2D() >= wishspeed)
        {
            float cappedSpeed = Max(wishspeed, oldspeed);
            Vector direction = mv->m_vecVelocity;
            VectorNormalizeFast(direction);
            mv->m_vecVelocity = direction * cappedSpeed;
        }
    }

    // Cap ground movement speed in tf2 modes
    if (g_pGameModeSystem->IsTF2BasedMode())
    {
        float flNewSpeed = VectorLength(mv->m_vecVelocity);
        if (flNewSpeed > mv->m_flMaxSpeed)
        {
            float flScale = (mv->m_flMaxSpeed / flNewSpeed);
            mv->m_vecVelocity.x *= flScale;
            mv->m_vecVelocity.y *= flScale;
        }

        // Scale backwards movement if going faster than 100u/s
        if (VectorLength(mv->m_vecVelocity) > 100.0f)
        {
            float flDot = DotProduct(forward, mv->m_vecVelocity);

            // are we moving backwards at all?
            if (flDot < 0)
            {
                Vector vecBackMove = forward * flDot;
                Vector vecRightMove = right * DotProduct(right, mv->m_vecVelocity);

                // clamp the back move vector if it is faster than max
                float flBackSpeed = VectorLength(vecBackMove);
                float flMaxBackSpeed = (mv->m_flMaxSpeed * 0.9f);

                if (flBackSpeed > flMaxBackSpeed)
                {
                    vecBackMove *= flMaxBackSpeed / flBackSpeed;
                }

                // reassemble velocity
                mv->m_vecVelocity = vecBackMove + vecRightMove;

                flNewSpeed = VectorLength(mv->m_vecVelocity);
                if (flNewSpeed > mv->m_flMaxSpeed)
                {
                    float flScale = (mv->m_flMaxSpeed / flNewSpeed);
                    mv->m_vecVelocity.x *= flScale;
                    mv->m_vecVelocity.y *= flScale;
                }
            }
        }
    }

    // Add in any base velocity to the current velocity.
    VectorAdd(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);

    spd = VectorLength(mv->m_vecVelocity);

    if (CloseEnough(spd, 0.0f))
    {
        mv->m_vecVelocity.Init();
        // Now pull the base velocity back out.   Base velocity is set if you are on a moving object, like a conveyor
        // (or maybe another monster?)
        VectorSubtract(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);
        return;
    }

    // first try just moving to the destination
    dest[0] = mv->GetAbsOrigin()[0] + mv->m_vecVelocity[0] * gpGlobals->frametime;
    dest[1] = mv->GetAbsOrigin()[1] + mv->m_vecVelocity[1] * gpGlobals->frametime;

    // The original code was "+ mv->m_vecVelocity[1]" which was obviously incorrect and should be [2] but after changing
    // it to [2] the sliding on sloped grounds started happening, so now I think this is be the solution
    dest[2] = mv->GetAbsOrigin()[2];

    // first try moving directly to the next spot
    TracePlayerBBox(mv->GetAbsOrigin(), dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

    // If we made it all the way, then copy trace end as new player position.
    mv->m_outWishVel += wishdir * wishspeed;

    if (pm.fraction == 1)
    {
        mv->SetAbsOrigin(pm.endpos);
        // Now pull the base velocity back out.   Base velocity is set if you are on a moving object, like a conveyor
        // (or maybe another monster?)
        VectorSubtract(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);

        StayOnGround();
        return;
    }

    // Don't walk up stairs if not on ground.
    if (oldground == nullptr && player->GetWaterLevel() == 0)
    {
        // Now pull the base velocity back out.   Base velocity is set if you are on a moving object, like a conveyor
        // (or maybe another monster?)
        VectorSubtract(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);
        return;
    }

    // If we are jumping out of water, don't do anything more.
    if (player->m_flWaterJumpTime)
    {
        // Now pull the base velocity back out.   Base velocity is set if you are on a moving object, like a conveyor
        // (or maybe another monster?)
        VectorSubtract(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);
        return;
    }

    StepMove(dest, pm);

    // Now pull the base velocity back out.   Base velocity is set if you are on a moving object, like a conveyor (or
    // maybe another monster?)
    VectorSubtract(mv->m_vecVelocity, player->GetBaseVelocity(), mv->m_vecVelocity);

    StayOnGround();
}

void CMomentumGameMovement::StepMove(Vector &vecDestination, trace_t &trace)
{
    if (!sv_rngfix_enable.GetBool() || g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
    {
        BaseClass::StepMove(vecDestination, trace);
        return;
    }

    Vector vecEndPos;
    VectorCopy(vecDestination, vecEndPos);

    // Try sliding forward both on ground and up 16 pixels
    //  take the move that goes farthest
    Vector vecPos, vecVel;
    VectorCopy(mv->GetAbsOrigin(), vecPos);
    VectorCopy(mv->m_vecVelocity, vecVel);

    // Slide move down.
    TryPlayerMove(&vecEndPos, &trace);

    // The original StepMove will use the up result but with the down result's z-velocity if the up result goes farther.
    // This generates z-velocity from nowhere, which means it is beneficial to hit uphill slopes without pressing jump
    // to do ground movement instead of air movement. Instead, we should use one entire result or the other instead of
    // combining them. The main reason we might want to use the down result over the up result even if the up result
    // goes farther is if the down result causes the player to not be grounded in the future.
    if (mv->m_vecVelocity.z > NON_JUMP_VELOCITY)
    {
        float flStepDist = mv->GetAbsOrigin().z - vecPos.z;
        if (flStepDist > 0.0f)
        {
            mv->m_outStepHeight += flStepDist;
        }
        return;
    }

    // Down results.
    Vector vecDownPos, vecDownVel;
    VectorCopy(mv->GetAbsOrigin(), vecDownPos);
    VectorCopy(mv->m_vecVelocity, vecDownVel);

    // Reset original values.
    mv->SetAbsOrigin(vecPos);
    VectorCopy(vecVel, mv->m_vecVelocity);

    // Move up a stair height.
    VectorCopy(mv->GetAbsOrigin(), vecEndPos);
    if (player->m_Local.m_bAllowAutoMovement)
    {
        vecEndPos.z += player->m_Local.m_flStepSize + DIST_EPSILON;
    }

    TracePlayerBBox(mv->GetAbsOrigin(), vecEndPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);
    if (!trace.startsolid && !trace.allsolid)
    {
        mv->SetAbsOrigin(trace.endpos);
    }

    // Slide move up.
    TryPlayerMove();

    // Move down a stair (attempt to).
    VectorCopy(mv->GetAbsOrigin(), vecEndPos);
    if (player->m_Local.m_bAllowAutoMovement)
    {
        vecEndPos.z -= player->m_Local.m_flStepSize + DIST_EPSILON;
    }

    TracePlayerBBox(mv->GetAbsOrigin(), vecEndPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);

    // If we are not on the ground any more then use the original movement attempt.
    if (trace.plane.normal[2] < 0.7)
    {
        mv->SetAbsOrigin(vecDownPos);
        VectorCopy(vecDownVel, mv->m_vecVelocity);
        float flStepDist = mv->GetAbsOrigin().z - vecPos.z;
        if (flStepDist > 0.0f)
        {
            mv->m_outStepHeight += flStepDist;
        }
        return;
    }

    // If the trace ended up in empty space, copy the end over to the origin.
    if (!trace.startsolid && !trace.allsolid)
    {
        mv->SetAbsOrigin(trace.endpos);
    }

    // Copy this origin to up.
    Vector vecUpPos;
    VectorCopy(mv->GetAbsOrigin(), vecUpPos);

    // decide which one went farther
    float flDownDist =
        (vecDownPos.x - vecPos.x) * (vecDownPos.x - vecPos.x) + (vecDownPos.y - vecPos.y) * (vecDownPos.y - vecPos.y);
    float flUpDist =
        (vecUpPos.x - vecPos.x) * (vecUpPos.x - vecPos.x) + (vecUpPos.y - vecPos.y) * (vecUpPos.y - vecPos.y);
    if (flDownDist > flUpDist)
    {
        mv->SetAbsOrigin(vecDownPos);
        VectorCopy(vecDownVel, mv->m_vecVelocity);
    }

    float flStepDist = mv->GetAbsOrigin().z - vecPos.z;
    if (flStepDist > 0)
    {
        mv->m_outStepHeight += flStepDist;
    }
}

bool CMomentumGameMovement::LadderMove()
{
    trace_t pm;
    bool onFloor;
    Vector floor;
    Vector wishdir;
    Vector end;

    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return false;

    if (!GameHasLadders())
    {
        return false;
    }

    if (m_pPlayer->GetGrabbableLadderTime() > 0.0f)
    {
        m_pPlayer->SetGrabbableLadderTime(m_pPlayer->GetGrabbableLadderTime() - gpGlobals->frametime);
    }

    // If I'm already moving on a ladder, use the previous ladder direction
    if (player->GetMoveType() == MOVETYPE_LADDER)
    {
        wishdir = -player->m_vecLadderNormal;
    }
    else
    {
        // otherwise, use the direction player is attempting to move
        if (mv->m_flForwardMove || mv->m_flSideMove)
        {
            for (int i = 0; i < 3; i++) // Determine x and y parts of velocity
                wishdir[i] = m_vecForward[i] * mv->m_flForwardMove + m_vecRight[i] * mv->m_flSideMove;

            VectorNormalize(wishdir);
        }
        else
        {
            // Player is not attempting to move, no ladder behavior
            return false;
        }
    }

    // wishdir points toward the ladder if any exists

    if (m_pPlayer->GetGrabbableLadderTime() > 0.0f && m_bCheckForGrabbableLadder)
    {
        Vector temp = mv->m_vecVelocity * 2.0f;

        temp.z = -temp.z;

        VectorNormalize(temp);
        VectorMA(mv->GetAbsOrigin(), 10.0f, -temp, end);
    }
    else
    {
        VectorMA(mv->GetAbsOrigin(), LadderDistance(), wishdir, end);
    }

    TracePlayerBBox(mv->GetAbsOrigin(), end, LadderMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

    // no ladder in that direction, return
    if (pm.fraction == 1.0f || pm.plane.normal.z == 1.0f || !OnLadder(pm))
    {
        return false;
    }

    if (m_pPlayer->GetGrabbableLadderTime() > 0.0f && m_bCheckForGrabbableLadder)
    {
        mv->m_vecVelocity.Init();
        mv->SetAbsOrigin(pm.endpos);
        m_pPlayer->SetGrabbableLadderTime(0.1f);
    }

    player->SetMoveType(MOVETYPE_LADDER);
    player->SetMoveCollide(MOVECOLLIDE_DEFAULT);

    // On ladder, convert movement to be relative to the ladder
    player->SetLadderNormal(pm.plane.normal);

    VectorCopy(mv->GetAbsOrigin(), floor);
    floor[2] += GetPlayerMins()[2] - 1;

    if (enginetrace->GetPointContents(floor) == CONTENTS_SOLID || player->GetGroundEntity() != nullptr)
    {
        onFloor = true;
    }
    else
    {
        onFloor = false;
    }

    player->SetGravity(1.0f); // Should be always set on 1.0..

    float climbSpeed = ClimbSpeed();

    float forwardSpeed = 0, rightSpeed = 0;
    if (mv->m_nButtons & IN_BACK)
        forwardSpeed -= climbSpeed;

    if (mv->m_nButtons & IN_FORWARD)
        forwardSpeed += climbSpeed;

    if (mv->m_nButtons & IN_MOVELEFT)
        rightSpeed -= climbSpeed;

    if (mv->m_nButtons & IN_MOVERIGHT)
        rightSpeed += climbSpeed;

    if (mv->m_nButtons & IN_JUMP)
    {
        player->SetMoveType(MOVETYPE_WALK);
        player->SetMoveCollide(MOVECOLLIDE_DEFAULT);

        VectorScale(pm.plane.normal, 270, mv->m_vecVelocity);
    }
    else
    {
        if (forwardSpeed != 0 || rightSpeed != 0)
        {
            Vector velocity, perp, cross, lateral, tmp;

            // ALERT(at_console, "pev %.2f %.2f %.2f - ",
            //    pev->velocity.x, pev->velocity.y, pev->velocity.z);
            // Calculate player's intended velocity
            // Vector velocity = (forward * gpGlobals->v_forward) + (right * gpGlobals->v_right);
            VectorScale(m_vecForward, forwardSpeed, velocity);
            VectorMA(velocity, rightSpeed, m_vecRight, velocity);

            // Perpendicular in the ladder plane
            VectorCopy(vec3_origin, tmp);
            tmp[2] = 1;
            CrossProduct(tmp, pm.plane.normal, perp);
            VectorNormalize(perp);

            // decompose velocity into ladder plane
            float normal = DotProduct(velocity, pm.plane.normal);

            // This is the velocity into the face of the ladder
            VectorScale(pm.plane.normal, normal, cross);

            // This is the player's additional velocity
            VectorSubtract(velocity, cross, lateral);

            // This turns the velocity into the face of the ladder into velocity that
            // is roughly vertically perpendicular to the face of the ladder.
            // NOTE: It IS possible to face up and move down or face down and move up
            // because the velocity is a sum of the directional velocity and the converted
            // velocity through the face of the ladder -- by design.
            CrossProduct(pm.plane.normal, perp, tmp);

            //=============================================================================
            // HPE_BEGIN
            // [sbodenbender] make ladders easier to climb in cstrike
            //=============================================================================
            // break lateral into direction along tmp (up the ladder) and direction along perp (perpendicular to ladder)
            float tmpDist = DotProduct(tmp, lateral);
            float perpDist = DotProduct(perp, lateral);

            Vector angleVec = perp * perpDist;
            angleVec += cross;
            // angleVec is our desired movement in the ladder normal/perpendicular plane
            VectorNormalize(angleVec);
            float angleDot = DotProduct(angleVec, pm.plane.normal);
            // angleDot is our angle of incidence to the laddernormal in the ladder normal/perpendicular plane

            if (angleDot < sv_ladder_angle.GetFloat())
                lateral = (tmp * tmpDist) + (perp * sv_ladder_dampen.GetFloat() * perpDist);
            //=============================================================================
            // HPE_END
            //=============================================================================

            VectorMA(lateral, -normal, tmp, mv->m_vecVelocity);

            if (onFloor && normal > 0) // On ground moving away from the ladder
            {
                VectorMA(mv->m_vecVelocity, MAX_CLIMB_SPEED, pm.plane.normal, mv->m_vecVelocity);
            }
            // pev->velocity = lateral - (CrossProduct( trace.vecPlaneNormal, perp ) * normal);
        }
        else
        {
            mv->m_vecVelocity.Init();
        }
    }

    return true;
}

void CMomentumGameMovement::HandleDuckingSpeedCrop()
{
    if (m_pPlayer->m_bIsPowerSliding)
        return;

    if (g_pGameModeSystem->IsTF2BasedMode() || g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP) || g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        return BaseClass::HandleDuckingSpeedCrop();
    }

    if (!(m_iSpeedCropped & SPEED_CROPPED_DUCK))
    {
        if ((mv->m_nButtons & IN_DUCK) || (player->m_Local.m_bDucking) || (player->GetFlags() & FL_DUCKING))
        {
            mv->m_flForwardMove *= DUCK_SPEED_MULTIPLIER;
            mv->m_flSideMove *= DUCK_SPEED_MULTIPLIER;
            mv->m_flUpMove *= DUCK_SPEED_MULTIPLIER;
            m_iSpeedCropped |= SPEED_CROPPED_DUCK;
        }
    }
}

float CMomentumGameMovement::GetTimeToDuck()
{
    if (g_pGameModeSystem->IsTF2BasedMode())
        return 0.2f;

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return m_pPlayer->m_bIsPowerSliding ? 0.2f : 0.5f;

    return BaseClass::GetTimeToDuck();
}

float CMomentumGameMovement::GetDuckTimer()
{
    return g_pGameModeSystem->GameModeIs(GAMEMODE_CONC) ? 400.0f : BaseClass::GetDuckTimer();
}

bool CMomentumGameMovement::CanUnduck()
{
    trace_t trace;
    Vector newOrigin;

    VectorCopy(mv->GetAbsOrigin(), newOrigin);

    if (player->GetGroundEntity() != nullptr || m_pPlayer->m_CurrentSlideTrigger)
    {
        newOrigin += VEC_DUCK_HULL_MIN - VEC_HULL_MIN;
    }
    else
    {
        // If in air and letting go of crouch, make sure we can offset origin to make
        //  up for uncrouching
        Vector hullSizeNormal = VEC_HULL_MAX - VEC_HULL_MIN;
        Vector hullSizeCrouch = VEC_DUCK_HULL_MAX - VEC_DUCK_HULL_MIN;

        newOrigin += -g_pGameModeSystem->GetGameMode()->GetViewScale() * (hullSizeNormal - hullSizeCrouch);
    }

    UTIL_TraceHull(mv->GetAbsOrigin(), newOrigin, VEC_HULL_MIN, VEC_HULL_MAX, PlayerSolidMask(), player,
                   COLLISION_GROUP_PLAYER_MOVEMENT, &trace);

    if (trace.startsolid || (trace.fraction != 1.0f))
        return false;

    return true;
}

void CMomentumGameMovement::Friction()
{
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
    {
        BaseClass::Friction();
        return;
    }

    // Friction shouldn't be affected by z velocity
    Vector velocity = mv->m_vecVelocity;
    velocity.z = 0.0f;

    DoFriction(velocity);

    mv->m_vecVelocity.x = velocity.x;
    mv->m_vecVelocity.y = velocity.y;
}

float CMomentumGameMovement::GetPlayerGravity()
{
    if (g_pGameModeSystem->IsTF2BasedMode())
        return BaseClass::GetPlayerGravity();

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return sv_pk_gravity_scale.GetFloat() * GetWallrunGravityScale();

    // We otherwise don't mind if player gravity is set to 0
    return player->GetGravity();
}

float CMomentumGameMovement::GetWaterWaistOffset()
{
    return g_pGameModeSystem->IsTF2BasedMode() ? 12.0f : BaseClass::GetWaterWaistOffset();
}

float CMomentumGameMovement::GetWaterJumpUpZVelocity()
{
    return g_pGameModeSystem->IsTF2BasedMode() ? 300.0f : BaseClass::GetWaterJumpUpZVelocity();
}

float CMomentumGameMovement::GetWaterJumpForward()
{
    return g_pGameModeSystem->IsTF2BasedMode() ? 30.0f : BaseClass::GetWaterJumpForward();
}

void CMomentumGameMovement::CalculateWaterWishVelocityZ(Vector &wishVel, const Vector &forward)
{
    const bool bTF2Mode = g_pGameModeSystem->IsTF2BasedMode();

    // if we have the jump key down, move us up as well
    if (mv->m_nButtons & IN_JUMP)
    {
        if (bTF2Mode && m_pPlayer->GetWaterLevel() != WL_Eyes)
            return;

        wishVel[2] += mv->m_flClientMaxSpeed;
    }
    // Sinking after no other movement occurs
    else if (!mv->m_flForwardMove && !mv->m_flSideMove && !mv->m_flUpMove)
    {
        wishVel[2] -= 60.0f; // drift towards bottom
    }
    else
    {
        if (bTF2Mode)
        {
            wishVel[2] += mv->m_flUpMove;
        }
        else
        {
            // exaggerate upward movement along forward as well
            float upwardMovememnt = mv->m_flForwardMove * forward.z * 2;
            upwardMovememnt = clamp(upwardMovememnt, 0.f, mv->m_flClientMaxSpeed);
            wishVel[2] += mv->m_flUpMove + upwardMovememnt;
        }
    }
}

void CMomentumGameMovement::Duck()
{
    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return;

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
    {
        BaseClass::Duck();
        return;
    }

    if (g_pGameModeSystem->IsTF2BasedMode())
    {
        // Don't allow ducking if deep enough in water
        if ((player->GetWaterLevel() >= WL_Feet && player->GetGroundEntity() == nullptr) ||
            player->GetWaterLevel() >= WL_Eyes)
        {
            mv->m_nButtons &= ~IN_DUCK;
        }

        if (gpGlobals->curtime < m_pPlayer->m_fDuckTimer && player->GetGroundEntity() != nullptr)
        {
            mv->m_nButtons &= ~IN_DUCK;
        }

        if (player->m_Local.m_bDucked && player->m_Local.m_bDucking)
        {
            mv->m_nButtons &= ~IN_DUCK;
        }
    }

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        // Speed is too high - force duck key
        const float stopSpeed = sv_pk_slide_max_stop_speed.GetFloat();
        if (m_pPlayer->m_bIsPowerSliding && mv->m_vecVelocity.Length2DSqr() > stopSpeed * stopSpeed)
        {
            mv->m_nButtons |= IN_DUCK;
        }
    }

    int buttonsChanged = (mv->m_nOldButtons ^ mv->m_nButtons); // These buttons have changed this frame
    int buttonsPressed = buttonsChanged & mv->m_nButtons;      // The changed ones still down are "pressed"
    int buttonsReleased = buttonsChanged & mv->m_nOldButtons; // The changed ones which were previously down are "released"

   
    if (mv->m_nButtons & IN_DUCK)
    {
        mv->m_nOldButtons |= IN_DUCK;
    }
    else
    {
        mv->m_nOldButtons &= ~IN_DUCK;
    }

    if (IsDead())
    {
        // Unduck
        if (player->GetFlags() & FL_DUCKING)
        {
            FinishUnDuck();
        }
        return;
    }

    HandleDuckingSpeedCrop();

    // Holding duck, in process of ducking or fully ducked?
    if ((mv->m_nButtons & IN_DUCK) || (player->m_Local.m_bDucking) || (player->GetFlags() & FL_DUCKING))
    {
        if (mv->m_nButtons & IN_DUCK)
        {
            DoDuck(buttonsPressed);
        }
        else
        {
            DoUnduck(buttonsReleased);
        }
    }
}

void CMomentumGameMovement::DoDuck(int iButtonsPressed)
{
    const bool bFullyCrouched = (player->GetFlags() & FL_DUCKING) == FL_DUCKING;
    const bool bInAir = player->GetGroundEntity() == nullptr;

    float duckTimer = GetDuckTimer();

    if (iButtonsPressed & IN_DUCK)
    {
        if (!bFullyCrouched)
        {
            // Use 1 second so super long jump will work
            const bool bWasDucking = player->m_Local.m_bDucking;
            player->m_Local.m_bDucking = true;

            if (m_pPlayer->m_bIsSprinting && !bInAir && (mv->m_flForwardMove || mv->m_flSideMove))
            {
                const float forward = mv->m_flForwardMove / m_pPlayer->MaxSpeed();

                if (forward >= sv_pk_slide_max_angle_dot.GetFloat())
                    CheckPowerSlide();
            }

            if (!bWasDucking)
            {
                player->m_Local.m_flDucktime = duckTimer;
            }
            else
            {
                Vector vDuckHullMin = GetPlayerMins(true);
                Vector vStandHullMin = GetPlayerMins(false);
                float fMore = vDuckHullMin.z - vStandHullMin.z;

                const float duckViewZ = GetPlayerViewOffset(true).z;
                const float standViewZ = GetPlayerViewOffset(false).z;
                const float currentViewZ = player->GetViewOffset().z;
                const float denom = (duckViewZ - fMore) - standViewZ;

                float duckFraction = 0.0f;
                if (fabsf(denom) > 1e-6f)
                    duckFraction = (currentViewZ - standViewZ) / denom;

                duckFraction = clamp(duckFraction, 0.0f, 1.0f);

                const float duckTimeMs = GetTimeToDuck() * 1000.0f;
                player->m_Local.m_flDucktime = duckTimer - (duckFraction * duckTimeMs);
            }
        }
        else if (player->m_Local.m_bDucking)
        {
            // Invert time if released before fully unducked
            float remainingDuckMilliseconds = (duckTimer - player->m_Local.m_flDucktime) * (GetTimeToDuck() / TIME_TO_UNDUCK);
            player->m_Local.m_flDucktime = duckTimer - (GetTimeToDuck() * 1000.0f) + remainingDuckMilliseconds;
        }
    }

    if (player->m_Local.m_bDucking)
    {
        float duckmilliseconds = max(0.0f, duckTimer - player->m_Local.m_flDucktime);
        float duckseconds = duckmilliseconds / 1000.0f;

        const bool bIsSliding = m_pPlayer->m_CurrentSlideTrigger != nullptr;

        // Finish ducking immediately if duck time is over or not on ground

        //MOM_TODO: Fzzy's original code replaces both GetTimeToDuck()s here with (duckTimer / 1000.0f) ... is that correct?

        if (duckseconds >= GetTimeToDuck() || (!bIsSliding && bInAir))
        {
            FinishDuck();
        }
        else
        {
            // Calc parametric time
            float duckFraction = SimpleSpline(duckseconds / GetTimeToDuck());
            SetDuckedEyeOffset(duckFraction);
        }
    }
}

void CMomentumGameMovement::DoUnduck(int iButtonsReleased)
{
    if (g_pGameModeSystem->IsTF2BasedMode())
    {
        if (iButtonsReleased & IN_DUCK)
        {
            m_pPlayer->m_fDuckTimer = gpGlobals->curtime + 0.3f;
        }
    }

    const bool bIsSliding = m_pPlayer->m_CurrentSlideTrigger != nullptr;
    const bool bInAir = player->GetGroundEntity() == nullptr;

    // Try to unduck unless automovement is not allowed
    // NOTE: When not onground, you can always unduck
    if (player->m_Local.m_bAllowAutoMovement || (!bIsSliding && bInAir))
    {
        if (iButtonsReleased & IN_DUCK)
        {
            if ((player->GetFlags() & FL_DUCKING) && !player->m_Local.m_bDucking)
            {
                // Use 1 second so super long jump will work
                player->m_Local.m_flDucktime = GetDuckTimer();
                player->m_Local.m_bDucking = true; // or unducking
            }
            else if (player->m_Local.m_bDucking)
            {
                // Invert time if released before fully ducked
                float remainingUnduckMilliseconds = (GetDuckTimer() - player->m_Local.m_flDucktime) * (TIME_TO_UNDUCK / GetTimeToDuck());

                player->m_Local.m_flDucktime = GetDuckTimer() - TIME_TO_UNDUCK_MS + remainingUnduckMilliseconds;
            }
        }

        if (CanUnduck())
        {
            // Unducking in FF is instant
            if (g_pGameModeSystem->GameModeIs(GAMEMODE_CONC))
            {
                FinishUnDuck();
                return;
            }

            if (m_pPlayer->m_bIsPowerSliding)
            {
                EndPowerSlide();
            }

            if (player->m_Local.m_bDucking || player->m_Local.m_bDucked) // or unducking
            {
                float duckmilliseconds = max(0.0f, GetDuckTimer() - player->m_Local.m_flDucktime);
                float duckseconds = duckmilliseconds / 1000.0f;

                // Finish ducking immediately if duck time is over or not on ground
                if ((duckseconds >= TIME_TO_UNDUCK) || (!bIsSliding && bInAir))
                {
                    FinishUnDuck();
                }
                else
                {
                    // Calc parametric time
                    float duckFraction = SimpleSpline(1.0f - (duckseconds / TIME_TO_UNDUCK));
                    SetDuckedEyeOffset(duckFraction);
                    player->m_Local.m_bDucking = true;
                }
            }
        }
        else
        {
            // Still under something where we can't unduck, so make sure we reset this timer so
            //  that we'll unduck once we exit the tunnel, etc.
            player->m_Local.m_flDucktime = GetDuckTimer();

            if (g_pGameModeSystem->IsTF2BasedMode() || g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
            {
                SetDuckedEyeOffset(1.0f);
                player->m_Local.m_bDucked = true;
                player->m_Local.m_bDucking = false;
                // FL_DUCKING flag is the important bit here,
                // as it will allow for ctaps.
                player->AddFlag(FL_DUCKING);

                if (!(player->GetGroundEntity() != nullptr || m_pPlayer->m_CurrentSlideTrigger)) // From CanUnduck()
                    m_pPlayer->UpdateLastAction(SurfInt::ACTION_CTAP);
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: Stop ducking
//-----------------------------------------------------------------------------
void CMomentumGameMovement::FinishUnDuck()
{
    Vector newOrigin;
    VectorCopy(mv->GetAbsOrigin(), newOrigin);

    if (player->GetGroundEntity() != nullptr || m_pPlayer->m_CurrentSlideTrigger)
    {
        newOrigin += VEC_DUCK_HULL_MIN - VEC_HULL_MIN;
    }
    else
    {
        // If in air and letting go of crouch, make sure we can offset origin to make
        // up for uncrouching
        Vector hullSizeNormal = VEC_HULL_MAX - VEC_HULL_MIN;
        Vector hullSizeCrouch = VEC_DUCK_HULL_MAX - VEC_DUCK_HULL_MIN;

        Vector viewDelta = -g_pGameModeSystem->GetGameMode()->GetViewScale() * (hullSizeNormal - hullSizeCrouch);

        if (g_pGameModeSystem->GameModeIs(GAMEMODE_CONC))
        {
            viewDelta /= 2.0f;
        }

        VectorAdd(newOrigin, viewDelta, newOrigin);
    }

    player->m_Local.m_bDucked = false;
    player->RemoveFlag(FL_DUCKING);
    player->m_Local.m_bDucking = false;
    player->m_Local.m_bInDuckJump = false;
    player->SetViewOffset(GetPlayerViewOffset(false));
    player->m_Local.m_flDucktime = 0.f;

    mv->SetAbsOrigin(newOrigin);

#ifdef CLIENT_DLL
    if (!g_pGameModeSystem->IsCSBasedMode())
    {
        player->ResetLatched();
    }
#endif

    // Recategorize position since ducking can change origin
    CategorizePosition();
}

//-----------------------------------------------------------------------------
// Purpose: Finish ducking
//-----------------------------------------------------------------------------
void CMomentumGameMovement::FinishDuck()
{
    Vector hullSizeNormal = VEC_HULL_MAX - VEC_HULL_MIN;
    Vector hullSizeCrouch = VEC_DUCK_HULL_MAX - VEC_DUCK_HULL_MIN;

    Vector viewDelta = g_pGameModeSystem->GetGameMode()->GetViewScale() * (hullSizeNormal - hullSizeCrouch);

    player->SetViewOffset(GetPlayerViewOffset(true));
    player->AddFlag(FL_DUCKING);
    player->m_Local.m_bDucking = false;

    if (!player->m_Local.m_bDucked)
    {
        Vector org = mv->GetAbsOrigin();

        if (player->GetGroundEntity() != nullptr || m_pPlayer->m_CurrentSlideTrigger)
        {
            org -= VEC_DUCK_HULL_MIN - VEC_HULL_MIN;
        }
        else
        {
            org += viewDelta;
        }
        mv->SetAbsOrigin(org);

        player->m_Local.m_bDucked = true;

#ifdef CLIENT_DLL
        if (!g_pGameModeSystem->IsCSBasedMode())
        {
            player->ResetLatched();
        }
#endif
    }

    // See if we are stuck?
    FixPlayerCrouchStuck(true);

    // Recategorize position since ducking can change origin
    CategorizePosition();
}

void CMomentumGameMovement::PlayerMove()
{
    BaseClass::PlayerMove();

    if (player->IsAlive())
    {
        // Check if our eye height is too close to the ceiling and lower it.
        // This is needed because we have taller models with the old collision bounds.

        const float eyeClearance = 12.0f; // eye pos must be this far below the ceiling

        Vector offset = player->GetViewOffset();

        Vector vHullMin = GetPlayerMins((player->m_Local.m_bDucked && !player->m_Local.m_bDucking));
        Vector vHullMax = GetPlayerMaxs((player->m_Local.m_bDucked && !player->m_Local.m_bDucking));
        vHullMax.z = (player->m_Local.m_bDucked && !player->m_Local.m_bDucking) ? VEC_DUCK_VIEW.z : VEC_VIEW.z;

        Vector start = mv->GetAbsOrigin();

        Vector end = start;
        end.z += eyeClearance;

        trace_t trace;
        Ray_t ray;
        ray.Init(start, end, vHullMin, vHullMax);
        UTIL_TraceRay(ray, PlayerSolidMask(), mv->m_nPlayerHandle.Get(), COLLISION_GROUP_PLAYER_MOVEMENT, &trace);

        // Clip player view height to ceiling (unless we're in noclip)
        if (trace.fraction < 1.0f && player->GetMoveType() != MOVETYPE_NOCLIP && !m_pPlayer->m_bIsWallrunning)
        {
            float est = vHullMax.z + trace.endpos.z - mv->GetAbsOrigin().z - eyeClearance;

            if ((player->GetFlags() & FL_DUCKING) == 0 && !player->m_Local.m_bDucking && !player->m_Local.m_bDucked)
            {
                offset.z = est;
            }
            else
            {
                offset.z = min(est, offset.z);
            }
            player->SetViewOffset(offset);
        }
        else if (g_pGameModeSystem->IsCSBasedMode())
        {
            if ((player->GetFlags() & FL_DUCKING) == 0 && !player->m_Local.m_bDucking && !player->m_Local.m_bDucked)
            {
                player->SetViewOffset(VEC_VIEW);
            }
            else if (player->m_Local.m_bDucked && !player->m_Local.m_bDucking)
            {
                player->SetViewOffset(VEC_DUCK_VIEW);
            }
        }
    }
    PredictWallrun();
}

#define RJ_BUNNYHOP_MAX_SPEED_FACTOR 1.2f
void CMomentumGameMovement::PreventBunnyHopping()
{
    float maxscaledspeed = RJ_BUNNYHOP_MAX_SPEED_FACTOR * mv->m_flMaxSpeed;
    if (maxscaledspeed <= 0.0f)
        return;

    float speed = mv->m_vecVelocity.Length();
    if (speed <= maxscaledspeed)
        return;

    float fraction = maxscaledspeed / speed;

    mv->m_vecVelocity *= fraction;
}

void CMomentumGameMovement::CheckWaterJump()
{
    BaseClass::CheckWaterJump();
}

void CMomentumGameMovement::WaterJump()
{
    BaseClass::WaterJump();
}

void CMomentumGameMovement::CheckVelocity()
{
    BaseClass::CheckVelocity();
}

bool CMomentumGameMovement::ShouldApplyGroundFriction()
{
    return BaseClass::ShouldApplyGroundFriction() || // ground
           m_pPlayer->m_bIsWallrunning; // wall
}

bool CMomentumGameMovement::IsJumpBufferActive()
{
    if (sv_pk_jump_buffer_ticks.GetInt() <= 0)
        return false;

    if (m_pPlayer->m_flJumpBufferTime <= 0.0f)
        return false;

    if (gpGlobals->curtime > m_pPlayer->m_flJumpBufferTime)
    {
        m_pPlayer->m_flJumpBufferTime = 0.0f;
        return false;
    }

    return true;
}

bool CMomentumGameMovement::ShouldDoBufferedJump()
{
    if (!g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return false;

    if (!IsJumpBufferActive())
        return false;

    return (player->GetGroundEntity() != nullptr) || m_pPlayer->m_bIsWallrunning;
}

bool CMomentumGameMovement::PredictGroundTouch(float predictTime, trace_t *outTrace)
{
    if (predictTime <= 0.0f)
        return false;

    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return false;

    const Vector start = mv->GetAbsOrigin();
    Vector end = start + mv->m_vecVelocity * predictTime;
    const float gravity = GetCurrentGravity() * GetPlayerGravity();
    end.z -= 0.5f * gravity * predictTime * predictTime;

    trace_t tr;
    TracePlayerBBox(start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);
    if (outTrace)
        *outTrace = tr;

    return (tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f);
}

bool CMomentumGameMovement::PredictWallTouch(float predictTime, Vector *outNormal)
{
    if (predictTime <= 0.0f)
        return false;

    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return false;

    if (player->GetGroundEntity() != nullptr)
        return false;

    const Vector start = mv->GetAbsOrigin();
    const Vector end = start + mv->m_vecVelocity * predictTime;

    trace_t tr;
    TracePlayerBBox(start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

    if (tr.fraction == 1.0f)
        return false;

    if (tr.plane.normal.z >= 0.7f)
        return false;

    bool isWeak = false;
    if (!IsWallEligibleForWallrun(tr.endpos, tr.plane.normal, isWeak))
        return false;

    if (outNormal)
        *outNormal = tr.plane.normal;

    return true;
}

bool CMomentumGameMovement::CheckJumpButton()
{
    trace_t pm;

    // Avoid nullptr access, return false if somehow we don't have a player
    if (!player)
        return false;

    if (player->pl.deadflag)
    {
        mv->m_nOldButtons |= IN_JUMP; // don't jump again until released
        return false;
    }

    // See if we are waterjumping.  If so, decrement count and return.
    if (player->m_flWaterJumpTime)
    {
        player->m_flWaterJumpTime -= gpGlobals->frametime;
        if (player->m_flWaterJumpTime < 0.0f)
            player->m_flWaterJumpTime = 0.0f;

        return false;
    }

    // If we are in the water most of the way...
    if (player->GetWaterLevel() >= 2)
    {
        // swimming, not jumping
        SetGroundEntity(nullptr);

        if (player->GetWaterType() == CONTENTS_WATER) // We move up a certain amount
            mv->m_vecVelocity[2] = 100;
        else if (player->GetWaterType() == CONTENTS_SLIME)
            mv->m_vecVelocity[2] = 80;

        // play swimming sound
        if (player->m_flSwimSoundTime <= 0.0f)
        {
            // Don't play sound again for 1 second
            player->m_flSwimSoundTime = 1000;
            PlaySwimSound();
        }

        return false;
    }

    // Cannot jump while ducked in TF2
    if (g_pGameModeSystem->IsTF2BasedMode() && player->GetFlags() & FL_DUCKING)
    {
        return false;
    }

    const bool bPressedJump = (mv->m_nButtons & IN_JUMP);
    const bool bJustPressedJump = (bPressedJump && !(mv->m_nOldButtons & IN_JUMP));
    const bool bJustJumped = bJustPressedJump || ShouldDoBufferedJump();

    const bool bCoyoteJump = (gpGlobals->curtime <= m_pPlayer->m_flCoyoteTime);
    const bool bInAir = (player->GetGroundEntity() == nullptr);
    bool bDoAirJump = false;
    const bool bParkour = g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR);

    if (bParkour && bJustPressedJump && bInAir && !bCoyoteJump && !m_pPlayer->m_bIsWallrunning)
        m_pPlayer->m_flJumpBufferTime = gpGlobals->curtime + sv_pk_jump_buffer_ticks.GetInt() * gpGlobals->interval_per_tick;

    if (bInAir)
    {
        if (bParkour)
        {
            if (!bCoyoteJump)
                mv->m_nOldButtons |= IN_JUMP;
        }
        else
        {
            mv->m_nOldButtons |= IN_JUMP;
            return false; // in air, so no effect
        }
    }

    // Prevent jump if needed
    const bool bPlayerBhopBlocked = m_pPlayer->m_bPreventPlayerBhop &&
                                    gpGlobals->tickcount - m_pPlayer->m_iLandTick < BHOP_DELAY_TIME;
    if (bPlayerBhopBlocked)
    {
        return false;
    }

    if (!g_pGameModeSystem->GetGameMode()->CanBhop())
    {
        PreventBunnyHopping();
    }

    if (bParkour && m_pPlayer->m_bIsWallrunning && !bJustJumped)
        return false;

    if (bParkour && bInAir && !bCoyoteJump && !m_pPlayer->m_bIsWallrunning)
    {
        if (!bJustPressedJump)
            return false;

        if (IsJumpBufferActive())
        {
            // Buffered jumping: Don't airjump if we're about to hit the ground or a wall
            const float bufferTime = sv_pk_jump_buffer_ticks.GetInt() * gpGlobals->interval_per_tick;
            if (PredictGroundTouch(bufferTime) || PredictWallTouch(bufferTime))
                return false;
        }

        if (m_pPlayer->CanAirJump())
        {
            bDoAirJump = true;
        }
        else
        {
            return false;
        }
    }

    if (mv->m_nOldButtons & IN_JUMP && !IsJumpBufferActive())
    {
        if (!bDoAirJump && !m_pPlayer->HasAutoBhop() && !m_pPlayer->m_bIsWallrunning)
        {
            return false; // don't pogo stick
        }
    }

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP) ||
        (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && !m_pPlayer->m_bIsPowerSliding)) // ignore duck checks if powersliding
    {
        // Cannot jump while in the unduck transition.
        if (player->m_Local.m_bDucking && (player->GetFlags() & FL_DUCKING))
            return false;

        // Still updating the eye position.
        if (player->m_Local.m_flDuckJumpTime > 0.0f)
            return false;
    }

    const bool bWasGrounded = player->GetGroundEntity() != nullptr;

    // In the air now.
    SetGroundEntity(nullptr);

    // Set the last jump time
    m_pPlayer->m_Data.m_flLastJumpTime = gpGlobals->curtime;

    // Play different sound for air jump
    if (bDoAirJump)
    {
        m_pPlayer->PlayAirjumpSound(mv->GetAbsOrigin()); // softer
    }
    else
    {
        player->PlayStepSound(mv->GetAbsOrigin(), player->m_pSurfaceData, 1.0, true);
    }

    // MoveHelper()->PlayerSetAnimation( PLAYER_JUMP );
    // player->DoAnimationEvent(PLAYERANIMEVENT_JUMP);

    float flGroundFactor = 1.0f;
    if (player->m_pSurfaceData)
    {
        flGroundFactor = player->m_pSurfaceData->game.jumpFactor;
    }

    // Refund speed boost if we jump while sliding before we finished ducking
    if (m_pPlayer->m_bIsPowerSliding && m_pPlayer->m_Local.m_bDucking)
    {
        Vector velocity = mv->m_vecVelocity;
        velocity.z = 0.0f;
        float speed = velocity.Length();
        float newSpeed = max(0.0f, speed - m_pPlayer->m_flLastSlideBoost);
        if (speed > 0.0f)
            VectorScale(velocity, newSpeed / speed, velocity);
        
        mv->m_vecVelocity[0] = velocity[0];
        mv->m_vecVelocity[1] = velocity[1];
    }

    // Accelerate upward
    float startz = mv->m_vecVelocity[2];
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        const bool bCoyoteWallJump = bCoyoteJump && m_pPlayer->m_flWallrunFallAwayTime != 0.0f;
        if (m_pPlayer->m_bIsWallrunning || bCoyoteWallJump)
        {
            EndWallRun();
            DoWallJump();
        }
        else if (bDoAirJump)
        {
            DoAirJump();
        }
        else
        {
            DoRegularJump();
        }
    }
    else if (!g_pGameModeSystem->IsCSBasedMode() && (player->m_Local.m_bDucking ||
                                                player->GetFlags() & FL_DUCKING))
    {
        mv->m_vecVelocity[2] = flGroundFactor * g_pGameModeSystem->GetGameMode()->GetJumpFactor();

        if (!player->GetGroundEntity() && !m_pPlayer->m_CurrentSlideTrigger)
            m_pPlayer->UpdateLastAction(SurfInt::ACTION_DUCKJUMP);
    }
    else
    {
        // NOTE: CS-based modes should automatically come down here and use this branch ONLY. It is
        // part of the fixes to make 64s more consistent done a while ago
        mv->m_vecVelocity[2] += flGroundFactor * g_pGameModeSystem->GetGameMode()->GetJumpFactor();

        if (!player->GetGroundEntity() && !m_pPlayer->m_CurrentSlideTrigger)
            m_pPlayer->UpdateLastAction(SurfInt::ACTION_JUMP);
    }

    // stamina stuff (scroll/kz gamemode only)
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_KZ))
    {
        if (m_pPlayer->m_flStamina > 0)
        {
            float flRatio = (STAMINA_MAX - ((m_pPlayer->m_flStamina / 1000.0) * STAMINA_RECOVER_RATE)) / STAMINA_MAX;
            mv->m_vecVelocity[2] *= flRatio;
        }

        m_pPlayer->m_flStamina = (STAMINA_COST_JUMP / STAMINA_RECOVER_RATE) * 1000.0;
    }

    // Add the accelerated hop movement
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
    {
        Vector vecForward;
        AngleVectors(mv->m_vecViewAngles, &vecForward);
        vecForward.z = 0.0f;
        VectorNormalize(vecForward);

        // We give a certain percentage of the current forward movement as a bonus to the jump speed.  That bonus is clipped
        // to not accumulate over time.
        float flSpeedBoostPerc = (!m_pPlayer->m_bIsSprinting && !player->m_Local.m_bDucked) ? 0.5f : 0.1f;
        float flSpeedAddition = fabsf(mv->m_flForwardMove * flSpeedBoostPerc);
        float flMaxSpeed = mv->m_flMaxSpeed + (mv->m_flMaxSpeed * flSpeedBoostPerc);
        float flNewSpeed = (flSpeedAddition + mv->m_vecVelocity.Length2D());

        // If we're over the maximum, we want to only boost as much as will get us to the goal speed
        if (flNewSpeed > flMaxSpeed)
        {
            flSpeedAddition -= flNewSpeed - flMaxSpeed;
        }

        if (mv->m_flForwardMove < 0.0f)
            flSpeedAddition *= -1.0f;

        // Add it on
        VectorAdd((vecForward * flSpeedAddition), mv->m_vecVelocity, mv->m_vecVelocity);
    }

    FinishGravity();

    mv->m_outWishVel.z += mv->m_vecVelocity[2] - startz;
    mv->m_outStepHeight += 0.1f;

    if (g_pGameModeSystem->IsTF2BasedMode() || g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
        mv->m_outStepHeight += 0.05f; // 0.15f total

    if (g_pGameModeSystem->IsCSBasedMode())
    {
        // First do a trace all the way down to the ground
        TracePlayerBBox(mv->GetAbsOrigin(),
                        mv->GetAbsOrigin() + Vector(0.0f, 0.0f, -(sv_considered_on_ground.GetFloat() + 0.1f)),
                        PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

        // Did we hit ground (ground is at max 2 units away so fraction cant be 1.0f)
        if (pm.fraction != 1.0f && !pm.startsolid && !pm.allsolid)
        {
            // Now we find 1.5f above ground
            TracePlayerBBox(pm.endpos, pm.endpos + Vector(0.0f, 0.0f, sv_jump_z_offset.GetFloat()), PlayerSolidMask(),
                            COLLISION_GROUP_PLAYER_MOVEMENT, pm);

            if (pm.fraction == 1.0f && !pm.startsolid && !pm.allsolid)
            {
                // Everything is p100
                mv->SetAbsOrigin(pm.endpos);
            }
        }
    }
    else if (g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
    {
        player->m_Local.m_flJumpTime = GAMEMOVEMENT_JUMP_TIME;
        player->m_Local.m_bInDuckJump = true;

#ifdef GAME_DLL
        // Reset toggle duck if it's toggled
        m_pPlayer->ResetToggledInput(IN_DUCK);
#endif
    }
    else if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        EndPowerSlide();

        // coyote time ends now
        m_pPlayer->m_flCoyoteTime = 0;
        m_pPlayer->m_bWallrunHasBoost = true;
    }

    // Flag that we jumped.
    mv->m_nOldButtons |= IN_JUMP;
    m_pPlayer->m_flJumpBufferTime = 0.0f;

#ifndef CLIENT_DLL
    m_pPlayer->SetIsInAirDueToJump(true);
    // Fire that we jumped
    m_pPlayer->OnJump();
    IGameEvent *pEvent = gameeventmanager->CreateEvent("player_jumped");
    if (pEvent)
        gameeventmanager->FireEvent(pEvent);
#endif

    return true;
}

void CMomentumGameMovement::DoRegularJump()
{
    float jumpHeight;
    if (m_pPlayer->m_bIsPowerSliding)
    {
        jumpHeight = sv_pk_slide_jump_height.GetFloat();
        m_pPlayer->m_bDoFOVScale = false;
    }
    else
    {
        jumpHeight = sv_pk_jump_height.GetFloat();
    }

    player->m_Local.m_vecPunchAngleVel += SampleViewPunch(ViewPunchEvent::JUMP) * PK_VIEWPUNCH_SCALE;

    const float timeSinceLanding =
        (static_cast<float>(gpGlobals->tickcount - m_pPlayer->m_iLandTick)) * gpGlobals->interval_per_tick;

    if (timeSinceLanding < sv_pk_skip_time.GetFloat())
    {
        jumpHeight *= sv_pk_skip_jump_height_fraction.GetFloat();

        // Reduce speed to skip_speed_retain without going below
        Vector velocity = mv->m_vecVelocity;
        velocity.z = 0.0f;
        float speed = velocity.Length();
        float newSpeed = max(sv_pk_skip_speed_retain.GetFloat(), speed - sv_pk_skip_speed_reduce.GetFloat());
        if (speed > sv_pk_skip_speed_retain.GetFloat())
            VectorScale(velocity, newSpeed / speed, velocity);

        mv->m_vecVelocity[0] = velocity[0];
        mv->m_vecVelocity[1] = velocity[1];
    }

    mv->m_vecVelocity[2] = sqrt(2.0f * jumpHeight * sv_gravity.GetFloat());
}

void CMomentumGameMovement::DoAirJump()
{
    m_pPlayer->m_iAirJumps--;
    m_pPlayer->m_bDoFOVScale = false;
    player->m_Local.m_vecPunchAngleVel += SampleViewPunch(ViewPunchEvent::AIRJUMP) * PK_VIEWPUNCH_SCALE;

    const float startZ = mv->m_vecVelocity.z;
    const float minUpSpeed = sqrt(2.0f * sv_pk_airjump_height.GetFloat() * sv_gravity.GetFloat());
    const float jumpFrac = sqrt(sv_pk_airjump_min_height_fraction.GetFloat());
    const float delta = max(minUpSpeed - startZ, minUpSpeed * jumpFrac);
    mv->m_vecVelocity.z += delta;

    // Lurch
    Vector wishdir = m_vecForward * mv->m_flForwardMove + m_vecRight * mv->m_flSideMove;
    wishdir.z = 0.0f;
    wishdir.NormalizeInPlace();

    Vector velocity = mv->m_vecVelocity;
    velocity.z = 0.0f;
    const float strengthFrac = 1.0f;
    const float speed = min(velocity.Length(), sv_pk_airjump_horz_speed.GetFloat());
    const float maxDelta = 2.0f * sv_pk_airjump_horz_speed.GetFloat();

    // Do both the sv_pk_airjump_horz_speed boost as well as redirection
    RedirectVelocity(wishdir, velocity.Normalized() * speed, maxDelta, strengthFrac, sv_pk_airjump_horz_speed.GetFloat());
}

void CMomentumGameMovement::DoWallJump()
{
    player->m_Local.m_vecPunchAngleVel += SampleViewPunch(ViewPunchEvent::JUMP) * PK_VIEWPUNCH_SCALE;

    const Vector wallNormal = m_pPlayer->m_vecLastWallNormal;
    const float upSpeed = sv_pk_wallrun_jump_upspeed.GetFloat();
    const float inputDirSpeed = sv_pk_wallrun_jump_inputdirspeed.GetFloat();
    float outSpeed = sv_pk_wallrun_jump_outwardspeed.GetFloat();

    Vector wishdir = m_vecForward * mv->m_flForwardMove + m_vecRight * mv->m_flSideMove;
    wishdir.z = 0.0f;
    VectorNormalize(wishdir);

    Vector forward2D = m_vecForward;
    forward2D.z = 0.0f;
    VectorNormalize(forward2D);

    Vector wallNormal2D = wallNormal;
    wallNormal2D.z = 0.0f;
    VectorNormalize(wallNormal2D);

    float lookIntoWall = forward2D.Dot(wallNormal2D);
    float wishIntoWall = wishdir.Dot(wallNormal2D);

    // Are we both looking into the wall and inputting into the wall?
    if (lookIntoWall < 0.0f && wishIntoWall < 0.0f)
    {
        // Penalty scales with the product of look alignment and wishdir alignment
        // 1.0 = Perfectly looking AND inputting into wall (max penalty -> 0.2x speed)
        // 0.0 = either looking OR inputting parallel to wall (no penalty -> 1.0x speed)
        float alignmentProduct = lookIntoWall * wishIntoWall;
        float multiplier = Lerp(alignmentProduct, 1.0f, 0.2f);
        outSpeed *= multiplier;
    }

    Vector inputDirImpulse = inputDirSpeed * wishdir;

    const float inputIntoWall = inputDirImpulse.Dot(wallNormal);
    if (inputIntoWall < 0.0f)
    {
        // Cancel the component of input going into the wall
        inputDirImpulse += wallNormal * -inputIntoWall;
    }

    Vector horizontalImpulse = inputDirImpulse;
    const float outSpeedNeeded = outSpeed - DotProduct(horizontalImpulse, wallNormal);
    if (outSpeedNeeded > 0.0f)
    {
        horizontalImpulse += wallNormal * outSpeedNeeded;
    }

    mv->m_vecVelocity += horizontalImpulse;

    float addSpeed = upSpeed - mv->m_vecVelocity.z;
    addSpeed = clamp(addSpeed, 0.0f, 1.5f * upSpeed);
    mv->m_vecVelocity.z += addSpeed;
}

void CMomentumGameMovement::CategorizePosition()
{
    Vector point;
    trace_t pm;

    // Reset this each time we-recategorize, otherwise we have bogus friction when we jump into water and plunge
    // downward really quickly
    player->m_surfaceFriction = 1.0f;

    // Doing this before we move may introduce a potential latency in water detection, but
    // doing it after can get us stuck on the bottom in water if the amount we move up
    // is less than the 1 pixel 'threshold' we're about to snap to.    Also, we'll call
    // this several times per frame, so we really need to avoid sticking to the bottom of
    // water on each call, and the converse case will correct itself if called twice.
    CheckWater();

    // observers don't have a ground entity
    if (player->IsObserver())
        return;

    float flOffset = sv_considered_on_ground.GetFloat();

    const Vector bumpOrigin = mv->GetAbsOrigin();
    const bool bParkourStick = g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && player->GetGroundEntity() != nullptr;

    point[0] = bumpOrigin[0];
    point[1] = bumpOrigin[1];
    point[2] = bumpOrigin[2] - flOffset;

    float zvel = mv->m_vecVelocity[2];
    bool bMovingUp = zvel > 0.0f;
    bool bMovingUpRapidly = zvel > NON_JUMP_VELOCITY && !bParkourStick;
    float flGroundEntityVelZ = 0.0f;
    if (bMovingUpRapidly)
    {
        // Tracker 73219, 75878:  ywb 8/2/07
        // After save/restore (and maybe at other times), we can get a case where we were saved on a lift and
        //  after restore we'll have a high local velocity due to the lift making our abs velocity appear high.
        // We need to account for standing on a moving ground object in that case in order to determine if we really
        //  are moving away from the object we are standing on at too rapid a speed.  Note that CheckJump already sets
        //  ground entity to NULL, so this wouldn't have any effect unless we are moving up rapidly not from the jump
        //  button.
        CBaseEntity *ground = player->GetGroundEntity();
        if (ground)
        {
            flGroundEntityVelZ = ground->GetAbsVelocity().z;
            bMovingUpRapidly = (zvel - flGroundEntityVelZ) > NON_JUMP_VELOCITY;
        }
    }

    // Was on ground, but now suddenly am not
    if (bMovingUpRapidly || (bMovingUp && player->GetMoveType() == MOVETYPE_LADDER))
    {
        SetGroundEntity(nullptr);
    }
    else
    {
        // Try and move down.
        TryTouchGround(bumpOrigin, point, GetPlayerMins(), GetPlayerMaxs(), MASK_PLAYERSOLID,
                       COLLISION_GROUP_PLAYER_MOVEMENT, pm);

        // Was on ground, but now suddenly am not.  If we hit a steep plane, we are not on ground
        if (!pm.m_pEnt || pm.plane.normal[2] < 0.7f)
        {
            // Test four sub-boxes, to see if any of them would have found shallower slope we could actually stand on
            TryTouchGroundInQuadrants(bumpOrigin, point, MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, pm);

            if (!pm.m_pEnt || pm.plane.normal[2] < 0.7f)
            {
                SetGroundEntity(nullptr);

                // probably want to add a check for a +z velocity too!
                if ((mv->m_vecVelocity.z > 0.0f) && (player->GetMoveType() != MOVETYPE_NOCLIP))
                {
                    player->m_surfaceFriction = 0.25f;
                }
            }
        }
        else
        {
            if (player->GetGroundEntity() == nullptr)
            {
                Vector vecNextVelocity = mv->m_vecVelocity;

                // Apply half of gravity as that would be done in the next tick before movement code
                vecNextVelocity.z -= player->GetGravity() * GetCurrentGravity() * 0.5f * gpGlobals->frametime;

                // Try to predict what would happen on the next tick if the player didn't get grounded
                bool bGrounded = true; // Would the player be grounded next tick?
                if (sv_edge_fix.GetBool() && !(m_pPlayer->HasAutoBhop() && (mv->m_nButtons & IN_JUMP)))
                {
                    trace_t pmFall;

                    // Don't simulate the fall to the ground if the player is already directly on it
                    if (pm.fraction != 0.0f)
                    {
                        // Simulate the fall next tick
                        Vector endFall;
                        VectorMA(mv->GetAbsOrigin(), gpGlobals->frametime, vecNextVelocity, endFall);

                        TracePlayerBBox(mv->GetAbsOrigin(), endFall, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pmFall);
                    }
                    else if (m_pPlayer->GetInteraction(0).tick == gpGlobals->tickcount)
                    {
                        // Pretend we are starting the edgebug from where we hit the surface and then simulate a max
                        // distance edgebug to ensure consistency (see note on the sliding simulation)
                        pmFall = m_pPlayer->GetInteraction(0).trace;
                    }
                    else
                    {
                        // Fallback if the fraction is zero, but the player hasn't collided this tick for some reason
                        pmFall = pm;
                    }

                    if (!pmFall.DidHit())
                    {
                        bGrounded = false; // Would miss the ground the next tick
                    }
                    else
                    {
                        ClipVelocity(vecNextVelocity, pmFall.plane.normal, vecNextVelocity, 1.0f);

                        // Simulate the sliding on the ground (notice that we are using the full frametime to allow
                        // consistent max distance edgebugs)
                        Vector endSlide;
                        VectorMA(pmFall.endpos, gpGlobals->frametime, vecNextVelocity, endSlide);

                        trace_t pmSlide;
                        TracePlayerBBox(mv->GetAbsOrigin(), endSlide, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pmSlide);

                        // Check if there is ground under the player (otherwise it would be an edgebug)
                        trace_t pmGround;
                        TryTouchGround(pmSlide.endpos, pmSlide.endpos - Vector(0.0f, 0.0f, flOffset), GetPlayerMins(),
                                       GetPlayerMaxs(), MASK_PLAYERSOLID, COLLISION_GROUP_PLAYER_MOVEMENT, pmGround);

                        if (!pmGround.DidHit())
                            bGrounded = false; // Would edgebug next tick
                    }
                }

                if (sv_rngfix_enable.GetBool())
                {
                    if (bGrounded)
                    {
                        if (sv_slope_fix.GetBool())
                        {
                            ClipVelocity(vecNextVelocity, pm.plane.normal, vecNextVelocity, 1.0f);

                            // What constitutes a favorable landing depends on if the player is allowed to bhop.

                            // It also depends on if ground speed is capped *and* if the player actually bhops
                            // on the next tick, but if we assume ground speed is capped only when bhopping isn't
                            // allowed, all that matters is if bhopping is allowed.

                            // On modes that can't bhop, colliding before landing is better if it means they start sliding.
                            // If this check fails, we want to pretend they collided first and couldn't land,
                            // so we don't set the ground entity.
                            if (g_pGameModeSystem->GetGameMode()->CanBhop() || vecNextVelocity.z <= NON_JUMP_VELOCITY)
                            {
                                // Only update velocity as if we collided if it results in horizontal speed gain.
                                // Otherwise, we are probably going uphill and are actually trying to avoid this collision.
                                if (vecNextVelocity.Length2DSqr() > mv->m_vecVelocity.Length2DSqr())
                                {
                                    VectorCopy(vecNextVelocity, mv->m_vecVelocity);
                                }

                                SetGroundEntity(&pm);
                            }
                        }
                        else
                        {
                            SetGroundEntity(&pm);
                        }
                    }
                }
                else
                {
                    ClipVelocity(vecNextVelocity, pm.plane.normal, vecNextVelocity, 1.0f);

                    // Set ground entity if the player is not going to slide on a ramp next tick and if they will be
                    // grounded (exception if the player wants to bhop)
                    if (vecNextVelocity.z <= NON_JUMP_VELOCITY && bGrounded)
                    {
                        // Make sure we check clip velocity on slopes/surfs before setting the ground entity and nulling out
                        // velocity.z
                        if (sv_slope_fix.GetBool() && vecNextVelocity.Length2DSqr() > mv->m_vecVelocity.Length2DSqr())
                        {
                            VectorCopy(vecNextVelocity, mv->m_vecVelocity);
                        }

                        SetGroundEntity(&pm);
                    }
                }
            }
            else
            {
                // This is not necessary to do for other gamemodes as they do not reset the vertical velocity before WalkMove()
                if (g_pGameModeSystem->IsTF2BasedMode() && player->GetGroundEntity() != nullptr &&
                    player->GetMoveType() == MOVETYPE_WALK && player->GetWaterLevel() < WL_Eyes)
                {
                    Vector org = mv->GetAbsOrigin();
                    org.z = pm.endpos.z;
                    mv->SetAbsOrigin(org);
                }
                
                SetGroundEntity(&pm); // Otherwise, point to index of ent under us.
            }
        }
    }
}

void CMomentumGameMovement::FinishGravity()
{
    if (m_pPlayer->m_CurrentSlideTrigger && m_pPlayer->m_CurrentSlideTrigger->m_bDisableGravity)
        return;

    BaseClass::FinishGravity();
}

void CMomentumGameMovement::StartGravity()
{
    if (m_pPlayer->m_CurrentSlideTrigger && m_pPlayer->m_CurrentSlideTrigger->m_bDisableGravity)
        return;

    BaseClass::StartGravity();
}

void CMomentumGameMovement::FullWalkMove()
{
    if (!InWater())
    {
        StartGravity();
    }

    // If we are leaping out of the water, just update the counters.
    if (player->m_flWaterJumpTime > 0.0f)
    {
        WaterJump();
        TryPlayerMove();
        CheckWater();
        return;
    }

    // Make sure we haven't run out of wallrun time or aren't holding duck
    CheckShouldWallrunEnd();

    // If we are swimming in the water, see if we are nudging against a place we can jump up out
    //  of, and, if so, start out jump.  Otherwise, if we are not moving up, then reset jump timer to 0
    // If sliding is set we prefer to simulate sliding than being in water.. Could be fun for some mappers
    // that want sliding/iceskating into water. Who knows.
    bool bIsSliding = m_pPlayer->m_CurrentSlideTrigger != nullptr;
    if ((player->GetWaterLevel() >= WL_Waist) && !bIsSliding)
    {
        if (player->GetWaterLevel() == WL_Waist)
        {
            CheckWaterJump();
        }

        // If we are falling again, then we must not trying to jump out of water any more.
        if (mv->m_vecVelocity[2] < 0.0f && player->m_flWaterJumpTime)
        {
            player->m_flWaterJumpTime = 0.0f;
        }

        // Was jump button pressed?
        if (mv->m_nButtons & IN_JUMP)
        {
            CheckJumpButton();
        }
        else
        {
            mv->m_nOldButtons &= ~IN_JUMP;
        }

        // Perform regular water movement
        WaterMove();

        // Redetermine position vars
        CategorizePosition();

        // If we are on ground, no downward velocity.
        if (player->GetGroundEntity() != nullptr)
        {
            mv->m_vecVelocity[2] = 0.f;
        }
    }
    else
    // Not fully underwater
    {
        // Was jump button pressed?
        if ((mv->m_nButtons & IN_JUMP) || ShouldDoBufferedJump())
        {
            // Player should be able to jump when on ground and sliding on a slide trigger 
            // that allows it, check for ground entity before jump so player actually jumps.
            if (bIsSliding && m_pPlayer->m_CurrentSlideTrigger->m_bAllowingJump &&
                !m_pPlayer->m_CurrentSlideTrigger->m_bStuckOnGround)
                CategorizePosition();
            CheckJumpButton();
        }
        else
        {
            mv->m_nOldButtons &= ~IN_JUMP;
        }

        // Friction is handled before we add in any base velocity. That way, if we are on a conveyor,
        //  we don't slow when standing still, relative to the conveyor.
        if (player->GetGroundEntity() != nullptr)
        {
            mv->m_vecVelocity[2] = 0.0f;

            if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && m_pPlayer->m_bIsPowerSliding)
            {
                PowerSlideFriction();
            }
            else // not powersliding - normal friction
            {
                Friction();
            }
        }

        float stopSpeed = sv_pk_slide_stop_speed.GetFloat();
        if (m_pPlayer->m_bIsPowerSliding && mv->m_vecVelocity.Length2DSqr() < stopSpeed * stopSpeed)
            EndPowerSlide();

        // Make sure velocity is valid.
        CheckVelocity();

        Vector vecOldOrigin;
        if (bIsSliding)
            vecOldOrigin = mv->GetAbsOrigin();

        if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && m_pPlayer->m_bIsWallrunning)
        {
            WallrunMove();
        }
        else if (player->GetGroundEntity() != nullptr)
        {
            if (g_pGameModeSystem->IsTF2BasedMode())
                mv->m_vecVelocity[2] = 0.f;

            WalkMove();

            SurfInt::Action action = (player->GetFlags() & FL_DUCKING) ? SurfInt::ACTION_DUCKWALK : SurfInt::ACTION_WALK;
            m_pPlayer->UpdateLastAction(action);

            if (g_pGameModeSystem->IsCSBasedMode())
            {
                CategorizePosition();
            }

            if (!g_pGameModeSystem->IsTF2BasedMode())
            {
                m_bCheckForGrabbableLadder = m_pPlayer->GetGroundEntity() == nullptr;
                if (m_bCheckForGrabbableLadder)
                {
                    // Next 0.1 seconds you can grab the ladder
                    m_pPlayer->SetGrabbableLadderTime(0.1f);
                    LadderMove();
                    m_bCheckForGrabbableLadder = false;
                }
            }
        }
        else
        {
            AirMove(); // Take into account movement when in air.
        }

        if (bIsSliding)
        {
            // Fixes some inaccuracies while going up slopes.
            // This should fix also the issue by being stuck on them.

            Vector vecVelocity = mv->m_vecVelocity;

            trace_t pm;
            Vector vecNewOrigin = mv->GetAbsOrigin();

            TracePlayerBBox(vecOldOrigin, vecNewOrigin, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

            StepMove(vecNewOrigin, pm);

            mv->m_vecVelocity = vecVelocity;

            if (pm.fraction == 1.0f)
                mv->SetAbsOrigin(vecNewOrigin);
        }

        // Apply speed reductions when stepping up while sliding
        if (m_pPlayer->m_bIsPowerSliding && mv->m_outStepHeight > 0.0f)
        {
            Vector velocity = mv->m_vecVelocity;
            velocity.z = 0.0f;
            float speed = velocity.Length();
            float newSpeed = max(0.0f, speed - mv->m_outStepHeight * sv_pk_slide_step_velocity_reduction.GetFloat());

            if (speed > 0.0f)
                VectorScale(velocity, newSpeed / speed, velocity);

            mv->m_vecVelocity[0] = velocity[0];
            mv->m_vecVelocity[1] = velocity[1];
        }

        bool bInAirBefore = player->GetGroundEntity() == nullptr;

        // Set final flags.
        CategorizePosition();

        bool bInAirAfter = player->GetGroundEntity() == nullptr;

        if (bInAirBefore && !bInAirAfter && m_pPlayer->m_bSurfing)
        {
            m_pPlayer->SetRampLeaveVelocity(mv->m_vecVelocity);
        }

        const SurfInt &surfInt = m_pPlayer->GetInteraction(0);
        if (bInAirBefore && bInAirAfter && surfInt.tick == gpGlobals->tickcount)
        {
            // Check if player edgebugged
            if (surfInt.trace.plane.normal.z >= 0.7f && mv->m_vecVelocity.z <= NON_JUMP_VELOCITY) // Player would be grounded
            {
                m_pPlayer->UpdateLastAction(SurfInt::ACTION_EDGEBUG);

                // Let player bhop after an edgebug
                if (sv_edge_fix.GetBool() && !bIsSliding &&
                    (m_pPlayer->HasAutoBhop() && (mv->m_nButtons & IN_JUMP))) // Player wants to bhop
                {
                    SetGroundEntity(&surfInt.trace); // Allows the player to jump next tick
                }
            }
            else if (m_pPlayer->GetInteractionIndex(SurfInt::TYPE_FLOOR) == 0)
            {
                m_pPlayer->UpdateLastAction(SurfInt::ACTION_SLIDE);
            }
        }

        if (!InWater())
        {
            FinishGravity();
            // If we are on ground, no downward velocity.
            if (player->GetGroundEntity() != nullptr)
            {
                mv->m_vecVelocity[2] = 0.f;
            }
        }

        // Make sure velocity is valid.
        CheckVelocity();

        CheckFalling();

        // Stuck the player to ground, if flag on sliding is set so.
        if (bIsSliding && m_pPlayer->m_CurrentSlideTrigger->m_bStuckOnGround)
        {
            StuckGround();
        }
    }

    if ((m_nOldWaterLevel == WL_NotInWater && player->GetWaterLevel() != WL_NotInWater) ||
        (m_nOldWaterLevel != WL_NotInWater && player->GetWaterLevel() == WL_NotInWater))
    {
        PlaySwimSound();
#if !defined(CLIENT_DLL)
        player->Splash();
#endif
    }

    // Check if player bhop is blocked and update buttons
    const bool bPlayerBhopBlocked = m_pPlayer->m_bPreventPlayerBhop &&
                                    gpGlobals->tickcount - m_pPlayer->m_iLandTick < BHOP_DELAY_TIME;

    // For the HUD (see hud_timer.cpp)
    if (bPlayerBhopBlocked)
        m_pPlayer->m_afButtonDisabled |= IN_BHOPDISABLED;
    else
        m_pPlayer->m_afButtonDisabled &= ~IN_BHOPDISABLED;
}

// This limits the player's speed in the start zone, depending on which gamemode the player is currently playing.
// On surf/other, it only limits practice mode speed. On bhop/scroll, it limits the movement speed above a certain
// threshold, and clamps the player's velocity if they go above it.
// This is to prevent prespeeding and is different per gamemode due to the different respective playstyles of surf and
// bhop.
// MOM_TODO: Update this to extend to start zones of stages (if doing ILs)
void CMomentumGameMovement::LimitStartZoneSpeed()
{
#ifndef CLIENT_DLL
    if (m_pPlayer->m_Data.m_bIsInZone && m_pPlayer->m_Data.m_iCurrentZone == 1 &&
        !g_pSavelocSystem->IsUsingSaveLocMenu()) // MOM_TODO: && g_Timer->IsForILs()
    {
        // set bhop flag to true so we can't prespeed with practice mode
        if (m_pPlayer->m_bHasPracticeMode)
            m_pPlayer->m_bDidPlayerBhop = true;

        // depending on gamemode, limit speed outright when player exceeds punish vel
        CTriggerTimerStart *startTrigger = g_pMomentumTimer->GetStartTrigger(m_pPlayer->m_Data.m_iCurrentTrack);
        // This does not look pretty but saves us a branching. The checks are:
        // no nullptr, correct gamemode, is limiting leave speed and
        //    enough ticks on air have passed
        if (startTrigger && startTrigger->HasSpawnFlags(SF_LIMIT_LEAVE_SPEED))
        {
            bool bShouldLimitSpeed = true;

            if (m_pPlayer->GetGroundEntity() != nullptr)
            {
                if (m_pPlayer->m_iLimitSpeedType == SPEED_LIMIT_INAIR)
                {
                    bShouldLimitSpeed = false;
                }

                if (!m_pPlayer->m_bWasInAir && m_pPlayer->m_iLimitSpeedType == SPEED_LIMIT_ONLAND)
                {
                    bShouldLimitSpeed = false;
                }

                m_pPlayer->m_bWasInAir = false;
            }
            else
            {
                if (m_pPlayer->m_iLimitSpeedType == SPEED_LIMIT_GROUND)
                {
                    bShouldLimitSpeed = false;
                }

                m_pPlayer->m_bWasInAir = true;
            }

            if (bShouldLimitSpeed)
            {
                Vector& velocity = mv->m_vecVelocity;
                float PunishVelSquared = startTrigger->GetSpeedLimit() * startTrigger->GetSpeedLimit();

                if (velocity.Length2DSqr() > PunishVelSquared) // more efficient to check against the square of velocity
                {
                    float flOldz = velocity.z;
                    VectorNormalizeFast(velocity);
                    velocity *= startTrigger->GetSpeedLimit();
                    velocity.z = flOldz;
                    // New velocity is the unitary form of the current vel vector times the max speed amount
                    m_pPlayer->m_bShouldLimitSpeed = true;
                }
            }
        }
    }
#endif
}

void CMomentumGameMovement::StuckGround()
{
    if (!m_pPlayer || !m_pPlayer->m_CurrentSlideTrigger.Get())
        return;

    // clang-format off

    /*
        How it works:

                  TRIGGER                              A-B segment is the distance we want to get to compare if, when we were inside the trigger, the trigger touched a solid surface under our feet.
        ---------------------------                    In this way, we can avoid teleporting the player directly to the skybox stupidly. And makes less efforts for putting the trigger.
        |                         |                   
        |      PLAYER ORIGIN      |                     
        |            A            |                    The Problem: Since we can't trace directly PLAYER_ORIGIN to A as the ClipTraceToEntity is considering that the player is being in a solid,
        |            |            |                    it avoids the trace between A & B so we can't calculate it like this.
        |            |            |                    We can't also considerate that the trigger is only a rectangle, so stuffs can be really complicated since I'm bad at maths.
        -------------B-------------                    
                     |                                 To solve this problem, we can get the distance between PLAYER ORIGIN and SURFACE, and subtract it with B & C.
                     |                                 Or better, check if B-C < 0.0 which means basically if the surface hits the trigger.
                     |
    _________________C___________________ 
    _____________________________________               
                   SURFACE
    */

    // The proper way for it is to calculate where we are in the trigger and get the distance between the surface below
    // the box. So it doesn't go dumbly all under the map.

    // clang-format on
    trace_t tr_Point_C;
    Ray_t ray;

    Vector vAbsOrigin = mv->GetAbsOrigin(), vEnd = vAbsOrigin;

    // So a trigger can be that huge? I doubt it. But we might change the value in case.
    vEnd.z -= 8192.0f;

    ray.Init(vAbsOrigin, vEnd, GetPlayerMins(), GetPlayerMaxs());

    {
        CTraceFilterSimple tracefilter(player, COLLISION_GROUP_NONE);
        enginetrace->TraceRay(ray, MASK_PLAYERSOLID, &tracefilter, &tr_Point_C);
    }

    // If we didn't find any ground, we stop here.
    if (tr_Point_C.fraction != 1.0f)
    {
        // Now we need to get the B point.
        ray.Init(tr_Point_C.endpos, vAbsOrigin);

        // Get B point.
        trace_t tr_Point_B;
        enginetrace->ClipRayToEntity(ray, MASK_ALL, m_pPlayer->m_CurrentSlideTrigger, &tr_Point_B);

        // Did we hit our trigger?
        if (m_pPlayer->m_CurrentSlideTrigger == tr_Point_B.m_pEnt)
        {
            // Yep gotcha.
            float flDist__B_C = (tr_Point_C.endpos.z - tr_Point_B.endpos.z);

            // If the surface was in the trigger, we can apply the stuck to ground.
            if (CloseEnough(flDist__B_C, 0.0f))
            {
                // If the distance is good, we can start being on the surface and follow it.
                mv->SetAbsOrigin(tr_Point_C.endpos);

                // Reject velocity normal to the ground
                mv->m_vecVelocity -= tr_Point_C.plane.normal * mv->m_vecVelocity.Dot(tr_Point_C.plane.normal);

                StayOnGround();
            }

            // engine->Con_NPrintf(0, "%i %f", m_pPlayer->m_SrvData.m_SlideData.IsEnabled(), flDist__A_B);
        }
    }
}

void CMomentumGameMovement::RedirectVelocity(const Vector &wishdir, Vector velocity, const float maxDelta,
                                             const float strengthFrac, const float targetSpeed)
{
    if (wishdir.IsZero())
        return;

    velocity.z = 0.0f;

    // Lerp to desired direction
    Vector targetVel = wishdir * targetSpeed;
    targetVel = VectorLerp(velocity, targetVel, strengthFrac);

    targetVel = targetVel.Normalized() * targetSpeed;

    // Clamp delta length to maxDelta
    Vector delta = targetVel - velocity;
    if (delta.IsLengthGreaterThan(maxDelta))
        delta = delta.Normalized() * maxDelta;

    mv->m_vecVelocity += delta;
}

// Checks if the player inputted a movement key - if so, lurch, otherwise, do nothing
// May only be called when in the air
void CMomentumGameMovement::PerformLurchChecks()
{
    int buttonsChanged = (mv->m_nOldButtons ^ mv->m_nButtons); // These buttons have changed this frame
    int buttonsPressed = buttonsChanged & mv->m_nButtons;      // The changed ones still down are "pressed"

    const int moveButtons = IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT;
    if (!(buttonsPressed & moveButtons))
        return;

    // Use buttons to derive wishdir 
    // If we use m_vecFoward/m_vecRight, we may get the wrong wishdir due to KeyState fractional behavior
    float f = mv->m_nButtons & IN_FORWARD ? 1 : 0;
    float s = (mv->m_nButtons & IN_MOVERIGHT ? 1 : 0) - (mv->m_nButtons & IN_MOVELEFT ? 1 : 0);
    Vector wishdir = m_vecForward * f + m_vecRight * s;
    wishdir.z = 0.0f;
    wishdir.NormalizeInPlace();

    const float timeSinceJump = (gpGlobals->tickcount - m_pPlayer->m_iJumpTick) * gpGlobals->interval_per_tick;

    float minTime = sv_pk_lurch_periodmin.GetFloat();
    float maxTime = sv_pk_lurch_periodmax.GetFloat();

    // Remap the timeSinceJump into the lurch period - at max until minTime, then drops off linearly until maxTime
    const float lurchFrac = RemapValClamped(timeSinceJump, minTime, maxTime, 1.0f, 0.0f);
    const float strengthFrac = sv_pk_lurch_strength.GetFloat() * lurchFrac;
    const float maxDelta = sv_pk_lurch_max.GetFloat() * PK_SPRINT_SPEED;

    RedirectVelocity(wishdir, mv->m_vecVelocity, maxDelta, strengthFrac, mv->m_vecVelocity.Length2D());
}

void CMomentumGameMovement::AirMove()
{
    BaseClass::AirMove();
    
    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        PerformLurchChecks();
    }

    if (!g_pGameModeSystem->IsTF2BasedMode())
    {
        if (m_pPlayer->GetGrabbableLadderTime() > 0.0f)
        {
            m_bCheckForGrabbableLadder = true;
            LadderMove();
            m_bCheckForGrabbableLadder = false;
        }
    }
}

int CMomentumGameMovement::TryPlayerMove(Vector *pFirstDest, trace_t *pFirstTrace)
{
    int bumpcount, numbumps;
    Vector dir;
    float d;
    int numplanes;
    Vector planes[MAX_CLIP_PLANES];
    Vector primal_velocity, original_velocity;
    Vector new_velocity;
    Vector fixed_origin;
    Vector valid_plane;
    int i, j, h;
    trace_t pm;
    Vector end;
    float time_left, allFraction;
    int blocked;
    bool stuck_on_ramp;
    bool has_valid_plane;
    numbumps = sv_ramp_bumpcount.GetInt();

    blocked = 0;   // Assume not blocked
    numplanes = 0; //  and not sliding along any planes

    stuck_on_ramp = false;   // lets assume client isn't stuck already
    has_valid_plane = false; // no plane info gathered yet

    VectorCopy(mv->m_vecVelocity, original_velocity); // Store original velocity
    VectorCopy(mv->m_vecVelocity, primal_velocity);
    VectorCopy(mv->GetAbsOrigin(), fixed_origin);

    allFraction = 0;
    time_left = gpGlobals->frametime; // Total time for this movement operation.

    new_velocity.Init();
    valid_plane.Init();

    Vector vecWallNormal;

    for (bumpcount = 0; bumpcount < numbumps; bumpcount++)
    {
        if (mv->m_vecVelocity.Length() == 0.0)
            break;

        if (stuck_on_ramp && sv_ramp_fix.GetBool())
        {
            if (!has_valid_plane)
            {
                if (!CloseEnough(pm.plane.normal, Vector(0.0f, 0.0f, 0.0f), FLT_EPSILON) &&
                    valid_plane != pm.plane.normal)
                {
                    valid_plane = pm.plane.normal;
                    has_valid_plane = true;
                }
                else
                {
                    for (i = numplanes; i-- > 0;)
                    {
                        if (!CloseEnough(planes[i], Vector(0.0f, 0.0f, 0.0f), FLT_EPSILON) &&
                            fabs(planes[i].x) <= 1.0f && fabs(planes[i].y) <= 1.0f && fabs(planes[i].z) <= 1.0f &&
                            valid_plane != planes[i])
                        {
                            valid_plane = planes[i];
                            has_valid_plane = true;
                            break;
                        }
                    }
                }
            }

            if (has_valid_plane)
            {
                if (valid_plane.z >= 0.7f && valid_plane.z <= 1.0f)
                {
                    ClipVelocity(mv->m_vecVelocity, valid_plane, mv->m_vecVelocity, 1);
                    VectorCopy(mv->m_vecVelocity, original_velocity);
                }
                else
                {
                    ClipVelocity(mv->m_vecVelocity, valid_plane, mv->m_vecVelocity,
                                 1.0f + sv_bounce.GetFloat() * (1.0f - player->m_surfaceFriction));
                    VectorCopy(mv->m_vecVelocity, original_velocity);
                }
            }
            else // We were actually going to be stuck, lets try and find a valid plane..
            {
                // this way we know fixed_origin isn't going to be stuck
                float offsets[] = {(bumpcount * 2) * -sv_ramp_initial_retrace_length.GetFloat(), 0.0f,
                                   (bumpcount * 2) * sv_ramp_initial_retrace_length.GetFloat()};
                int valid_planes = 0;
                valid_plane.Init(0.0f, 0.0f, 0.0f);

                // we have 0 plane info, so lets increase our bbox and search in all 27 directions to get a valid plane!
                for (i = 0; i < 3; i++)
                {
                    for (j = 0; j < 3; j++)
                    {
                        for (h = 0; h < 3; h++)
                        {
                            Vector offset = {offsets[i], offsets[j], offsets[h]};

                            Vector offset_mins = offset / 2.0f;
                            Vector offset_maxs = offset / 2.0f;

                            if (offset.x > 0.0f)
                                offset_mins.x /= 2.0f;
                            if (offset.y > 0.0f)
                                offset_mins.y /= 2.0f;
                            if (offset.z > 0.0f)
                                offset_mins.z /= 2.0f;

                            if (offset.x < 0.0f)
                                offset_maxs.x /= 2.0f;
                            if (offset.y < 0.0f)
                                offset_maxs.y /= 2.0f;
                            if (offset.z < 0.0f)
                                offset_maxs.z /= 2.0f;

                            Ray_t ray;
                            ray.Init(fixed_origin + offset, end - offset, GetPlayerMins() - offset_mins,
                                     GetPlayerMaxs() + offset_maxs);
                            UTIL_TraceRay(ray, PlayerSolidMask(), mv->m_nPlayerHandle.Get(),
                                          COLLISION_GROUP_PLAYER_MOVEMENT, &pm);

                            // Only use non deformed planes and planes with values where the start point is not from a
                            // solid
                            if (fabs(pm.plane.normal.x) <= 1.0f && fabs(pm.plane.normal.y) <= 1.0f &&
                                fabs(pm.plane.normal.z) <= 1.0f && pm.fraction > 0.0f && pm.fraction < 1.0f &&
                                !pm.startsolid)
                            {
                                valid_planes++;
                                valid_plane += pm.plane.normal;
                            }
                        }
                    }
                }

                if (valid_planes && !CloseEnough(valid_plane, Vector(0.0f, 0.0f, 0.0f), FLT_EPSILON))
                {
                    has_valid_plane = true;
                    valid_plane.NormalizeInPlace();
                    continue;
                }
            }

            if (has_valid_plane)
            {
                VectorMA(fixed_origin, sv_ramp_initial_retrace_length.GetFloat(), valid_plane, fixed_origin);
            }
            else
            {
                stuck_on_ramp = false;
                continue;
            }
        }

        // Assume we can move all the way from the current origin to the
        //  end point.

        VectorMA(fixed_origin, time_left, mv->m_vecVelocity, end);

        // See if we can make it from origin to end point.
        // If their velocity Z is 0, then we can avoid an extra trace here during WalkMove.
        if (pFirstDest && end == *pFirstDest)
            pm = *pFirstTrace;
        else
        {
#if defined(PLAYER_GETTING_STUCK_TESTING)
            trace_t foo;
            TracePlayerBBox(mv->GetAbsOrigin(), mv->GetAbsOrigin(), PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT,
                            foo);
            if (foo.startsolid || foo.fraction != 1.0f)
            {
                Msg("bah\n");
            }
#endif
            if (stuck_on_ramp && has_valid_plane && sv_ramp_fix.GetBool())
            {
                TracePlayerBBox(fixed_origin, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);
                pm.plane.normal = valid_plane;
            }
            else
            {
                TracePlayerBBox(mv->GetAbsOrigin(), end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

                if (sv_rngfix_enable.GetBool() && sv_slope_fix.GetBool() && player->GetMoveType() == MOVETYPE_WALK &&
                    player->GetGroundEntity() == nullptr && player->GetWaterLevel() < WL_Waist &&
                    g_pGameModeSystem->GetGameMode()->CanBhop() && !g_pGameModeSystem->GameModeIs(GAMEMODE_AHOP))
                {
                    bool bValidHit = !pm.allsolid && pm.fraction < 1.0f;

                    bool bCouldStandHere = pm.plane.normal.z >= 0.7f && 
                                           mv->m_vecVelocity.z <= NON_JUMP_VELOCITY &&
                                           m_pPlayer->m_CurrentSlideTrigger == nullptr;

                    bool bMovingIntoPlane2D = (pm.plane.normal.x * mv->m_vecVelocity.x) + (pm.plane.normal.y * mv->m_vecVelocity.y) < 0.0f;

                    // Don't perform this fix on additional collisions this tick which have trace fraction == 0.0.
                    // This situation occurs when wedged between a standable slope and a ceiling.
                    // The player would be locked in place with full velocity (but no movement) without this check.
                    bool bWedged = m_pPlayer->GetInteraction(0).tick == gpGlobals->tickcount && pm.fraction == 0.0f;

                    if (bValidHit && bCouldStandHere && bMovingIntoPlane2D && !bWedged)
                    {
                        Vector vecNewVelocity;
                        ClipVelocity(mv->m_vecVelocity, pm.plane.normal, vecNewVelocity, 1.0f);

                        // Make sure allowing this collision would not actually be beneficial (2D speed gain)
                        if (vecNewVelocity.Length2DSqr() <= mv->m_vecVelocity.Length2DSqr())
                        {
                            // A fraction of 1.0 implies no collision, which means ClipVelocity will not be called.
                            // It also suggests movement for this tick is complete, so TryPlayerMove won't perform
                            // additional movement traces and the tick will essentially end early. We want this to
                            // happen because we need landing/jumping logic to be applied before movement continues.
                            // Ending the tick early is a safe and easy way to do this.

                            pm.fraction = 1.0f;
                        }
                    }
                }
            }
        }

        if (bumpcount && sv_ramp_fix.GetBool() && player->GetGroundEntity() == nullptr && !IsValidMovementTrace(pm))
        {
            has_valid_plane = false;
            stuck_on_ramp = true;
            continue;
        }

        // If we started in a solid object, or we were in solid space
        //  the whole way, zero out our velocity and return that we
        //  are blocked by floor and wall.

        if (pm.allsolid && !sv_ramp_fix.GetBool())
        {
            // entity is trapped in another solid
            VectorCopy(vec3_origin, mv->m_vecVelocity);
            return 4;
        }

        // If we moved some portion of the total distance, then
        //  copy the end position into the pmove.origin and
        //  zero the plane counter.
        if (pm.fraction > 0.0f)
        {
            if ((!bumpcount || player->GetGroundEntity() != nullptr || !sv_ramp_fix.GetBool()) && numbumps > 0 && pm.fraction == 1.0f)
            {
                // There's a precision issue with terrain tracing that can cause a swept box to successfully trace
                // when the end position is stuck in the triangle.  Re-run the test with an unswept box to catch that
                // case until the bug is fixed.
                // If we detect getting stuck, don't allow the movement
                trace_t stuck;
                TracePlayerBBox(pm.endpos, pm.endpos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, stuck);

                if ((stuck.startsolid || stuck.fraction != 1.0f) && !bumpcount && sv_ramp_fix.GetBool())
                {
                    has_valid_plane = false;
                    stuck_on_ramp = true;
                    continue;
                }
                else if (stuck.startsolid || stuck.fraction != 1.0f)
                {
                    Msg("Player will become stuck!!! allfrac: %f pm: %i, %f, %f, %f vs stuck: %i, %f, %f\n",
                        allFraction, pm.startsolid, pm.fraction, pm.plane.normal.z, pm.fractionleftsolid,
                        stuck.startsolid, stuck.fraction, stuck.plane.normal.z);
                    VectorCopy(vec3_origin, mv->m_vecVelocity);
                    break;
                }
            }

#if defined(PLAYER_GETTING_STUCK_TESTING)
            trace_t foo;
            TracePlayerBBox(pm.endpos, pm.endpos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, foo);
            if (foo.startsolid || foo.fraction != 1.0f)
            {
                Msg("Player will become stuck!!!\n");
            }
#endif
            if (sv_ramp_fix.GetBool())
            {
                has_valid_plane = false;
                stuck_on_ramp = false;
            }

            // actually covered some distance
            VectorCopy(mv->m_vecVelocity, original_velocity);
            mv->SetAbsOrigin(pm.endpos);
            VectorCopy(mv->GetAbsOrigin(), fixed_origin);
            allFraction += pm.fraction;
            numplanes = 0;
        }

        // If we covered the entire distance, we are done
        //  and can return.
        if (CloseEnough(pm.fraction, 1.0f, FLT_EPSILON))
        {
            if (bumpcount == 0)
            {
                if (m_pPlayer->m_bSurfing)
                {
                    m_pPlayer->SetRampLeaveVelocity(mv->m_vecVelocity);
                }

                // This was needed so filters that check for floor sliding or surfing can function properly
                if (m_pPlayer->m_bSurfing || m_pPlayer->GetInteractionIndex(SurfInt::TYPE_FLOOR) == 0)
                {
                    m_pPlayer->SetLastInteraction(pm, mv->m_vecVelocity, SurfInt::TYPE_LEAVE);
                    m_pPlayer->UpdateLastAction(SurfInt::ACTION_RAMPLEAVE);
                }
            }

            break; // moved the entire distance
        }

        // Save entity that blocked us (since fraction was < 1.0)
        //  for contact
        // Add it if it's not already in the list!!!
        MoveHelper()->AddToTouched(pm, mv->m_vecVelocity);

        if (player->GetGroundEntity() == nullptr)
        {
            SurfInt::Type type = SurfInt::TYPE_WALL;
            if (pm.plane.normal.z > 0.0f)
            {
                type = SurfInt::TYPE_FLOOR;
            }
            else if (pm.plane.normal.z < 0.0f)
            {
                type = SurfInt::TYPE_CEILING;
            }
            
            m_pPlayer->SetLastInteraction(pm, mv->m_vecVelocity, type);
        }

        // If the plane we hit has a high z component in the normal, then
        //  it's probably a floor
        if (pm.plane.normal[2] >= 0.7)
        {
            blocked |= 1; // floor
        }

        if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR) && pm.plane.normal[2] < 0.7f)
        {
            blocked |= 2;
            vecWallNormal = pm.plane.normal;
        }
        else if (CloseEnough(pm.plane.normal[2], 0.0f, FLT_EPSILON))
        {
            // If the plane has a zero z component in the normal, then it's a step or wall
            blocked |= 2; // step / wall
        }

        // Reduce amount of m_flFrameTime left by total time left * fraction
        //  that we covered.
        time_left -= time_left * pm.fraction;

        // Did we run out of planes to clip against?
        if (numplanes >= MAX_CLIP_PLANES)
        {
            // this shouldn't really happen
            //  Stop our movement if so.
            VectorCopy(vec3_origin, mv->m_vecVelocity);
            // Con_DPrintf("Too many planes 4\n");
            break;
        }

        // Set up next clipping plane
        VectorCopy(pm.plane.normal, planes[numplanes]);
        numplanes++;

        // modify original_velocity so it parallels all of the clip planes
        //

        // reflect player velocity
        // Only give this a try for first impact plane because you can get yourself stuck in an acute corner by
        // jumping in place
        //  and pressing forward and nobody was really using this bounce/reflection feature anyway...
        if (numplanes == 1 && player->GetMoveType() == MOVETYPE_WALK && player->GetGroundEntity() == nullptr)
        {
            // Is this a floor/slope that the player can walk on?
            if (planes[0][2] >= 0.7)
            {
                ClipVelocity(original_velocity, planes[0], new_velocity, 1);
                VectorCopy(new_velocity, original_velocity);
            }
            else // either the player is surfing or slammed into a wall
            {
                const bool bSurfing = planes[0][2] > 0.0f;
                if (bSurfing && !m_pPlayer->m_bSurfing)
                {
                    m_pPlayer->SetRampBoardVelocity(mv->m_vecVelocity);
                }

                ClipVelocity(original_velocity, planes[0], new_velocity, 1.0f + sv_bounce.GetFloat() * (1.0f - player->m_surfaceFriction));
            }

            VectorCopy(new_velocity, mv->m_vecVelocity);
            VectorCopy(new_velocity, original_velocity);
        }
        else
        {
            for (i = 0; i < numplanes; i++)
            {
                ClipVelocity(original_velocity, planes[i], mv->m_vecVelocity, 1.0);
                for (j = 0; j < numplanes; j++)
                {
                    if (j != i)
                    {
                        // Are we now moving against this plane?
                        if (mv->m_vecVelocity.Dot(planes[j]) < 0)
                            break; // not ok
                    }
                }

                if (j == numplanes) // Didn't have to clip, so we're ok
                    break;
            }

            // Did we go all the way through plane set
            if (i != numplanes)
            {
                // go along this plane
                // pmove.velocity is set in clipping call, no need to set again.
            }
            else
            { // go along the crease
                if (numplanes != 2)
                {
                    VectorCopy(vec3_origin, mv->m_vecVelocity);
                    break;
                }

                // Fun fact time: these next five lines of code fix (vertical) rampbug
                if (CloseEnough(planes[0], planes[1], FLT_EPSILON))
                {
                    // Why did the above return true? Well, when surfing, you can "clip" into the
                    // ramp, due to the ramp not pushing you away enough, and when that happens,
                    // a surfer cries. So the game thinks the surfer is clipping along two of the exact
                    // same planes. So what we do here is take the surfer's original velocity,
                    // and add the along the normal of the surf ramp they're currently riding down,
                    // essentially pushing them away from the ramp.

                    // Note: Technically the 20.0 here can be 2.0, but that causes "jitters" sometimes, so I found
                    // 20 to be pretty safe and smooth. If it causes any unforeseen consequences, tweak it!
                    VectorMA(original_velocity, 20.0f, planes[0], new_velocity);
                    mv->m_vecVelocity.x = new_velocity.x;
                    mv->m_vecVelocity.y = new_velocity.y;
                    // Note: We don't want the player to gain any Z boost/reduce from this, gravity should be the
                    // only force working in the Z direction!

                    // Lastly, let's get out of here before the following lines of code make the surfer lose speed.
                    break;
                }

                // Though now it's good to note: the following code is needed for when a ramp creates a "V" shape,
                // and pinches the surfer between two planes of differing normals.
                CrossProduct(planes[0], planes[1], dir);
                dir.NormalizeInPlace();
                d = dir.Dot(mv->m_vecVelocity);
                VectorScale(dir, d, mv->m_vecVelocity);
            }

            //
            // if original velocity is against the original velocity, stop dead
            // to avoid tiny oscillations in sloping corners
            //
            d = mv->m_vecVelocity.Dot(primal_velocity);
            if (d <= 0)
            {
                // Con_DPrintf("Back\n");
                VectorCopy(vec3_origin, mv->m_vecVelocity);
                break;
            }
        }
    }

    if (CloseEnough(allFraction, 0.0f, FLT_EPSILON))
    {
        // We don't want to touch this!
        // If a client is triggering this, and if they are on a surf ramp they will stand still but gain velocity
        // that can build up for ever. 
        // ...
        // However, if the player is currently sliding, another trace is needed to make sure the player does not
        // get stuck on an obtuse angle (slope to a flat ground) [eg bhop_w1s2]
        if (m_pPlayer->m_CurrentSlideTrigger.Get())
        {
            // Let's retrace in case we can go on our wanted direction.
            TracePlayerBBox(mv->GetAbsOrigin(), end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);

            // If we found something we stop.
            if (pm.fraction < 1.0f)
            {
                VectorCopy(vec3_origin, mv->m_vecVelocity);
            }
            // Otherwise we just set our next pos and we ignore the bug.
            else
            {
                mv->SetAbsOrigin(end);

                // Adjust to be sure that we are on ground.
                StayOnGround();
            }
        }
        else 
        {
            // otherwise default behavior
            VectorCopy(vec3_origin, mv->m_vecVelocity);
        }
    }

    if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
    {
        if (/*fLateralStoppingAmount > 1.0f &&*/
            blocked == 2 &&
            player->GetGroundEntity() == nullptr)
        {
            OnWallTouch(vecWallNormal, pm);
        }
    }

    float fLateralStoppingAmount = primal_velocity.Length2D() - mv->m_vecVelocity.Length2D();
    if (fLateralStoppingAmount > PLAYER_MAX_SAFE_FALL_SPEED)
    {
        float fSlamVol = (fLateralStoppingAmount > PLAYER_MAX_SAFE_FALL_SPEED * 2.0f) ? 1.0f : 0.85f;
        
        // Play rough landing sound with last traced surface.
        PlayerRoughLandingEffects(fSlamVol, pm.surface.surfaceProps);
    }

    return blocked;
}

void CMomentumGameMovement::SetGroundEntity(const trace_t *pm)
{
    // We check jump button because the player might want jumping while sliding
    // And it's more fun like this
    const auto pSlideTrigger = m_pPlayer->m_CurrentSlideTrigger.Get();

    if (pSlideTrigger)
    {
        bool bHasJumped = (mv->m_nButtons & IN_JUMP) && (m_pPlayer->HasAutoBhop() || !(mv->m_nOldButtons & IN_JUMP));
        bool bCanJumpTF2 = !(g_pGameModeSystem->IsTF2BasedMode() && (player->GetFlags() & FL_DUCKING));
        // Disallow stuckonground jumps as they instantly shoot you up slopes.
        bool bSlideAllowsJump = pSlideTrigger->m_bAllowingJump && !pSlideTrigger->m_bStuckOnGround;
        
        if (!(bSlideAllowsJump && (bHasJumped && bCanJumpTF2)))
            pm = nullptr;
    }
        

    bool bLanded = false;
    if (player->GetGroundEntity() == nullptr && (pm && pm->m_pEnt))
    {
        bLanded = true;

        if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        {
            if (m_pPlayer->m_bIsWallrunning)
                EndWallRun();

            // start slide from air
            if (mv->m_nOldButtons & IN_DUCK)
                CheckPowerSlide();

            const float fallSpeed = player->m_Local.m_flFallVelocity;
            const float fallHeight = fallSpeed * fallSpeed  / (12.0f * 2.0f * GetCurrentGravity());
            const float distMin = sv_pk_viewpunch_fall_distmin.GetFloat();
            const float distMax = sv_pk_viewpunch_fall_distmax.GetFloat();
            const float distMaxScale = sv_pk_viewpunch_fall_distmaxscale.GetFloat();
            float fallFrac;
            if (fallHeight < distMin)
                fallFrac = clamp(sinf(fallHeight / distMin * M_PI_F * 0.5f), 0.0f, 1.0f);
            else
                fallFrac = RemapValClamped(fallHeight, distMin, distMax, 1.0f, distMaxScale);

            player->m_Local.m_vecPunchAngleVel +=
                SampleViewPunch(ViewPunchEvent::FALL) * fallFrac * PK_VIEWPUNCH_SCALE;

            m_pPlayer->m_bWallrunHasBoost = false;
            m_pPlayer->m_bWallrunWeak = false;
            m_pPlayer->m_bHasLastWallrunStartPos = false;
            m_pPlayer->m_vecWallNormal.Init();
            m_pPlayer->m_vecTargetWallNormal.Init();
            m_pPlayer->m_flWallrunFallAwayTime = 0.0f;
            m_pPlayer->m_iAirJumps = sv_pk_airjump_max.GetInt();
        }
    }
    else if (player->GetGroundEntity() && !(pm && pm->m_pEnt))
    {
        if (g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        {
            // mobility - make sure can airjump after walking off ledge
            if (!(mv->m_nButtons & IN_JUMP))
            {
                // being airborn and holding down the jump button from 
                // a previous jump is the same as if you norm jumped
                // (as far as airjumping is concerned)
                // Msg("Not on ground anymore, AIRJUMP_NORM_JUMPING\n");
                m_pPlayer->m_flCoyoteTime = gpGlobals->curtime + sv_pk_coyote_time.GetFloat();
            }
        }
    }

    if (pm && pm->m_pEnt) // if (newGround)
    {
        SurfInt::Type type = m_pPlayer->GetGroundEntity() ? SurfInt::TYPE_GROUNDED : SurfInt::TYPE_LAND;
        m_pPlayer->SetLastInteraction(*pm, mv->m_vecVelocity, type);
    }
    else if (m_pPlayer->GetGroundEntity()) // Leaving ground
    {
        SurfInt::Action action = m_pPlayer->GetInteraction(0).action;
        m_pPlayer->SetLastInteraction(m_pPlayer->GetInteraction(0).trace, mv->m_vecVelocity, SurfInt::TYPE_LEAVE);
        
        if (action != SurfInt::ACTION_LAND && action != SurfInt::ACTION_GROUNDED)
            m_pPlayer->UpdateLastAction(action);
    }

    BaseClass::SetGroundEntity(pm);

    if (pm && pm->m_pEnt)
    {
         VectorCopy(pm->endpos, mv->m_vecGroundPosition);
    }

    // Doing this after the BaseClass call in case OnLand wants to use the new ground stuffs
    if (bLanded)
    {
#ifdef GAME_DLL
        m_pPlayer->SetIsInAirDueToJump(false);

        // Set the tick that we landed on something solid (can jump off of this)
        m_pPlayer->OnLand();
#endif
    }
}

bool CMomentumGameMovement::CanAccelerate()
{
    return BaseClass::CanAccelerate() || (player && player->IsObserver());
}

void CMomentumGameMovement::CheckParameters()
{
    // shift-walking useful for some maps with tight jumps
    if (mv->m_nButtons & IN_SPEED && g_pGameModeSystem->IsCSBasedMode())
    {
        mv->m_flClientMaxSpeed = CS_WALK_SPEED;
    }

    mv->m_flRawForwardMove = mv->m_flForwardMove;
    mv->m_flRawSideMove = mv->m_flSideMove;

    BaseClass::CheckParameters();
}

void CMomentumGameMovement::ReduceTimers()
{
    float frame_msec = 1000.0f * gpGlobals->frametime;

    if (m_pPlayer->m_flStamina > 0)
    {
        m_pPlayer->m_flStamina -= frame_msec;

        if (m_pPlayer->m_flStamina < 0)
        {
            m_pPlayer->m_flStamina = 0;
        }
    }

    if (m_pPlayer->m_Local.m_slideBoostCooldown > 0)
    {
        m_pPlayer->m_Local.m_slideBoostCooldown -= frame_msec;

        if (m_pPlayer->m_Local.m_slideBoostCooldown < 0)
        {
            m_pPlayer->m_Local.m_slideBoostCooldown = 0;
        }
    }

    BaseClass::ReduceTimers();
}

int CMomentumGameMovement::ClipVelocity(Vector in, Vector &normal, Vector &out, float overbounce)
{
    const int blocked = BaseClass::ClipVelocity(in, normal, out, overbounce);

    if (sv_rngfix_enable.GetBool())
        return blocked;

    // Check if the jump button is held to predict if the player wants to jump up an incline. Not checking for jumping
    // could allow players that hit the slope almost perpendicularly and still surf up the slope because they would
    // retain their horizontal speed
    if (sv_slope_fix.GetBool() && m_pPlayer->HasAutoBhop() && (mv->m_nButtons & IN_JUMP))
    {
        bool canJump = normal[2] >= 0.7f && out.z <= NON_JUMP_VELOCITY;

        if (m_pPlayer->m_CurrentSlideTrigger)
            canJump &= m_pPlayer->m_CurrentSlideTrigger->m_bAllowingJump;

        // If the player do not gain horizontal speed while going up an incline, then act as if the surface is flat
        if (canJump && (normal.x * in.x + normal.y * in.y < 0.0f) && out.Length2DSqr() <= in.Length2DSqr())
        {
            out.x = in.x;
            out.y = in.y;
            out.z = 0.0f;
        }
    }

    // Return blocking flags.
    return blocked;
}

inline float VectorYaw(const Vector &v)
{
    QAngle ang;
    VectorAngles(v, ang);
    return AngleNormalizePositive(ang[YAW]);
}

//-----------------------------------------------------------------------------
// Purpose: Check if the player should powerslide
//          * moving faster than normal run speed
//          * have HEV suit
//          (only called if on the ground and ducking)
//-----------------------------------------------------------------------------
void CMomentumGameMovement::CheckPowerSlide()
{
    if (!g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return;

    // Dead
    if (player->pl.deadflag)
        return;
    
    float speed = mv->m_vecVelocity.Length2D();

    if (speed < sv_pk_slide_required_start_speed.GetFloat())
        return;

    m_pPlayer->m_bIsPowerSliding = true;
    m_pPlayer->m_flLastSlideBoost = 0.0f;

    // boost speed toward the goal speed but not over
    if (player->m_Local.m_slideBoostCooldown <= 0)
    {
        float speedBoost = sv_pk_slide_speed_boost.GetFloat();
        float addSpeed = sv_pk_slide_speed_boost_cap.GetFloat() - speed;
        addSpeed = clamp(addSpeed, 0.0f, speedBoost);

        
        if (addSpeed > 0)
        {
            m_pPlayer->m_flLastSlideBoost = addSpeed;
            float newSpeed = speed + addSpeed;
            VectorScale(mv->m_vecVelocity, newSpeed / speed, mv->m_vecVelocity);
        }
        
        m_pPlayer->m_bDoFOVScale = true;
    }
    player->m_Local.m_slideBoostCooldown = sv_pk_slide_boost_cooldown.GetFloat() * 1000.f;

    m_pPlayer->PlayPowerSlideSound(mv->GetAbsOrigin());

    // Workaround for bug - if they slide, then jump, then slide on landing
    // the view stays at standing height from the second time onwards.
    // Only happens when using toggle duck. For now, just override it
    if ((player->m_Local.m_flDuckJumpTime == 0.0f) &&
        (fabsf(player->GetViewOffset().z - GetPlayerViewOffset(true).z) > 0.1f))
    {
        // set the eye height to the non-ducked height
        SetDuckedEyeOffset( /*duckFraction=*/1.0f);
    }

    // Immediately shrink our hull so we don't hit our heads at the start of a slide
    player->AddFlag(FL_DUCKING);
    player->m_Local.m_bDucked = true;
}

//-----------------------------------------------------------------------------
// Purpose: Ends powersliding
//          * sets vars
//          * stops sound
//-----------------------------------------------------------------------------
void CMomentumGameMovement::EndPowerSlide()
{
    m_pPlayer->m_bIsPowerSliding = false;
    m_pPlayer->StopPowerSlideSound();
}

//-----------------------------------------------------------------------------
// Purpose: Called instead of Friction() when powersliding. Ignore
//          surface friction and just steadily slow down to crawl
//          speed and apply speedgain from inclined surfaces
//-----------------------------------------------------------------------------
void CMomentumGameMovement::PowerSlideFriction()
{
    if (!m_pPlayer->m_bIsPowerSliding || m_pPlayer->m_flWaterJumpTime > 0.0f)
        return;

    // Friction shouldn't be affected by z velocity
    Vector velocity = mv->m_vecVelocity;
    velocity.z = 0.0f;

    // Calculate speed
    float speed = VectorLength(velocity);

    if (speed < 0.1f)
        return;

    float decel = sv_pk_slide_decel.GetFloat();

    // Trying to stand up - apply more decel
    if (!(player->m_nButtons & IN_DUCK) && speed > sv_pk_slide_max_stop_speed.GetFloat())
    {
        decel = sv_pk_slide_want_to_stop_decel.GetFloat();
    }

    float decay = min(1.f, sv_pk_slide_velocity_decay.GetFloat());
    float decayScale = pow(decay, gpGlobals->frametime);
    velocity.x *= decayScale;
    velocity.y *= decayScale;

    speed = VectorLength(velocity);
    float drop = decel * gpGlobals->frametime;
    float newSpeed = speed - drop;

    if (speed < 0.1f)
        return;

    if (newSpeed < 0)
        newSpeed = 0;

    if (newSpeed != speed) {
        VectorScale(velocity, newSpeed / speed, velocity);
    }

    mv->m_vecVelocity.x = velocity.x;
    mv->m_vecVelocity.y = velocity.y;

    // Apply incline gravity
    Vector up(0.0f, 0.0f, 1.0f);
    Vector groundNormal = m_pPlayer->GetInteraction(0).trace.plane.normal;
    float gravityScale = 0.5f * GetCurrentGravity() * GetPlayerGravity() * gpGlobals->frametime;
    Vector slideForce = groundNormal * up.Dot(groundNormal) * gravityScale;
    mv->m_vecVelocity.x += slideForce.x;
    mv->m_vecVelocity.y += slideForce.y;
}

//-----------------------------------------------------------------------------
// Purpose: Calculate the yaw angle between the player and the wall
//-----------------------------------------------------------------------------
float CMomentumGameMovement::GetWallRunYaw()
{
    QAngle angles;
    float player_yaw = AngleNormalizePositive(mv->m_vecAbsViewAngles[YAW]);
    VectorAngles(m_pPlayer->m_vecWallNormal, angles);
    float wall_yaw = AngleNormalizePositive(angles[YAW]);

    return player_yaw - wall_yaw;
}

bool CMomentumGameMovement::CanFeetReachWall(const Vector &position, const Vector &wallNormal)
{
    Vector mins = GetPlayerMins();
    Vector maxs = GetPlayerMaxs();
    maxs.z *= 0.5f;

    trace_t tr;
    const Vector end = position - wallNormal * sv_pk_wallrun_allowed_wall_dist.GetFloat();
    UTIL_TraceHull(position, end, mins, maxs, PlayerSolidMask(), player, COLLISION_GROUP_PLAYER_MOVEMENT, &tr);

    return tr.fraction < 1.0f;
}

bool CMomentumGameMovement::IsNearTopWall(const Vector &position, const Vector &wallNormal)
{
    trace_t tr;
    const float wallDist = sv_pk_wallrun_allowed_wall_dist.GetFloat();
    const Vector wallPos = position - wallNormal * wallDist;

    TracePlayerBBox(position, wallPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);
    if (tr.fraction == 1.0f)
        return false;

    const float stepSize = player->GetStepSize();
    const Vector stepUp = tr.endpos + Vector(0.0f, 0.0f, stepSize);
    TracePlayerBBox(tr.endpos, stepUp, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);
    if (tr.fraction < 1.0f)
        return false;

    const Vector stepAcross = tr.endpos - wallNormal * 2.0f;
    TracePlayerBBox(tr.endpos, stepAcross, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

    const Vector stepDown = tr.endpos + Vector(0.0f, 0.0f, -stepSize);
    TracePlayerBBox(tr.endpos, stepDown, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

    return tr.fraction < 1.0f && tr.plane.normal.z >= 0.7f;
}

bool CMomentumGameMovement::IsStep(const Vector &position, const Vector &wallNormal)
{
    trace_t tr;
    const Vector end = position + Vector(0.0f, 0.0f, -player->GetStepSize());
    TracePlayerBBox(position, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

    if (tr.fraction == 1.0f || tr.plane.normal.z < 0.7f)
        return false;

    return IsNearTopWall(tr.endpos, wallNormal);
}

bool CMomentumGameMovement::IsWallEligibleForWallrun(
    const Vector &wallPosition, const Vector &wallNormal, bool &outWeak)
{
    outWeak = false;

    // Feet are too far away from the wall - can't start a wallrun here
    if (!CanFeetReachWall(wallPosition, wallNormal))
        return false;

    // This "wall" is actually just a step
    if (IsStep(wallPosition, wallNormal))
        return false;

    if (player->m_Local.m_bDucked && !CanUnduck())
        return false;

    // The wall normal is different enough
    const float sameWallDot = sv_pk_wallrun_samewall_dot.GetFloat();
    if (!m_pPlayer->m_bHasLastWallrunStartPos || DotProduct(wallNormal, m_pPlayer->m_vecLastWallNormal) <= sameWallDot)
    {
        outWeak = !m_pPlayer->m_bWallrunHasBoost;
        return true;
    }

    // We can't start a wallrun on the same wall at a higher point
    const float heightDelta = wallPosition.z - m_pPlayer->m_vecLastWallrunStartPos.z;
    if (heightDelta > sv_pk_wallrun_samewall_height.GetFloat())
        return false;

    // We're landing on the same wall at a lower point - it's weak
    outWeak = true;
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Check whether we should start wallrunning. Called when we hit 
//          a wall while airborn
//-----------------------------------------------------------------------------
void CMomentumGameMovement::OnWallTouch(Vector &vecWallNormal, trace_t &pm)
{
    if (!g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return;

    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return;

    if (m_pPlayer->m_bIsWallrunning)
        return;

    if (player->GetWaterLevel() > WL_NotInWater)
        return;

    if (player->pl.deadflag)
        return;

    bool isWeak = false;
    if (!IsWallEligibleForWallrun(mv->GetAbsOrigin(), vecWallNormal, isWeak))
        return;

    m_pPlayer->m_bIsWallrunning = true;
    m_pPlayer->m_flWallrunStartTime = gpGlobals->curtime;
    m_pPlayer->m_flWallrunFallAwayTime = 0.0f;
    m_pPlayer->m_iAirJumps = sv_pk_airjump_max.GetInt();
    m_pPlayer->m_bWallrunWeak = isWeak;
    m_pPlayer->m_vecWallNormal = vecWallNormal;
    m_pPlayer->m_vecTargetWallNormal = vecWallNormal;
    m_pPlayer->m_vecLastWallNormal = vecWallNormal;
    m_pPlayer->m_vecLastWallrunStartPos = mv->GetAbsOrigin();
    m_pPlayer->m_bHasLastWallrunStartPos = true;
#ifdef GAME_DLL
    m_pPlayer->DeriveMaxSpeed();
#endif

    // If we've jumped since landing on the ground or touching the wall, give a boost
    const float upWallBoost = sv_pk_wallrun_upwallboost.GetFloat();
    if (m_pPlayer->m_bWallrunHasBoost)
    {
        float addSpeed = clamp(upWallBoost - mv->m_vecVelocity.z, 0.0f, upWallBoost);
        mv->m_vecVelocity.z += addSpeed;
        m_pPlayer->m_bWallrunHasBoost = false;
    }

    player->m_Local.m_vecPunchAngleVel +=
        SampleViewPunch(ViewPunchEvent::FALL) * PK_VIEWPUNCH_SCALE;

    // Apply more sideways viewpunch when looking more parallel to the wall
    const Vector wallHorizontal = vecWallNormal.Cross(Vector(0.0f, 0.0f, -1.0f));
    Vector forward2D = m_vecForward;
    forward2D.z = 0.0f;
    float forwardFrac = wallHorizontal.Dot(forward2D);
    QAngle startPunch = SampleViewPunch(ViewPunchEvent::WALLRUN_START) * PK_VIEWPUNCH_SCALE;
    startPunch.y *= forwardFrac;
    startPunch.z *= forwardFrac;
    player->m_Local.m_vecPunchAngleVel += startPunch;
}

void CMomentumGameMovement::DoWallRunFriction(Vector &velocity, const float friction)
{
    const float speed = velocity.Length();
    const float drop = speed * friction * gpGlobals->frametime;
    const float newSpeed = max(0.0f, speed - drop);
    
    if (newSpeed != speed)
    {
        velocity *= newSpeed / speed;
    }
}

float CMomentumGameMovement::GetWallrunGravityScale()
{
   if (!m_pPlayer->m_bIsWallrunning)
       return 1.0f;

   const float wallrunTime = gpGlobals->curtime - m_pPlayer->m_flWallrunStartTime;
   return m_pPlayer->m_bWallrunWeak ? 1.0f : clamp(wallrunTime / sv_pk_wallrun_gravity_rampuptime.GetFloat(), 0.0f, 1.0f);
}

void CMomentumGameMovement::PredictWallrun()
{
    if (!g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return;

    if (player->GetMoveType() == MOVETYPE_NOCLIP)
        return;

    if (m_pPlayer->m_bIsWallrunning)
        return;

    m_pPlayer->m_vecWallNormal.Init();

    if (player->GetGroundEntity() != nullptr)
        return;

    const float predictTime = sv_pk_wallrun_viewtilt_predict_time.GetFloat();
    if (predictTime <= 0.0f)
        return;

    Vector wallNormal;
    if (!PredictWallTouch(predictTime, &wallNormal))
        return;

    m_pPlayer->m_vecWallNormal = wallNormal;
}

void CMomentumGameMovement::FallAwayFromWall(const bool giveCoyoteTime)
{
    EndWallRun();
    mv->m_vecVelocity += m_pPlayer->m_vecLastWallNormal * sv_pk_wallrun_fallawayspeed.GetFloat();

    m_pPlayer->m_flWallrunFallAwayTime = gpGlobals->curtime;
    m_pPlayer->m_flCoyoteTime =
        gpGlobals->curtime + (giveCoyoteTime ? sv_pk_coyote_time.GetFloat() : 0.0f);
}

void CMomentumGameMovement::CheckShouldWallrunEnd()
{
    if (!m_pPlayer->m_bIsWallrunning)
        return;

    if (mv->m_nButtons & IN_DUCK)
    {
        FallAwayFromWall(false);
        return;
    }

    const float wallrunTime = gpGlobals->curtime - m_pPlayer->m_flWallrunStartTime;
    if (wallrunTime > sv_pk_wallrun_timelimit.GetFloat())
    {
        player->m_Local.m_vecPunchAngleVel += -SampleViewPunch(ViewPunchEvent::JUMP) * PK_VIEWPUNCH_SCALE;
        FallAwayFromWall(true);
        return;
    }

    const float pushAwayTime = gpGlobals->curtime - m_pPlayer->m_flWallrunPushAwayTime;
    if (m_pPlayer->m_flWallrunPushAwayTime != 0.0f && pushAwayTime > sv_pk_wallrun_pushaway_fallofftime.GetFloat())
    {
        FallAwayFromWall(true);
    }
}

void CMomentumGameMovement::StayOnWall()
{
    const Vector oldWallNormal = m_pPlayer->m_vecWallNormal;

    trace_t tr;
    const Vector start = mv->GetAbsOrigin();
    const Vector end = start - oldWallNormal * (player->GetStepSize() + sv_pk_wallrun_allowed_wall_dist.GetFloat());
    TracePlayerBBox(start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

    if (tr.fraction < 1.0f && tr.plane.normal.z < 0.7f)
    {
        m_pPlayer->m_vecTargetWallNormal = tr.plane.normal;
    }

    // Give the wall normal a chance to catch up before we check if we can stick to the wall
    UpdateWallNormal();

    // Only snap to the wall if the angle change isn't too sharp
    if (tr.fraction < 1.0f &&
        tr.plane.normal.Dot(m_pPlayer->m_vecWallNormal) >= sv_pk_wallrun_anglechange_mincos.GetFloat())
    {
        mv->SetAbsOrigin(tr.endpos);
    }

    if (!CanFeetReachWall(mv->GetAbsOrigin(), m_pPlayer->m_vecWallNormal))
    {
        FallAwayFromWall(true);
    }
}

// Slerp the current wall normal to the desired wall normal
void CMomentumGameMovement::UpdateWallNormal()
{
    Vector wallNormal = m_pPlayer->m_vecWallNormal;
    const Vector targetWallNormal = m_pPlayer->m_vecTargetWallNormal;
    const float dot = clamp(wallNormal.Dot(targetWallNormal), -1.0f, 1.0f);
    const float angle = acosf(dot);
    if (angle > 0.0f)
    {
        const float maxStep = sv_pk_wallrun_rotate_maxrate.GetFloat() * gpGlobals->frametime;
        if (maxStep >= angle)
        {
            wallNormal = targetWallNormal;
        }
        else
        {
            const float t = maxStep / angle;
            const float sinAngle = sinf(angle);
            const float fromScale = sinf((1.0f - t) * angle) / sinAngle;
            const float toScale = sinf(t * angle) / sinAngle;
            wallNormal = wallNormal * fromScale + targetWallNormal * toScale;
            wallNormal.NormalizeInPlace();
        }
    }

    m_pPlayer->m_vecWallNormal = wallNormal;
    m_pPlayer->m_vecLastWallNormal = wallNormal;
}

int CMomentumGameMovement::WallrunStepMove(const Vector &stepDir, Vector &vecDestination, trace_t &trace)
{
    Vector vecEndPos = vecDestination;
    Vector vecPos = mv->GetAbsOrigin();
    Vector vecVel = mv->m_vecVelocity;

    // Try moving directly
    int blocked = TryPlayerMove(&vecEndPos, &trace);

    Vector vecDownPos = mv->GetAbsOrigin();
    Vector vecDownVel = mv->m_vecVelocity;

    // Try stepping out from the wall
    mv->SetAbsOrigin(vecPos);
    mv->m_vecVelocity = vecVel;

    const float stepSize = player->GetStepSize() + DIST_EPSILON;
    Vector vecStepPos = vecPos + stepDir * stepSize;
    TracePlayerBBox(vecPos, vecStepPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);
    if (!trace.startsolid && !trace.allsolid)
    {
        mv->SetAbsOrigin(trace.endpos);
    }

    // Move while stepped out
    blocked |= TryPlayerMove();

    // Step back toward the wall.
    const Vector backStart = mv->GetAbsOrigin();
    Vector vecBackPos = backStart - stepDir * stepSize;
    TracePlayerBBox(backStart, vecBackPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);
    if (trace.fraction == 1.0f || trace.startsolid || trace.allsolid)
    {
        mv->SetAbsOrigin(vecDownPos);
        mv->m_vecVelocity = vecDownVel;
        return blocked;
    }

    const float minWallDot = 1.0f;
    const Vector stepPos = trace.endpos;
    const float dot = trace.plane.normal.Dot(stepDir);
    if (dot <= 0.0f)
    {
        mv->SetAbsOrigin(vecDownPos);
        mv->m_vecVelocity = vecDownVel;
        return blocked;
    }
    if (dot < minWallDot)
    {
        const float frac = clamp(dot / minWallDot, 0.0f, 1.0f);
        mv->SetAbsOrigin(vecDownPos + (stepPos - vecDownPos) * frac * frac * frac);
    }
    else
    {
        mv->SetAbsOrigin(stepPos);
    }

    Vector vecUpPos = mv->GetAbsOrigin();

    Vector deltaDown = vecDownPos - vecPos;
    deltaDown -= stepDir * stepDir.Dot(deltaDown);
    Vector deltaUp = vecUpPos - vecPos;
    deltaUp -= stepDir * stepDir.Dot(deltaUp);

    // Keep the result that moved farther along the wall plane
    if (deltaDown.LengthSqr() > deltaUp.LengthSqr())
    {
        mv->SetAbsOrigin(vecDownPos);
        mv->m_vecVelocity = vecDownVel;
    }

    return blocked;
}

ConVar pk_max_move("pk_max_move", "400.0");

// Handle wallrun movement and friction
void CMomentumGameMovement::WallrunMove()
{
    float verticalFriction, horizontalFriction;
    verticalFriction = horizontalFriction = sv_pk_wallrun_friction.GetFloat();

    // If we're moving downwards and slipping, restrict our ability to resist gravity
    if (mv->m_vecVelocity.z < 0.0f)
    {
        // TODO: Multiply verticalFriction by slip scale based on time spent wallrunning
    }

    Vector wallNormal = m_pPlayer->m_vecWallNormal;

    Vector horzVelocity = mv->m_vecVelocity;
    horzVelocity.z = 0.0f;
    DoWallRunFriction(horzVelocity, horizontalFriction);
    Vector vertVelocity = mv->m_vecVelocity;
    vertVelocity.x = vertVelocity.y = 0.0f;
    DoWallRunFriction(vertVelocity, verticalFriction);
    mv->m_vecVelocity.x = horzVelocity.x;
    mv->m_vecVelocity.y = horzVelocity.y;
    mv->m_vecVelocity.z = vertVelocity.z;

    const float angleDiff = AngleNormalize(GetWallRunYaw());
    const bool bIsClimbing = fabsf(angleDiff) < 45.0f || fabsf(angleDiff) > 135.0f;
    Vector wishVel;

    if (bIsClimbing)
    {
        // If we're looking within 45 degrees into or away from the wall, map forward/back to up/down
        const float lookingIn = Sign(m_vecForward.Dot(-wallNormal));
        Vector wallHorizontal = wallNormal.Cross(Vector(0, 0, -1.0f));
        wishVel = lookingIn * Vector(0.0f, 0.0f, 1.0f) * mv->m_flRawForwardMove + lookingIn * wallHorizontal * mv->m_flRawSideMove;
    }
    else
    {
        // Project m_vecForward onto the wall plane to get our "forward" direction
        const float forwardDotNormal = m_vecForward.Dot(wallNormal);
        Vector wallForward = m_vecForward - (wallNormal * forwardDotNormal);
        wallForward.NormalizeInPlace();
        wishVel = wallForward * mv->m_flRawForwardMove;

        if (mv->m_flRawForwardMove > 0.0f)
        {
            wishVel.z += fabsf(mv->m_flRawForwardMove) * sv_pk_wallrun_upwardautopush.GetFloat();
        }

        // Remap sideways into-wall input as up relative to player, and ignore away input
        const bool bPushingIntoWall = DotProduct(m_vecRight * mv->m_flRawSideMove, wallNormal) < 0.0f;
        if (bPushingIntoWall)
        {
            wishVel += m_vecUp * fabsf(mv->m_flRawSideMove);
        }
    }

    Vector wishDir = wishVel;
    const float wishSpeed = wishDir.NormalizeInPlace();

    const float horzInput = Vector(wishVel.x, wishVel.y, 0.0f).Length();
    const float vertInput = fabsf(wishVel.z);

    const float horzInputFrac = clamp(horzInput / pk_max_move.GetFloat(), 0.0f, 1.0f);
    const float vertInputFrac = clamp(vertInput / pk_max_move.GetFloat(), 0.0f, 1.0f);

    const bool bMovingBackwards = wishDir.Dot(m_vecForward) < -0.6f;
    const float horzMaxSpeed = bMovingBackwards ? sv_pk_wallrun_maxspeed_horizontal_backwards.GetFloat()
                                                : sv_pk_wallrun_maxspeed_horizontal.GetFloat();
    const float horzWishSpeed = horzInputFrac * horzMaxSpeed;
    const float vertWishSpeed = vertInputFrac * sv_pk_wallrun_maxspeed_vertical.GetFloat();

    Vector horzWishDir = wishDir;
    horzWishDir.z = 0.0f;
    if (horzWishDir.NormalizeInPlace() > 0.0f)
    {
        Accelerate(horzWishDir, horzWishSpeed, sv_pk_wallrun_accel_horizontal.GetFloat());
    }

    if (vertInputFrac > 0.0f)
    {
        Vector vertWishDir(0.0f, 0.0f, wishDir.z > 0.0f ? 1.0f : -1.0f);
        Accelerate(vertWishDir, vertWishSpeed, sv_pk_wallrun_accel_vertical.GetFloat());
    }

    // Check if we're pushing away from the wall
    Vector rawWishDir = m_vecForward * mv->m_flForwardMove + m_vecRight * mv->m_flSideMove;
    rawWishDir.z = 0.0f;
    if (rawWishDir.Normalized().Dot(wallNormal) <= 0.707f)
    {
        m_pPlayer->m_flWallrunPushAwayTime = 0.0f;
    }
    else if (m_pPlayer->m_flWallrunPushAwayTime == 0.0f)
    {
        m_pPlayer->m_flWallrunPushAwayTime = gpGlobals->curtime;
    }

    // Clip velocity to stay on the current wall plane
    ClipVelocity(mv->m_vecVelocity, wallNormal, mv->m_vecVelocity, 1.0f);

    // See if we can move directly
    Vector dest = mv->GetAbsOrigin() + mv->m_vecVelocity * gpGlobals->frametime;
    trace_t pm;
    TracePlayerBBox(mv->GetAbsOrigin(), dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);
    if (pm.fraction == 1.0f)
    {
        mv->SetAbsOrigin(pm.endpos);
        StayOnWall();
        return;
    }

    int blocked = WallrunStepMove(wallNormal, dest, pm);
    StayOnWall();

    if (blocked & 1) // floor
        EndWallRun();
}

// Handle end of wallrun - set vars, stop sound
void CMomentumGameMovement::EndWallRun()
{
    if (!m_pPlayer->m_bIsWallrunning)
        return;

    m_pPlayer->m_vecWallNormal.Init();

    //Msg( "End Wallrun\n" );
    //m_pPlayer->StopWallRunSound();
    m_pPlayer->m_bIsWallrunning = false;
    m_pPlayer->m_flWallrunPushAwayTime = 0.0f;
#ifdef GAME_DLL
    m_pPlayer->DeriveMaxSpeed();
#endif
}

// Expose our interface.
static CMomentumGameMovement g_GameMovement;
CMomentumGameMovement *g_pMomentumGameMovement = &g_GameMovement;
IGameMovement *g_pGameMovement = &g_GameMovement;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CMomentumGameMovement, IGameMovement, INTERFACENAME_GAMEMOVEMENT, g_GameMovement);
