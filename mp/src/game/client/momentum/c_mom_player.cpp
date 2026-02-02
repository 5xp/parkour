#include "cbase.h"
#include "c_mom_player.h"
#include "view.h"

#include <steam/isteamuser.h>

#include "c_mom_online_ghost.h"
#include "c_mom_replay_entity.h"

#include "tier0/memdbgon.h"
#include "movevars_shared.h"

#include "mom_system_gamemode.h"
// band-aid solution for a crash when including mom_system_gamemode.h
#ifdef CMomentumPlayer
#undef CMomentumPlayer
#endif

IMPLEMENT_CLIENTCLASS_DT(C_MomentumPlayer, DT_MOM_Player, CMomentumPlayer)
RecvPropBool(RECVINFO(m_bIsSprinting)),
RecvPropBool(RECVINFO(m_bIsWalking)),
RecvPropBool(RECVINFO(m_bIsPowerSliding)),
RecvPropBool(RECVINFO(m_bDoFOVScale)),
RecvPropBool(RECVINFO(m_bIsWallrunning)),
RecvPropFloat(RECVINFO(m_flWallrunStartTime)),
RecvPropVector(RECVINFO(m_vecWallNormal)),
RecvPropFloat(RECVINFO(m_flWallrunRelativeYaw)),
RecvPropFloat(RECVINFO(m_flWallrunRelativeCorrectSpeed)),
RecvPropBool(RECVINFO(m_bHasPracticeMode)),
RecvPropBool(RECVINFO(m_bPreventPlayerBhop)),
RecvPropInt(RECVINFO(m_iJumpTick)),
RecvPropInt(RECVINFO(m_iLandTick)),
RecvPropBool(RECVINFO(m_bResumeZoom)),
RecvPropInt(RECVINFO(m_iShotsFired), SPROP_UNSIGNED),
RecvPropInt(RECVINFO(m_iDirection), SPROP_UNSIGNED),
RecvPropInt(RECVINFO(m_iLastZoomFOV), SPROP_UNSIGNED),
RecvPropInt(RECVINFO(m_afButtonDisabled)),
RecvPropEHandle(RECVINFO(m_CurrentSlideTrigger)),
RecvPropBool(RECVINFO(m_bAutoBhop)),
RecvPropFloat(RECVINFO(m_fDuckTimer)),
RecvPropBool(RECVINFO(m_bSurfing)),
RecvPropInt(RECVINFO(m_nButtonsToggled)),
RecvPropVector(RECVINFO(m_vecRampBoardVel)),
RecvPropVector(RECVINFO(m_vecRampLeaveVel)),
RecvPropArray3(RECVINFO_ARRAY(m_iZoneCount), RecvPropInt(RECVINFO(m_iZoneCount[0]), SPROP_UNSIGNED)),
RecvPropArray3(RECVINFO_ARRAY(m_iLinearTracks), RecvPropInt(RECVINFO(m_iLinearTracks[0]), SPROP_UNSIGNED)),
RecvPropDataTable(RECVINFO_DT(m_Data), SPROP_PROXY_ALWAYS_YES | SPROP_CHANGES_OFTEN, &REFERENCE_RECV_TABLE(DT_MomRunEntityData)),
RecvPropDataTable(RECVINFO_DT(m_RunStats), SPROP_PROXY_ALWAYS_YES | SPROP_CHANGES_OFTEN, &REFERENCE_RECV_TABLE(DT_MomRunStats)),
END_RECV_TABLE();

BEGIN_PREDICTION_DATA(C_MomentumPlayer)
DEFINE_PRED_FIELD(m_iShotsFired, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
DEFINE_PRED_FIELD(m_iDirection, FIELD_INTEGER, FTYPEDESC_INSENDTABLE),
DEFINE_PRED_FIELD(m_fDuckTimer, FIELD_FLOAT, FTYPEDESC_INSENDTABLE),
DEFINE_PRED_FIELD(m_bSurfing, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE),
DEFINE_PRED_FIELD(m_vecRampBoardVel, FIELD_VECTOR, FTYPEDESC_INSENDTABLE),
DEFINE_PRED_FIELD(m_vecRampLeaveVel, FIELD_VECTOR, FTYPEDESC_INSENDTABLE),
END_PREDICTION_DATA();

static C_MomentumPlayer *s_pLocalPlayer = nullptr;

C_MomentumPlayer::C_MomentumPlayer(): m_pSpecTarget(nullptr)
{
    ConVarRef scissor("r_flashlightscissor");
    scissor.SetValue("0");
    m_bSurfing = false;
    m_vecRampBoardVel.Init();
    m_vecRampLeaveVel.Init();
    m_bHasPracticeMode = false;
    m_afButtonDisabled = 0;
    m_flStartSpeed = 0.0f;
    m_flEndSpeed = 0.0f;
    m_flStamina = 0.0f;
    m_flGrabbableLadderTime = 0.0f;
    m_bJustTeleported = false;

    m_iLandTick = 0;
    m_iJumpTick = 0;
    m_bAutoBhop = true;
    m_CurrentSlideTrigger = nullptr;
    m_RunStats.Init();
    m_fDuckTimer = 0.0f;

    m_bIsWalking = false;
    m_bIsSprinting = false;
    m_bIsPowerSliding = false;
    m_flLastSlideBoost = 0.0f;
    m_bDoFOVScale = false;
    m_bUsedSlideBoost = false;
    m_flFOVScaleFrac = 0.0f;
    m_flSprintTiltFrac = 0.0f;
    m_flSprintTiltVelocity = 0.0f;

    m_iAirJumps = 0;
    m_flJumpBufferTime = 0.0f;

    m_bIsWallrunning = false;
    m_flWallrunStartTime = 0.0f;
    m_flWallrunFallAwayTime = 0.0f;
    m_flWallrunPushAwayTime = 0.0f;
    m_bWallrunHasBoost = false;
    m_bWallrunWeak = false;
    m_bHasLastWallrunStartPos = false;
    m_vecWallrunTilt.Init();
    m_vecWallNormal.Init();
    m_vecTargetWallNormal.Init();
    m_vecLastWallNormal.Init();
    m_nWallrunSurfaceProp = 0;
    m_vecLastWallrunStartPos.Init();
    m_flWallrunRelativeYaw = 0.0f;
    m_flWallrunRelativeCorrectSpeed = 0.0f;
    m_flWallrunRelativeYawOffset = 0.0f;
    m_flWallrunRelativeYawNetPrev = 0.0f;
    m_bWallrunRelativeYawInit = false;

    m_nButtonsToggled = 0;
}

C_MomentumPlayer::~C_MomentumPlayer()
{
    if (this == s_pLocalPlayer)
        s_pLocalPlayer = nullptr;
}

C_MomentumPlayer *C_MomentumPlayer::GetLocalMomPlayer()
{
    return s_pLocalPlayer;
}

CMomRunEntity *C_MomentumPlayer::GetCurrentUIEntity()
{
    if (!m_hObserverTarget.Get())
        m_pSpecTarget = nullptr;
    else if (!m_pSpecTarget)
        m_pSpecTarget = dynamic_cast<CMomRunEntity*>(m_hObserverTarget.Get());

    return m_pSpecTarget ? m_pSpecTarget : this;
}

CMomRunEntityData *C_MomentumPlayer::GetCurrentUIEntData()
{
    return GetCurrentUIEntity()->GetRunEntData();
}

CMomRunStats *C_MomentumPlayer::GetCurrentUIEntStats()
{
    return GetCurrentUIEntity()->GetRunStats();
}

//-----------------------------------------------------------------------------
// Purpose: Input handling
//-----------------------------------------------------------------------------
bool C_MomentumPlayer::CreateMove(float flInputSampleTime, CUserCmd *pCmd)
{
    // Bleh... we will wind up needing to access bones for attachments in here.
    C_BaseAnimating::AutoAllowBoneAccess boneaccess(true, true);

    return BaseClass::CreateMove(flInputSampleTime, pCmd);
}

void C_MomentumPlayer::OnDataChanged(DataUpdateType_t type)
{
    // clear the sprint toggle if we just stopped not sprinting
    /*if (m_bWasSprinting && !m_bIsSprinting)
    {
        IN_ClearSpeedToggle();
    }

    m_bWasSprinting = m_bIsSprinting;*/


    BaseClass::OnDataChanged(type);

    UpdateVisibility();
}


void C_MomentumPlayer::PostDataUpdate(DataUpdateType_t updateType)
{
    if (updateType == DATA_UPDATE_CREATED)
    {
        if (engine->GetLocalPlayer() == m_index)
        {
            Assert(s_pLocalPlayer == nullptr);
            s_pLocalPlayer = this;
        }
    }

    // C_BaseEntity assumes we're networking the entity's angles, so pretend that it
    // networked the same value we already have.
    SetNetworkAngles(GetLocalAngles());

    BaseClass::PostDataUpdate(updateType);
}

int C_MomentumPlayer::GetSpecEntIndex() const
{
    return m_hObserverTarget.GetEntryIndex();
}

// Overridden for Ghost entity
Vector C_MomentumPlayer::GetChaseCamViewOffset(C_BaseEntity* target)
{
    C_MomentumGhostBaseEntity *pGhost = dynamic_cast<C_MomentumGhostBaseEntity*>(target);
    if (pGhost)
    {
        if (pGhost->GetFlags() & FL_DUCKING)
            return VEC_DUCK_VIEW_SCALED(pGhost);

        return VEC_VIEW_SCALED(pGhost);
    }

    // Resort to base class for player code
    return BaseClass::GetChaseCamViewOffset(target);
}

void C_MomentumPlayer::CalcInEyeCamView(Vector& eyeOrigin, QAngle& eyeAngles, float& fov)
{
    C_BaseEntity *target = GetObserverTarget();

    if (!target)
    {
        VectorCopy(EyePosition(), eyeOrigin);
        VectorCopy(EyeAngles(), eyeAngles);
        return;
    }

    if (!target->IsAlive())
    {
        CalcChaseCamView(eyeOrigin, eyeAngles, fov);
        return;
    }

    fov = GetFOV();

    m_flObserverChaseDistance = 0.0f;

    eyeAngles = target->EyeAngles();
    eyeOrigin = target->GetAbsOrigin();

    const auto pReplayGhost = dynamic_cast<C_MomentumReplayGhostEntity*>(target);
    if (pReplayGhost && pReplayGhost->IsReplayGhost())
    {
        VectorAdd(eyeAngles, pReplayGhost->m_angViewPunch, eyeAngles);
        eyeAngles[ROLL] += pReplayGhost->m_flViewRoll;
    }
    else
    {
        VectorAdd(eyeAngles, GetPunchAngle(), eyeAngles);
    }

    if (engine->IsHLTV())
    {
        C_BaseAnimating *pTargetAnimating = target->GetBaseAnimating();
        if (target->GetFlags() & FL_DUCKING)
        {
            eyeOrigin += pTargetAnimating ? VEC_DUCK_VIEW_SCALED(pTargetAnimating) : VEC_DUCK_VIEW;
        }
        else
        {
            eyeOrigin += pTargetAnimating ? VEC_VIEW_SCALED(pTargetAnimating) : VEC_VIEW;
        }
    }
    else
    {
        Vector offset = GetViewOffset();
#ifdef HL2MP
        offset = target->GetViewOffset();
#endif
        eyeOrigin += offset;
    }

    engine->SetViewAngles(eyeAngles);
}

void C_MomentumPlayer::OnObserverTargetUpdated()
{
    m_pSpecTarget = nullptr; // Hard-set to null upon observer change

    BaseClass::OnObserverTargetUpdated();
}

float C_MomentumPlayer::GetCurrentRunTime()
{
    int iTotalTicks = 0;
    if (m_Data.m_iTimerState != TIMER_STATE_NOT_RUNNING)
    {
        iTotalTicks = gpGlobals->tickcount - m_Data.m_iStartTick;
    }
    else if (m_Data.m_bMapFinished)
    {
        iTotalTicks = m_Data.m_iRunTime;
    }

    return float(iTotalTicks) * m_Data.m_flTickRate;
}

uint64 C_MomentumPlayer::GetSteamID()
{
    return SteamUser() ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
}

void C_MomentumPlayer::CalcViewRoll(QAngle &eyeAngles)
{
    BaseClass::CalcViewRoll(eyeAngles);

    if (!g_pGameModeSystem->GameModeIs(GAMEMODE_PARKOUR))
        return;

    bool lerpIn = m_bIsPowerSliding && m_bDoFOVScale;
    float lerpTime = lerpIn ? sv_pk_slide_fov_lerp_in_time.GetFloat() : sv_pk_slide_fov_lerp_out_time.GetFloat();
    float lerpTo = lerpIn ? 1.0f : 0.0f;
    m_flFOVScaleFrac = Approach(lerpTo, m_flFOVScaleFrac, 1.0f / lerpTime * gpGlobals->frametime);

    ApplyWallrunViewCorrection(eyeAngles);
    ApplySlideViewTilt(eyeAngles);
    ApplyWallrunViewTilt(eyeAngles);
    ApplySprintViewTilt(eyeAngles);
}

void C_MomentumPlayer::ApplySlideViewTilt(QAngle &eyeAngles)
{
    const float totalRoll = CalcSlideViewRoll(eyeAngles, GetAbsVelocity(), m_bIsPowerSliding, m_Local.m_vecSlideTilt);
    eyeAngles[ROLL] += totalRoll;
}

void C_MomentumPlayer::ApplyWallrunViewTilt(QAngle &eyeAngles)
{
    const float wallrunRoll = CalcWallrunViewRoll(eyeAngles, m_bIsWallrunning, m_flWallrunStartTime, m_vecWallNormal,
                                                  m_vecWallrunTilt);
    eyeAngles[ROLL] += wallrunRoll;
}

void C_MomentumPlayer::ApplySprintViewTilt(QAngle &eyeAngles)
{
    const float dt = gpGlobals->frametime;
    if (dt <= 0.0f)
        return;

    const bool bDoTilt = m_bIsSprinting && GetGroundEntity() != nullptr;
    
    const float yawDelta = bDoTilt ? AngleDiff(MainViewAngles()[YAW], PrevMainViewAngles()[YAW]) : 0.0f;
    const float turnRate = yawDelta / dt;
    const float targetFrac = clamp(turnRate / sv_pk_sprinttilt_turn_range.GetFloat(), -1.0f, 1.0f);

    const float maxVel = sv_pk_sprinttilt_max_vel.GetFloat();
    const float accel = sv_pk_sprinttilt_accel.GetFloat();

    const float dist = targetFrac - m_flSprintTiltFrac;
    const float stopSpeed = sqrt(2.0f * accel * fabs(dist));

    const float targetVel = min(maxVel, stopSpeed);
    const float desiredVel = (dist > 0) ? targetVel : -targetVel;

    m_flSprintTiltVelocity = Approach(desiredVel, m_flSprintTiltVelocity, accel * dt);

    const float nextFrac = m_flSprintTiltFrac + (m_flSprintTiltVelocity * dt);

    const bool crossedTarget = (m_flSprintTiltFrac < targetFrac && nextFrac >= targetFrac) ||
                         (m_flSprintTiltFrac > targetFrac && nextFrac <= targetFrac);

    if (crossedTarget)
    {
        m_flSprintTiltFrac = targetFrac;
        m_flSprintTiltVelocity = 0.0f;
    }
    else
    {
        m_flSprintTiltFrac = nextFrac;
    }

    m_flSprintTiltFrac = clamp(m_flSprintTiltFrac, -1.0f, 1.0f);
    eyeAngles[ROLL] += -m_flSprintTiltFrac * sv_pk_sprinttilt_max_roll.GetFloat();
}

void C_MomentumPlayer::ApplyWallrunViewCorrection(QAngle &eyeAngles)
{
    if (!m_bIsWallrunning)
    {
        m_flWallrunRelativeYawOffset = 0.0f;
        m_flWallrunRelativeYawNetPrev = AngleNormalize(m_flWallrunRelativeYaw);
        m_bWallrunRelativeYawInit = false;
        return;
    }

    QAngle horzEyeAngles, vertEyeAngles;
    horzEyeAngles = vertEyeAngles = eyeAngles;
    horzEyeAngles[PITCH] = horzEyeAngles[ROLL] = 0.0f;
    vertEyeAngles[YAW] = vertEyeAngles[ROLL] = 0.0f;

    Vector forward;
    AngleVectors(horzEyeAngles, &forward, nullptr, nullptr);

    Vector velocity = GetAbsVelocity();
    if (!velocity.IsZero() && forward.Dot(velocity) >= 0.0f)
    {
        const float playerSpeed = velocity.Length2D();
        const float speedFrac = RemapValClamped(playerSpeed, 0.0f, 100.0f, 0.0f, 1.0f);

        QAngle wallAngles;
        VectorAngles(m_vecWallNormal, wallAngles);
        wallAngles[PITCH] = 0.0f;
        wallAngles[ROLL] = 0.0f;
        CorrectWallrunYaw(m_vecWallNormal, horzEyeAngles, playerSpeed);
        CorrectWallrunPitch(wallAngles, vertEyeAngles, speedFrac);
    }
    
    const float yawCorrectionDelta = AngleNormalize(horzEyeAngles[YAW] - eyeAngles[YAW]);
    PreserveWallrunYaw(horzEyeAngles, yawCorrectionDelta);
    eyeAngles[YAW] = horzEyeAngles[YAW];
    eyeAngles[PITCH] = vertEyeAngles[PITCH];

    engine->SetViewAngles(eyeAngles);
}

// Preserve the player's yaw relative to the wall when the wall curves
void C_MomentumPlayer::PreserveWallrunYaw(QAngle &horzEyeAngles, float yawCorrectionDelta)
{
    const float netYaw = AngleNormalize(m_flWallrunRelativeYaw);
    if (!m_bWallrunRelativeYawInit)
    {
        m_flWallrunRelativeYawNetPrev = netYaw;
        m_bWallrunRelativeYawInit = true;
    }

    const float deltaYaw = AngleDiff(netYaw, m_flWallrunRelativeYawNetPrev);
    m_flWallrunRelativeYawNetPrev = netYaw;
    m_flWallrunRelativeYawOffset = AngleNormalize(m_flWallrunRelativeYawOffset + deltaYaw);
    if (yawCorrectionDelta * m_flWallrunRelativeYawOffset > 0.0f)
    {
        m_flWallrunRelativeYawOffset = AngleNormalize(m_flWallrunRelativeYawOffset - yawCorrectionDelta);
    }

    const float yawOffset = m_flWallrunRelativeYawOffset;

    float approachSpeed = fabsf(yawOffset) * 2.0f * gpGlobals->frametime;
    const float minSpeed = m_flWallrunRelativeCorrectSpeed * gpGlobals->frametime;
    if (approachSpeed < minSpeed)
    {
        approachSpeed = minSpeed;
    }

    const float newYawOffset = AngleNormalize(ApproachAngle(0.0f, yawOffset, approachSpeed));
    const float appliedYaw = AngleNormalize(yawOffset - newYawOffset);
    m_flWallrunRelativeYawOffset = newYawOffset;
    horzEyeAngles[YAW] = AngleNormalize(horzEyeAngles[YAW] + appliedYaw);
}

// Turn the player's view away from the wall when moving forward
void C_MomentumPlayer::CorrectWallrunYaw(const Vector &wallNormal, QAngle &horzEyeAngles, float playerSpeed)
{
    const float safetyAngle = sv_pk_wallrun_viewcorrect_yaw_offset.GetFloat();

    Vector wallForward = wallNormal.Cross(Vector(0.0f, 0.0f, 1.0f));

    const float currentYaw = AngleNormalize(horzEyeAngles[YAW]);
    QAngle wallForwardAngles;
    VectorAngles(wallForward, wallForwardAngles);
    float parallelYaw = wallForwardAngles[YAW];
    const float parallelYawAlt = AngleNormalize(parallelYaw + 180.0f);
    if (fabsf(AngleDiff(parallelYawAlt, currentYaw)) < fabsf(AngleDiff(parallelYaw, currentYaw)))
        parallelYaw = parallelYawAlt;

    Vector right;
    AngleVectors(horzEyeAngles, nullptr, &right, nullptr);
    const bool wallOnRight = wallNormal.Dot(right) < 0.0f;

    const float targetYaw = AngleNormalize(parallelYaw + (wallOnRight ? safetyAngle : -safetyAngle));
    const float diff = AngleNormalize(targetYaw - currentYaw);

    if ((wallOnRight && diff < 0.0f) || (!wallOnRight && diff > 0.0f))
        return;

    const float absDiff = fabsf(diff);
    const float speedClamped = min(playerSpeed, 90.0f);
    const float strengthFactor = sinf(DEG2RAD(speedClamped));
    if (strengthFactor <= 0.0f)
        return;

    const float maxRate = sv_pk_wallrun_viewcorrect_yaw_speed.GetFloat() * strengthFactor;
    const float decayK = sv_pk_wallrun_viewcorrect_yaw_decay.GetFloat() * strengthFactor;

    const float desiredRate = absDiff * decayK;
    const float actualRate = min(maxRate, desiredRate);

    float step = actualRate * gpGlobals->frametime;

    horzEyeAngles[YAW] = AngleNormalize(ApproachAngle(targetYaw, currentYaw, step));
}

// Keep the player's pitch level
void C_MomentumPlayer::CorrectWallrunPitch(const QAngle &wallAngles, QAngle &vertEyeAngles, float speedFrac)
{
    const float correctedAngleOffsetMin = sv_pk_wallrun_viewcorrect_pitch_min.GetFloat();
    const float correctedAngleOffsetMax = sv_pk_wallrun_viewcorrect_pitch_max.GetFloat();
    if (correctedAngleOffsetMin <= 0.0f || correctedAngleOffsetMax < correctedAngleOffsetMin)
        return;

    const float angleDiff = AngleDiff(wallAngles[PITCH], vertEyeAngles[PITCH]);
    const float absDiff = fabsf(angleDiff);
    if (absDiff < correctedAngleOffsetMin || absDiff > correctedAngleOffsetMax)
        return;

    const float correctedAngle1 = AngleNormalize(wallAngles[PITCH] + correctedAngleOffsetMin);
    const float correctedAngle2 = AngleNormalize(wallAngles[PITCH] - correctedAngleOffsetMin);
    const float diff1 = fabsf(AngleDiff(correctedAngle1, vertEyeAngles[PITCH]));
    const float diff2 = fabsf(AngleDiff(correctedAngle2, vertEyeAngles[PITCH]));
    const float targetPitch = diff1 < diff2 ? correctedAngle1 : correctedAngle2;

    const float maxStep =
        sv_pk_wallrun_viewcorrect_pitch_speed.GetFloat() * speedFrac * gpGlobals->frametime;
    if (maxStep <= 0.0f)
        return;

    vertEyeAngles[PITCH] = AngleNormalize(ApproachAngle(targetPitch, vertEyeAngles[PITCH], maxStep));
}

float C_MomentumPlayer::GetFOV()
{
    float fovScale = Lerp(m_flFOVScaleFrac, 1.0f, sv_pk_slide_fov_scale.GetFloat());
    return BaseClass::GetFOV() * fovScale;
}