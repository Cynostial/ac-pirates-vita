-- Vita controls for the runner missions (the Mayan temple run): buttons and
-- the left stick instead of swipes, taps, holds and tilting. A virtual finger
-- adds its events to the engine's own touch events (TouchInputManager
-- :GetTouchEvents), so the game's swipe detection, tutorials and hold-to-
-- strafe react exactly as to the touch screen. The stick replaces the
-- accelerometer in the runner's tilt steering.
--   D-pad LEFT/RIGHT, L/R  swipe left/right (turn, change lane, dash, wall run)
--   D-pad UP, CROSS        swipe up (jump, climb, zipline, grab)
--   D-pad DOWN, CIRCLE     swipe down (slide, dive)
--   SQUARE                 tap (pick up, skip a cinematic)
--   Left stick             steer (tilt mode) or hold to move aside (swipe mode)
--   CROSS on a popup       its OK button; CIRCLE stays the back button
-- Own function scope: the support files share one chunk and its 200 locals.
do local function runner_module()
local band=bit.band
local UP,RIGHT,DOWN,LEFT,L,R,CIRCLE,CROSS,SQUARE=16,32,64,128,256,512,8192,16384,32768
local FINGER=7801     -- gestures
local HOLD=7802       -- held to one side (swipe-only mode, hold lessons)

-- Touch positions as GetTouchEvents reports them: [0,1], y downwards.
local function swipe(dx,dy)
    local x,y=0.5+dx*0.25,0.5+dy*0.25
    return {{"Pressed",0.5,0.5},{"Moved",x,y},{"Released",x,y}}
end
local TAP={{"Pressed",0.5,0.5},{"Released",0.5,0.5}}
local bindings={
    {LEFT,swipe(-1,0)},{L,swipe(-1,0)},{RIGHT,swipe(1,0)},{R,swipe(1,0)},
    {UP,swipe(0,-1)},{CROSS,swipe(0,-1)},{DOWN,swipe(0,1)},{CIRCLE,swipe(0,1)},{SQUARE,TAP},
}

local state={previous=0,steps={},events={},hold=false,active=false}
local function stick(value)
    local v=((value or 128)-128)/127
    if math.abs(v)<0.2 then return 0 end
    return math.max(-1,math.min(1,(math.abs(v)-0.2)/0.8))*(v<0 and -1 or 1)
end
local function event(id,phase,x,y)
    state.events[#state.events+1]={TouchId=id,State=phase,TimeStamp=AbsoluteTime or 0,
        Position=Vector2(x,y),Pressure=0,Elevation=0,Angle=0}
end
local function release_all()
    if state.hold then event(HOLD,"Released",state.hold,0.5);state.hold=false end
    local step=state.steps[1]
    if step and step[1]~="Pressed" then event(FINGER,"Released",step[2],step[3]) end
    state.steps={}
end

local function runner()
    local game=GetSubProcess and GetSubProcess("TagRunnerGame")
    if game and not game.IsDestroyed then return game end
end
local function back_manager() return MainProcess:GetEntity("TagDeviceBackButtonManager") end
local function paused()
    local pause=MainProcess:GetEntity("TagPauseGUILayer")
    return pause and (pause.IsScrolledDown or pause.ScrollingDown)
end
local function popup_shown()
    local manager=back_manager()
    return manager and manager:HasDisplayedPopup()
end
-- CIRCLE slides while running; with a popup or the pause menu it stays back.
function ACPVitaRunnerOwnsCircle()
    return runner()~=nil and not paused() and not popup_shown()
end

local function usable(button)
    return type(button)=="table" and not button.IsDestroyed and button.Visible~=false and button.IsEnabled~=false
end
-- OK popups (save me, first-run hints): CROSS presses OK. The results and
-- "desynchronized" popups have their own button navigation (vita_controls).
local function popups(b)
    if band(b,CROSS)==0 or band(state.previous,CROSS)~=0 then return end
    local manager=back_manager()
    local top=manager and manager._PopupStack and manager._PopupStack[1]
    local button=top and usable(top.OkButton) and top.OkButton
    if button then
        if ACPVitaConsume then ACPVitaConsume(CROSS) end
        button:OnButtonReleased()
    end
end

local function install_finger()
    local manager=MainProcess:GetEntity("TagTouchInputManager")
    if type(manager)~="table" or rawget(manager,"_VitaFinger") then return end
    local native=manager.GetTouchEvents
    if type(native)~="function" then return end
    rawset(manager,"GetTouchEvents",function(self,pad,...)
        local events=native(self,pad,...)
        if (pad or 0)~=0 or #state.events==0 then return events end
        local merged={}
        for _,e in pairs(events or {}) do merged[#merged+1]=e end
        for _,e in ipairs(state.events) do merged[#merged+1]=e end
        return merged
    end)
    rawset(manager,"_VitaFinger",true)
end

function ACPVitaRunnerFrame()
    state.events={}
    local game=runner()
    local b,lx=ACPVitaPad()
    if not game then
        if state.active then release_all();state.active=false end
        state.previous=b;return
    end
    state.active=true
    install_finger()
    if paused() or popup_shown() then
        release_all()
        if not paused() then popups(b) end
        state.previous=b;return
    end
    for _,binding in ipairs(bindings) do
        local mask=binding[1]
        if band(b,mask)~=0 and band(state.previous,mask)==0 and #state.steps==0 then
            for _,step in ipairs(binding[2]) do state.steps[#state.steps+1]=step end
        end
    end
    if #state.steps>0 then
        -- The runner follows one touch at a time: lift a held finger first.
        if state.hold then event(HOLD,"Released",state.hold,0.5);state.hold=false
        else
            local step=table.remove(state.steps,1)
            event(FINGER,step[1],step[2],step[3])
        end
    else
        -- Swipe-only mode (and the hold lessons): holding the stick to a side
        -- holds a finger on that side of the screen. Tilt mode steers instead.
        -- RunnerGame.Character reads GameScene.Character; GameScene is false
        -- until the runner's OnCreate.
        local scene=game.GameScene
        local character=scene and scene.Character
        local side=0
        if not (character and character.GyroEnabled) then
            local x=stick(lx)
            side=x>0.5 and 1 or x<-0.5 and -1 or 0
        end
        -- 0.95 = screen-space 0.9: inside the hold lessons' zones, near full
        -- wall-slide strafe speed (it scales with the finger's x).
        local want=side~=0 and 0.5+side*0.45 or false
        if state.hold and state.hold~=want then event(HOLD,"Released",state.hold,0.5);state.hold=false
        elseif want and not state.hold then event(HOLD,"Pressed",want,0.5);state.hold=want end
    end
    state.previous=b
end

-- Tilt steering and its lesson read the stick instead of gravity.
local function adapt_gyro(def)
    if type(def._ProcessGyroData)~="function" then return end
    def._ProcessGyroData=function(o)
        local _,lx=ACPVitaPad()
        o.GyroWheelValue=stick(lx)
    end
end
-- The HUD button that offers swipe or tilt controls: the stick does both.
-- The HUD shows it for a few seconds of each run; the first time, after the
-- runner tutorial, a lesson pauses the run (and the pause menu) until it is
-- tapped. None of it is created; the lesson completes at once, keeping the
-- game's default tilt + swipe controls.
local function adapt_switch_button(def)
    local create=def.OnCreateAfterSetters
    def.OnCreateAfterSetters=function(o,...)
        if create then create(o,...) end
        o:SetVisible(false);o.IsEnabled=false
    end
end
local function adapt_hud(def)
    local layer=def.Layer
    if type(layer)=="table" and type(layer.CanShowSwitchInputButton)=="function" then
        layer.CanShowSwitchInputButton=function() return false end
    end
end
local function adapt_switch_lesson(def)
    if type(def.EnterTapButtonTutorial)~="function" then return end
    def.EnterTapButtonTutorial=function(o)
        local profile=MainProcess:GetEntity("TagPlayerProfile")
        profile.RunnerGyroControlSelected=true
        profile:SaveRunnerOptions()
        local game=GetSubProcess("TagRunnerGame")
        if game and game.Character then game.Character:ResetGyro() end
        o.State="Completed"
    end
end

-- The tilt lesson's tilting-device picture; its prompt names the stick.
local function adapt_gyro_lesson(def)
    local triggered=def.OnTriggered
    if type(triggered)~="function" then return end
    def.OnTriggered=function(o,...)
        local a,b,c=triggered(o,...)
        if o._TiltArrows then o._TiltArrows:SetVisible(false) end
        return a,b,c
    end
end

-- Sideways swipe lessons only say "swipe to turn": the touch hand showed
-- which way. Their prompts name the lesson's direction instead.
local SIDEWAYS={TUTO_RUNTU_00003=true,TUTO_RUNTU_00004=true,TUTO_RUNTU_00007=true,
    TUTO_RUNTU_00011=true,TUTO_RUNTU_00014=true}
local function adapt_swipe_lesson(def)
    local triggered=def.OnTriggered
    if type(triggered)~="function" then return end
    def.OnTriggered=function(o,...)
        local objective=o.Objective
        if SIDEWAYS[objective] and (o.Direction=="Left" or o.Direction=="Right") then
            o.Objective=objective.."_"..o.Direction
        end
        local a,b,c=triggered(o,...)
        o.Objective=objective
        return a,b,c
    end
end

function ACPVitaRunner(def)
    local name=def.ClassName
    if name=="RunnerGyro" then adapt_gyro(def)
    elseif name=="RunnerSwitchInputButton" then adapt_switch_button(def)
    elseif name=="HUD" then adapt_hud(def)
    elseif name=="RunnerSwitchInput_tutorial" then adapt_switch_lesson(def)
    elseif name=="GyroTutorial" then adapt_gyro_lesson(def)
    elseif name=="BaseSwipeTutorial" then adapt_swipe_lesson(def) end
end
end runner_module() end
