-- Vita sailing controls: engaging targets, camera views, the spyglass, the
-- tactical map and the world map. Every action goes through the game's own
-- button, camera or touch handler, so mission rules and tutorials apply.
-- Own function scope: the support files share one chunk and its 200 locals.
do local function sailing_module()
local band=bit.band
local SELECT,UP,RIGHT,DOWN,LEFT,CROSS,SQUARE=1,16,32,64,128,16384,32768
local HALF_W,HALF_H=480,272      -- Position2D spans [-1,1] over 960x544
local TOUCH_ID=7002
local HELM_REACH=250             -- sea units from the ship to the steering finger
local api
local state={previous=0,time=false,list=false,touch=false,flick=false,glyphs={},shown=false}

local function edge(b,mask) return band(b,mask)~=0 and band(state.previous,mask)==0 end
local function shown(e) return e and not e.IsDestroyed and e.Visible~=false end

-- Prompts --------------------------------------------------------------------
local function glyph(owner,key,name,anchor,visible,scale)
    local icon=owner[key]
    if not icon then
        if not visible then return end
        icon=api.icon(owner,key,name,anchor,scale or 0.6)
        if not icon then return end
    end
    if visible then
        if anchor and owner._VitaMoving==key then icon.LocalPosition2D=anchor elseif anchor then icon.AnchorPosition2D=anchor end
        if not owner[key.."Listed"] then owner[key.."Listed"]=true;state.glyphs[#state.glyphs+1]={owner,key} end
    end
    api.visible(icon,visible)
end
local function label(layer,key,text,anchor,visible)
    local e=layer[key]
    if not e then
        if not visible then return end
        e=layer:CreateEntity("LocalizedText2D",{Text=text,IsAlreadyLocalized=true,FontSize=28,Color=Color_White,
            AnchorPosition2D=anchor,PivotPosition2D=Vector2_Zero,LocalDepth=-0.2})
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

-- Touch ----------------------------------------------------------------------
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
end

-- CROSS: the visible tap target nearest the screen centre (ships to engage,
-- barrels, spyglass targets, story objects).
local function nearest_target(process)
    local gui=process.GameGUI
    local manager=gui and gui.SelectionManager
    local camera=process.GameCamera
    if not manager or not camera then return nil end
    local best,bx,by,bd=nil,0,0,math.huge
    for _,button in pairs(manager._SelectableTriggerButtonMap or {}) do
        if button and not button.IsDestroyed and button.Visible and button.IsEnabled~=false and button.Position then
            local p=camera:GetScreenPosition(button.Position)
            if p and math.abs(p.x)<=1 and math.abs(p.y)<=1 then
                local px,py=p.x*HALF_W,p.y*HALF_H
                local d=px*px+py*py
                if d<bd then best,bx,by,bd=button,p.x,p.y,d end
            end
        end
    end
    return best,bx,by
end
local function targets(o,b,layer,blocked)
    state.scan=not state.scan
    if state.scan or not state.target or state.target.IsDestroyed or not state.target.Visible then
        state.target,state.target_x,state.target_y=nearest_target(o.Process)
    end
    local target,x,y=state.target,state.target_x,state.target_y
    if target and not blocked and edge(b,CROSS) then
        api.consume(CROSS);target:OnButtonReleased()
        ACPVitaLog("sailing: activated "..tostring(target.SelectableEntity and target.SelectableEntity.ClassName))
    end
    if layer then
        -- Follows the target each frame: LocalPosition2D, like the game's battle markers.
        layer._VitaMoving="_VitaTarget"
        if target and not layer._VitaTarget then glyph(layer,"_VitaTarget","Cross",Vector2(0,0),true) end
        glyph(layer,"_VitaTarget","Cross",target and Vector2(x+0.07,y+0.13) or nil,target and true or false)
    end
end

-- Views: SQUARE switches deck/behind view and leaves the spyglass; pushing the
-- right stick up zooms one step closer (behind -> deck -> spyglass).
local function views(o,b,rx,ry,dt,layer,blocked)
    local process=o.Process
    local switch=process:GetEntity("TagSwitchViewPinchController")
    local camera=process.GameCamera
    if not switch or not camera then return end
    local name=camera:GetCurrentCameraManName()
    local blending=camera._BlendingPosition and camera._BlendingPosition:IsBlending()
    local sound=MainProcess:GetEntity("TagGameSound")
    local up=api.axis(ry)<-0.7
    local flick=up and not state.flick
    if math.abs(api.axis(ry))<0.3 then state.flick=false elseif up then state.flick=true end
    if not blocked and not blending then
        if edge(b,SQUARE) then
            api.consume(SQUARE)
            if name=="CameraImmersiveFree" and switch:CanGoCameraBehind() then
                sound:CallEventFiltered("Sound_Zoom_Out");switch:GoCameraBehind()
            elseif name=="CameraShipBehind" then
                sound:CallEventFiltered("Sound_Zoom_In");switch:GoImmersiveCamera(true)
            elseif name=="CameraSpyGlass" then
                sound:CallEventFiltered("Sound_Zoom_Out");switch:GoImmersiveCamera(false)
                process:OnSetVisible("TagPlayerShip",true)
            end
        elseif flick then
            local finder
            for e in process:Entities("TagSpyGlassButton") do
                if e.Active and shown(e) then finder=e end
            end
            if finder then finder:OnButtonReleased()
            elseif name=="CameraShipBehind" then
                sound:CallEventFiltered("Sound_Zoom_In");switch:GoImmersiveCamera(true)
            elseif name=="CameraImmersiveFree" and switch:CanGoSpyGlass() then
                sound:CallEventFiltered("Sound_Zoom_In");switch:GoCameraSpyGlass(true)
            end
        end
    end
    local spyglass=name=="CameraSpyGlass"
    if spyglass and not blending then
        -- Look around with the right stick, as the touch drag did.
        local man=camera:GetCameraManByName("CameraSpyGlass")
        local ctrl=process:GetEntity("TagSpyGlassController")
        local ax,ay=api.axis(rx),api.axis(ry)
        if man and (ax~=0 or ay~=0) then
            man.TargetAngleYaw=man.TargetAngleYaw-ax*1.1*dt
            man.TargetAnglePitch=math.max(ctrl and ctrl.MinPitch or -0.15,math.min(ctrl and ctrl.MaxPitch or 0.45,man.TargetAnglePitch-ay*0.6*dt))
        end
    end
    if layer then
        glyph(layer,"_VitaSpyBack","Square",Vector2(-0.86,-0.86),spyglass)
        label(layer,"_VitaSpyBackText","Back",Vector2(-0.74,-0.86),spyglass)
    end
    for e in process:Entities("TagSpyGlassButton") do
        glyph(e,"_VitaSpyStick","RightStick",Vector2(-0.55,0.55),e.Active and shown(e) or false)
    end
end

-- SELECT: the radar's tactical view, and back.
local function tactical_toggle(o,b,blocked,tactical)
    local gui=o.Process.GameGUI
    if tactical then
        local layer=o.Process:GetEntity("TagTacticalGUILayer")
        local back=layer and layer.BackToImmersiveButton
        if back and not blocked and edge(b,SELECT) and shown(back) then api.consume(SELECT);back:OnButtonReleased() end
        if back then glyph(back,"_VitaSelect","Select",Vector2(0,1.25),shown(back)) end
    else
        local radar=gui and gui.FreeRoamingLayer and gui.FreeRoamingLayer.RadarWidget
        if not radar then return end
        local open=shown(radar) and not radar.IsLocked
        if open and not blocked and edge(b,SELECT) then api.consume(SELECT);radar:OnButtonReleased() end
        glyph(radar,"_VitaSelect","Select",Vector2(0.62,0.62),open)
    end
end

-- Tactical view: the left stick pulls a finger out from the ship, so the
-- game draws a short route in that direction and the ship sails it.
local function helm(o,lx,ly,active)
    local ax,ay=api.axis(lx),-api.axis(ly)
    local tilt=math.sqrt(ax*ax+ay*ay)
    local t=state.touch
    local ship=Player and not Player.IsDestroyed and Player.Position
    if not active or tilt<0.25 or not ship then
        if t then send(o,"OnTouchReleased","Released") end
        return
    end
    local camera=o.Process.GameCamera
    local p=camera:GetScreenPosition(ship)
    if not p then return end
    if not t then send(o,"OnTouchPressed","Pressed",p.x,p.y);return end
    -- The route only grows once the finger is two path steps (80 units) from
    -- the ship, so reach a fixed sea distance in the stick's screen direction.
    local drawing=o._ControllerList[1]
    local toward=drawing and drawing.GetSeaPointFromPos2D and drawing:GetSeaPointFromPos2D(Vector2(p.x+ax/tilt*0.05,p.y+ay/tilt*0.05*HALF_W/HALF_H))
    if not toward then return end
    local dir=(toward-ship):SetZ(0):Normalize()
    local q=camera:GetScreenPosition(ship+dir*HELM_REACH*math.min(1,tilt))
    if q then send(o,"OnTouchMoved","Moved",q.x,q.y) end
end

-- The first course lesson ("draw your course", TUTO_BASIC_00019) stays active
-- without pausing until the ship reaches the mission: gameplay input is off
-- during lessons, so keep the helm itself live in the tactical view.
local function course_lesson()
    local tm=MainProcess:GetEntity("TagTutorialManager") or TM
    local layer=tm and tm.IsActive and tm.TutorialLayer
    return layer and layer._VitaPromptKey=="TUTO_BASIC_00019" or false
end

local function update(o)
    local b,lx,ly,rx,ry=api.pad()
    local now=AbsoluteTime
    local dt=state.time and math.max(0,math.min(0.1,now-state.time)) or 0
    if dt==0 and state.time then return end
    state.time=now
    local list=o._ControllerList
    if list~=state.list then state.list=list;state.touch=false end
    local modes=o.ControllerModeList
    local battle=list==modes.Battle or list==modes.FruitNinjaSwivel
    local tactical=list==modes.Tactical or list==modes.AssassinationMission
    local active=not battle and (api.active(o) or tactical and course_lesson()) and o.IsTouchEnabled~=false
    if active then
        local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
        local blocked=manager and (manager._DialogLayer or manager._CutSceneSkipButton or manager:HasDisplayedPopup())
        local gui=o.Process.GameGUI
        local layer=tactical and o.Process:GetEntity("TagTacticalGUILayer") or (gui and gui.FreeRoamingLayer)
        state.shown=now
        targets(o,b,layer,blocked)
        if not tactical then
            for _,c in ipairs(list) do
                if c.ClassName=="SwitchViewPinchController" then views(o,b,rx,ry,dt,layer,blocked);break end
            end
        end
        tactical_toggle(o,b,blocked,tactical)
        if tactical then helm(o,lx,ly,not blocked) end
    elseif state.touch and tactical then
        helm(o,128,128,false)
    end
    state.previous=b
end

-- World map: the D-pad or left stick moves between nodes, CROSS selects.
local map={previous=0,focus=false,dir=0,repeat_time=0}
-- The focus uses the node's own pressed look. It must not set Toggled: with
-- no finger down the map commits a toggled node as a selection.
local function highlight(button,on)
    if not button or button.IsDestroyed then return end
    button.AmbientColor=on and button.PressedColor or button.IdleColor
    button.TargetScale2D=on and button._PressedScale2D or button.BaseScale2D
end
local function focus_node(o,node)
    local camera=o.Process.NodeMapCamera
    if map.focus and map.focus~=node then highlight(map.focus.NodeButton,false) end
    map.focus=node
    if node and node.NodeButton then
        highlight(node.NodeButton,true)
        -- Recentre only when the node leaves the view, the way the map places
        -- its camera when it opens (its drag clamp rejects such jumps).
        local dl,tr,p=camera and camera.DownLeftCorner,camera and camera.TopRightCorner,node.Position2D
        local inside=dl and tr and p.x>dl.x+(tr.x-dl.x)*0.1 and p.x<tr.x-(tr.x-dl.x)*0.1
            and p.y>dl.y+(tr.y-dl.y)*0.1 and p.y<tr.y-(tr.y-dl.y)*0.1
        if camera and not inside then
            camera:StopInertia();camera.Position2D=p;camera:ClampToMap()
        end
    end
end
local function map_update(o)
    local b,lx,ly=api.pad()
    local world=o.Process.NodeWorldMap
    local graph=world and world.NodeMapGraph
    local manager=MainProcess:GetEntity("TagDeviceBackButtonManager")
    local ship=world and world.NodeMapPlayerShip
    local layer=world and world.NodeMapWorldLayer
    if not graph or not o.Active or (layer and layer.IsEnabled==false) or (ship and ship.Sailing)
       or (manager and manager:HasDisplayedPopup()) or MainProcess:GetEntity("TagFadeFX").Visible then
        map.previous=b;return
    end
    if not map.focus or not map.focus.NodeButton or map.focus.NodeButton.IsDestroyed then
        focus_node(o,ship and graph.NodeList[ship.CurrentNodeIdx])
    end
    local dx,dy=0,0
    local ax,ay=api.axis(lx),-api.axis(ly)
    if band(b,LEFT)~=0 or ax<-0.55 then dx=-1 elseif band(b,RIGHT)~=0 or ax>0.55 then dx=1
    elseif band(b,UP)~=0 or ay>0.55 then dy=1 elseif band(b,DOWN)~=0 or ay<-0.55 then dy=-1 end
    local dir=dx~=0 and dx or dy*2
    map.repeat_time=map.repeat_time-math.max(0,Dt)
    if map.focus and dir~=0 and (dir~=map.dir or map.repeat_time<=0) then
        local from=map.focus.Position2D
        local best,score=nil,math.huge
        for _,node in pairs(graph.NodeList) do
            local button=node.NodeButton
            if node~=map.focus and button and not button.IsDestroyed and button.Visible~=false then
                local x,y=node.Position2D.x-from.x,node.Position2D.y-from.y
                local along=x*dx+y*dy
                if along>0.01 then
                    local value=along+math.abs(x*dy-y*dx)*2
                    if value<score then best,score=node,value end
                end
            end
        end
        if best then
            focus_node(o,best);MainProcess:GetEntity("TagGameSound"):CallEventFiltered("Sound_Node_Selection")
        end
        map.repeat_time=dir~=map.dir and 0.35 or 0.18
    end
    map.dir=dir
    if map.focus and band(b,CROSS)~=0 and band(map.previous,CROSS)==0 then
        local button=map.focus.NodeButton
        api.consume(CROSS)
        ACPVitaLog("world map: node "..tostring(map.focus.NodeIndex))
        highlight(button,false);map.focus=false
        button:ButtonToggleOn();button:ButtonToggleOff(true)
    end
    map.previous=b
end

-- Entry points -----------------------------------------------------------------
function ACPVitaSailingIdle()
    if #state.glyphs>0 and (not state.shown or AbsoluteTime-state.shown>0.1) then hide_glyphs() end
end
-- Sailing lessons wait for a tap target, the radar, or a swipe to look around.
function ACPVitaSailingTutorial(tm,b,previous,ry)
    local function hit(mask) return band(b,mask)~=0 and band(previous,mask)==0 end
    local ref=tm._ButtonReference
    local touch={Id=TOUCH_ID,State="Pressed",Position2D=Vector2(0,0.4)}
    if ref and not ref.IsDestroyed and ref.HasTag then
        local mask=ref:HasTag("TagGUITriggerButton") and CROSS or ref:HasTag("TagRadarWidget") and SELECT
        if mask and hit(mask) then tm._ButtonReferencePressed=true;tm.TouchPressed=touch;return true end
        return false
    end
    local layer=tm.TutorialLayer
    -- The first lessons wait for the camera itself to change, and gameplay
    -- input is off while a lesson shows: SQUARE switches the view here.
    local key=layer and layer._VitaPromptKey
    if (key=="TUTO_BASIC_00020" or key=="TUTO_BASIC_00021") and hit(SQUARE) then
        local process=GetSubProcess("TagGameProcess")
        local switch=process and process:GetEntity("TagSwitchViewPinchController")
        local camera=process and process.GameCamera
        if not switch or not camera or (camera._BlendingPosition and camera._BlendingPosition:IsBlending()) then return false end
        local name=camera:GetCurrentCameraManName()
        local sound=MainProcess:GetEntity("TagGameSound")
        if key=="TUTO_BASIC_00020" and name=="CameraImmersiveFree" and switch:CanGoCameraBehind() then
            sound:CallEventFiltered("Sound_Zoom_Out");switch:GoCameraBehind();return true
        elseif key=="TUTO_BASIC_00021" and name=="CameraShipBehind" then
            sound:CallEventFiltered("Sound_Zoom_In");switch:GoImmersiveCamera(true);return true
        end
        return false
    end
    if layer and layer._VitaPromptKey=="TUTO_BASIC_00022" and api and api.axis(ry or 128)~=0 then
        tm.TouchPressed=touch
        return true
    end
    return false
end
function ACPVitaSailing(def,ui)
    api=ui
    if def.ClassName=="GameController" then api.after(def,"OnUpdate",update);return true end
    if def.ClassName=="NodeMapController" then api.after(def,"OnUpdate",map_update);return true end
    return false
end
end sailing_module() end
