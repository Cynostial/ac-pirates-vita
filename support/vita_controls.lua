-- Vita-specific adapters. All gameplay calls run on the game's Lua thread.
local band = bit.band
local consumed = 0
local sail_order={NO_SAIL=0,HALF_SAIL=1,FULL_SAIL=2,ANIMUS_SPEED=3}
local sail_titles={[0]="Stopped","Half sail","Full sail","Travel speed"}
-- Settings are read every frame by several adapters; keep the native value
-- until the Settings menu writes a new one.
local setting_cache,setting_source={},nil
local function setting(index,value)
    if not ACPVitaSettings then return (index==2 or index==3 or index==4) and 1 or 0 end
    if ACPVitaSettings~=setting_source then setting_cache={};setting_source=ACPVitaSettings end
    if value~=nil then setting_cache[index]=nil;return ACPVitaSettings(index,value) end
    local cached=setting_cache[index]
    if cached==nil then cached=ACPVitaSettings(index);setting_cache[index]=cached end
    return cached
end
local function camera_sign() return setting(1)==1 and -1 or 1 end
local function pad()
    local b,lx,ly,rx,ry=ACPVitaPad()
    consumed=band(consumed,b)
    return band(b,bit.bnot(consumed)),lx,ly,rx,ry
end
local function consume(buttons)
    consumed=bit.bor(consumed,buttons)
    if ACPVitaConsume then ACPVitaConsume(buttons) end
end
local prompts={
    TUTO_BASIC_00001="Press R to raise sails and get underway.",
    TUTO_BASIC_00002="Press R again for full sail.",
    TUTO_BASIC_00003="Press L to lower sails and slow down.",
    TUTO_BASIC_00004="Hold the left stick LEFT to turn.",
    TUTO_BASIC_00023="Keep holding the left stick LEFT.",
    -- Battle (vita_battle.lua): face buttons and shoulders ready weapons, CROSS fires.
    TUTO_BASIC_00007="Press SQUARE to ready the Round Shot.",
    TUTO_BASIC_00008="Aim with the left stick. Hold CROSS, then release to fire.",
    TUTO_BASIC_00010="Press TRIANGLE for the spider shot to break the enemy attack.",
    TUTO_BASIC_00011="Press L to dodge left.",
    TUTO_BASIC_00012="Press R to dodge right.",
    TUTO_NWDF_00005="Push the left stick toward each dot in order to steer the chain!",
    TUTO_WEAP_00002="Aim with the left stick and hold CROSS. Follow your target.",
    TUTO_WEAP_00004="Press CROSS twice to lock the lines and shoot.",
    TUTO_WEAP_00006="Move the target with the left stick and press CROSS to fire.",
    GAME_DAILY_00009="Press CROSS to open your rewards!",
    TUTO_NWDF_00001="You're under enemy fire! Press L or R to shoot down the cannonball on that side before it hits your ship!",
    TUTO_NWDF_00002="Shoot down every cannonball with L and R!",
    TUTO_NWDF_00004="Press L or R to break it on that side!",
    TUTO_NWDF_00006="Press CROSS to continue.",
    -- Sailing (vita_sailing.lua).
    TUTO_BASIC_00006="Press CROSS to engage the ship.",
    TUTO_BASIC_00018="Press SELECT to open the tactical view.",
    TUTO_BASIC_00019="Steer with the left stick to the next mission.",
    TUTO_BASIC_00020="Press SQUARE to view the ship from behind.",
    TUTO_BASIC_00022="Use the right stick to look around.",
    TUTO_BASIC_00021="Press SQUARE to return to the deck view.",
    -- Runner (vita_runner.lua): the temple run uses buttons and the left stick.
    TUTO_RUNTU_00001="Press RIGHT or R to move right.",
    TUTO_RUNTU_00019="Press RIGHT or R to move right.",
    TUTO_RUNTU_00002="Press LEFT or L to move left.",
    TUTO_RUNTU_00018="Press LEFT or L to move left.",
    TUTO_RUNTU_00003="Press LEFT or RIGHT to run along the wall.",
    TUTO_RUNTU_00004="Press LEFT or RIGHT to run back to your lane.",
    TUTO_RUNTU_00005="Press CROSS to jump.",
    TUTO_RUNTU_00006="Press CROSS to use the rope.",
    TUTO_RUNTU_00007="Press LEFT or RIGHT to turn.",
    TUTO_RUNTU_00008="Press CIRCLE to slide.",
    TUTO_RUNTU_00009="Press CROSS to climb.",
    TUTO_RUNTU_00010="Press SQUARE to pick it up.",
    TUTO_RUNTU_00011="Press LEFT or RIGHT to change lanes.",
    TUTO_RUNTU_00012="Hold the left stick RIGHT to move right.",
    TUTO_RUNTU_00013="Hold the left stick LEFT to move left.",
    TUTO_RUNTU_00014="Press LEFT or RIGHT to jump between the walls.",
    TUTO_RUNTU_00015="Keep holding the left stick!",
    TUTO_RUNTU_00017="Press CIRCLE to dive.",
    TUTO_RUNTU_00020="Steer with the left stick to collect the Mayan stones.",
    TUTO_RUNTU_00016="Your turn, pirate! Press SQUARE to start.",
    -- Sideways swipe lessons, by the lesson's direction (vita_runner.lua).
    TUTO_RUNTU_00003_Left="Press LEFT or L to run on the wall.",
    TUTO_RUNTU_00003_Right="Press RIGHT or R to run on the wall.",
    TUTO_RUNTU_00004_Left="Press LEFT or L to land back on the path.",
    TUTO_RUNTU_00004_Right="Press RIGHT or R to land back on the path.",
    TUTO_RUNTU_00007_Left="Press LEFT or L to turn left.",
    TUTO_RUNTU_00007_Right="Press RIGHT or R to turn right.",
    TUTO_RUNTU_00011_Left="Press LEFT or L to change lanes.",
    TUTO_RUNTU_00011_Right="Press RIGHT or R to change lanes.",
    TUTO_RUNTU_00014_Left="Press LEFT or L to jump to the other wall.",
    TUTO_RUNTU_00014_Right="Press RIGHT or R to jump to the other wall.",
}
local prompt_icons={TUTO_BASIC_00001="R",TUTO_BASIC_00002="R",TUTO_BASIC_00003="L",TUTO_BASIC_00004="LeftStick",
    TUTO_BASIC_00007="BattleSquare",TUTO_BASIC_00008="BattleCross",TUTO_BASIC_00010="BattleTriangle",TUTO_BASIC_00011="BattleL",
    TUTO_BASIC_00012="BattleR",TUTO_NWDF_00005="LeftStick",
    TUTO_BASIC_00006="Cross",TUTO_BASIC_00018="Select",TUTO_BASIC_00019="LeftStick",TUTO_BASIC_00020="Square",
    TUTO_BASIC_00021="Square",TUTO_BASIC_00022="RightStick",
    TUTO_RUNTU_00001="R",TUTO_RUNTU_00019="R",TUTO_RUNTU_00002="L",TUTO_RUNTU_00018="L",
    TUTO_RUNTU_00005="Cross",TUTO_RUNTU_00006="Cross",TUTO_RUNTU_00009="Cross",TUTO_RUNTU_00008="Circle",
    TUTO_RUNTU_00017="Circle",TUTO_RUNTU_00010="Square",TUTO_RUNTU_00012="LeftStick",TUTO_RUNTU_00013="LeftStick",
    TUTO_RUNTU_00015="LeftStick",TUTO_RUNTU_00020="LeftStick",TUTO_RUNTU_00016="Square"}
for _,lesson in ipairs{"00003","00004","00007","00011","00014"} do
    prompt_icons["TUTO_RUNTU_"..lesson.."_Left"]="L";prompt_icons["TUTO_RUNTU_"..lesson.."_Right"]="R"
end
-- The spider-shot dots fill the lower half of the screen.
local prompt_positions={TUTO_NWDF_00005={0,0.12},
    -- Cannonballs fly through the middle of the screen.
    TUTO_NWDF_00001={0,0.70},TUTO_NWDF_00002={0,0.70},TUTO_NWDF_00004={0,0.70},TUTO_NWDF_00006={0,0.70}}
local function prompt_key(key)
    return tostring(key):gsub("^ME_","")
end
local function tutorial_active()
    local tutorial=MainProcess and MainProcess:GetEntity("TagTutorialManager") or TM
    return tutorial and tutorial.IsActive
end
local function gameplay_active(o)
    local game=GetSubProcess and GetSubProcess("TagGameProcess") or o.Process
    return not tutorial_active() and not game.GameProcessPaused and (game.GameDt or Dt)>0
end
local function sail_prompt(o)
    local ref=o._ButtonReference
    if not ref or not ref.SpeedPreset then return nil end
    local current=sail_order[Player.CurrentSpeedPreset]
    local target=sail_order[ref.SpeedPreset]
    if not current or not target or current==target then return nil end
    return target>current and 512 or 256
end
local function pressed(o, buttons, mask)
    return band(buttons,mask) ~= 0 and band(o._VitaPrevious or 0,mask) == 0
end
local function axis(value)
    local v = (value-128)/127
    if math.abs(v)<0.16 then return 0 end
    return math.max(-1,math.min(1,(math.abs(v)-0.16)/0.84))*(v<0 and -1 or 1)
end
local function hide(e)
    if e then
        if e.Visible~=false then e:SetVisible(false) end
        if e.IsEnabled~=false then e:SetEnable(false) end
    end
end
-- These are port-owned leaf icons/labels. Parent layer changes still update
-- their actual visibility; inspect that state rather than caching a stale bit.
local function hud_visible(e,visible)
    if e and e.Visible~=visible then e:SetVisible(visible) end
end
-- Make one nested menu icon invisible and inert while its children (laid out
-- relative to it) stay usable.
local function ghost(e)
    if e and not e.IsDestroyed and not e._VitaGhost then
        local clear=Color(1,1,1,0)
        e.AmbientColor=clear;e.IdleColor=clear;e.PressedColor=clear;e.IsEnabled=false
        e._VitaGhost=true
    end
end
local function menu_hide(o)
    hide(o.ButtonConnect); hide(o.LogoFBConnect); hide(o.LogoGoogleConnect)
    if o.ButtonConnect then hide(o.ButtonConnect.FirstConnectText) end
    -- Online-only: Ubisoft web news ("More games"), Game Center trophies,
    -- leaderboards and the web privacy policy. Language and Settings stay.
    hide(o.LogoUbi);hide(o.Privacy)
    if o.Privacy then hide(o.Privacy.TextPrivacy) end -- its label does not follow the parent
    local trophies=o.LogoTrophies
    if trophies then ghost(trophies);ghost(trophies.LogoPodium) end
end
local function after(def,key,fn)
    local original=def[key]
    def[key]=function(o,...)
        if original then original(o,...) end
        fn(o,...)
    end
end
local function controller_icon(parent,key,name,position,scale)
    if ACPVitaIcons()~=1 then return nil end
    if not parent[key] then
        parent[key]=parent:CreateEntity("PakGUIElement2D",{
            Texture="ACPVita"..name,AnchorPosition2D=position,PivotPosition2D=Vector2_Zero,
            ScaleFactor2D=Vector2_UnitScale*(scale or 0.65),Color=Color_White,LocalDepth=-0.2,RealPixelSize=true,
        })
    end
    if tostring(parent[key].Texture)~="ACPVita"..name then parent[key].Texture="ACPVita"..name end
    hud_visible(parent[key],true)
    return parent[key]
end
local function hold_indicator(layer,progress)
    if ACPVitaIcons(1)~=1 then return end
    if not layer._VitaHoldRing then
        layer._VitaHoldBack=layer:CreateEntity("PakGUIElement2D",{
            Texture="ACPVitaHold32",AnchorPosition2D=Vector2(0,-0.06),PivotPosition2D=Vector2_Zero,
            PixelSize2D=Vector2(76,76),RealPixelSize=true,LocalDepth=-0.22,Alpha=0.25,
        })
        layer._VitaHoldRing=layer:CreateEntity("PakGUIElement2D",{
            Texture="ACPVitaHold01",AnchorPosition2D=Vector2(0,-0.06),PivotPosition2D=Vector2_Zero,
            PixelSize2D=Vector2(76,76),RealPixelSize=true,LocalDepth=-0.24,
        })
    end
    local step=math.min(32,math.max(0,math.floor(progress*32+0.5)))
    hud_visible(layer._VitaHoldBack,true);hud_visible(layer._VitaHoldRing,step>0)
    if step>0 and step~=layer._VitaHoldStep then
        layer._VitaHoldRing.Texture=string.format("ACPVitaHold%02d",step)
    end
    layer._VitaHoldStep=step
end
local function hide_hold_indicator(layer)
    for _,name in ipairs{"_VitaHoldRing","_VitaHoldBack"} do
        hud_visible(layer[name],false)
    end
end
local function focus(o,button)
    if o._VitaFocus and o._VitaFocus~=button and not o._VitaFocusVisual.IsDestroyed then
        o._VitaFocusVisual.AmbientColor=o._VitaFocusColor
    end
    if o._VitaFocus~=button then
        o._VitaFocus=button
        if button then
            local visual=button
            if not button.Texture and button.Parent and button.Parent.Texture then visual=button.Parent end
            o._VitaFocusVisual=visual;o._VitaFocusColor=visual.AmbientColor
        end
    end
    if button then o._VitaFocusVisual.AmbientColor=Color(1,0.82,0.38,1) end
end
local function usable(button,root)
    if button.IsDestroyed or button.IsEnabled==false then return false end
    local p=button
    for i=1,20 do
        -- Sibling buttons are nested for layout. Their enabled state does not
        -- disable descendants (e.g. Settings beneath locked Fishing).
        if not p or (p.Visible==false and p~=root._VitaNavTransparentContainer) then return false end
        if p==root then return true end
        p=p.Parent
    end
    return false
end
local function navigation(o,active,ownsPopup)
    local b,lx,ly=pad()
    if not active then focus(o,nil);o._VitaNavPrevious=b;o._VitaDirection=0;return end
    local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
    if (manager:HasDisplayedPopup() and not ownsPopup) or MainProcess:GetEntity("TagFadeFX").Visible then return end
    local entries=o._VitaNavEntries or {}
    if not o._VitaNavReported then
        ACPVitaLog("menu navigation: "..tostring(o.ClassName).." entries="..#entries)
        o._VitaNavReported=true
    end
    local selected=o._VitaFocus
    if not selected or not usable(selected,o) then
        selected=nil
        for _,e in ipairs(entries) do if usable(e,o) then selected=e;break end end
        focus(o,selected)
    end
    local dx,dy=0,0
    if band(b,128)~=0 or axis(lx)<-0.55 then dx=-1
    elseif band(b,32)~=0 or axis(lx)>0.55 then dx=1
    elseif band(b,16)~=0 or axis(ly)<-0.55 then dy=1
    elseif band(b,64)~=0 or axis(ly)>0.55 then dy=-1 end
    local dir=dx~=0 and dx or dy*2
    o._VitaRepeat=(o._VitaRepeat or 0)-math.max(0,Dt)
    if selected and dir~=0 and (dir~=o._VitaDirection or o._VitaRepeat<=0) then
        local best,score=nil,math.huge
        local pos=selected.Position2D
        for _,e in ipairs(entries) do
            if e~=selected and usable(e,o) then
                local p=e.Position2D;local x,y=p.x-pos.x,p.y-pos.y
                local along=x*dx+y*dy;local across=math.abs(x*dy-y*dx)
                if along>0.015 then
                    local value=along+across*3
                    if value<score then best=e;score=value end
                end
            end
        end
        if best then selected=best;focus(o,best) end
        o._VitaRepeat=dir~=o._VitaDirection and 0.35 or 0.14
    end
    if selected and band(b,16384)~=0 and band(o._VitaNavPrevious or 0,16384)==0 then
        consume(b);selected:OnButtonReleased()
    end
    o._VitaNavPrevious=b;o._VitaDirection=dir
end
-- The Go button of the top popup when it is a request with Go and Close
-- (missions, temples, fast travel); Close is its back button.
local function mission_go(manager)
    local top=manager._PopupStack and manager._PopupStack[1]
    local go=top and not top.IsDestroyed and not top.Closed and top.Visible~=false and top.CloseButton and top.GoButton
    if go and not go.IsDestroyed and go.Visible~=false and go.IsEnabled~=false then return go end
end
local function install_navigation(def,active,ownsPopup)
    local paths={}
    local function walk(t,path,depth)
        if depth>12 then return end
        for key,v in pairs(t) do
            if type(key)=="string" and type(v)=="table" and type(v[1])=="string" then
                local nextPath={};for i,k in ipairs(path) do nextPath[i]=k end;nextPath[#nextPath+1]=key
                local toggle=v[1]=="PakGUIToggleButton2D"
                if (v.OnButtonReleased or toggle) and key~="InvisibleButton" and key~="AbstergoButton" then
                    paths[#paths+1]=nextPath
                end
                walk(v,nextPath,depth+1)
            end
        end
    end
    walk(def,{},0)
    table.sort(paths,function(a,b)return table.concat(a,".")<table.concat(b,".")end)
    local updatable=false;for _,v in ipairs(def) do if v=="PakUpdatable" then updatable=true end end
    if not updatable then table.insert(def,"PakUpdatable") end
    after(def,"OnCreateAfterSubEntities",function(o)
        o._VitaNavEntries={}
        for _,path in ipairs(paths) do
            local e=o;for _,key in ipairs(path) do e=e and e[key] end
            if e then o._VitaNavEntries[#o._VitaNavEntries+1]=e end
        end
        ACPVitaLog("menu initialized: "..tostring(o.ClassName).." paths="..#paths.." entries="..#o._VitaNavEntries)
    end)
    after(def,"OnUpdate",function(o)navigation(o,active(o),ownsPopup)end)
end
local function replace_option(container,prefix,index,title)
    local label=container[prefix.."Text"]
    label.Text=title;label.IsAlreadyLocalized=true;label.FontSize=30
    local button=container[prefix.."Button"]
    local function draw(o,value,first)
        if not first and setting(index,value)<0 then value=setting(index) end
        o.IsPushed=value==1
        o.Texture=value==1 and "SETTINGS_OPTION_ON_OFF" or "SETTINGS_OPTION_OFF_ON"
        o.IdleTexture=o.Texture
        local on,off=o[prefix.."OnText"],o[prefix.."OffText"]
        for _,pair in ipairs{{on,value==1},{off,value==0}} do
            local text,enabled=pair[1],pair[2]
            text.ScaleFactor2D=Vector2_UnitScale*(enabled and 0.8 or 0.6)
            text.Color=enabled and Color_White or Color(0.5,0.5,0.5)
            text.AnchorPosition2D=enabled and text.ActivatedPosition2D or text.DisabledPosition2D
        end
        ACPVitaLog("setting "..title.."="..value)
    end
    button.OnButtonToggleOn=function(o,first)draw(o,1,first)end
    button.OnButtonToggleOff=function(o,first)draw(o,0,first)end
    button.OnCreateAfterSubEntities=function(o)
        local value=setting(index);draw(o,value,true);o.IsPushed=value==1
    end
    button.OnSocialManagerConnection=function()end
end
-- Settings "Performance mode" (on by default): apply the quality switches of
-- Ubisoft's own lowest Android preset (SD2) on top of the preset the game
-- picks for "PS Vita". HD textures are kept so the converted texture cache
-- stays valid. Device parameters are read once at startup.
local performance_params={
    EnableWaterReflection=false,EnableWaterPlayerSailsReflection=false,EnableWaterReflectionBattle=false,
    EnablePlayerRimLight=false,EnablePlayerSpecular=false,EnablePlayerShadowes=false,
    EnableGodrays=false,EnableColorGrading=false,EnableLensflare=false,EnableBloom=false,
    EnableCritters=false,EnableFireFly=false,EnableSpeedFX=false,LODDistanceFactor=0.4,
    -- Designed but unshipped low-end paths: no foam-map camera pass and no
    -- post-process composite (its bloom/god-ray/grading inputs are off above).
    EnableFoamMap=false,EnableFoamMapBattle=false,EnablePostFX=false,
}
local WATER_TINT={0.9,0.95,1.4}
local water_tinted={"WaterColorDeep","WaterColorShallow","WaterColorNear","WaterColorFar"}
-- Performance mode: run a purely visual update on alternate frames. It
-- receives the frame time (Dt and the process GameDt) of both frames.
local function half_rate(def,method)
    local update=def[method]
    if not update then return end
    def[method]=function(o,...)
        if setting(3)~=1 then return update(o,...) end
        local process=o.Process
        local dt=(o._VitaDt or 0)+Dt
        local game=(o._VitaGameDt or 0)+(process and process.GameDt or 0)
        if o._VitaSkip then o._VitaSkip=false;o._VitaDt=dt;o._VitaGameDt=game;return end
        o._VitaSkip=true;o._VitaDt=0;o._VitaGameDt=0
        local frame,frameGame=Dt,process and process.GameDt
        Dt=dt;if process then process.GameDt=game end
        local ok,message=pcall(update,o,...)
        Dt=frame;if process then process.GameDt=frameGame end
        if not ok then error(message,0) end
    end
end
-- Performance mode: the minimap radar redraws on alternate frames. Its eased
-- north/wind arrows receive the time elapsed since their previous update.
local function adapt_free_roaming_layer(def)
    local update=def.UpdateLayer
    def.UpdateLayer=function(o)
        if setting(3)~=1 then o._VitaRadarElapsed=nil;o._VitaRadarSkip=nil;return update(o) end
        local elapsed=(o._VitaRadarElapsed or 0)+Dt
        if o._VitaRadarSkip then o._VitaRadarSkip=false;o._VitaRadarElapsed=elapsed;return end
        o._VitaRadarSkip=true;o._VitaRadarElapsed=0
        local frame=Dt;Dt=elapsed
        local ok,message=pcall(update,o)
        Dt=frame
        if not ok then error(message,0) end
    end
end
-- From 00.56 to 00.79 no Box2D contact reached Lua (the native report skipped
-- entities with IsDestroyed=false; fixed in native/destroyed_checks.c), so
-- fleets never spotted the player and StoryMission1's gunboats could not
-- start their battle. This also reports the player's overlap with enemy
-- line-of-sight/line-of-attack circles as Box2D's OnCollision2D does; a
-- second report for the same pair in a frame changes nothing. The original
-- notifier raises OnCollision2DStart/End and the game's rules decide the rest.
local function player_reach(shape)
    local c=shape.Collision2DShape
    if not c then return 0 end
    if c.Radius then return c.Radius end
    local reach=0
    for _,v in ipairs(c.Vertices or {}) do
        local length=v:Length()
        if length>reach then reach=length end
    end
    return reach
end
local function adapt_line_detection(def)
    after(def,"OnUpdate",function(o)
        if o.IsDestroyed or not o.PhysicsEnabled then return end
        local player=Player
        local shape=player and not player.IsDestroyed and player.ShipCollisionShape
        if not shape or shape.IsDestroyed or not shape.PhysicsEnabled then return end
        local circle=o.Collision2DShape
        local radius=circle and circle.Radius
        if not radius then return end
        local position=shape.Position2D
        local offset=position-o.Position2D
        local distance=offset:Length()
        if distance<=radius+player_reach(shape) then
            o:OnCollision2D(shape,position,distance>0 and offset*(1/distance) or offset)
        end
    end)
end
-- Performance mode: NPC ships animate their sails only within a kilometre of
-- the player (any distance in the spyglass). Once fleets react to the player
-- again, all 42 world ships of a region stay unfrozen and animated their
-- sails at 0.6-7 km, a few pixels on screen: about 5 ms a frame of
-- EdgeAnimationManager on hardware. Twice a second, far ships' sails
-- (EnemyShipElement) stop; nearer than 900 m the NPCAnimation rule above
-- (tactical view, visibility, the ship's freeze) applies again. Sinking
-- (locked) animations are left alone.
local SAIL_FAR,SAIL_NEAR,SAIL_PERIOD=1000,900,0.5
local sails_due=0
local function adapt_far_sails(def)
    after(def,"OnUpdate",function(o)
        local now=AbsoluteTime or 0
        if now<sails_due and now>sails_due-1 then return end
        sails_due=now+SAIL_PERIOD
        local process=GetSubProcess and GetSubProcess("TagGameProcess")
        local camera=process and process.GameCamera
        if not camera then return end
        local player=Player
        local eye=setting(3)==1 and player and not player.IsDestroyed and
            camera:GetCurrentCameraManName()~="CameraSpyGlass" and player.Position2D
        local mode=process and process.GameModeManager and process.GameModeManager.CurrentGameMode
        local name=mode and mode.GameModeName
        local tactical=name==GAMEMODES_TACTICAL or name==GAMEMODES_ASSASSINATIONMISSION
        for v in process:Entities("TagAnimationBase") do
            if v.ClassName=="EnemyShipElement" then   -- an NPC ship's sails (NPCAnimation)
                local ship=v.Parent
                local d=eye and ship and not ship.IsDestroyed and (ship.Position2D-eye):Length()
                local far=rawget(v,'_VitaFar')
                if d and not v.LockedAnimation and (d>SAIL_FAR or (far and d>SAIL_NEAR)) then
                    rawset(v,'_VitaFar',true);v:HasToBeUpdated(false)
                elseif far then
                    rawset(v,'_VitaFar',nil)
                    v:HasToBeUpdated(not tactical and v.Visible~=false and not (ship and ship._Frozen))
                end
            end
        end
    end)
end
-- Performance mode: lifecycle flags (resume, sleep, low memory, orientation,
-- headset, location) stay set until handled, so poll them every other frame.
local function adapt_device_manager(def)
    local update=def.OnUpdate
    def.OnUpdate=function(o,...)
        if setting(3)==1 then
            o._VitaPollSkip=not o._VitaPollSkip
            if o._VitaPollSkip then return end
        end
        return update(o,...)
    end
end
local function adapt_device_parameters(def)
    after(def,"OnCreateAfterSetters",function(o)
        if setting(3)~=1 then ACPVitaLog("Graphics: original preset");return end
        local params=o.Params
        for key,value in pairs(performance_params) do params[key]=value end
        ACPVitaLog("Graphics: performance mode (Ubisoft low-end effects, LOD 0.4)")
    end)
end
function ACPVitaAdapt(original)
    local economy_api={after=after,focus=focus,navigation=navigation}
    local battle_api={after=after,pad=pad,consume=consume,axis=axis,icon=controller_icon,visible=hud_visible,active=gameplay_active,setting=setting}
    local online_api={after=after,pad=pad,consume=consume,icon=controller_icon,setting=setting,navigation=navigation}
    local error_reporter_installed=false
    local pacing_installed=false
    local pending_loop,pending_finish
    local runner_failed=false
    local online_failed=false
    local function adapt_frame_pacing(def)
        if type(ACPVitaFramePhase)~="function" or type(def.FrameEnd)~="function" or
           type(LuaSpark2_OnUpdateStart)~="function" or type(LuaSpark2_OnUpdateEnd)~="function" then return end
        local phase=ACPVitaFramePhase
        local late=phase(-1,0)==1
        if not pacing_installed then
            local start,finish=LuaSpark2_OnUpdateStart,LuaSpark2_OnUpdateEnd
            LuaSpark2_OnUpdateStart=function(...)
                pending_loop,pending_finish=nil,nil
                ACPVitaSampleActive=phase(0,1)==1
                if ACPVitaHitchesFrame then ACPVitaHitchesFrame() end
                if ACPVitaRunnerFrame then
                    local ok,err=pcall(ACPVitaRunnerFrame)
                    if not ok and not runner_failed then runner_failed=true;ACPVitaLog("Runner controls failed: "..tostring(err)) end
                end
                if ACPVitaOnlineFrame then
                    local ok,err=pcall(ACPVitaOnlineFrame)
                    if not ok and not online_failed then online_failed=true;ACPVitaLog("Online features failed: "..tostring(err)) end
                end
                return start(...)
            end
            LuaSpark2_OnUpdateEnd=function(...)
                local loop,frame_end=pending_loop,pending_finish
                pending_loop,pending_finish=nil,nil
                phase(2,1)
                local result=finish(...)
                phase(2,0)
                -- Resource deletion and queue flushing belong to this frame's
                -- budget too. Keep the original timer, target and Dt policy.
                if frame_end and not loop.IsDestroyed then frame_end(loop) end
                phase(0,0)
                ACPVitaSampleActive=false
                return result
            end
            pacing_installed=true
            ACPVitaLog(late and "Frame pacing: wait after resource maintenance" or "Frame pacing: original order (emulator comparison)")
        end
        local frame_end=def.FrameEnd
        local function measured(o)
            phase(1,1)
            if late and type(ACPVitaFrameWait)=="function" and o.IsFixedTimeStep and o.TargetElapsedTime<=1 then
                Timer_Update(o._Timer)
                local remaining=o.TargetElapsedTime-(Timer_GetTotalTime(o._Timer)-o._LastFrameBeginTime)
                o._IsRunningSlowly=remaining<=0
                if remaining>0 then ACPVitaFrameWait(remaining) end
            else frame_end(o) end
            phase(1,0)
        end
        def.FrameEnd=function(o)
            if late then pending_loop,pending_finish=o,measured else measured(o) end
        end
    end
    local adapted
    adapted=function(def)
        -- EntryPoints defines the crash-handler list after Class is loaded.
        -- Register when it becomes available, preserving the game's handling.
        if not error_reporter_installed then
            local register_error=rawget(_G,"_RegisterCrashHandler")
            if type(register_error)=="function" then
                register_error(function(message,stack)
                    ACPVitaLog("Lua error: "..tostring(message))
                    if type(stack)=="table" then
                        for i=1,math.min(24,#stack) do
                            local frame=stack[i]
                            ACPVitaLog("Lua stack: "..tostring(frame.func).." "..tostring(frame.filePath)..":"..tostring(frame.fileLine))
                        end
                    end
                end)
                error_reporter_installed=true
            end
        end
        if type(def)=="string" then
            local className=def
            return function(t) t.ClassName=className;return adapted(t) end
        end
        local name=def.ClassName
        ACPVitaRenderWork(def)
        if ACPVitaHitches then ACPVitaHitches(def) end
        if ACPVitaRunner then ACPVitaRunner(def) end
        if ACPVitaOnline then ACPVitaOnline(def,online_api) end
        if ACPVitaEconomy and ACPVitaEconomy(def,economy_api) then
            -- Economy adapters retain the original challenge UI and payouts.
        elseif name=="GameLoop" then
            adapt_frame_pacing(def)
        elseif name=="GameController" and ACPVitaBattle then
            ACPVitaBattle(def,battle_api)
            if ACPVitaSailing then ACPVitaSailing(def,battle_api) end
        elseif name=="NodeMapController" and ACPVitaSailing then
            ACPVitaSailing(def,battle_api)
        elseif name=="NodeMapTravelPopup" then
            -- Sail / Cancel / OK of the world map's travel prompt.
            install_navigation(def,function(o)
                return o.Visible~=false and o.Position2D and math.abs(o.Position2D.x)<0.5
            end,true)
        elseif name=="MissionFailedPopup" or name=="RunnerCompletePopup" then
            -- Quit / Retry after a failed mission or temple run, Close / Play
            -- again after a finished run. The free choice is focused first.
            -- Shown, these popups keep their root hidden (only the content
            -- draws): active while on top of the back-button popup stack.
            install_navigation(def,function(o)
                local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
                return not o.Closed and manager._PopupStack and manager._PopupStack[1]==o
            end,true)
            after(def,"OnCreateAfterSubEntities",function(o)
                o._VitaNavTransparentContainer=o
                local entries=o._VitaNavEntries or {}
                for i,e in ipairs(entries) do
                    if e==(o.Center and o.Center.CloseButton) or e==o.QuitButton then
                        table.remove(entries,i);table.insert(entries,1,e);break
                    end
                end
            end)
        elseif name=="DeviceParameters" then
            adapt_device_parameters(def)
        elseif name=="DodgeEffect" or name=="EnemyWidget" then
            -- Battle overlays: the dodge lane's scrolling material and the enemy
            -- health-bar placement keep their state between updates.
            half_rate(def,"OnUpdate")
        elseif name=="NPCAnimation" then
            -- Every game-mode start re-enabled the sail animation of every NPC
            -- ship, overriding the freeze of far, hidden ships: a battle then
            -- animated all 48 world ships to show two (6.2 ms on hardware).
            -- Keep the original tactical rule and the ship's freeze/visibility.
            def.OnGameModeStart=function(o,mode)
                if o.LockedAnimation then return end
                local tactical=mode.GameModeName==GAMEMODES_TACTICAL or mode.GameModeName==GAMEMODES_ASSASSINATIONMISSION
                local ship=o.Parent
                o:HasToBeUpdated(not tactical and o.Visible~=false and not (ship and ship._Frozen) and not rawget(o,'_VitaFar'))
            end
        elseif name=="NPCManager" then
            adapt_far_sails(def)
        elseif name=="ShipLineDetectionBase" then
            adapt_line_detection(def)
        elseif name=="DeviceManager" then
            adapt_device_manager(def)
        elseif name=="FreeRoamingGUILayer" then
            adapt_free_roaming_layer(def)
        elseif name=="MagnetManager" then
            -- services.c has no Magnet provider: all six loaded/dismissed
            -- queries are permanently false. Keep construction and handlers,
            -- but do not cross Lua/C/JNI six times each frame to poll them.
            def.OnUpdate=function()end
        elseif name=="OptionsGUILayer" then
            replace_option(def.Background.NotificationOptionGradientBg.NotificationOptionBg,"Notification",1,"Invert Right Stick")
            replace_option(def.Background.FacebookOptionGradientBg.FacebookOptionBg,"Facebook",2,"Toggle FPS counter")
            -- The Android-only Google Game Services row uses the same option
            -- art; give it the ordinary toggle base used by the rows above.
            local services=def.Background.GameServicesOptionGradientBg.GameServicesOptionBg
            services.GameServicesButton[1]="PakGUIToggleButton2D"
            replace_option(services,"GameServices",3,"Performance mode")
            -- The phone-tilt steering row (no tilt steering on Vita) becomes Aim assist.
            local gyro=def.Background.GyroscopeOptionGradientBg.GyroscopeOptionBg
            gyro.GyroscopeButton.EnableGyroscopeController=function()end
            replace_option(gyro,"Gyroscope",4,"Aim assist")
            local anchors=def.ApplyAnchorPattern
            def.ApplyAnchorPattern=function(o,options)
                local found,services_found=false,false
                for _,v in ipairs(options) do
                    if v==o.Background.FacebookOptionGradientBg then found=true end
                    if v==o.Background.GameServicesOptionGradientBg then services_found=true end
                end
                if not found then table.insert(options,4,o.Background.FacebookOptionGradientBg) end
                if not services_found then
                    o.Background.GameServicesOptionGradientBg:SetVisible(true)
                    table.insert(options,o.Background.GameServicesOptionGradientBg)
                end
                local gyro_found=false
                for _,v in ipairs(options) do if v==o.Background.GyroscopeOptionGradientBg then gyro_found=true end end
                if not gyro_found then
                    o.Background.GyroscopeOptionGradientBg:SetVisible(true)
                    table.insert(options,o.Background.GyroscopeOptionGradientBg)
                end
                return anchors(o,options)
            end
            install_navigation(def,function(o)
                return o.IsEnabled and MainProcess:GetEntity("TagDeviceBackButtonManager")._GUILayer==o
            end)
        elseif name=="PauseGUILayer" then
            install_navigation(def,function(o)
                return o.IsScrolledDown and not o.ScrollingUp and not o.ScrollingDown and not o._IsInSubMenu
            end)
            -- Opening the pause menu mutes the wind loops and halves music and
            -- ambience; the touch buttons restore them before ScrollUp, but the
            -- back button (START/CIRCLE) resumes with a bare ScrollUp, leaving
            -- the game muted. The restore event sets fixed volumes, so sending
            -- it again on every real resume is harmless.
            local scroll_up=def.ScrollUp
            if type(scroll_up)=="function" then
                def.ScrollUp=function(o,paused,...)
                    if not paused then MainProcess:GetEntity("TagGameSound"):CallEventFiltered("Sound_Pause_Menu_Volume_Unmute") end
                    return scroll_up(o,paused,...)
                end
            end
        elseif name=="CreditsGUILayer" then
            after(def,"OnCreate",function(o)
                for i,row in ipairs(o.Credits_CSVTable) do
                    if tostring(row[2]):lower():find("ubisoft mobile paris",1,true) then
                        table.insert(o.Credits_CSVTable,i,{"Name","PS Vita Port: Apolune(Cynostial) 2025-2026"})
                        break
                    end
                end
            end)
        elseif name=="SpeedButton" then
            def.ContainsPosition=function()return false end
            def.OnTouchPressed=function()end;def.OnTouchMoved=function()end;def.OnTouchReleased=function()end
        elseif name=="IntroVideoLayer" then
            -- Remove the child definitions before Class constructs PakBink,
            -- its texture or sound thread. MainMenu registers its completion
            -- listener AFTER CreateEntity/ChangeGUIMode, so dispatch on the
            -- first enabled update, never during construction or activation.
            def.IntroVideo=nil
            def._SkipButton=nil
            def.OnCreateAfterSubEntities=function()end
            def.AfterActivate=function(o)o._VitaIntroCompleted=false end
            def.AfterDeactivate=function()end
            def.SkipVideo=function()end
            def.OnUpdate=function(o)
                if o.IsEnabled and not o._VitaIntroCompleted then
                    o._VitaIntroCompleted=true
                    ACPVitaLog("Intro video: bypassed before decoder creation")
                    MainProcess:GetEntity("TagEventManager"):DispatchEvent("OnIntroVideoEnd")
                end
            end
        elseif name=="LocalizedText2D" then
            local update=def.UpdateTextAndFont
            def.UpdateTextAndFont=function(o,key,params)
                local replacement=prompts[prompt_key(key or o.Text)]
                if prompt_key(key or o.Text)=="GAME_00013" then replacement="" end
                if replacement then
                    local localized=o.IsAlreadyLocalized
                    o.IsAlreadyLocalized=true;o.Text=replacement
                    update(o,replacement,params)
                    o.IsAlreadyLocalized=localized
                else update(o,key,params) end
            end
        elseif name=="TutorialManager" then
            local update=def.OnUpdate
            def.OnUpdate=function(o,...)
                local b=pad()
                if o.IsActive then
                    local mask=sail_prompt(o)
                    if mask and pressed(o,b,mask) then
                        -- Use the tutorial's own delayed completion and callback.
                        -- It deliberately disables the ordinary sail buttons.
                        o._ButtonReferencePressed=true
                        o.TouchPressed=true
                        consume(b)
                        ACPVitaLog("tutorial: sail trigger accepted")
                    elseif (ACPVitaBattleTutorial and ACPVitaBattleTutorial(o,b,o._VitaPrevious or 0))
                        or (ACPVitaSailingTutorial and ACPVitaSailingTutorial(o,b,o._VitaPrevious or 0,select(5,pad()))) then
                        consume(b)
                        ACPVitaLog("tutorial: battle button accepted")
                    end
                end
                o._VitaPrevious=b
                update(o,...)
                -- Some lessons animate their touch hand from the completion check.
                local layer=o.TutorialLayer
                if o.IsActive and layer and layer._VitaControllerPrompt and layer.Hand and layer.Hand.Visible then
                    layer.Hand.Visible=false;layer.Hand:HideDot()
                end
            end
        elseif name=="TutorialLayer" then
            after(def,"SetText",function(o,text)
                o._VitaPromptKey=prompt_key(text)
                o._VitaControllerPrompt=prompts[o._VitaPromptKey]~=nil
                o._VitaPromptIcon=prompt_icons[o._VitaPromptKey]
            end)
            after(def,"UpdateLayer",function(o)
                if o._VitaControllerPrompt then
                    o.Hand.Visible=false;o.Hand:HideDot()
                    if o._Arrow then o._Arrow.Visible=false end
                    o._LatchTextOnHand=false
                    o._TutoText.PivotPosition2D=Vector2_Zero
                    local at=prompt_positions[o._VitaPromptKey]
                    local x,y=at and at[1] or 0,at and at[2] or -0.30
                    o:SetTextPosition(Vector2(x,y))
                    if o._VitaPromptIcon then controller_icon(o,"_VitaIcon",o._VitaPromptIcon,Vector2(x,y+0.24),0.8) end
                end
            end)
            after(def,"End",function(o)
                o._VitaHoldProgress=0
                if o._VitaIcon then o._VitaIcon:SetVisible(false) end
                hide_hold_indicator(o)
            end)
        elseif name=="StoryMission0" then
            -- Replace the touch-only three-stage gesture lesson with a real
            -- stick lesson. Keep the mission's pause, sound, and next-phase flow.
            def.Phase17.Start=function(o)
                MainProcess:GetEntity("TagGameSound"):CallEventUnfiltered("Sound_Ambience_Volume_Decrease")
                local held=0
                local lastStep=-1
                o._TutorialManager:StartTutorial({
                    TutorialType="Custom",Pause=true,Delay=0,
                    Text="TUTO_BASIC_00004",
                    CustomMethod=function(t)
                        local b,lx=pad()
                        local left=axis(lx)<-0.5 or band(b,128)~=0
                        held=left and math.min(1.5,held+math.max(0,Dt)) or math.max(0,held-math.max(0,Dt)*3)
                        local step=math.min(10,math.floor(held/1.5*10))
                        t.TutorialLayer._VitaHoldProgress=held/1.5
                        t.TutorialLayer:SetTextPosition(Vector2(0,-0.30))
                        controller_icon(t.TutorialLayer,"_VitaIcon","LeftStick",Vector2(0,-0.06),0.8)
                        hold_indicator(t.TutorialLayer,held/1.5)
                        if step~=lastStep then
                            lastStep=step
                            local text=left and "Keep holding the left stick LEFT." or prompts.TUTO_BASIC_00004
                            local label=t.TutorialLayer._TutoText.Text
                            local localized=label.IsAlreadyLocalized
                            label.IsAlreadyLocalized=true;label.Text=text
                            label:UpdateTextAndFont(text);label.IsAlreadyLocalized=localized
                        end
                        return held>=1.5
                    end,
                    Callback=function() ACPVitaLog("tutorial: stick hold complete");o:StartPhase(18) end,
                })
            end
            def.Phase17.End=function(o)
                if o._TutorialManager.TutorialLayer._VitaIcon then o._TutorialManager.TutorialLayer._VitaIcon:SetVisible(false) end
                hide_hold_indicator(o._TutorialManager.TutorialLayer)
                MainProcess:GetEntity("TagGameSound"):CallEventUnfiltered("Sound_Ambience_Volume_Increase")
            end
        elseif name=="DialogLayer" then
            -- OnTouchReleased dispatch also invokes the inherited touch-event
            -- handler, which requires an actual touch object. Controller input
            -- calls just the dialogue's reveal/advance action.
            def.VitaContinue=def.OnTouchReleased
            after(def,"OnCreateAfterSubEntities",function(o)
                if ACPVitaIcons()==1 then
                    o._DialogText.DownArrow.Texture="ACPVitaCross"
                    o._DialogText.DownArrow.PixelSize2D=Vector2(40,40)
                end
            end)
        elseif name=="WheelGUIElement" then
            -- The engine still uses this entity for wheel animation state;
            -- suppress only its touch feedback mesh, including paused frames.
            def.IdleColorAtStartAlpha=0;def.IdleColorLaterAlpha=0;def.SelectedColorAlpha=0;def.IdleColorInertiaAlpha=0
            after(def,"OnCreateAfterSubEntities",function(o) o:SetVisible(false) end)
            for _,entry in ipairs(def) do
                if type(entry)=="table" and entry[1]=="FeedbackVisible" then
                    entry.Set=function(o,v)o._FeedbackVisible=false;o.Visible=false end
                end
            end
        elseif name=="PakWater" then
            after(def,"OnUpdate",function(o)
                if not o._Active then return end
                local frame=(o._TimeAcc*o.NormalMapAnimationSpeed)%o.NormalMapsCount
                local index=(math.floor(frame)+1)%o.NormalMapsCount
                if not o._VitaNormalFrames then o._VitaNormalFrames={} end
                if not o._VitaNormalFrames[index] then
                    o._VitaNormalFrames[index]={TextureName=string.format("WATER_NRM_%02d",index),
                        AddressU="Wrap",AddressV="Wrap",Filter="Anisotropic",MaxAnisotropy=4}
                end
                o:SetShaderParameter("u_ACPNormalNext",o._VitaNormalFrames[index])
                o:SetShaderParameter("u_ACPNormalBlend",frame-math.floor(frame))
                -- Performance mode drops the sea's sky reflection, which left
                -- the depth colours reading green. Tint the current preset
                -- colours toward the reflected blue (from the game's own
                -- values each frame, so weather changes never compound).
                local material=setting(3)==1 and o.Material and o.Material[1]
                if material then
                    for _,key in ipairs(water_tinted) do
                        local c=o[key]
                        if c then
                            material["u_"..key]=Vector3(math.min(1,c.r*WATER_TINT[1]),math.min(1,c.g*WATER_TINT[2]),math.min(1,c.b*WATER_TINT[3]))
                        end
                    end
                end
            end)
        elseif name=="WaterReflection" then
            -- The mobile material clears uncovered reflection pixels to its
            -- white rim-light color. Distorted lookups cross that edge and
            -- flash white. Uncovered reflection must contribute no radiance;
            -- the sky and geometry still render into this target normally.
            def.SetClearColor=function(o,color)
                o._Camera2.BackColor=Color(0,0,0,0)
            end
            local create=def.OnCreateAfterSubEntities
            def.OnCreateAfterSubEntities=function(o,...)
                local old=o.Process.PARAMS.ReflectionScaleFactor
                o.Process.PARAMS.ReflectionScaleFactor=0.5
                create(o,...)
                o._Camera2.BackColor=Color(0,0,0,0)
                o.Process.PARAMS.ReflectionScaleFactor=old
            end
            ACPVitaLog("reflection target: 480x272")
        elseif name=="LoadingScreen" then
            -- 444 MHz while a loading screen shows; native code restores the
            -- stock clock (and has a watchdog).
            -- Pending saves are written as a loading screen opens; first-battle
            -- controls are built as it closes, still at the loading clock.
            after(def,"OnShow",function()
                if ACPVitaLoading then ACPVitaLoading(2,1) end
                if ACPVitaSavesFlush then ACPVitaSavesFlush() end
            end)
            after(def,"OnHide",function()
                if ACPVitaPrewarmBattle then
                    local ok,err=pcall(ACPVitaPrewarmBattle)
                    if not ok then ACPVitaLog("Battle controls prewarm skipped: "..tostring(err)) end
                end
                if ACPVitaLoading then ACPVitaLoading(2,0) end
            end)
        elseif name=="MainMenu" then
            local layer=def.PageLayer
            after(layer,"AfterActivate",menu_hide)
            after(layer,"SetButtonsPosition",menu_hide)
            -- There is no Facebook on the Vita: never ask to "sign in on
            -- Facebook and get coins". Marked as seen, the menu goes straight
            -- to the daily reward, as after the popup's own LATER button.
            local intro_end=def.OnIntroVideoEnd
            def.OnIntroVideoEnd=function(o,...)
                local profile=MainProcess:GetEntity("TagPlayerProfile")
                if profile then profile.HasSeenFacebookAsk=true end
                return intro_end(o,...)
            end
            after(def,"OnUpdate",function(o)
                local b=pad()
                if not o.IntroVideoLayer and o.PageLayer then
                    menu_hide(o.PageLayer)
                    local button=o.PageLayer.ButtonPlay
                    local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
                    if button.Visible and button.IsEnabled and not manager:HasDisplayedPopup() and not manager._GUILayer
                       and not MainProcess:GetEntity("TagFadeFX").Visible then
                        controller_icon(o.PageLayer,"_VitaCross","Cross",Vector2(-0.20,button.AnchorPosition2D.y),0.6)
                        -- The menu is ready: launch loading is over (444 -> 333 MHz).
                        if ACPVitaLoading and not o._VitaLaunchLoaded then o._VitaLaunchLoaded=true;ACPVitaLoading(1,0) end
                        if pressed(o,b,16384) then consume(b);ACPVitaLog("Cross: Play");button:OnButtonReleased() end
                    end
                end
                o._VitaPrevious=b
            end)
            ACPVitaLog("adapted MainMenu")
        elseif name=="CutSceneSkipWidget" then
            -- CROSS already presses this skip button (DeviceBackButtonManager);
            -- show it beside the original >> arrow.
            after(def,"OnCreateAfterSubEntities",function(o)
                controller_icon(o,"_VitaCross","Cross",Vector2(1.45,-0.5),0.5)
            end)
        elseif name=="DailyRewardPopup" then
            -- The chests and OK are touch-only. CROSS runs the popup's own
            -- back-button action (open every chest), then its OK button.
            after(def,"OnUpdate",function(o)
                local b=pad()
                local ok=o.PopUpContent and o.PopUpContent.OKButton
                local ready=o.State=="Opened" and ok and ok.IsEnabled~=false and ok.Visible~=false
                if pressed(o,b,16384) then
                    if o.State=="DisplayStepUnpack" and o.UnpackAllGiftHack then
                        consume(16384);o.UnpackAllGiftHack:OnButtonReleased()
                    elseif ready then
                        consume(16384);ok:OnButtonReleased()
                    end
                end
                if ok and (ready or ok._VitaCross) then
                    local icon=controller_icon(ok,"_VitaCross","Cross",Vector2(-0.78,0),0.6)
                    if icon then hud_visible(icon,ready and true or false) end
                end
                o._VitaPrevious=b
            end)
        elseif name=="DeviceBackButtonManager" then
            table.insert(def,"PakUpdatable")
            def.OnUpdate=function(o)
                local b=pad()
                if ACPVitaBattleIdle then ACPVitaBattleIdle() end
                if ACPVitaSailingIdle then ACPVitaSailingIdle() end
                -- In battle CIRCLE readies the Canister Shot; START still pauses.
                local circle=pressed(o,b,8192) and not (ACPVitaBattleOwnsCircle and ACPVitaBattleOwnsCircle() and not o:HasDisplayedPopup())
                    and not (ACPVitaRunnerOwnsCircle and ACPVitaRunnerOwnsCircle())
                if pressed(o,b,8) or circle then
                    -- Each pause/resume rebuilds the pause menu: mashing
                    -- START/CIRCLE stalled frames. Ignore repeats for 0.4 s
                    -- (AbsoluteTime keeps running while the game is paused).
                    local at=o._VitaBackAt
                    if not at or AbsoluteTime<at or AbsoluteTime-at>=0.4 then
                        o._VitaBackAt=AbsoluteTime;o:OnBackButtonPressed()
                    end
                elseif pressed(o,b,16384) then
                    o:CleanInvalidElements()
                    if o._CutSceneSkipButton then consume(b);o._CutSceneSkipButton:OnButtonReleased()
                    elseif o._DialogLayer then consume(b);o._DialogLayer:VitaContinue()
                    elseif mission_go(o) then consume(b);mission_go(o):OnButtonReleased() end
                end
                -- Mission and temple requests: CROSS = Go (CIRCLE is Close).
                local go=mission_go(o)
                if go or o._VitaGo then
                    if o._VitaGo and o._VitaGo~=go and not o._VitaGo.IsDestroyed and o._VitaGo._VitaCross then
                        hud_visible(o._VitaGo._VitaCross,false)
                    end
                    o._VitaGo=go
                    if go then controller_icon(go,"_VitaCross","Cross",Vector2(-0.78,0),0.6) end
                end
                o._VitaPrevious=b
            end
        elseif name=="BottomScreenWheelController" then
            -- This entity still owns wheel animation and controller steering.
            -- Touch must neither start steering nor cancel an active stick hold.
            def.OnTouchPressed=function() end
            def.OnTouchMoved=function() end
            def.OnTouchReleased=function() end
            local updateWheel=def.UpdateWheelPos
            def.UpdateWheelPos=function(o,...)
                if updateWheel then updateWheel(o,...) end
                local wheel=o.Process:GetEntity("TagShipSteer")
                if wheel and wheel.VisualFeedback then wheel.VisualFeedback.Visible=false end
            end
            after(def,"OnUpdate",function(o)
                local b,lx,ly,rx=pad()
                local steer=axis(lx)
                if band(b,128)~=0 then steer=-1 elseif band(b,32)~=0 then steer=1 end
                if o.IsActive and gameplay_active(o) and o.CanUseBottomWheel and o:CanUseInCurrentCameraMan() then
                    local cameraName=o.Process.GameCamera:GetCurrentCameraManName()
                    local cameraController=o.Process:GetEntity(cameraName=="CameraImmersiveFree" and "TagCameraImmersiveFreeController" or "TagCameraBehindFreeController")
                    if (cameraName=="CameraImmersiveFree" or cameraName=="CameraShipBehind") and (not cameraController or not cameraController.IsActive) then
                        local camera=o.Process.GameCamera:GetCameraManByName(cameraName)
                        camera.YawTarget=camera.YawTarget+camera_sign()*axis(rx)*1.8*math.min(Dt,0.1)
                        if pressed(o,b,4096) then camera.YawTarget=0 end
                    end
                    if steer~=0 then
                        if not o._VitaSteering then
                            o:StopInertia();o.Process:GetEntity("TagShipSteer"):IncreaseReaction()
                            o.Process:DispatchEvent("OnWheelStickEvent",false)
                        end
                        o._VitaSteering=true;o.XValue=steer;o.ShouldUpdateWheel=true
                        Player:SetAngularVelocityWithRatio(steer)
                        o.Process:DispatchEvent("OnWheelStickValueChanged",steer)
                        o:UpdateWheelPos()
                    elseif o._VitaSteering then
                        o._VitaSteering=false;o.ShouldUpdateWheel=false;o:EndBottomScreenWheel()
                    end
                elseif o._VitaSteering then
                    o._VitaSteering=false;o.ShouldUpdateWheel=false;o:EndBottomScreenWheel()
                end
                if o.GUIFeedback and o.GUIFeedback.Visible~=false then o.GUIFeedback:SetVisible(false) end
                o._VitaPrevious=b
            end)
            after(def,"OnFinish",function(o)
                if o._VitaSteering then o._VitaSteering=false;o.ShouldUpdateWheel=false;o:EndBottomScreenWheel() end
            end)
            ACPVitaLog("adapted ship steering")
        elseif name=="CameraImmersiveFreeController" or name=="CameraBehindFreeController" then
            if name=="CameraBehindFreeController" then table.insert(def,"PakUpdatable") end
            local cameraName=name=="CameraImmersiveFreeController" and "CameraImmersiveFree" or "CameraShipBehind"
            after(def,"OnUpdate",function(o)
                local b,lx,ly,rx=pad()
                if o.IsActive and gameplay_active(o) and o.Process.GameCamera:GetCurrentCameraManName()==cameraName then
                    local camera=o:GetCameraMan()
                    camera.YawTarget=camera.YawTarget+camera_sign()*axis(rx)*1.8*math.min(Dt,0.1)
                    if pressed(o,b,4096) then camera.YawTarget=0 end
                end
                o._VitaPrevious=b
            end)
        elseif name=="SpeedWidget" then
            -- Keep the original buttons' game rules and tutorial restrictions,
            -- but operate them with the triggers and suppress their touch art.
            def._HotFix=function() end
            -- This method only animates the removed touch-only locked badge
            -- and runs the removed touch-layout repair. Sail rules, race boost,
            -- button activation and tutorial actions live in separate methods.
            def.UpdateWidget=function() end
            table.insert(def,"PakUpdatable")
            after(def,"OnUpdate",function(o)
                local b=pad()
                if o.WidgetActive and o.Parent.IsEnabled and gameplay_active(o) then
                    local nextSpeed=pressed(o,b,512) or pressed(o,b,16)
                    local previousSpeed=pressed(o,b,256) or pressed(o,b,64)
                    local current=sail_order[Player.CurrentSpeedPreset] or 0
                    if ACPVitaIcons()==1 then
                        local parent=o.Parent
                        if parent._VitaSlow then hud_visible(parent._VitaSlow,true)
                        else controller_icon(parent,"_VitaSlow","L",Vector2(0.51,-0.78),0.55) end
                        if parent._VitaFast then hud_visible(parent._VitaFast,true)
                        else controller_icon(parent,"_VitaFast","R",Vector2(0.89,-0.78),0.55) end
                        if not parent._VitaSailText then
                            parent._VitaSailText=parent:CreateEntity("LocalizedText2D",{Text=sail_titles[current],IsAlreadyLocalized=true,FontSize=28,Color=Color_White,AnchorPosition2D=Vector2(0.70,-0.78),PivotPosition2D=Vector2_Zero,LocalDepth=-0.2})
                        end
                        if o._VitaSail~=current then
                            o._VitaSail=current;parent._VitaSailText.Text=sail_titles[current];parent._VitaSailText:UpdateTextAndFont(sail_titles[current])
                        end
                        hud_visible(parent._VitaSailText,true)
                    end
                    if nextSpeed or previousSpeed then for _,button in ipairs(o._AllButtons) do
                        local n=sail_order[button.SpeedPreset]
                        if n and button.IsActive and button.IsEnabled and
                            ((nextSpeed and n==current+1) or (previousSpeed and n==current-1)) then
                            ACPVitaLog("trigger: "..button.SpeedPreset);button:OnButtonReleased();break
                        end
                    end end
                else
                    for _,name in ipairs{"_VitaSlow","_VitaFast","_VitaSailText"} do
                        hud_visible(o.Parent[name],false)
                    end
                end
                for _,button in ipairs(o._AllButtons) do
                    if button.Visible~=false then button:SetVisible(false) end
                    -- The locked badge can be animated independently of its
                    -- already hidden parent by the original tutorial code.
                    if button.LockedEffect and button.LockedEffect.Visible~=false then
                        button.LockedEffect:SetVisible(false)
                    end
                end
                o._VitaPrevious=b
            end)
            ACPVitaLog("adapted sail controls")
        end
        return original(def)
    end
    return adapted
end
