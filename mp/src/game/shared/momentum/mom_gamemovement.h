#pragma once

#include "gamemovement.h"

#ifdef CLIENT_DLL
#define CMomentumPlayer C_MomentumPlayer
#endif

class CMomentumPlayer;

class CMomentumGameMovement : public CGameMovement
{
    typedef CGameMovement BaseClass;

public:
    CMomentumGameMovement();

    void SetGroundEntity(const trace_t *pm) override;

    void AirAccelerate( Vector& wishdir, float wishspeed, float accel ) override;
    bool CanAccelerate() override;
    bool CheckJumpButton() override;
    void PlayerMove() override;
    void AirMove() override;
    void WalkMove() override;

    int ClipVelocity(Vector in, Vector &normal, Vector &out, float overbounce) override;

    // Ladder
    float LadderDistance() const override;
    bool GameHasLadders() const override;
    unsigned int LadderMask() const override { return MASK_PLAYERSOLID & (~CONTENTS_PLAYERCLIP); }
    float LadderLateralMultiplier() const override;
    float ClimbSpeed() const override;
    bool LadderMove() override;

    // Override for fixing punchangle
    void DecayPunchAngle() override;

    int TryPlayerMove(Vector *pFirstDest = nullptr, trace_t *pFirstTrace = nullptr) override;
    void FullWalkMove() override;
    void StepMove(Vector &vecDestination, trace_t &trace) override;
    void CategorizePosition() override;

    void ProcessMovement(CBasePlayer *pBasePlayer, CMoveData *pMove) override;

    void Friction() override;

    float GetWaterWaistOffset() override;
    float GetWaterJumpUpZVelocity() override;
    float GetWaterJumpForward() override;
    void CalculateWaterWishVelocityZ(Vector &wishVel, const Vector &forward) override;

    // Duck
    void Duck() override;
    void DoDuck(int iButtonsPressed);
    void FinishDuck() override;
    bool CanUnduck() override;
    void DoUnduck(int iButtonsReleased);
    void FinishUnDuck() override;
    void HandleDuckingSpeedCrop() override;
    float GetTimeToDuck() override;
    float GetDuckTimer() override;

    void CheckParameters() override;
    void ReduceTimers() override;

    void StartGravity() override;
    float GetPlayerGravity() override;
    void FinishGravity() override;

    // Momentum-specific
    virtual void StuckGround();
    virtual void LimitStartZoneSpeed();

    // Validate tracerays
    bool IsValidMovementTrace(trace_t &tr);

    // Limited bunnyhopping in rocket jumping
    void PreventBunnyHopping();

    void CheckWaterJump() override;
    void WaterJump() override;
    void CheckVelocity() override;
    bool ShouldApplyGroundFriction() override;

    // ========== Parkour-only methods
    void DoRegularJump();
    void DoAirJump();
    void DoWallJump();

    // Special friction for powersliding
    void            PowerSlideFriction();

    // Check if player should powerslide
    // Called when we duck or land on the ground while ducked
    virtual void    CheckPowerSlide();

    // End powerslide - reset the vars, stop the sound
    virtual void    EndPowerSlide();

    // Get the yaw angle between the player and the wall normal
    virtual float   GetWallRunYaw();

    // Check if player should start wallrunning,
    // i.e. hit a suitable wall while airborn.
    virtual void    OnWallTouch(Vector &vecWallNormal, trace_t &pm, float preWallrunSpeed);

    // Handle wallrun movement
    virtual void    DoWallRunFriction(Vector &velocity, const float friction);
    virtual void    WallrunMove();

    // Handle end of wallrun - set vars, stop sound
    virtual void    EndWallRun();

    void RedirectVelocity(const Vector &wishdir, Vector velocity, const float maxDelta, const float strengthFrac, const float targetSpeed);
    void PerformLurchChecks();

private:
    CMomentumPlayer *m_pPlayer;

    void ParkourAccelerate(Vector &velocity, const Vector &wishDir, const float wishSpeed, const float acceleration);
    void ParkourDecelerate(Vector &velocity, const Vector &wishDir, const float wishSpeed, const float deceleration);

    bool IsJumpBufferActive();
    bool ShouldDoBufferedJump();
    void StartJumpBuffer();
    void ApplySlideGravity();
    void ApplySlideStepVelocityReduction();
    bool PredictGroundTouch(float predictTime, trace_t *outTrace = nullptr);
    bool PredictWallTouch(float predictTime, Vector *outNormal = nullptr);

    void FallAwayFromWall(const bool fromCrouch);
    void CheckShouldWallrunEnd();
    bool CanFeetReachWall(const Vector &position, const Vector &wallNormal);
    bool IsNearTopWall(const Vector &position, const Vector &wallNormal);
    bool IsStep(const Vector &position, const Vector &wallNormal);
    bool IsWallEligibleForWallrun(const Vector &wallPosition, const Vector &wallNormal, bool &outWeak);
    float GetWallrunGravityScale();
    void PredictWallrun();
    void UpdateWallNormal();
    void StayOnWall();
    int WallrunStepMove(const Vector &stepDir, Vector &vecDestination, trace_t &trace);
    void OnLand(bool fromWallrun);

    bool m_bCheckForGrabbableLadder;
};

extern CMomentumGameMovement *g_pMomentumGameMovement;
