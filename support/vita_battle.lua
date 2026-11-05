-- Vita battle controls: weapons, dodge and the spider shot.
-- The original controllers keep every rule (reloads, ammo, upgrades, weak
-- points, slow motion, tutorials). Face buttons press the original GUI
-- buttons. The left stick drives one virtual finger that enters through the
-- game's own touch dispatcher (GameController), so every battle controller
-- receives it exactly as it would receive a finger on the screen.
-- Own function scope: the support files share one chunk and its 200 locals.
do local function battle_module()
local band=bit.band
local L,R,TRIANGLE,CIRCLE,CROSS,SQUARE=256,512,4096,8192,16384,32768
-- CROSS fires; the face buttons around it and the shoulders ready weapons.
local FIRE=CROSS
local weapon_masks={RoundShot=SQUARE,SwivelGun=TRIANGLE,HeavyGun=CIRCLE,Mortar=R,LineTiming=L}
-- Battle prompts use the brass-medallion style (ACPVitaBattle* textures).
local glyph_names={[SQUARE]="BattleSquare",[TRIANGLE]="BattleTriangle",[CIRCLE]="BattleCircle",[CROSS]="BattleCross",[L]="BattleL",[R]="BattleR"}
-- Where each weapon's aim starts when nothing is on screen (its own default).
local aim_defaults={RoundShot={0,0},SwivelGun={0,0.35},HeavyGun={0,0},Mortar={0,0.25},LineTiming={0,0}}
local TOUCH_ID=7001
local HALF_W,HALF_H=480,272      -- Position2D spans [-1,1] over 960x544
local AIM_SPEED=540              -- pixels per second at full stick
local CHAIN_SPEED=1500
-- Prompt anchors (parent-relative for buttons, screen for the battle layer).
local CORNER,TOP,CENTER,FIRE_AT,FIRE_TEXT_AT,PUT_AWAY_AT={0.62,0.62},{0,0.95},{0,0},{0.17,-0.89},{0.29,-0.89},{-0.24,-0.89}
local api
local state={previous=0,time=false,touch=false,list=false,aim_x=0,aim_y=0,aim_for=false,dodge={},glyphs={},
    aim_ship=false,base_x=0,base_y=0,offset_x=0,offset_y=0}

local function clamp(v,lo,hi) return v<lo and lo or (v>hi and hi or v) end
local function edge(b,mask) return band(b,mask)~=0 and band(state.previous,mask)==0 end

-- Touch events -------------------------------------------------------------
local function send(dispatcher,event,phase,x,y)
    local t=state.touch
    if phase=="Pressed" then
        t={Id=TOUCH_ID,State="Pressed",Position2D=Vector2(x,y),LastPosition2D=false,
           TimeStamp=AbsoluteTime,LastTimeStamp=false,PressedTimeStamp=AbsoluteTime}
        t.PressedPosition2D=t.Position2D
        state.touch=t
    else
        if x then t.LastPosition2D=t.Position2D;t.Position2D=Vector2(x,y) end
        t.LastTimeStamp=t.TimeStamp;t.TimeStamp=AbsoluteTime;t.State=phase
        if phase~="Moved" then state.touch=false end
    end
    dispatcher[event](dispatcher,t)
    return t
end

-- Aim ------------------------------------------------------------------------
local function shot_offset(name,ctrl)
    if name=="Mortar" then return 0.25 end
    if name=="SwivelGun" or name=="HeavyGun" then
        local offset=ctrl.TargetOffset2D
        return offset and offset.y or 0
    end
    return 0
end
local function clamp_aim(name)
    local x0,x1,y0,y1=-0.97,0.97,-0.95,0.95
    if name=="Mortar" and Mortar_MinTouchX then
        x0,x1=Mortar_MinTouchX,Mortar_MaxTouchX
        y0,y1=Mortar_MinTouchY+0.25,Mortar_MaxTouchY+0.25
    end
    state.aim_x=clamp(state.aim_x,x0,x1);state.aim_y=clamp(state.aim_y,y0,y1)
end
local function enemy_points(process)
    local points={}
    local manager,camera=process.BattleManager,process.GameCamera
    if not manager or not camera then return points end
    for _,ship in ipairs(manager.Enemies or {}) do
        if not ship.IsDestroyed and ship.Position then
            local p=camera:GetScreenPosition(ship.Position)
            if p and math.abs(p.x)<=1 and math.abs(p.y)<=1 then points[#points+1]={x=p.x,y=p.y,ship=ship} end
        end
    end
    return points
end
-- The aim is held relative to the chosen ship: each weapon swings the camera
-- into its own view after it is readied, and the aim must stay on the ship.
local function target_point(process)
    local ship=state.aim_ship
    if ship and not ship.IsDestroyed and ship.Position then
        local p=process.GameCamera:GetScreenPosition(ship.Position)
        if p then state.base_x,state.base_y=p.x,p.y end
    end
    state.aim_x,state.aim_y=state.base_x+state.offset_x,state.base_y+state.offset_y
end
-- Settings > Aim assist (on by default): start on the nearest enemy ship and
-- ease the aim over ships and weak points.
local function assist() return not api.setting or api.setting(4)==1 end
local function start_aim(name,process)
    local d=aim_defaults[name] or aim_defaults.RoundShot
    local best=math.huge
    state.aim_ship=false;state.base_x,state.base_y=d[1],d[2];state.offset_x,state.offset_y=0,0
    if not assist() then return end
    for _,p in ipairs(enemy_points(process)) do
        local dx,dy=(p.x-d[1])*HALF_W,(p.y-d[2])*HALF_H
        if dx*dx+dy*dy<best then best=dx*dx+dy*dy;state.aim_ship=p.ship;state.base_x,state.base_y=p.x,p.y end
    end
end
local function move_aim(name,ctrl,process,lx,ly,dt)
    target_point(process)
    local ax,ay=api.axis(lx),api.axis(ly)
    local moved=ax~=0 or ay~=0
    if moved then
        local speed=AIM_SPEED*dt
        -- A stick is slower than a finger: ease the aim over ships and weak points.
        for _,p in ipairs(assist() and enemy_points(process) or {}) do
            local dx,dy=(p.x-state.aim_x)*HALF_W,(p.y-state.aim_y)*HALF_H
            if dx*dx+dy*dy<70*70 then speed=speed*0.6;break end
        end
        if name=="SwivelGun" and assist() and ctrl.IsTargetingWeakPoint and ctrl:IsTargetingWeakPoint(Vector2(state.aim_x,state.aim_y)) then
            speed=speed*0.6
        end
        state.aim_x=state.aim_x+ax*math.abs(ax)*speed/HALF_W
        state.aim_y=state.aim_y-ay*math.abs(ay)*speed/HALF_H
    end
    clamp_aim(name)
    -- Only the stick changes the offset; a camera move that carries the ship
    -- out of a weapon's area must not push the aim away from it for good.
    if moved then state.offset_x,state.offset_y=state.aim_x-state.base_x,state.aim_y-state.base_y end
end
-- Show the aim with each weapon's own feedback before the trigger is pressed.
local function show_aim(name,ctrl)
    local x,y=state.aim_x,state.aim_y
    local marker=name=="HeavyGun" and ctrl.LoadedTargets and ctrl.LoadedTargets[1]
    if state.shown_ctrl==ctrl and state.shown_x==x and state.shown_y==y and state.shown_marker==marker and not ctrl.Touch then return end
    state.shown_ctrl,state.shown_x,state.shown_y,state.shown_marker=ctrl,x,y,marker
    if name=="RoundShot" then
        if not ctrl.Touch then
            ctrl.LastTouchPosition2D=Vector2(x,y)
            if not ctrl.PressedTime then ctrl.PressedTime=ctrl.Process.GameTime-1 end
        end
    elseif name=="SwivelGun" then
        if not ctrl.Touch then ctrl.FinalShootPosition2D=Vector2(x,y) end
    elseif name=="HeavyGun" then
        -- The next shot's own target marker sits on the aim.
        if marker and not marker.IsDestroyed then marker.LocalPosition2D=Vector2(x,y) end
    elseif name=="Mortar" then
        if not ctrl.Touch then
            local finger=Vector2(x,y-0.25)
            ctrl:SetTargetsFromPos2D(finger);ctrl.ShotPosition2D=finger
        end
    end
end

-- Button prompts ---------------------------------------------------------------
local function glyph(owner,mask,anchor,visible,key)
    key=key or "_VitaGlyph"
    local icon=owner[key]
    if not icon then
        if not visible then return end
        icon=api.icon(owner,key,glyph_names[mask],Vector2(anchor[1],anchor[2]),0.7)
        if not icon then return end
    elseif visible and owner[key.."Mask"]~=mask then
        icon.Texture="ACPVita"..glyph_names[mask]
    end
    owner[key.."Mask"]=mask
    if visible and not owner[key.."Listed"] then owner[key.."Listed"]=true;state.glyphs[#state.glyphs+1]={owner,key} end
    api.visible(icon,visible)
end
local function label(layer,key,text,anchor,visible)
    local e=layer[key]
    if not e then
        if not visible then return end
        e=layer:CreateEntity("LocalizedText2D",{Text=text,IsAlreadyLocalized=true,FontSize=28,Color=Color_White,
            AnchorPosition2D=Vector2(anchor[1],anchor[2]),PivotPosition2D=Vector2_Zero,LocalDepth=-0.2})
        layer[key]=e
    end
    if visible and not layer[key.."Listed"] then layer[key.."Listed"]=true;state.glyphs[#state.glyphs+1]={layer,key} end
    api.visible(e,visible)
end
local function hide_glyphs()
    for _,entry in ipairs(state.glyphs) do
        local owner,key=entry[1],entry[2]
        owner[key.."Listed"]=nil
        if not owner.IsDestroyed and owner[key] then api.visible(owner[key],false) end
    end
    state.glyphs={}
end
local function shown(e) return e and not e.IsDestroyed and e.Visible~=false end

-- Battle phases --------------------------------------------------------------
local function attack_input(o,b,attack,lx,ly,dt,blocked)
    local widget=attack.WeaponsWidget
    if not widget then return end
    local selected=widget.SelectedWeapon
    if not blocked then
        if selected and not selected.IsDestroyed then
            -- Pressing the selected weapon's button again puts it away.
            local cancel=widget.CancelButton
            local mask=weapon_masks[selected.WeaponName]
            if mask and edge(b,mask) and shown(cancel) and cancel.IsEnabled then
                api.consume(mask);cancel:OnButtonReleased()
                ACPVitaLog("battle: put away "..tostring(selected.WeaponName))
            end
        else
            for _,button in ipairs(widget.WeaponButtons) do
                local mask=weapon_masks[button.WeaponName]
                if mask and edge(b,mask) and shown(button) and button.IsEnabled~=false then
                    api.consume(mask)
                    if not button.IsReloading and button.HasAmmo then
                        button.IsPushed=true;button:OnButtonToggleOn()
                        ACPVitaLog("battle: ready "..tostring(button.WeaponName))
                    end
                    break
                end
            end
        end
    end
    local name=attack.CurrentWeaponName or false
    local data=name and attack.WeaponTechniques[name]
    local ctrl=data and data.Controller
    if name~=state.aim_for then
        state.aim_for=name;state.shown_ctrl=nil -- the weapon resets its own aim feedback
        if name then start_aim(name,o.Process) end
    end
    if ctrl and ctrl.IsWeaponActive then
        move_aim(name,ctrl,o.Process,lx,ly,dt)
        show_aim(name,ctrl)
    end
    -- CROSS is the trigger: press and release exactly like a finger.
    local down=band(b,FIRE)~=0
    local t=state.touch
    if down and not t and ctrl and ctrl.IsWeaponActive and not blocked and (edge(b,FIRE) or ACPVitaBattleHold) then
        ACPVitaBattleHold=false
        send(o,"OnTouchPressed","Pressed",state.aim_x,state.aim_y-shot_offset(name,ctrl))
    elseif t and not t.Spider then
        local finger_y=state.aim_y-(ctrl and shot_offset(name,ctrl) or 0)
        if down then
            local p=t.Position2D
            if p.x~=state.aim_x or p.y~=finger_y then send(o,"OnTouchMoved","Moved",state.aim_x,finger_y) end
        elseif not (name=="RoundShot" and ctrl.Touch==t and not ctrl:HasPressedEnough()) then
            -- A quick tap still fires: hold the finger for the volley's
            -- preparation time, as a short touch would be discarded.
            send(o,"OnTouchReleased","Released")
        end
    end
    if not down then ACPVitaBattleHold=false end
    -- Prompts on the original buttons.
    for _,button in ipairs(widget.WeaponButtons) do
        local mask=weapon_masks[button.WeaponName]
        if mask then glyph(button,mask,CORNER,shown(button) and (button.Alpha or 1)>0.3) end
    end
    -- The original put-away button is a large X that reads as the CROSS
    -- button. While a weapon is readied it shows that weapon's own button
    -- instead (press again to put away), with CROSS = Fire beside it.
    local cancel=widget.CancelButton
    selected=widget.SelectedWeapon
    local mask=selected and not selected.IsDestroyed and weapon_masks[selected.WeaponName]
    local readied=cancel and mask and shown(cancel) or false
    if readied and ACPVitaIcons()==1 then
        local texture="ACPVita"..glyph_names[mask]
        if tostring(cancel.Texture)~=texture then cancel.Texture=texture end
    end
    local layer=o.Process:GetEntity("TagBattleGUILayer")
    if layer then
        glyph(layer,FIRE,FIRE_AT,readied,"_VitaFire")
        label(layer,"_VitaFireText","Fire",FIRE_TEXT_AT,readied)
        label(layer,"_VitaPutAwayText","Put away",PUT_AWAY_AT,readied)
    end
end
local function dodge_input(b,dodge,blocked)
    for _,side in ipairs{{L,dodge.MoveLeftButton,"OnSwipeLeft"},{R,dodge.MoveRightButton,"OnSwipeRight"}} do
        local mask,button,swipe=side[1],side[2],side[3]
        if button and not button.IsDestroyed then
            local active=dodge.DefenseGameplayActive and shown(button)
            if active and not blocked and edge(b,mask) then
                if button.IsEnabled then
                    button:OnButtonPressed();state.dodge[mask]=button
                else
                    -- The original swipe order waits up to a second for reload.
                    dodge[swipe](dodge)
                end
            elseif state.dodge[mask] and band(b,mask)==0 then
                state.dodge[mask]=nil;button:OnButtonReleased()
            end
            glyph(button,mask,TOP,active)
        end
    end
end
local function chain_input(o,b,chain,blocked)
    local button=chain.ChainShotButton
    if not button or button.IsDestroyed then return end
    local active=chain.DefenseGameplayActive and shown(button)
    if active and not blocked and edge(b,TRIANGLE) and button.IsEnabled then
        api.consume(TRIANGLE);button:OnButtonReleased()
    end
    glyph(button,TRIANGLE,TOP,active)
end
-- Spider shot: push the stick toward the next point and the chain slides
-- there; the original controller decides when each point is reached.
local function steer_chain(o,chain,lx,ly,dt)
    local t=state.touch
    if not chain._SpiderStarted or (t and t.Spider and chain._SpiderTouch~=t) then
        if t and t.Spider then send(o,"OnTouchCanceled","Canceled") end
        return
    end
    if t and not t.Spider then return end
    local ax,ay=api.axis(lx),-api.axis(ly)
    local tilt=math.sqrt(ax*ax+ay*ay)
    if tilt<0.5 then return end
    local nodes=chain._SpiderGUIElements
    if not t then
        if chain._SpiderTouch or chain._SpiderTimerBeforeSuccess or (chain._NbPointsReached or 0)>0 then return end
        local first=nodes and nodes[1]
        if not first then return end
        local p=first.Position2D
        send(o,"OnTouchPressed","Pressed",p.x,p.y).Spider=true
        return
    end
    local target=nodes[chain._CurrentPointToGo]
    if not target then return end
    local p,q=t.Position2D,target.Position2D
    local dx,dy=(q.x-p.x)*HALF_W,(q.y-p.y)*HALF_H
    local distance=math.sqrt(dx*dx+dy*dy)
    if distance<0.5 or (ax*dx+ay*dy)/(tilt*distance)<0.6 then return end
    local f=math.min(1,CHAIN_SPEED*dt/distance)
    send(o,"OnTouchMoved","Moved",p.x+(q.x-p.x)*f,p.y+(q.y-p.y)*f)
end

-- Cannonball defense ("slice"): L shoots down the most urgent cannonball on
-- the left half of the screen, R the right half, through the defense's own
-- actions (normal: explode; armored: break).
local PARRY_SLOTS=6
local function parry_input(o,b,fps,blocked)
    local manager=fps.FPSDefenseManager
    local layer=o.Process:GetEntity("TagBattleGUILayer")
    local active=fps.DefenseGameplayActive and manager and manager.CurrentCannonBalls and not manager.GameplayPaused
    local camera=o.Process.GameCamera
    local used=0
    local best={}
    if active then
        for _,ball in ipairs(manager.CurrentCannonBalls) do
            -- Explosive crates ("barrel") fall short into the water; hitting
            -- one damages the player, so they get no prompt and no action.
            if ball and not ball.IsDestroyed and ball.Type~="barrel" and manager:CanShowCannonBall(ball) then
                local p=camera:GetScreenPosition(ball.Position)
                local side=p.x<0 and L or R
                local urgency=ball:GetTimeProgression()
                if not best[side] or urgency>best[side][2] then best[side]={ball,urgency,p} end
                used=used+1
                if layer and used<=PARRY_SLOTS then
                    local key="_VitaParry"..used
                    glyph(layer,side,CENTER,true,key)
                    local icon=layer[key]
                    -- Moving prompts use LocalPosition2D, like the game's own battle markers.
                    if icon then icon.LocalPosition2D=Vector2(p.x+0.08,p.y+0.12) end
                end
            end
        end
        if not blocked then
            for _,side in ipairs{L,R} do
                local target=best[side]
                if target and edge(b,side) then
                    local ball,p=target[1],target[3]
                    if manager:CanBreakArmorOfCannonBall(ball) then manager:BreakCannonBallArmor(ball,"Tap")
                    elseif manager:CanExploseCannonBall(ball) then manager:ExploseCannonBall(ball,Vector2(side==L and -1 or 1,0)) end
                    MainProcess:GetEntity("TagGameSound"):CallEventUnfiltered("Sound_Ninja_Defense_Hit")
                end
            end
        end
    end
    if layer then
        for i=used+1,PARRY_SLOTS do
            local icon=layer["_VitaParry"..i]
            if icon then api.visible(icon,false) else break end
        end
    end
end
local function controllers(o)
    local list=o._ControllerList
    if list~=state.list then
        state.list=list;state.touch=false;state.aim_for=false;state.dodge={}
        state.attack,state.dodger,state.chain,state.fps=nil,nil,nil,nil
        if list==o.ControllerModeList.Battle or list==o.ControllerModeList.FruitNinjaSwivel then
            for _,c in ipairs(list) do
                if c.ClassName=="BattleAttackController" then state.attack=c
                elseif c.ClassName=="DodgeDefenseController" then state.dodger=c
                elseif c.ClassName=="ChainShotDefenseController" then state.chain=c
                elseif c.ClassName=="FPSDefenseController" then state.fps=c end
            end
        end
    end
    return state.attack
end
local function update(o)
    local b,lx,ly=api.pad()
    local now=AbsoluteTime
    local dt=state.time and clamp(now-state.time,0,0.1) or 0
    if dt==0 and state.time then return end -- already handled this frame
    state.time=now
    local attack=controllers(o)
    if attack and o.IsTouchEnabled~=false then
        -- The spider shot also runs in its own paused tutorial.
        if state.chain then steer_chain(o,state.chain,lx,ly,dt) end
        local live=api.active(o)
        local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
        local blocked=manager and (manager._DialogLayer or manager._CutSceneSkipButton or manager:HasDisplayedPopup())
        -- The cannonball lessons pause the game and feed touches themselves.
        if state.fps and (live or (TM and TM.IsActive)) then
            state.shown=now
            parry_input(o,b,state.fps,live and blocked)
        end
        if live then
            state.shown=now
            attack_input(o,b,attack,lx,ly,dt,blocked)
            if state.dodger then dodge_input(b,state.dodger,blocked) end
            if state.chain then chain_input(o,b,state.chain,blocked) end
        end
    end
    state.previous=b
end

-- Entry points -----------------------------------------------------------------
-- In battle the circle button selects a weapon; START still opens the pause menu.
function ACPVitaBattleOwnsCircle()
    return state.shown~=nil and AbsoluteTime-state.shown<0.1
end
-- Called every frame from an always-updating entity: hide prompts when the
-- battle is paused, frozen or over.
function ACPVitaBattleIdle()
    if #state.glyphs>0 and (not state.shown or AbsoluteTime-state.shown>0.1) then hide_glyphs() end
end
-- Battle tutorials wait for one of the original buttons, or for a touch.
function ACPVitaBattleTutorial(tm,b,previous)
    local function hit(mask) return band(b,mask)~=0 and band(previous,mask)==0 end
    local ref=tm._ButtonReference
    local mask
    if ref and not ref.IsDestroyed and ref.HasTag then
        if ref:HasTag("TagPakWeaponButton") then mask=weapon_masks[ref.WeaponName]
        elseif ref:HasTag("TagDodgeButton") then mask=ref.DodgeDirection=="Left" and L or R
        elseif ref:HasTag("TagChainShotButton") then mask=TRIANGLE end
    end
    local touch={Id=TOUCH_ID,State="Pressed",Position2D=Vector2(0,0)}
    if mask and hit(mask) then
        tm._ButtonReferencePressed=true;tm.TouchPressed=touch
        return true
    end
    local layer=tm.TutorialLayer
    if layer and (layer._VitaPromptKey=="TUTO_NWDF_00006" or layer._VitaPromptKey=="TUTO_NWDF_00003") and hit(CROSS) then
        tm.TouchPressed=touch -- "Tap here to continue"
        return true
    end
    if not ref and layer and layer._VitaPromptKey=="TUTO_BASIC_00008" and hit(FIRE) then
        -- Keep holding CROSS after the lesson: the volley fires on release.
        tm.TouchPressed=touch;ACPVitaBattleHold=true
        return true
    end
    return false
end
function ACPVitaBattle(def,ui)
    api=ui
    if def.ClassName~="GameController" then return false end
    api.after(def,"OnUpdate",update)
    return true
end
end battle_module() end
