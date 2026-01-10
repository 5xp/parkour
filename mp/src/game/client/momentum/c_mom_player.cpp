#include "cbase.h"
#include "c_mom_player.h"

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
    m_flFOVScaleFrac = 0.0f;

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
    m_vecLastWallrunStartPos.Init();

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

    bool lerpIn = m_bIsPowerSliding && m_bDoFOVScale;
    float lerpTime = lerpIn
        ? sv_pk_slide_fov_lerp_in_time.GetFloat()
        : sv_pk_slide_fov_lerp_out_time.GetFloat();
    float lerpTo = lerpIn ? 1.0f : 0.0f;
    m_flFOVScaleFrac = Approach(lerpTo, m_flFOVScaleFrac, 1.0f / lerpTime * gpGlobals->frametime);

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

    ApplySlideViewTilt(eyeAngles);
    ApplyWallrunViewTilt(eyeAngles);
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

float C_MomentumPlayer::GetFOV()
{
    float fovScale = Lerp(m_flFOVScaleFrac, 1.0f, sv_pk_slide_fov_scale.GetFloat());
    return BaseClass::GetFOV() * fovScale;
}